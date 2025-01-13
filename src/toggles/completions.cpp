// SPDX-License-Identifier: MIT
// Copyright 2019 Massachusetts Institute of Technology
#include "precompiled.hpp"
#include "completions.hpp"
#include "intervals.hpp"
#include <boost/container/static_vector.hpp>
#include <boost/container/small_vector.hpp>

using std::vector;
using std::pair;
using std::uint64_t;
using namespace std::literals::string_view_literals;

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

struct LSM {
	//Testing hint: decrease N and greatly decrease page_size to force compactions.
	static constexpr unsigned int N = 8;
	static constexpr unsigned int page_size = (4096-16) / sizeof(std::pair<uint64_t, uint64_t>);
	using pointer = const std::pair<uint64_t, uint64_t>*;
	using range = std::pair<pointer, pointer>;
	using level = boost::container::static_vector<range, N>;
	//This points at the caller's stack and is const to ensure we don't
	//accidentally pass it to cursor::get (which would make it point into the
	//database and be subject to invalidation on writes).
	const std::string_view kind;
	//These point into the database, so they might be invalidated on write (I'm
	//not quite sure what LMDB's rules are).  But that's okay because we only
	//write during compaction (and we clear .levels[i] as appropriate) and to
	//write a single L0 key.
	range main;
	boost::container::small_vector<level, 4> levels;
	//We might be able to save some formatting operations by storing
	//std::string_views pointing into the database (and being very careful/
	//reinitializing on write), or otherwise caching strings parallel to .levels.
};

LSM::range lsm_value_to_range(std::string_view value, std::string_view key = "") {
	if (value.size() % sizeof(pair<uint64_t, uint64_t>) != 0)
		throw std::logic_error(fmt::format("parse_lsm: key {} has value size {} (not a multiple of {})",
				key, value.size(), sizeof(pair<uint64_t, uint64_t>)));
	const pair<uint64_t, uint64_t>* first = reinterpret_cast<const pair<uint64_t, uint64_t>*>(value.data());
	const pair<uint64_t, uint64_t>* last = first + value.size() / sizeof(pair<uint64_t, uint64_t>);
	return {first, last};
}

//be careful not to assign result directly to string_view
//TODO: should take a std::string& arg, both to reuse the memory during repeated
//formats and to prevent assigning to string_view; if the kind is the same, the
//string won't reallocate, so the string_view can't get stale
std::string make_lsm_key(std::string_view kind, unsigned int level, unsigned int page) {
	//Deliberately not using fmt::format for speed.
	std::string key;
	static_assert(LSM::N < 10, "need to reserve more space here");
	key.reserve(kind.size() + 4);//kind + two hyphens + single-digit level + single-digit page
	MAYBE_UNUSED auto reserved_capacity = key.capacity(); //save the actual value in case of SSO
	key.append(kind.begin(), kind.end());
	key.push_back('-');
	key.push_back(numeric_cast<char>(level + '0'));
	key.push_back('-');
	key.push_back(numeric_cast<char>(page + '0'));
	assert(key.capacity() == reserved_capacity); //i.e., we didn't reallocate
	return key;
}

LSM parse_lsm(lmdb::cursor& cur, const std::string_view kind) {
	auto parse_uint_or_throw = [](std::string_view& k, std::string_view emsg) {
		unsigned int v;
		std::from_chars_result r = std::from_chars(k.begin(), k.end(), v);
		if (r.ec == std::errc()) {
			k.remove_prefix(std::distance(k.begin(), r.ptr));
			return v;
		} else
			throw std::runtime_error(std::string(emsg));
	};

	std::string_view key = kind, value;
	cur.get(key, value, MDB_SET); //usually already positioned here, but we want to get the value
	LSM ret = {kind, lsm_value_to_range(value, key), {}};
	while (cur.get(key, value, MDB_NEXT) &&
			//TODO C++20: starts_with
			key.compare(0, ret.kind.size(), ret.kind) == 0) {
		std::string_view k = key;
		k.remove_prefix(ret.kind.size());
		if (k.empty())
			throw std::logic_error(fmt::format("can't happen: two copies of key {}", key));
		if (k[0] != '-')
			//e.g., combine-24593 immediately follows combine-2 and shares a
			//prefix, but isn't a combine-2 page.
			break;
		k.remove_prefix(1);
		unsigned int level = parse_uint_or_throw(k, key);
		if (k.empty() || k[0] != '-')
			throw std::runtime_error(std::string(key));
		k.remove_prefix(1);
		unsigned int index = parse_uint_or_throw(k, key);
		if (!k.empty())
			throw std::runtime_error(std::string(key));

		if (level < ret.levels.size()) {
			if (level != ret.levels.size()-1)
				throw std::logic_error(fmt::format("can't happen: keys out of order? found {} at level {}", key, ret.levels.size()-1));
			if (index != ret.levels.back().size())
				throw std::runtime_error(fmt::format("key gap for {} at level {}: expected {} but found {}; detected at {}",
						ret.kind, level, ret.levels.back().size(), index, key));
			ret.levels.back().push_back(lsm_value_to_range(value, key));
		} else if (level == ret.levels.size()) {
			if (index != 0)
				throw std::runtime_error(fmt::format("key gap for {} at level {}: expected {} but found {}; detected at {}",
						ret.kind, level, 0, index, key));
			ret.levels.push_back({});
			ret.levels.back().push_back(lsm_value_to_range(value, key));
		}
	}
	return ret;
}

