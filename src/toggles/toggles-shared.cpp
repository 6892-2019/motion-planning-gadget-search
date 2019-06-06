#include "precompiled.hpp"
#include "toggles-shared.hpp"
#include "gadget-encoding-stats.hpp"
#include "stringutils.hpp"
#include "tsl/ordered_set.h"
#include "intervals.hpp"
#include <jemalloc/jemalloc.h>
#include <regex>

using std::vector;
using std::pair;
using std::uint64_t;
using namespace std::literals::string_view_literals;

void jemalloc_tuning() {
	//Both the runner and driver often block (on lmdb or on running tasks), so
	//configure jemalloc background threads to let jemalloc yield unused memory
	//back to the operating system.
	bool yes_please = true;
	int rc = mallctl("background_thread", nullptr, 0, &yes_please, sizeof(yes_please));
	if (rc)
		fmt::print("warning: problem initializing jemalloc opts: {} {}", rc, strerror(rc));
}



GadgetSet parse_gid_specs(const std::vector<std::string_view>& specs) {
	std::regex is_integer(R"((\d+))"), is_range(R"((\(|\[)(\d+), ?(\d+)(\)|\]))");
	std::cmatch match;
	GadgetSet g;
	for (std::string_view v : specs) {
		if (std::regex_match(v.begin(), v.end(), is_integer))
			g.ids.push_back(from_string<uint64_t>(v));
		else if (std::regex_match(v.begin(), v.end(), match, is_range)) {
			uint64_t lower = from_string<uint64_t>(std::string_view(match[2].first, match[2].length())),
					upper = from_string<uint64_t>(std::string_view(match[3].first, match[3].length()));
			if (match[1] == "(")
				++lower;
			if (match[4] == "]")
				++upper;
			if (!(lower < upper))
				throw std::runtime_error(fmt::format("bad gid range: {}", v));
			g.ranges.emplace_back(lower, upper);
		} else
			g.names.emplace_back(v);
	}
	return g;
}

std::string format_gadget_set(const GadgetSet& spec) {
	vector<std::string> parts;
	for (uint64_t id : spec.ids)
		parts.push_back(fmt::format("{}", id));
	for (pair<uint64_t, uint64_t> p : spec.ranges)
		parts.push_back(fmt::format("[{},{})", p.first, p.second));
	for (const std::string& n : spec.names)
		parts.push_back(fmt::format("{}", n));
	return join(parts, " ");
}

std::vector<std::uint64_t> collect_initial_gadget_set(lmdb::env& env, const GadgetSet& gs) {
	lmdb::dbi gadget_hashtable, gadget_index, names;
	{
		auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		names = lmdb::dbi::open(txn, "names");
		txn.commit();
	}
	return collect_initial_gadget_set(env, gadget_hashtable, gadget_index, names, gs);
}
std::vector<std::uint64_t> collect_initial_gadget_set(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, lmdb::dbi& names, const GadgetSet& gs) {
	//Our ids are dense between 1 and the max.
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	uint64_t max_id = gadget_index.stat(txn).ms_entries;
	//vector_ordered_set
	tsl::ordered_set<uint64_t, farmhash_hash, std::equal_to<uint64_t>, std::allocator<uint64_t>, std::vector<uint64_t>> set;

	for (uint64_t i : gs.ids) {
		if (i < 1 || i > max_id)
			throw std::runtime_error(fmt::format("id {} not in database; max is {}", i, max_id));
	}
	set.insert(gs.ids.begin(), gs.ids.end());

	for (std::pair<uint64_t, uint64_t> p : gs.ranges) {
		if (p.first == p.second)
			throw std::runtime_error(fmt::format("range {} is empty", p));
		if (p.first < 1 || p.second < 1 || p.first > max_id || p.second > max_id+1)
			throw std::runtime_error(fmt::format("range {} not in databaes; max id is {}", p, max_id));
		auto r = xrange(p.first, p.second);
		set.insert(r.begin(), r.end());
	}

	for (const std::string& name : gs.names) {
		std::string_view data;
		if (!names.get(txn, name, data))
			throw std::runtime_error(fmt::format("name '{}' not in database", name));
		if (data.size() == 0 || data.size() % sizeof(uint64_t) != 0)
			//Either we screwed up in sync or the database is corrupt.
			throw std::logic_error(fmt::format("name '{}' found, but has size {}", name, data.size()));
		const uint64_t* first = reinterpret_cast<const uint64_t*>(data.data());
		const uint64_t* last = first + data.size()/sizeof(uint64_t);
		set.insert(first, last);
	}

	txn.commit();
	vector<uint64_t> ret = std::move(set).values_container();
	std::sort(ret.begin(), ret.end());
	return ret;
}



