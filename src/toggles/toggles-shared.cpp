#include "precompiled.hpp"
#include "toggles-shared.hpp"
#include "stringutils.hpp"
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

namespace {
std::string build_missing_gadget_ids_query_immediate(const vector<uint64_t>& gids) {
	vector<std::string> things;
	things.reserve(gids.size());
	for (auto i : gids)
		things.push_back(fmt::format("({})", i));
	return "select * from (values " +
			join(things, ", ") +
			") as maybe(id) where not exists (select 1 from gadgets where gadgets.id = maybe.id limit 1)";
}

std::string build_missing_gadget_id_ranges_query_immediate(const vector<pair<uint64_t, uint64_t>>& ranges) {
	vector<std::string> things;
	things.reserve(ranges.size());
	for (auto r : ranges)
		things.push_back(fmt::format("(int8range({}, {}))", r.first, r.second));
	return "select * from (values " +
			join(things, ", ") +
			") as maybe(r) where not exists (select 1 from gadgets where gadgets.id <@ maybe.r limit 1)";
}

std::string build_missing_names_query(std::size_t count) {
	//The other two missing queries are immediates, but we want parameterized
	//here to tolerate ' in gadget names.
	vector<std::string> things;
	things.reserve(count);
	for (std::size_t i = 1; i <= count; ++i)
		things.push_back(fmt::format("(${}::text)", i));
	return "select * from (values " +
			join(things, ", ") +
			") as maybe(name) where not exists (select 1 from names where names.name = maybe.name limit 1)";
}

std::string build_name_to_ids_query(std::size_t count) {
	vector<std::string> things;
	things.reserve(count);
	for (std::size_t i = 1; i <= count; ++i)
		things.push_back(fmt::format("${}::text", i));
	return "select gadget_id from names where name in (" + join(things, ", ") + ");";
}

std::string build_ids_from_specs_immediate(const vector<uint64_t>& gids, const vector<pair<uint64_t, uint64_t>>& ranges) {
	vector<std::string> ids, rstr;
	ids.reserve(gids.size());
	for (auto i : gids)
		ids.push_back(fmt::format("{}", i));
	rstr.reserve(ranges.size());
	for (auto r : ranges)
		rstr.push_back(fmt::format("int8range({}, {})", r.first, r.second));
	//'in ()' is a syntax error, so use known-invalid ids.
	if (ids.empty())
		ids.push_back("-1");
	if (rstr.empty())
		rstr.push_back("int8range(-2, -1)");
	return "select id from gadgets where id in (" + join(ids, ", ") + ") or id <@ any(array[" + join(rstr, ", ") + "]);";
}
} //anonymous namespace

vector<uint64_t> collect_initial_gadget_set(pqxx::connection& conn, const GadgetSet& gs) {
	return retry_db_operation([&]() {
		ro_transaction trans(conn);

		if (!gs.ids.empty()) {
			pqxx::result result = trans.exec(build_missing_gadget_ids_query_immediate(gs.ids));
			if (result.size()) {
				vector<std::string> missing;
				missing.reserve(result.size());
				for (const auto& r : result)
					missing.push_back(fmt::format("{}", r[0].as<uint64_t>()));
				throw std::runtime_error(fmt::format("ids not found in database: {}", join(missing, ", ")));
			}
		}
		if (!gs.ranges.empty()) {
			pqxx::result result = trans.exec(build_missing_gadget_id_ranges_query_immediate(gs.ranges));
			if (result.size()) {
				vector<std::string> missing;
				missing.reserve(result.size());
				for (const auto& r : result)
					missing.push_back(fmt::format("{}", r[0].c_str()));
				throw std::runtime_error(fmt::format("ranges did not contain any gadgets: {}", join(missing, ", ")));
			}
		}
		if (!gs.names.empty()) {
			pqxx::result result = trans.exec_params(build_missing_names_query(gs.names.size()),
					pqxx::prepare::make_dynamic_params(gs.names));
			if (result.size()) {
				vector<std::string> missing;
				missing.reserve(result.size());
				for (const auto& r : result)
					missing.push_back(r[0].c_str());
				throw std::runtime_error(fmt::format("unknown gadget names: {}", join(missing, ", ")));
			}
		}

		vector<uint64_t> ids = gs.ids;
		if (!gs.names.empty()) {
			pqxx::result result = trans.exec_params(build_name_to_ids_query(gs.names.size()),
					pqxx::prepare::make_dynamic_params(gs.names));
			for (const auto& r : result)
				ids.push_back(r[0].as<uint64_t>());
		}
		pqxx::result result = trans.exec(build_ids_from_specs_immediate(ids, gs.ranges));
		ids.clear();
		ids.reserve(result.size());
		for (const auto& r : result)
			ids.push_back(r[0].as<uint64_t>());

		trans.commit();
		return ids;
	}, 10, "collect_initial_gadget_set");
}