template<class Iterator>
vector<pair<uint64_t, uint64_t>> merge_ranges(Iterator first, Iterator last) {
	boost::container::small_vector<vector<pair<uint64_t, uint64_t>>, LSM::N/2+1> intervals;
	for (; first != last; ++first)
		if (Iterator second = std::next(first); second != last) {
			intervals.push_back(interval_union(first->first, first->second, second->first, second->second));
			first = second;
		} else
			intervals.push_back({first->first, first->second});

	while (intervals.size() > 1)
		for (std::size_t i = 0; i+1 < intervals.size(); ++i) {
			intervals[i] = interval_union(intervals[i].begin(), intervals[i].end(), intervals[i+1].begin(), intervals[i+1].end());
			intervals.erase(intervals.begin() + (i+1));
		}
	return std::move(intervals[0]);
}

void compact_lsm_full(LSM& lsm, lmdb::cursor& cur) {
	//We can't just compact each level, then the single top-level key with main
	//because this is the fallback for level overflow.  That would also write
	//some database keys we'd just immediately delete, anyway.  Instead we merge
	//each level, including the level below as another key (so not strictly a
	//binary merge), and then merge at the top level.
	boost::container::static_vector<LSM::range, LSM::N+1> ranges;
	vector<pair<uint64_t, uint64_t>> accumulator;
	for (std::size_t i = 0; i < lsm.levels.size(); ++i) {
		ranges = lsm.levels[i];
		if (!accumulator.empty())
			ranges.push_back({accumulator.data(), accumulator.data() + accumulator.size()});
		accumulator = merge_ranges(ranges.begin(), ranges.end());
	}
	accumulator = interval_union(lsm.main.first, lsm.main.second, accumulator.begin(), accumulator.end());

	std::string_view key = lsm.kind, value(reinterpret_cast<const char*>(accumulator.data()),
				accumulator.size() * sizeof(pair<uint64_t, uint64_t>));
	if (!cur.put(key, value))
		throw std::logic_error(fmt::format("failed to overwrite {} during full compaction", lsm.kind));

	while (cur.get(key, MDB_NEXT) && key.compare(0, lsm.kind.size(), lsm.kind) == 0 && //TODO C++20 starts_with
			lsm.kind.size() < key.size() && key[lsm.kind.size()] == '-')
		cur.del();
	lsm.levels.clear();

	//Because we deleted keys, we have to refresh this, but I don't think we
	//actually need it, so just clear it out.
//	key = lsm.kind;
//	cur.get(key, value, MDB_SET);
//	lsm.main = lsm_value_to_range(value, lsm.kind);
	lsm.main.first = lsm.main.second = nullptr;
}

