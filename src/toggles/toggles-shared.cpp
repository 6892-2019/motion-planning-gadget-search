#include "precompiled.hpp"
#include "toggles-shared.hpp"
#include "stringutils.hpp"
#include "tsl/ordered_set.h"
#include "intervals.hpp"
#include <regex>

using std::vector;
using std::pair;
using std::uint64_t;
using namespace std::literals::string_view_literals;

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
std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> select_gadget_id_to_data0(
		lmdb::env& env, lmdb::txn& txn, lmdb::dbi& gadget_hashtable,
		vector<pair<uint64_t, std::size_t>>& id_to_hash) {
	//TODO: merge with the other std::get-based comparators
	std::sort(id_to_hash.begin(), id_to_hash.end(), [](const auto& a, const auto& b) {
		return std::get<1>(a) < std::get<1>(b);
	});
	std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> ret;
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
		vector<std::byte> value_copy;
		value_copy.resize(value.size());
		std::memcpy(value_copy.data(), value.data(), value.size());
		ret.emplace_back(p.first, std::move(value_copy));
	}
	txn.commit();
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
	return select_gadget_id_to_data0(env, txn, gadget_hashtable, id_to_hash);
}
std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> select_gadget_id_to_data(
		lmdb::env& env, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& gid_intervals) {
	assert(std::is_sorted(gid_intervals.begin(), gid_intervals.end()));
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
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
	return select_gadget_id_to_data0(env, txn, gadget_hashtable, id_to_hash);
}

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
	auto comp_range = get_completions_key(env, txn, completions, kind);
	if (!comp_range.first)
		return intervals;
	auto ret = interval_difference(intervals.begin(), intervals.end(), comp_range.first, comp_range.second);
	txn.commit();
	return ret;
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
	vector<pair<uint64_t, uint64_t>> accum;
	vector<uint64_t> buf;
	buf.reserve(256);

	auto drain_buffer = [&]() {
		//TODO: I guess we might want to abort the txn and renew it and the
		//cursor before we return.  need to be careful with key/value lifetime
		std::sort(buf.begin(), buf.end());
		buf.erase(std::unique(buf.begin(), buf.end()), buf.end());
		//TODO: we could avoid this temporary with a maximal_intervals overload
		//using an output iterator (a back_inserter into accum);
		auto ints = maximal_intervals(buf.begin(), buf.end());
		accum.insert(accum.end(), ints.begin(), ints.end());
		buf.clear();
	};

	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	lmdb::cursor cur = lmdb::cursor::open(txn, edge_db);
	for (const pair<uint64_t, uint64_t>& p : sources) {
		std::string_view key = lmdb::to_sv(p.first), value;
		if (!cur.get(key, value, MDB_SET_RANGE))
			break; //reached end of database
		while (lmdb::from_sv<uint64_t>(key) < p.second) {
			//If we have to replace this pointer-based code for alignment etc.,
			//we can instead template this function on sizeof(Edge), relying on
			//'output' being the first member.  (Or maybe still template on
			//Edge, but use sizeof/offsetof to achieve the same.)
			if (value.size() == 0 || value.size() % sizeof(Edge) != 0)
				throw std::logic_error(fmt::format("edge data of type {} has value length {} (not a multiple of {})",
						//We want the dbi's name here, but I don't see how to get it.
						//The message won't distinguish close and mirror.
						typeid(Edge).name(), value.size(), sizeof(Edge)));
			const Edge* first = reinterpret_cast<const Edge*>(value.data());
			const Edge* last = first + value.size() / sizeof(Edge);
			std::size_t count = numeric_cast<std::size_t>(last - first);
			if (count > buf.capacity())
				throw std::logic_error(fmt::format("target buffer has capacity {}, but key {} has {} edges of type {}",
						buf.capacity(), lmdb::from_sv<uint64_t>(key), count, typeid(Edge).name()));
			if (count > buf.capacity() - buf.size())
				drain_buffer();
			while (first != last)
				buf.push_back(first++->output);

			if (!cur.get(key, value, MDB_NEXT)) break;
		}
	}
	drain_buffer();
	txn.commit();

	std::sort(accum.begin(), accum.end());
	return interval_coalesce(accum.cbegin(), accum.cend());
}

//explicitly instantiate the three we need
template std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_edges<CombineEdge>(
		lmdb::env& env,	lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources);
template std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_edges<ConnectEdge>(
		lmdb::env& env,	lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources);
template std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_edges<SimpleEdge>(
		lmdb::env& env,	lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources);