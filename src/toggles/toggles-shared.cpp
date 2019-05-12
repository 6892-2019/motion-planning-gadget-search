#include "precompiled.hpp"
#include "toggles-shared.hpp"
#include "stringutils.hpp"
#include "tsl/ordered_set.h"
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



std::string build_select_gadget_id_to_data_immediate(const std::vector<std::uint64_t>& gids) {
	//TODO: stringutils.hpp:join overload?  (use std::to_chars)
	vector<std::string> gids_as_strings;
	for (std::uint64_t t : gids)
		gids_as_strings.push_back(std::to_string(t));
	std::string in_clause_list = join(gids_as_strings, ",");
	return "select id, data from gadgets where id in ("+in_clause_list+")";
}

vector<pair<std::uint64_t, vector<std::byte>>> select_gadget_id_to_data(pqxx::connection& conn, const vector<std::uint64_t>& gids) {
	pqxx::result input_data = retry_db_operation([&](){
		ro_transaction trans(conn);
		pqxx::result result = trans.exec_n(gids.size(), build_select_gadget_id_to_data_immediate(gids));
		trans.commit();
		return result;
	}, 10, "select_gadget_id_to_data_immediate");

	//Returned rows are text internally, so we may as well convert now.
	vector<pair<std::uint64_t, vector<std::byte>>> inputs;
	for (const auto& row : input_data) {
		std::uint64_t gid = row.at(0).as<std::uint64_t>();
		pqxx::binarystring data(row.at(1));
		vector<std::byte> bytes;
		bytes.insert(bytes.begin(), reinterpret_cast<const std::byte*>(data.begin()), reinterpret_cast<const std::byte*>(data.end()));
		inputs.emplace_back(gid, std::move(bytes));
	}
	return inputs;
}