std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> select_gadget_id_to_data(
		lmdb::env& env, const std::vector<std::uint64_t>& gids) {
	lmdb::dbi gadget_hashtable, gadget_index;
	{
		auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		txn.commit();
	}
	return select_gadget_id_to_data(env, gadget_hashtable, gadget_index, gids);
}
std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> select_gadget_id_to_data(
		lmdb::env& env, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& gid_intervals) {
	lmdb::dbi gadget_hashtable, gadget_index;
	{
		auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		txn.commit();
	}
	return select_gadget_id_to_data(env, gadget_hashtable, gadget_index, gid_intervals);
}

namespace {
std::vector<std::pair<std::uint64_t, std::uint64_t>> select_gadget_id_to_hash(
		lmdb::txn& txn, lmdb::dbi& gadget_index, const vector<pair<uint64_t, uint64_t>>& gid_intervals) {
	vector<pair<uint64_t, std::size_t>> id_to_hash;
	lmdb::cursor cur = lmdb::cursor::open(txn, gadget_index);
	for (pair<uint64_t, uint64_t> p : gid_intervals) {
		uint64_t id = p.first;
		std::string_view key = lmdb::to_sv(id), value;
		if (!cur.get(key, value, MDB_SET))
			throw std::logic_error(fmt::format("id {} (from interval {}) not found", id, p.first));
		id_to_hash.emplace_back(id, lmdb::from_sv<std::size_t>(value));

		while (++id < p.second) {
			if (!cur.get(key, value, MDB_NEXT))
				throw std::logic_error(fmt::format("id {} (from interval {}) not found", id, p));
			if (lmdb::from_sv<uint64_t>(key) != id) //might mean discontiguous ids
				throw std::logic_error(fmt::format("expected id {} (from interval {}), but next was {}", id, p, lmdb::from_sv<uint64_t>(key)));
			id_to_hash.emplace_back(id, lmdb::from_sv<std::size_t>(value));
		}
	}
	return id_to_hash;
}

struct gadget_copier {
	std::vector<std::byte> operator()(std::string_view gadget_hashtable_value) {
		vector<std::byte> value_copy;
		value_copy.resize(gadget_hashtable_value.size());
		std::memcpy(value_copy.data(), gadget_hashtable_value.data(), gadget_hashtable_value.size());
		return value_copy;
	}
};
struct stats_extractor {
	encoding::Stats operator()(std::string_view gadget_hashtable_value) {
		return encoding::stats(reinterpret_cast<const std::byte*>(gadget_hashtable_value.data()));
	}
};

template<class ValueExtractor, class V = decltype(ValueExtractor()(""sv))>
std::vector<std::pair<std::uint64_t, V>> select_gadget_id_to_value(
		lmdb::env& env, lmdb::txn& txn, lmdb::dbi& gadget_hashtable,
		vector<pair<uint64_t, std::size_t>>& id_to_hash) {
	//TODO: merge with the other std::get-based comparators
	std::sort(id_to_hash.begin(), id_to_hash.end(), [](const auto& a, const auto& b) {
		return std::get<1>(a) < std::get<1>(b);
	});
	std::vector<std::pair<std::uint64_t, V>> ret;
	ret.reserve(id_to_hash.size());
	for (auto& p : id_to_hash) {
		std::string_view value;
		if (!gadget_hashtable.get(txn, lmdb::to_sv(p.second), value))
			throw std::logic_error(fmt::format("hash {} not found (for id {})", p.second, p.first));
		//The last 8 bytes are the id.  Check them, then don't return them.
		uint64_t appended_id = lmdb::from_sv<uint64_t>(value.substr(value.size()-8));
		if (appended_id != p.first)
			throw std::logic_error(fmt::format("looked up hash {} for id {}, but hashtable gives id {}",
					p.second, p.first, appended_id));
		value.remove_suffix(8);
		ret.emplace_back(p.first, ValueExtractor()(value));
	}
	return ret;
}
} //anonymous namespace

std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> select_gadget_id_to_data(
		lmdb::env& env, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index,
		const std::vector<std::uint64_t>& gids) {
	assert(std::is_sorted(gids.begin(), gids.end()));
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	vector<pair<uint64_t, std::size_t>> id_to_hash;
	for (uint64_t id : gids) {
		std::string_view value;
		if (!gadget_index.get(txn, lmdb::to_sv(id), value))
			throw std::logic_error(fmt::format("id {} not found", id));
		id_to_hash.emplace_back(id, lmdb::from_sv<std::size_t>(value));
	}
	auto ret = select_gadget_id_to_value<gadget_copier>(env, txn, gadget_hashtable, id_to_hash);
	txn.commit();
	return ret;
}
std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> select_gadget_id_to_data(
		lmdb::env& env, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& gid_intervals) {
	assert(std::is_sorted(gid_intervals.begin(), gid_intervals.end()));
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	auto id_to_hash = select_gadget_id_to_hash(txn, gadget_index, gid_intervals);
	auto ret = select_gadget_id_to_value<gadget_copier>(env, txn, gadget_hashtable, id_to_hash);
	txn.commit();
	return ret;
}



uint64_t get_current_max_gadget_id(lmdb::env& env) {
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	auto ret = get_current_max_gadget_id(txn);
	txn.commit();
	return ret;
}
uint64_t get_current_max_gadget_id(lmdb::env& env, lmdb::dbi& gadget_index) {
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	auto ret = get_current_max_gadget_id(txn, gadget_index);
	txn.commit();
	return ret;
}
uint64_t get_current_max_gadget_id(lmdb::txn& txn) {
	lmdb::dbi gadget_index = lmdb::dbi::open(txn, "gadget_index");
	return get_current_max_gadget_id(txn, gadget_index);
}
uint64_t get_current_max_gadget_id(lmdb::txn& txn, lmdb::dbi& gadget_index) {
	//Duplicated from selsert_gadget_by_data.
	lmdb::cursor index_cur = lmdb::cursor::open(txn, gadget_index);
	std::string_view last_id_view;
	if (index_cur.get(last_id_view, MDB_LAST))
		return lmdb::from_sv<std::uint64_t>(last_id_view);
	return 0; //note that 0 is not the id of any actual gadget
}