void compact_lsm(LSM& lsm, lmdb::cursor& cur, unsigned int level = 0) {
	//We rely on having single-digit levels for sort order.
	if (level > 8)
		return compact_lsm_full(lsm, cur);
	//Compact the next level up if we need to make space.
	if (level+1 < lsm.levels.size() && lsm.levels[level+1].size() == LSM::N) {
		compact_lsm(lsm, cur, level+1);
		//Compacting modified the database, so we need to re-read our level.
		for (unsigned int i = 0; i < lsm.levels[level].size(); ++i) {
			std::string key = make_lsm_key(lsm.kind, level, i);
			std::string_view key_view = key, value;
			if (!cur.get(key_view, value, MDB_SET))
				throw std::logic_error(fmt::format("in compact_lsm: level {} page {} missing after compacting next level (kind {})",
						level, i, lsm.kind));
			lsm.levels[level][i] = lsm_value_to_range(value, key);
		}
	}

	vector<pair<uint64_t, uint64_t>> merged = merge_ranges(lsm.levels[level].cbegin(), lsm.levels[level].cend());

	for (unsigned int i = 0; i < lsm.levels[level].size(); ++i) {
		std::string key = make_lsm_key(lsm.kind, level, i);
		std::string_view key_view = key;
		if (!cur.get(key_view, MDB_SET))
			throw std::logic_error(fmt::format("in compact_lsm: level {} page {} is in the parsed LSM tree, but couldn't get {} (kind {})",
					level, i, key, lsm.kind));
		cur.del();
	}
	lsm.levels[level].clear();

	//Create the next level if it doesn't exist.  If it exists, we know there's
	//space at the end because we checked about compacting it earlier.
	if (level+1 == lsm.levels.size())
		lsm.levels.push_back({});
	unsigned int page = numeric_cast<unsigned int>(lsm.levels[level+1].size());
	assert(page < LSM::N);

	std::string key = make_lsm_key(lsm.kind, level+1, page);
	std::string_view value(reinterpret_cast<const char*>(merged.data()),
				merged.size() * sizeof(pair<uint64_t, uint64_t>));
	if (!cur.put(key, value, MDB_NOOVERWRITE))
		throw std::logic_error(fmt::format("can't happen: unable to insert {} during LSM compaction", key));
	lsm.levels[level+1].push_back(lsm_value_to_range(value, key));
}
}//anonymous namespace

/**
 * Adds the given intervals to the given completion set.
 */
bool record_completion(lmdb::txn& txn, lmdb::dbi& completions, const std::string_view kind,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals,
		bool allow_overlap) {
	check_completions_key(kind);
	lmdb::cursor cur = lmdb::cursor::open(txn, completions);

	std::string_view dummy_kind = kind; //work around cur.get modifying kind
	if (!cur.get(dummy_kind, MDB_SET)) { //we're the first!
		std::string_view value(reinterpret_cast<const char*>(intervals.data()),
				intervals.size() * sizeof(pair<uint64_t, uint64_t>));
		if (!cur.put(dummy_kind, value))
			throw std::logic_error(fmt::format("can't happen: unable to insert novel completion key {}", kind));
		return true;
	}

	//While we won't overlap in the usual case, we have to check all the values
	//before concluding that, so we might as well parse the keys up front and
	//build an explicit representation of the LSM-ish tree.
	LSM lsm = parse_lsm(cur, kind);
	std::optional<unsigned int> l0page;
	if (!allow_overlap) {
		if (interval_overlap(lsm.main.first, lsm.main.second, intervals.cbegin(), intervals.cend()))
			return false;
		if (lsm.levels.empty())
			l0page = 0;
		else {
			//Check high to low levels, both because high levels are larger and because
			//we're going to look for space in L0.
			for (std::size_t l = lsm.levels.size(); l-- > 1;)
				for (LSM::range r : lsm.levels[l])
					if (interval_overlap(r.first, r.second, intervals.cbegin(), intervals.cend()))
						return false;
			auto novel = intervals.size();
			bool found_adjacent_page = false;
			for (unsigned int i = 0; i < lsm.levels[0].size(); ++i) {
				LSM::range r = lsm.levels[0][i];
				overlap_result o = interval_overlap(r.first, r.second, intervals.cbegin(), intervals.cend());
				if (o == overlap_result::overlap)
					return false;
				//First-fit, but preferring adjacent pages.  We can't break the loop
				//because we still have to check for overlap.
				if (found_adjacent_page || (l0page && o != overlap_result::adjacent))
					continue;
				auto existing = std::distance(r.first, r.second);
				//If we're adjacent, we will merge at least one interval.
				if (existing + novel - (o == overlap_result::adjacent) <= LSM::page_size) {
					l0page = i;
					found_adjacent_page = o == overlap_result::adjacent;
				}
			}
		}
	} else {
		//Even when overlap is allowed, it's uncommon, so we don't bother
		//subtracting from intervals or looking for a merge.  We can stop as
		//soon as we find space.
		if (lsm.levels.empty())
			l0page = 0;
		else {
			for (unsigned int i = 0; i < lsm.levels[0].size(); ++i)
				if (std::distance(lsm.levels[0][i].first, lsm.levels[0][i].second) + intervals.size() <= LSM::page_size) {
					l0page = i;
					break;
				}
		}
	}

	if (!l0page)
		//If we didn't find space, maybe we can add a page.
		if (lsm.levels[0].size() < LSM::N)
			l0page = lsm.levels[0].size(); //we'll check later not to merge with an existing key
		else {
			//Otherwise, we have to compact level 0, and maybe more levels.
			//There's no looking for space at the higher levels, just merging.
			compact_lsm(lsm, cur);
			assert(lsm.levels[0].empty());
			l0page = 0;
		}

	std::string key = make_lsm_key(lsm.kind, 0, *l0page);
	std::string_view key_view = key;
	if (!lsm.levels.empty() && *l0page < lsm.levels[0].size()) {
		auto real_intervals = interval_union(lsm.levels[0][*l0page].first, lsm.levels[0][*l0page].second,
				intervals.cbegin(), intervals.cend());
		std::string_view value(reinterpret_cast<const char*>(real_intervals.data()),
				real_intervals.size() * sizeof(pair<uint64_t, uint64_t>));
		if (!cur.put(key_view, value))
			throw std::logic_error(fmt::format("can't happen: failed to replace page {}", key));
	} else {
		std::string_view value(reinterpret_cast<const char*>(intervals.data()),
				intervals.size() * sizeof(pair<uint64_t, uint64_t>));
		if (!cur.put(key_view, value, MDB_NOOVERWRITE))
			throw std::logic_error(fmt::format("can't happen: failed to create new page {}", key));
	}
	return true;
}

