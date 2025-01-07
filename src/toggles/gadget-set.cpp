#include "precompiled.hpp"
#include "gadget-set.hpp"
#include "select-by-id.hpp"
#include "stringutils.hpp"
#include <tsl/ordered_set.h>
#include <regex>

using std::vector;
using std::pair;
using std::uint64_t;

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
	uint64_t max_id = get_current_max_gadget_id(txn, gadget_index);
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
	vector<uint64_t> ret = set.release();
	std::sort(ret.begin(), ret.end());
	return ret;
}
