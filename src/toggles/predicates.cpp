#include "precompiled.hpp"
#include "predicates.hpp"
#include "select-by-id.hpp"
#include "gadget-encoding-stats.hpp"
#include "lmdb-interval-list.hpp"
#include "intervals.hpp"
#include "transform_reduce.hpp"
#include "stringutils.hpp"
#include "proj_compare.hpp"

using std::vector;
using std::pair;
using std::uint64_t;
using namespace std::literals::string_view_literals;

namespace {
struct PredicateDemand {
	uint64_t beginInclusive, endExclusive;
	vector<unsigned int> locations, states;
	std::size_t size() const {return endExclusive - beginInclusive;}
};
PredicateDemand update_SL_predicates_discover(lmdb::env& env, lmdb::dbi& predicates,
		lmdb::dbi& gadget_index, uint64_t valid_before) {
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	valid_before = std::min(valid_before, get_current_max_gadget_id(txn, gadget_index)+1);

	lmdb::cursor cur = lmdb::cursor::open(txn, predicates);
	std::string_view key = "valid_before", value = "";
	if (!cur.get(key, value, MDB_SET))
		throw std::runtime_error("missing predicates valid_before key (corrupt database?)");
	uint64_t validity = lmdb::from_sv<uint64_t>(value);
	if (valid_before <= validity)
		return {0, 0, {}, {}};
	//update interval is [validity, valid_before).

	//TODO: probably goes in stringutils.hpp?
	auto remove_prefix_if = [](std::string_view& key, std::string_view thing) -> bool {
		if (key.compare(0, thing.size(), thing) == 0) {
			key.remove_prefix(thing.size());
			return true;
		}
		return false;
	};

	//Find the predicates to update.  The locations predicates should always be
	//the same, but state predicates are computed on demand.
	vector<unsigned int> locations, states;
	cur.get(key, MDB_FIRST); //we know there's at least one key
	do {
		if (key == "valid_before"sv) continue;
		else if (remove_prefix_if(key, "locations<="))
			locations.push_back(from_string<unsigned int>(key));
		else if (remove_prefix_if(key, "states<="))
			states.push_back(from_string<unsigned int>(key));
		else
			throw std::runtime_error(fmt::format("unrecognized predicates key: {}", key));
	} while (cur.get(key, MDB_NEXT));
	cur.close();
	txn.commit();

	std::sort(locations.begin(), locations.end());
	std::sort(states.begin(), states.end());
	return {validity, valid_before, std::move(locations), std::move(states)};
}

struct PredicateUpdateResult {
	vector<pair<unsigned int, vector<pair<uint64_t, uint64_t>>>> locations, states;
	static PredicateUpdateResult merge(PredicateUpdateResult&& left, PredicateUpdateResult&& right) {
		left.locations = merge0(std::move(left.locations), std::move(right.locations));
		left.states = merge0(std::move(left.states), std::move(right.states));
		return left;
	}
	static vector<pair<unsigned int, vector<pair<uint64_t, uint64_t>>>> merge0(
			const vector<pair<unsigned int, vector<pair<uint64_t, uint64_t>>>> us,
			vector<pair<unsigned int, vector<pair<uint64_t, uint64_t>>>> them) {
		assert(us.size() == them.size());
		vector<pair<unsigned int, vector<pair<uint64_t, uint64_t>>>> ret;
		for (std::size_t i = 0; i < us.size(); ++i) {
			assert(us[i].first == them[i].first);
			ret.emplace_back(us[i].first, interval_union(us[i].second.begin(), us[i].second.end(),
					them[i].second.begin(), them[i].second.end()));
		}
		return ret;
	}
};

struct PredicateUpdateAccumulator {
	PredicateUpdateAccumulator(const PredicateDemand& demand) {
		locations.reserve(demand.locations.size());
		for (unsigned int i : demand.locations)
			locations.emplace_back(i, 1024);
		states.reserve(demand.states.size());
		for (unsigned int i : demand.states)
			states.emplace_back(i, 1024);
	};
	void operator()(uint64_t gadget_id, const encoding::Stats& stats) {
		//I tried commoning these with a lambda, but we'd have to work a
		//pointer-to-data-member into it, so I gave up.
		auto lit = std::lower_bound(locations.begin(), locations.end(), stats.locations, coord_less_left<0>());
		if (lit != locations.end())
			(lit->second)(gadget_id);
		auto sit = std::lower_bound(states.begin(), states.end(), stats.states, coord_less_left<0>());
		if (sit != states.end())
			(sit->second)(gadget_id);
	}
	PredicateUpdateResult finish() && {
		PredicateUpdateResult ret;
		convert_to_less_than(ret.locations, locations);
		convert_to_less_than(ret.states, states);
		return ret;
	}
	static void convert_to_less_than(vector<pair<unsigned int, vector<pair<uint64_t, uint64_t>>>>& ret,
			vector<pair<unsigned int, interval_accumulator<uint64_t>>>& accum) {
		//We accumulated equality above, but our predicates are <=, so we need to
		//union each set with the smaller sets.
		ret.reserve(accum.size());
		for (std::size_t i = 0; i < accum.size(); ++i) {
			ret.emplace_back(accum[i].first, std::move(accum[i].second).finish());
			if (i > 0)
				ret[i].second = interval_union(ret[i-1].second.cbegin(), ret[i-1].second.cend(),
						ret[i].second.begin(), ret[i].second.end());
		}
	}
	vector<pair<unsigned int, interval_accumulator<uint64_t>>> locations, states;
};

const std::size_t update_SL_predicates_basecase_batch_size = 10000;
PredicateUpdateResult update_SL_predicates_basecase(lmdb::env& env,
		lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index, PredicateDemand&& demand) {
	PredicateUpdateAccumulator accum(demand);
	while (demand.beginInclusive < demand.endExclusive) {
		//Work in batches to keep transactions short.
		std::size_t batch_size = std::min(demand.endExclusive - demand.beginInclusive,
				update_SL_predicates_basecase_batch_size);
		vector<pair<uint64_t, uint64_t>> batch = {{demand.beginInclusive, demand.beginInclusive + batch_size}};

		//We could (should?) be using abort/renew here, but they're awkward to
		//use with the lmdbxx wrapper.
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		vector<pair<uint64_t, encoding::Stats>> stats = select_gadget_id_to_stats(
				env, gadget_hashtable, gadget_index, batch);
		txn.commit();

		for (const pair<uint64_t, encoding::Stats>& p : stats)
			accum(p.first, p.second);
		demand.beginInclusive += batch_size;
	}
	return std::move(accum).finish();
}

PredicateUpdateResult update_SL_predicates_random_access(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, unsigned int threads, const PredicateDemand& demand) {
	if (threads <= 1 || demand.size() < 4*update_SL_predicates_basecase_batch_size)
		return update_SL_predicates_basecase(env, gadget_hashtable, gadget_index, PredicateDemand(demand));
	else {
		vector<pair<uint64_t, uint64_t>> demanded_interval = {{demand.beginInclusive, demand.endExclusive}};
		//TODO: We could avoid some allocations here by making chunks a vector
		//of pairs (or PredicateDemands) instead of a vector of vectors of pairs.
		auto chunks = interval_chunk(demanded_interval.begin(), demanded_interval.end(), update_SL_predicates_basecase_batch_size);
		return transform_reduce(std::move(chunks), threads,
				[&](vector<pair<uint64_t, uint64_t>> chunk) -> PredicateUpdateResult {
					assert(chunk.size() == 1);
					PredicateDemand task = demand;
					task.beginInclusive = chunk[0].first;
					task.endExclusive = chunk[0].second;
					return update_SL_predicates_basecase(env, gadget_hashtable, gadget_index, std::move(task));
				}, PredicateUpdateResult::merge);
	}
}

PredicateUpdateResult update_SL_predicates_sequential(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		unsigned int threads, const PredicateDemand& demand) {
	//Scan all of gadget_hashtable in parallel.
	std::size_t chunk_size = std::numeric_limits<std::size_t>::max() / threads;
	vector<pair<std::size_t, std::size_t>> hash_ranges; //inclusive!
	for (unsigned int i = 0; i < threads; ++i)
		hash_ranges.emplace_back(chunk_size*i, chunk_size*(i+1)-1);
	//Ensure we cover the whole space even if it didn't divide evenly.
	hash_ranges.back().second = std::numeric_limits<std::size_t>::max();

	return transform_reduce(std::move(hash_ranges), threads, [&](pair<std::size_t, std::size_t> range) {
		PredicateUpdateAccumulator accum(demand);
		auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		lmdb::cursor cur = lmdb::cursor::open(txn, gadget_hashtable);
		std::string_view hash = lmdb::to_sv(range.first), gadget;
		if (cur.get(hash, gadget, MDB_SET_RANGE))
			while (lmdb::from_sv<uint64_t>(hash) <= range.second) {//yes, inclusive
				uint64_t id = lmdb::from_sv<uint64_t>(gadget.substr(gadget.size()-8, gadget.size()));
				if (demand.beginInclusive <= id && id < demand.endExclusive)
					accum(id, encoding::stats(reinterpret_cast<const std::byte*>(gadget.data())));
				if (!cur.get(hash, gadget, MDB_NEXT)) break;
			}
		return std::move(accum).finish();
	}, PredicateUpdateResult::merge);
}

bool update_SL_predicates_commit(lmdb::env& env, lmdb::dbi& predicates, uint64_t speculative_value_before,
		PredicateUpdateResult result) {
	bool updated = false;
	auto txn = lmdb::txn::begin(env); //write txn
	{
		//Ensure cursor is freed before the write transaction commits/aborts.  (See lmdbxx docs.)
		lmdb::cursor cur = lmdb::cursor::open(txn, predicates);
		std::string_view valid_before_key = "valid_before", value = "";
		if (!cur.get(valid_before_key, value, MDB_SET))
			throw std::runtime_error("missing predicates valid_before key (corrupt database?)");
		uint64_t actual_value_before = lmdb::from_sv<uint64_t>(value);
		if (speculative_value_before < actual_value_before)
			//If we're updating, someone else already did it.  If we're creating a
			//new index, we can't commit because our results aren't valid for the
			//full range.  (There's an LL/SC like thing going on there.)
			return false;
		if (speculative_value_before > actual_value_before) {
			value = lmdb::to_sv(speculative_value_before);
			if (!cur.put(valid_before_key, value))
				throw new std::logic_error("can't happen? failed to put valid_before key when committing");
		}

		auto commit_stuff = [&](vector<pair<unsigned int, vector<pair<uint64_t, uint64_t>>>> stuff,
				const char* key_format_string) {
			for (pair<unsigned int, vector<pair<uint64_t, uint64_t>>>& p : stuff) {
				std::string real_key = fmt::format(key_format_string, p.first);
				std::string_view key = real_key;
				//If we're committing a new state predicate, the key may not exist.
				if (cur.get(key, value, MDB_SET)) {
					//If we're up-to-date, actually doing the union will be useless.
					//We're just looking for novel keys.
					if (speculative_value_before == actual_value_before)
						continue;
					//If we've nothing to add, don't.  (We do have to fetch the key
					//before checking this, because creating an empty predicate is fine.)
					if (p.second.empty()) continue;

					if (value.size() % sizeof(pair<uint64_t, uint64_t>) != 0)
						throw std::logic_error(fmt::format("predicates key {} has value length {} (not a multiple of {})",
								key, value.size(), sizeof(pair<uint64_t, uint64_t>)));
					const pair<uint64_t, uint64_t>* first = reinterpret_cast<const pair<uint64_t, uint64_t>*>(value.data());
					const pair<uint64_t, uint64_t>* last = first + value.size() / sizeof(pair<uint64_t, uint64_t>);
					p.second = interval_union(p.second.begin(), p.second.end(), first, last);
				}

				std::string_view value(reinterpret_cast<const char*>(p.second.data()),
					p.second.size() * sizeof(pair<uint64_t, uint64_t>));
				if (!cur.put(key, value))
					throw std::logic_error(fmt::format("can't happen? failed to put predicate data for {} with {} intervals ({} bytes)",
							key, p.second.size(), value.size()));
				updated = true;
			}
		};
		commit_stuff(result.locations, "locations<={}");
		commit_stuff(result.states, "states<={}");
	}
	txn.commit();
	return updated;
}

bool meet_update_demand(lmdb::env& env, lmdb::dbi& predicates, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, unsigned int threads, PredicateDemand demand) {
	if (demand.size() <= 0) return false;

	PredicateUpdateResult result;
	//If we're going to touch most of the hashtable leaves anyway, we might as
	//well scan the whole table, so we are sequential and also bypass the index.
	//TODO: do the math to find the expected fraction of leaves touched based on
	//items per page and the demand size
	//TODO: both update_SL_predicates_discover and create_state_predicate have a
	//txn to get the max id inside, instead of this small one; could stash it in
	//the demand
	if (demand.size() >= get_current_max_gadget_id(env, gadget_index)/8)
		result = update_SL_predicates_sequential(env, gadget_hashtable, threads, demand);
	else
		result = update_SL_predicates_random_access(env, gadget_hashtable, gadget_index, threads, demand);
	return update_SL_predicates_commit(env, predicates, demand.endExclusive, std::move(result));
}
}//anonymous namespace

//update SL predicates through given id (default max) using N threads (or using given executor)
bool update_SL_predicates(lmdb::env& env, lmdb::dbi& predicates, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, uint64_t valid_before, unsigned int threads) {
	return meet_update_demand(env, predicates, gadget_hashtable, gadget_index, threads,
			update_SL_predicates_discover(env, predicates, gadget_index, valid_before));
}

//create and update new state predicate to current validity using N threads
//It would be easy to create multiple predicates at once should we need that.
//Just filter the missing keys and fill in the PredicateDemand.
bool create_state_predicate(lmdb::env& env, lmdb::dbi& predicates, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, unsigned int less_than_or_equal_to, unsigned int threads) {
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	std::string_view value;
	//If it exists, nothing to do.  This will page in (the start of) the value,
	//but we're usually just about to read it anyway, so that's fine.
	if (predicates.get(txn, fmt::format("states<={}", less_than_or_equal_to), value))
		return false;
	if (!predicates.get(txn, "valid_before", value))
		throw std::runtime_error("missing predicates valid_before key (corrupt database?)");
	uint64_t valid_before = lmdb::from_sv<uint64_t>(value);
	txn.commit();

	if (!meet_update_demand(env, predicates, gadget_hashtable, gadget_index, threads,
			{1, valid_before, {}, {less_than_or_equal_to}}))
		//We could get the new valid_before and scan just a bit more, then union
		//with the previous result (if we don't move it).  We should also check
		//the other process didn't already create the key, too.
		throw std::logic_error("TODO implement retry logic for when create_state_predicate speculation fails");
	return true;
}

namespace {
vector<pair<uint64_t, uint64_t>> get_predicate(lmdb::txn& txn, lmdb::dbi& predicates,
		std::string_view kind, unsigned int limit) {
	std::string key = fmt::format("{}<={}", kind, limit);
	auto view = view_interval_list(txn, predicates, key);
	if (!view.first)
		return {};
	return {view.first, view.second};
}
vector<pair<uint64_t, uint64_t>> get_predicate(lmdb::env& env, lmdb::dbi& predicates,
		std::string_view kind, unsigned int limit) {
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	auto ret = get_predicate(txn, predicates, kind, limit);
	txn.commit();
	return ret;
}

vector<pair<uint64_t, uint64_t>> get_predicate_range_query(lmdb::txn& txn, lmdb::dbi& predicates,
		std::string_view kind, unsigned int lowerExclusive, unsigned int upperInclusive) {
	std::string key = fmt::format("{}<={}", kind, upperInclusive);
	auto upper = view_interval_list(txn, predicates, key);
	if (!upper.first)
		throw std::runtime_error(fmt::format("can't do {} {} {} range query if upper key missing",
				kind, lowerExclusive, upperInclusive));
	key = fmt::format("{}<={}", kind, lowerExclusive);
	auto lower = view_interval_list(txn, predicates, key);
	if (!lower.first)
		throw std::runtime_error(fmt::format("can't do {} {} {} range query if lower key missing",
				kind, lowerExclusive, upperInclusive));
	return interval_difference(upper.first, upper.second, lower.first, lower.second);
}
vector<pair<uint64_t, uint64_t>> get_predicate_range_query(lmdb::env& env, lmdb::dbi& predicates,
		std::string_view kind, unsigned int lowerExclusive, unsigned int upperInclusive) {
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	auto ret = get_predicate_range_query(txn, predicates, kind, lowerExclusive, upperInclusive);
	txn.commit();
	return ret;
}

vector<pair<uint64_t, uint64_t>> subtract_predicate(lmdb::txn& txn, lmdb::dbi& predicates,
		std::string_view kind, unsigned int limit, const vector<pair<uint64_t, uint64_t>>& intervals) {
	std::string key = fmt::format("{}<={}", kind, limit);
	auto view = view_interval_list(txn, predicates, key);
	if (!view.first)
		throw std::logic_error(fmt::format("can't subtract_predicate {} {} if key missing", kind, limit));
	return interval_difference(intervals.begin(), intervals.end(), view.first, view.second);
}
vector<pair<uint64_t, uint64_t>> subtract_predicate(lmdb::env& env, lmdb::dbi& predicates,
		std::string_view kind, unsigned int limit, const vector<pair<uint64_t, uint64_t>>& intervals) {
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	auto ret = subtract_predicate(txn, predicates, kind, limit, intervals);
	txn.commit();
	return ret;
}

vector<pair<uint64_t, uint64_t>> intersect_predicate(lmdb::txn& txn, lmdb::dbi& predicates,
		std::string_view kind, unsigned int limit, const vector<pair<uint64_t, uint64_t>>& intervals) {
	std::string key = fmt::format("{}<={}", kind, limit);
	auto view = view_interval_list(txn, predicates, key);
	if (!view.first)
		throw std::logic_error(fmt::format("can't intersect_predicate {} {} if key missing", kind, limit));
	return interval_intersection(intervals.begin(), intervals.end(), view.first, view.second);
}
vector<pair<uint64_t, uint64_t>> intersect_predicate(lmdb::env& env, lmdb::dbi& predicates,
		std::string_view kind, unsigned int limit, const vector<pair<uint64_t, uint64_t>>& intervals) {
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	auto ret = intersect_predicate(txn, predicates, kind, limit, intervals);
	txn.commit();
	return ret;
}
}//anonymous namespace

//get predicate (copy from DB)
vector<pair<uint64_t, uint64_t>> get_location_predicate(lmdb::env& env, lmdb::dbi& predicates, unsigned int max_locations) {
	return get_predicate(env, predicates, "locations", max_locations);
}
vector<pair<uint64_t, uint64_t>> get_location_predicate(lmdb::txn& txn, lmdb::dbi& predicates, unsigned int max_locations) {
	return get_predicate(txn, predicates, "locations", max_locations);
}
vector<pair<uint64_t, uint64_t>> get_state_predicate(lmdb::env& env, lmdb::dbi& predicates, unsigned int max_states) {
	return get_predicate(env, predicates, "states", max_states);
}
vector<pair<uint64_t, uint64_t>> get_state_predicate(lmdb::txn& txn, lmdb::dbi& predicates, unsigned int max_states) {
	return get_predicate(txn, predicates, "states", max_states);
}
//get equality predicate (difference between two predicates from DB)
vector<pair<uint64_t, uint64_t>> get_equal_location_predicate(lmdb::env& env, lmdb::dbi& predicates, unsigned int locations) {
	if (locations < 2) throw std::logic_error(fmt::format("get_equal_location_predicate {}", locations));
	if (locations == 2) //1-location gadgets aren't in the database
		return get_location_predicate(env, predicates, locations);
	return get_predicate_range_query(env, predicates, "locations", locations-1, locations);
}
vector<pair<uint64_t, uint64_t>> get_equal_location_predicate(lmdb::txn& txn, lmdb::dbi& predicates, unsigned int locations) {
	if (locations < 2) throw std::logic_error(fmt::format("get_equal_location_predicate {}", locations));
	if (locations == 2) //1-location gadgets aren't in the database
		return get_location_predicate(txn, predicates, locations);
	return get_predicate_range_query(txn, predicates, "locations", locations-1, locations);
}
vector<pair<uint64_t, uint64_t>> get_equal_state_predicate(lmdb::env& env, lmdb::dbi& predicates, unsigned int states) {
	if (states == 0) throw std::logic_error(fmt::format("get_equal_state_predicate {}", states));
	return get_predicate_range_query(env, predicates, "states", states-1, states);
}
vector<pair<uint64_t, uint64_t>> get_equal_state_predicate(lmdb::txn& txn, lmdb::dbi& predicates, unsigned int states) {
	if (states == 0) throw std::logic_error(fmt::format("get_equal_state_predicate {}", states));
	return get_predicate_range_query(txn, predicates, "states", states-1, states);
}
//predicate_difference (for excluding impossible combines)
vector<pair<uint64_t, uint64_t>> subtract_location_predicate(lmdb::env& env, lmdb::dbi& predicates,
		unsigned int max_locations,	const vector<pair<uint64_t, uint64_t>>& intervals) {
	return subtract_predicate(env, predicates, "locations", max_locations, intervals);
}
vector<pair<uint64_t, uint64_t>> subtract_location_predicate(lmdb::txn& txn, lmdb::dbi& predicates,
		unsigned int max_locations,	const vector<pair<uint64_t, uint64_t>>& intervals) {
	return subtract_predicate(txn, predicates, "locations", max_locations, intervals);
}
vector<pair<uint64_t, uint64_t>> subtract_state_predicate(lmdb::env& env, lmdb::dbi& predicates,
		unsigned int max_states, const vector<pair<uint64_t, uint64_t>>& intervals) {
	return subtract_predicate(env, predicates, "states", max_states, intervals);
}
vector<pair<uint64_t, uint64_t>> subtract_state_predicate(lmdb::txn& txn, lmdb::dbi& predicates,
		unsigned int max_states, const vector<pair<uint64_t, uint64_t>>& intervals) {
	return subtract_predicate(txn, predicates, "states", max_states, intervals);
}
//predicate_intersection (for retaining only possible connects)
vector<pair<uint64_t, uint64_t>> intersect_location_predicate(lmdb::env& env, lmdb::dbi& predicates,
		unsigned int max_locations, const vector<pair<uint64_t, uint64_t>>& intervals) {
	return intersect_predicate(env, predicates, "locations", max_locations, intervals);
}
vector<pair<uint64_t, uint64_t>> intersect_location_predicate(lmdb::txn& txn, lmdb::dbi& predicates,
		unsigned int max_locations, const vector<pair<uint64_t, uint64_t>>& intervals) {
	return intersect_predicate(txn, predicates, "locations", max_locations, intervals);
}
vector<pair<uint64_t, uint64_t>> intersect_state_predicate(lmdb::env& env, lmdb::dbi& predicates,
		unsigned int max_states, const vector<pair<uint64_t, uint64_t>>& intervals) {
	return intersect_predicate(env, predicates, "states", max_states, intervals);
}
vector<pair<uint64_t, uint64_t>> intersect_state_predicate(lmdb::txn& txn, lmdb::dbi& predicates,
		unsigned int max_states, const vector<pair<uint64_t, uint64_t>>& intervals) {
	return intersect_predicate(txn, predicates, "states", max_states, intervals);
}