std::vector<std::pair<std::uint64_t, std::uint64_t>> subtract_completion(
		lmdb::txn& txn, lmdb::dbi& completions, const std::string_view kind,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals) {
	check_completions_key(kind);
	//We subtract each key in turn, starting with the largest.  Usually we'll
	//have compacted first, but there's no need to mandate that.
	lmdb::cursor cur = lmdb::cursor::open(txn, completions);
	std::string_view dummy_kind = kind; //work around cur.get modifying kind
	if (!cur.get(dummy_kind, MDB_SET))
		return intervals; //no completions yet
	LSM lsm = parse_lsm(cur, kind);
	vector<pair<uint64_t, uint64_t>> ret = interval_difference(intervals.begin(), intervals.end(), lsm.main.first, lsm.main.second);
	for (std::size_t i = lsm.levels.size(); i-- > 0;)
		for (LSM::range r : lsm.levels[i])
			ret = interval_difference(ret.begin(), ret.end(), r.first, r.second);
	return ret;
}
std::vector<std::pair<std::uint64_t, std::uint64_t>> subtract_completion(
		lmdb::env& env, lmdb::dbi& completions, const std::string_view kind,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals) {
	check_completions_key(kind);
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	auto ret = subtract_completion(txn, completions, kind, intervals);
	txn.commit();
	return ret;
}

std::vector<std::pair<std::uint64_t, std::uint64_t>> intersect_completion(
		lmdb::txn& txn, lmdb::dbi& completions, const std::string_view kind,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals) {
	check_completions_key(kind);
	//Intersect with every key and take the union of the results.  This is
	//inefficient but we currently only call this from the reporter, so there's
	//no time pressure and the driver should have compacted at the end of its run.
	lmdb::cursor cur = lmdb::cursor::open(txn, completions);
	std::string_view dummy_kind = kind; //work around cur.get modifying kind
	if (!cur.get(dummy_kind, MDB_SET))
		return {}; //no completions yet
	LSM lsm = parse_lsm(cur, kind);

	//fast path for fully compacted
	if (lsm.levels.empty())
		return interval_intersection(intervals.begin(), intervals.end(), lsm.main.first, lsm.main.second);

	vector<vector<pair<uint64_t, uint64_t>>> sources;
	sources.push_back(interval_intersection(intervals.begin(), intervals.end(), lsm.main.first, lsm.main.second));
	for (std::size_t i = lsm.levels.size(); i-- > 0;)
		for (LSM::range r : lsm.levels[i])
			sources.push_back(interval_intersection(intervals.begin(), intervals.end(), r.first, r.second));
	vector<LSM::range> ranges;
	ranges.reserve(sources.size());
	for (const auto& s : sources)
		ranges.emplace_back(s.data(), s.data() + s.size());
	return merge_ranges(ranges.begin(), ranges.end());
}

bool compact_completion(lmdb::txn& txn, lmdb::dbi& completions, const std::string_view kind,
		unsigned int tolerated_levels) {
	check_completions_key(kind);
	lmdb::cursor cur = lmdb::cursor::open(txn, completions);
	std::string_view dummy_kind = kind; //work around cur.get modifying kind
	if (!cur.get(dummy_kind, MDB_SET))
		return false;
	LSM lsm = parse_lsm(cur, kind);
	if (lsm.levels.size() <= tolerated_levels) return false;
	compact_lsm_full(lsm, cur);
	return true;
}
bool compact_completion(lmdb::env& env, lmdb::dbi& completions, const std::string_view kind,
		unsigned int tolerated_levels) {
	check_completions_key(kind);
	auto txn = lmdb::txn::begin(env);
	bool ret = compact_completion(txn, completions, kind, tolerated_levels);
	txn.commit();
	return ret;
}