namespace {
struct PredicateDemand {
	uint64_t beginInclusive, endExclusive;
	vector<unsigned int> locations, states;
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

const std::size_t update_SL_predicates_basecase_batch_size = 10000;
struct PredicateUpdateResult {
	vector<pair<unsigned int, vector<pair<uint64_t, uint64_t>>>> locations, states;
	void merge(PredicateUpdateResult&& other) {
		locations = merge0(std::move(locations), std::move(other.locations));
		states = merge0(std::move(states), std::move(other.states));
	}
	vector<pair<unsigned int, vector<pair<uint64_t, uint64_t>>>> merge0(
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
PredicateUpdateResult update_SL_predicates_basecase(lmdb::env& env, lmdb::dbi& predicates,
		lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index, PredicateDemand demand) {
	vector<pair<unsigned int, interval_accumulator<uint64_t>>> locations, states;
	locations.reserve(demand.locations.size());
	for (unsigned int i : demand.locations)
		locations.emplace_back(i, 1024);
	states.reserve(demand.states.size());
	for (unsigned int i : demand.states)
		states.emplace_back(i, 1024);
	auto first_cmp = [](const auto& a, const auto& b) {
		//not quite proj_compare, but close...
		return std::get<0>(a) < b;
	};

	while (demand.beginInclusive < demand.endExclusive) {
		//Work in batches to keep transactions short.
		std::size_t batch_size = std::min(demand.endExclusive - demand.beginInclusive,
				update_SL_predicates_basecase_batch_size);
		vector<pair<uint64_t, uint64_t>> batch = {{demand.beginInclusive, demand.beginInclusive + batch_size}};

		//We could (should?) be using abort/renew here, but they're awkward to
		//use with the lmdbxx wrapper.
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		//We could reuse id_to_hash between iterations.
		auto id_to_hash = select_gadget_id_to_hash(txn, gadget_index, batch);
		vector<pair<uint64_t, encoding::Stats>> stats = select_gadget_id_to_value<stats_extractor>(
				env, txn, gadget_hashtable, id_to_hash);
		txn.commit();

		for (const pair<uint64_t, encoding::Stats>& p : stats) {
			//I tried commoning these with a lambda, but we'd have to work a
			//pointer-to-data-member into it, so I gave up.
			auto lit = std::lower_bound(locations.begin(), locations.end(), p.second.locations, first_cmp);
			if (lit != locations.end())
				(lit->second)(p.first);
			auto sit = std::lower_bound(states.begin(), states.end(), p.second.states, first_cmp);
			if (sit != states.end())
				(sit->second)(p.first);
		}
		demand.beginInclusive += batch_size;
	}

	PredicateUpdateResult ret;
	//We accumulated equality above, but our predicates are <=, so we need to
	//union each set with the smaller sets.
	auto convert_to_less_than = [](auto& ret, auto& accum) {
		ret.reserve(accum.size());
		for (std::size_t i = 0; i < accum.size(); ++i) {
			ret.emplace_back(accum[i].first, std::move(accum[i].second).finish());
			if (i > 0)
				ret[i].second = interval_union(ret[i-1].second.cbegin(), ret[i-1].second.cend(),
						ret[i].second.begin(), ret[i].second.end());
		}
	};
	convert_to_less_than(ret.locations, locations);
	convert_to_less_than(ret.states, states);
	return ret;
}

bool update_SL_predicates_commit(lmdb::env& env, lmdb::dbi& predicates, uint64_t speculative_value_before,
		PredicateUpdateResult result) {
	auto txn = lmdb::txn::begin(env); //write txn
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

	bool updated = false;
	auto commit_stuff = [&](vector<pair<unsigned int, vector<pair<uint64_t, uint64_t>>>> stuff,
			const char* key_format_string) {
		for (pair<unsigned int, vector<pair<uint64_t, uint64_t>>>& p : stuff) {
			if (p.second.empty()) continue;
			std::string real_key = fmt::format(key_format_string, p.first);
			std::string_view key = real_key;
			//If we're committing a new state predicate, the key may not exist.
			if (cur.get(key, value, MDB_SET)) {
				//If we're up-to-date, actually doing the union will be useless.
				//We're just looking for novel keys.
				if (speculative_value_before == actual_value_before)
					continue;

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
	txn.commit();
	return updated;
}
}//anonymous namespace

//update SL predicates through given id (default max) using N threads (or using given executor)
bool update_SL_predicates(lmdb::env& env, lmdb::dbi& predicates, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, uint64_t valid_before, unsigned int threads) {
	PredicateDemand demand = update_SL_predicates_discover(env, predicates, gadget_index, valid_before);
	if (demand.endExclusive <= demand.beginInclusive) return false;

	//TODO: fork threads for large updates
	PredicateUpdateResult result = update_SL_predicates_basecase(env, predicates, gadget_hashtable, gadget_index, demand);
	return update_SL_predicates_commit(env, predicates, demand.endExclusive, std::move(result));
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

	PredicateDemand demand = {1, valid_before, {}, {less_than_or_equal_to}};
	//TODO: threads
	PredicateUpdateResult result = update_SL_predicates_basecase(env, predicates, gadget_hashtable, gadget_index, demand);
	if (!update_SL_predicates_commit(env, predicates, demand.endExclusive, std::move(result)))
		//We could get the new valid_before and scan just a bit more, then union
		//with the previous result (if we don't move it).  We should also check
		//the other process didn't already create the key, too.
		throw std::logic_error("TODO implement retry logic for when create_state_predicate speculation fails");
	return true;
}

//get predicate (copy from DB)
vector<pair<uint64_t, uint64_t>> get_location_predicate(lmdb::env& env, lmdb::dbi& predicates, unsigned int max_locations);
vector<pair<uint64_t, uint64_t>> get_location_predicate(lmdb::txn& txn, lmdb::dbi& predicates, unsigned int max_locations);
vector<pair<uint64_t, uint64_t>> get_state_predicate(lmdb::env& env, lmdb::dbi& predicates, unsigned int max_states);
vector<pair<uint64_t, uint64_t>> get_state_predicate(lmdb::txn& txn, lmdb::dbi& predicates, unsigned int max_states);
//get equality predicate (difference between two predicates from DB)
vector<pair<uint64_t, uint64_t>> get_equal_location_predicate(lmdb::env& env, lmdb::dbi& predicates, unsigned int locations);
vector<pair<uint64_t, uint64_t>> get_equal_location_predicate(lmdb::txn& txn, lmdb::dbi& predicates, unsigned int locations);
vector<pair<uint64_t, uint64_t>> get_equal_state_predicate(lmdb::env& env, lmdb::dbi& predicates, unsigned int states);
vector<pair<uint64_t, uint64_t>> get_equal_state_predicate(lmdb::txn& txn, lmdb::dbi& predicates, unsigned int states);
//predicate_difference (for excluding impossible combines)
vector<pair<uint64_t, uint64_t>> subtract_state_predicate(lmdb::env& env, lmdb::dbi& predicates,
		unsigned int max_locations,	const vector<pair<uint64_t, uint64_t>>& intervals);
vector<pair<uint64_t, uint64_t>> subtract_state_predicate(lmdb::txn& txn, lmdb::dbi& predicates,
		unsigned int max_locations,	const vector<pair<uint64_t, uint64_t>>& intervals);
vector<pair<uint64_t, uint64_t>> subtract_location_predicate(lmdb::env& env, lmdb::dbi& predicates,
		unsigned int max_states, const vector<pair<uint64_t, uint64_t>>& intervals);
vector<pair<uint64_t, uint64_t>> subtract_location_predicate(lmdb::txn& txn, lmdb::dbi& predicates,
		unsigned int max_states, const vector<pair<uint64_t, uint64_t>>& intervals);
//predicate_intersection (for retaining only possible connects)
vector<pair<uint64_t, uint64_t>> intersect_state_predicate(lmdb::env& env, lmdb::dbi& predicates,
		unsigned int max_locations, const vector<pair<uint64_t, uint64_t>>& intervals);
vector<pair<uint64_t, uint64_t>> intersect_state_predicate(lmdb::txn& txn, lmdb::dbi& predicates,
		unsigned int max_locations, const vector<pair<uint64_t, uint64_t>>& intervals);
vector<pair<uint64_t, uint64_t>> intersect_location_predicate(lmdb::env& env, lmdb::dbi& predicates,
		unsigned int max_states, const vector<pair<uint64_t, uint64_t>>& intervals);
vector<pair<uint64_t, uint64_t>> intersect_location_predicate(lmdb::txn& txn, lmdb::dbi& predicates,
		unsigned int max_states, const vector<pair<uint64_t, uint64_t>>& intervals);



namespace {
std::array<std::string_view, 3> completions_key_whitelist = {
	"connect"sv,
	"close"sv,
	"mirror"sv,
};
std::array<std::string_view, 1> completions_key_prefix_whitelist = {
	"combine-"sv,
};
void check_completions_key(std::string_view key) {
	for (std::string_view x : completions_key_whitelist)
		if (key == x)
			return;
	for (std::string_view x : completions_key_prefix_whitelist)
		if (key.size() >= x.size() && key.compare(0, x.size(), x) == 0)
			return;
	throw std::logic_error(fmt::format("bad completions key: {}", key));
}

pair<const pair<uint64_t, uint64_t>*, const pair<uint64_t, uint64_t>*>
get_completions_key(lmdb::env& env, lmdb::txn& txn, lmdb::dbi& completions, std::string_view kind) {
	check_completions_key(kind);
	std::string_view value;
	if (!completions.get(txn, kind, value))
		//We checked the key validity above, so we must have no completions yet.
		//It's a bit silly to copy here, but this probably isn't the common case.
		return {nullptr, nullptr};
	//TODO: this kind of logic is pretty common, but with different types/throw info;
	//maybe there's a helper that returns a range or throws via a lambda?
	if (value.size() % sizeof(pair<uint64_t, uint64_t>) != 0)
		throw std::logic_error(fmt::format("completions key {} has value length {} (not a multiple of {})",
				kind, value.size(), sizeof(pair<uint64_t, uint64_t>)));
	const pair<uint64_t, uint64_t>* first = reinterpret_cast<const pair<uint64_t, uint64_t>*>(value.data());
	const pair<uint64_t, uint64_t>* last = first + value.size() / sizeof(pair<uint64_t, uint64_t>);
	return {first, last};
}
}

std::vector<std::pair<std::uint64_t, std::uint64_t>> filter_completion(
		lmdb::env& env, lmdb::dbi& completions, std::string_view kind,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals) {
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	auto ret = filter_completion(env, txn, completions, kind, intervals);
	txn.commit();
	return ret;
}
std::vector<std::pair<std::uint64_t, std::uint64_t>> filter_completion(
		lmdb::env& env, lmdb::txn& txn, lmdb::dbi& completions, std::string_view kind,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals) {
	auto comp_range = get_completions_key(env, txn, completions, kind);
	if (!comp_range.first)
		return intervals;
	return interval_difference(intervals.begin(), intervals.end(), comp_range.first, comp_range.second);
}

std::vector<std::pair<std::uint64_t, std::uint64_t>> intersect_completion(
		lmdb::env& env, lmdb::txn& txn, lmdb::dbi& completions, std::string_view kind,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals) {
	auto comp_range = get_completions_key(env, txn, completions, kind);
	if (!comp_range.first)
		return {};
	return interval_intersection(intervals.begin(), intervals.end(), comp_range.first, comp_range.second);
}

std::vector<std::pair<std::uint64_t, std::uint64_t>> union_completion(
		lmdb::env& env, lmdb::txn& txn, lmdb::dbi& completions, std::string_view kind,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals) {
	auto comp_range = get_completions_key(env, txn, completions, kind);
	auto result = comp_range.first ?
		interval_union(comp_range.first, comp_range.second, intervals.begin(), intervals.end()) :
		intervals; //if key not present, our intervals are the first
	std::string_view value(reinterpret_cast<const char*>(result.data()),
			result.size() * sizeof(pair<uint64_t, uint64_t>));
	if (!completions.put(txn, kind, value))
		throw std::logic_error(fmt::format("can't happen? failed to put completion data for {} with {} intervals ({} bytes)",
				kind, result.size(), value.size()));
	//We may as well return this given we computed it.
	return result;
}



template<class Edge>
std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_edges(lmdb::env& env,
		lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources) {
	interval_accumulator<uint64_t> accum(256);
	visit_edges<Edge>(env, edge_db, sources, [&](uint64_t, const Edge& e) {
		accum(e.output);
		return VisitEdgeResult::proceed;
	});
	return std::move(accum).finish();
}

//explicitly instantiate the three we need
template std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_edges<CombineEdge>(
		lmdb::env& env,	lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources);
template std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_edges<ConnectEdge>(
		lmdb::env& env,	lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources);
template std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_edges<SimpleEdge>(
		lmdb::env& env,	lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources);