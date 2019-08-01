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

PredicateKind predicate_kind_from_string(std::string_view s) {
	if (s == "location" || s == "locations")
		return PredicateKind::locations;
	if (s == "state" || s == "states")
		return PredicateKind::states;
	if (s == "uedge" || s == "uedges")
		return PredicateKind::uedges;
	if (s == "dedge" || s == "dedges")
		return PredicateKind::dedges;
	if (s == "tedge" || s == "tedges" || s == "edge" || s == "edges")
		return PredicateKind::total_edges;
	if (s == "component" || s == "components")
		return PredicateKind::components;
	throw std::runtime_error("predicate_kind_from_string: "+std::string(s));
}

namespace {
struct PredicateDemand {
	uint64_t beginInclusive, endExclusive;
	vector<unsigned int> locations, states, uedges, dedges, tedges, components;
	uint64_t max_gadget_id;
	std::size_t size() const {return endExclusive - beginInclusive;}
	bool empty() const {
		return size() == 0 ||
				locations.size() + states.size() + uedges.size() + dedges.size() +
				tedges.size() + components.size() == 0;
	}
	void add(PredicateKind kind, unsigned int number) {
		switch (kind) {
			case PredicateKind::locations: locations.push_back(number); return;
			case PredicateKind::states: states.push_back(number); return;
			case PredicateKind::uedges: uedges.push_back(number); return;
			case PredicateKind::dedges: dedges.push_back(number); return;
			case PredicateKind::total_edges: tedges.push_back(number); return;
			case PredicateKind::components: components.push_back(number); return;
		}
	}
	void sort() {
		std::sort(locations.begin(), locations.end());
		std::sort(states.begin(), states.end());
		std::sort(uedges.begin(), uedges.end());
		std::sort(dedges.begin(), dedges.end());
		std::sort(tedges.begin(), tedges.end());
		std::sort(components.begin(), components.end());
	}
};
PredicateDemand update_SL_predicates_discover(lmdb::env& env, lmdb::dbi& predicates,
		lmdb::dbi& gadget_index, uint64_t valid_before) {
	PredicateDemand demand;
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	demand.max_gadget_id = get_current_max_gadget_id(txn, gadget_index);
	valid_before = std::min(valid_before, demand.max_gadget_id+1);

	lmdb::cursor cur = lmdb::cursor::open(txn, predicates);
	std::string_view key = "valid_before", value = "";
	if (!cur.get(key, value, MDB_SET))
		throw std::runtime_error("missing predicates valid_before key (corrupt database?)");
	uint64_t validity = lmdb::from_sv<uint64_t>(value);
	if (valid_before <= validity) {
		demand.beginInclusive = demand.endExclusive = 0;
		return demand;
	}
	demand.beginInclusive = validity;
	demand.endExclusive = valid_before;

	//TODO: probably goes in stringutils.hpp?
	auto remove_prefix_if = [](std::string_view& key, std::string_view thing) -> bool {
		if (key.compare(0, thing.size(), thing) == 0) {
			key.remove_prefix(thing.size());
			return true;
		}
		return false;
	};

	//Find the predicates to update.  Some should always be the same (locations),
	//but others are generated on demand.
	cur.get(key, MDB_FIRST); //we know there's at least one key
	do {
		if (key == "valid_before"sv) continue;
		else if (remove_prefix_if(key, "locations<="))
			demand.locations.push_back(from_string<unsigned int>(key));
		else if (remove_prefix_if(key, "states<="))
			demand.states.push_back(from_string<unsigned int>(key));
		else if (remove_prefix_if(key, "uedges<="))
			demand.uedges.push_back(from_string<unsigned int>(key));
		else if (remove_prefix_if(key, "dedges<="))
			demand.dedges.push_back(from_string<unsigned int>(key));
		else if (remove_prefix_if(key, "edges<="))
			demand.tedges.push_back(from_string<unsigned int>(key));
		else if (remove_prefix_if(key, "components<="))
			demand.components.push_back(from_string<unsigned int>(key));
		else
			throw std::runtime_error(fmt::format("unrecognized predicates key: {}", key));
	} while (cur.get(key, MDB_NEXT));
	cur.close();
	txn.commit();

	demand.sort();
	return demand;
}

struct PredicateUpdateResult {
	vector<pair<unsigned int, vector<pair<uint64_t, uint64_t>>>> locations, states, uedges, dedges, tedges, components;
	static PredicateUpdateResult merge(PredicateUpdateResult&& left, PredicateUpdateResult&& right) {
		left.locations = merge0(std::move(left.locations), std::move(right.locations));
		left.states = merge0(std::move(left.states), std::move(right.states));
		left.uedges = merge0(std::move(left.uedges), std::move(right.uedges));
		left.dedges = merge0(std::move(left.dedges), std::move(right.dedges));
		left.tedges = merge0(std::move(left.tedges), std::move(right.tedges));
		left.components = merge0(std::move(left.components), std::move(right.components));
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
		auto init = [](auto& accum, const vector<unsigned int>& demand) {
			accum.reserve(demand.size());
			for (unsigned int i : demand)
				accum.emplace_back(i, 1024);
		};
		init(locations, demand.locations);
		init(states, demand.states);
		init(uedges, demand.uedges);
		init(dedges, demand.dedges);
		init(tedges, demand.tedges);
		init(components, demand.components);
	};
	void operator()(uint64_t gadget_id, const encoding::Stats& stats) {
		auto process = [gadget_id](auto& accum, unsigned int datum) {
			auto lit = std::lower_bound(accum.begin(), accum.end(), datum, coord_less_left<0>());
			if (lit != accum.end())
				(lit->second)(gadget_id);
		};
		process(locations, stats.locations);
		process(states, stats.states);
		process(uedges, stats.undirected_edges);
		process(dedges, stats.directed_edges);
		process(tedges, stats.undirected_edges + stats.directed_edges);
		process(components, stats.components);
	}
	PredicateUpdateResult finish() && {
		PredicateUpdateResult ret;
		convert_to_less_than(ret.locations, locations);
		convert_to_less_than(ret.states, states);
		convert_to_less_than(ret.uedges, uedges);
		convert_to_less_than(ret.dedges, dedges);
		convert_to_less_than(ret.tedges, tedges);
		convert_to_less_than(ret.components, components);
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
	vector<pair<unsigned int, interval_accumulator<uint64_t>>> locations, states, uedges, dedges, tedges, components;
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

		vector<pair<uint64_t, encoding::Stats>> stats = select_gadget_id_to_stats(
				env, gadget_hashtable, gadget_index, batch);

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
		txn.commit();
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
		commit_stuff(result.uedges, "uedges<={}");
		commit_stuff(result.dedges, "dedges<={}");
		commit_stuff(result.tedges, "edges<={}");
		commit_stuff(result.components, "components<={}");
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
	if (demand.size() >= demand.max_gadget_id / 8)
		result = update_SL_predicates_sequential(env, gadget_hashtable, threads, demand);
	else
		result = update_SL_predicates_random_access(env, gadget_hashtable, gadget_index, threads, demand);
	return update_SL_predicates_commit(env, predicates, demand.endExclusive, std::move(result));
}
}//anonymous namespace

//update SL predicates through given id (default max) using N threads (or using given executor)
bool update_predicates(lmdb::env& env, lmdb::dbi& predicates, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, uint64_t valid_before, unsigned int threads) {
	return meet_update_demand(env, predicates, gadget_hashtable, gadget_index, threads,
			update_SL_predicates_discover(env, predicates, gadget_index, valid_before));
}

bool create_predicates(lmdb::env& env, lmdb::dbi& predicates, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index,
		std::vector<std::pair<unsigned int, PredicateKind>> less_than_or_equal_to, unsigned int threads) {
	PredicateDemand demand;
	demand.beginInclusive = 1;

	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	std::string_view value;
	if (!predicates.get(txn, "valid_before", value))
		throw std::runtime_error("missing predicates valid_before key (corrupt database?)");
	demand.endExclusive = lmdb::from_sv<uint64_t>(value);
	demand.max_gadget_id = get_current_max_gadget_id(txn, gadget_index);
	for (std::pair<unsigned int, PredicateKind> p : less_than_or_equal_to)
		//If it exists, nothing to do for that one.
		if (!predicates.get(txn, fmt::format("{}<={}", p.second, p.first), value))
			demand.add(p.second, p.first);
	txn.commit();

	if (demand.endExclusive == 1) {
		//We've never updated predicates, so we don't have to fill in anything.
		//We just want to create any missing keys (with empty values).
		value = ""sv;
		auto txn = lmdb::txn::begin(env);
		for (std::pair<unsigned int, PredicateKind> p : less_than_or_equal_to)
			predicates.put(txn, fmt::format("{}<={}", p.second, p.first), value, MDB_NOOVERWRITE);
		txn.commit();
		return true;
	}

	if (demand.empty())
		return false;
	if (!meet_update_demand(env, predicates, gadget_hashtable, gadget_index, threads, demand))
		//We could get the new valid_before and scan just a bit more, then union
		//with the previous result (if we don't move it).  We should also check
		//the other process didn't already create the key, too.
		throw std::logic_error("TODO implement retry logic for when create_predicates speculation fails");
	return true;
}



std::vector<std::pair<std::uint64_t, std::uint64_t>> get_predicate(lmdb::env& env,
		lmdb::dbi& predicates, PredicateKind kind, unsigned int less_than_or_equal_to) {
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	auto ret = get_predicate(txn, predicates, kind, less_than_or_equal_to);
	txn.commit();
	return ret;
}
std::vector<std::pair<std::uint64_t, std::uint64_t>> get_predicate(lmdb::txn& txn,
		lmdb::dbi& predicates, PredicateKind kind, unsigned int less_than_or_equal_to) {
	std::string key = fmt::format("{}<={}", kind, less_than_or_equal_to);
	auto view = view_interval_list(txn, predicates, key);
	if (!view.first)
		throw std::runtime_error("missing predicate "+key);
	return {view.first, view.second};
}

namespace {
vector<pair<uint64_t, uint64_t>> get_predicate_range_query(lmdb::txn& txn, lmdb::dbi& predicates,
		PredicateKind kind, unsigned int lowerExclusive, unsigned int upperInclusive) {
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
}//anonymous namespace

//get equality predicate (difference between two predicates from DB)
//We could also expose the range query, though if the upper bound is greater than
//we've computed, we'd fail.
std::vector<std::pair<std::uint64_t, std::uint64_t>> get_equal_predicate(lmdb::env& env,
		lmdb::dbi& predicates, PredicateKind kind, unsigned int equal_to) {
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	auto ret = get_equal_predicate(txn, predicates, kind, equal_to);
	txn.commit();
	return ret;
}
std::vector<std::pair<std::uint64_t, std::uint64_t>> get_equal_predicate(lmdb::txn& txn,
		lmdb::dbi& predicates, PredicateKind kind, unsigned int equal_to) {
	if (kind == PredicateKind::locations && equal_to == 2)
		//1-location gadgets aren't in the database
		return get_predicate(txn, predicates, kind, equal_to);
	return get_predicate_range_query(txn, predicates, kind, equal_to-1, equal_to);
}

//predicate difference (remove matching)
std::vector<std::pair<std::uint64_t, std::uint64_t>> subtract_predicate(lmdb::env& env, lmdb::dbi& predicates,
		PredicateKind kind, unsigned int less_than_or_equal_to,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals) {
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	auto ret = subtract_predicate(txn, predicates, kind, less_than_or_equal_to, intervals);
	txn.commit();
	return ret;
}
std::vector<std::pair<std::uint64_t, std::uint64_t>> subtract_predicate(lmdb::txn& txn, lmdb::dbi& predicates,
		PredicateKind kind,	unsigned int less_than_or_equal_to,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals) {
	std::string key = fmt::format("{}<={}", kind, less_than_or_equal_to);
	auto view = view_interval_list(txn, predicates, key);
	if (!view.first)
		throw std::logic_error(fmt::format("can't subtract_predicate {} {} if key missing", kind, less_than_or_equal_to));
	return interval_difference(intervals.begin(), intervals.end(), view.first, view.second);
}

//predicate intersection (retain only matching)
std::vector<std::pair<std::uint64_t, std::uint64_t>> intersect_predicate(lmdb::env& env, lmdb::dbi& predicates,
		PredicateKind kind, unsigned int less_than_or_equal_to,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals) {
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	auto ret = intersect_predicate(txn, predicates, kind, less_than_or_equal_to, intervals);
	txn.commit();
	return ret;
}
std::vector<std::pair<std::uint64_t, std::uint64_t>> intersect_predicate(lmdb::txn& txn, lmdb::dbi& predicates,
		PredicateKind kind,	unsigned int less_than_or_equal_to,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals) {
		std::string key = fmt::format("{}<={}", kind, less_than_or_equal_to);
	auto view = view_interval_list(txn, predicates, key);
	if (!view.first)
		throw std::logic_error(fmt::format("can't intersect_predicate {} {} if key missing", kind, less_than_or_equal_to));
	return interval_intersection(intervals.begin(), intervals.end(), view.first, view.second);
}
