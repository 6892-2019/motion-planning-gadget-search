#include "precompiled.hpp"
#include "database.hpp"
#include "stringutils.hpp"
#include "ioutils.hpp"
#include <regex>

using std::vector;
using std::pair;
using std::uint64_t;
using namespace std::literals::string_view_literals;

struct GadgetSet {
	vector<uint64_t> ids;
	vector<pair<uint64_t, uint64_t>> ranges; //inclusive, exclusive
	vector<std::string> names;
};

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

std::string build_filter_ids_range_table(std::size_t count, std::string_view table) {
	vector<std::string> things;
	things.reserve(count);
	for (std::size_t i = 1; i <= count; ++i)
		things.push_back(fmt::format("(${}::int8)", i));
	return "select * from (values " +
			join(things, ", ") +
			fmt::format(") as maybe(id) where not exists (select 1 from {} where maybe.id <@ {}.r)", table, table);
}

std::string build_filter_ids_needing_mirror(std::size_t count) {
	return build_filter_ids_range_table(count, "completed_mirrors");
}
std::string build_filter_ids_needing_close(std::size_t count) {
	return build_filter_ids_range_table(count, "completed_closes");
}
std::string build_filter_ids_needing_connect(std::size_t count) {
	//TODO: probably also want to filter out any gadgets too small to connect
	return build_filter_ids_range_table(count, "completed_connects");
}



const std::pair<std::size_t, std::string_view> filter_mirrors_prepared[] = {
	{50000, "filter_mirrors_50000"sv},
	{25000, "filter_mirrors_25000"sv},
	{10000, "filter_mirrors_10000"sv},
	{5000, "filter_mirrors_5000"sv},
	{1000, "filter_mirrors_1000"sv},
	{500, "filter_mirrors_500"sv},
};

void prepare_statements(pqxx::connection& conn) {
	for (const auto& p : filter_mirrors_prepared)
		conn.prepare(std::string(p.second), build_filter_ids_needing_mirror(p.first));
}


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
		vector<pair<uint64_t, uint64_t>> ranges = gs.ranges;
		pqxx::result result = trans.exec(build_ids_from_specs_immediate(ids, gs.ranges));
		ids.clear();
		ids.reserve(result.size());
		for (const auto& r : result)
			ids.push_back(r[0].as<uint64_t>());

		trans.commit();
		return ids;
	}, 10, "collect_initial_gadget_set");
}

vector<uint64_t> filter_ids_needing_mirror(pqxx::connection& conn, const std::vector<uint64_t>& ids) {
	return retry_db_operation([&]() {
		ro_transaction trans(conn);
		vector<uint64_t> needs;
		std::size_t cur = 0;
		for (const auto& p : filter_mirrors_prepared) {
			while (ids.size() - cur >= p.first) {
				pqxx::result rows = trans.exec_prepared(std::string(p.second),
						pqxx::prepare::make_dynamic_params(ids.begin()+cur, ids.begin()+cur+p.first));
				for (const auto& r : rows)
					needs.push_back(r[0].as<uint64_t>());
				cur += p.first;
			}
		}
		if (ids.size() - cur > 0) {
			pqxx::result rows = trans.exec_params(build_filter_ids_needing_mirror(ids.size() - cur),
					pqxx::prepare::make_dynamic_params(ids.begin()+cur, ids.end()));
			for (const auto& r : rows)
				needs.push_back(r[0].as<uint64_t>());
		}
		trans.commit();
		return needs;
	}, 10, "filter_ids_needing_mirror");
}

int main(int argc, char* argv[]) { //genbuild entrypoint
	std::string_view db_user = "jbosboom", db_pass = "", db_host = "127.0.0.1",
			db_port = "5432", db_name = "togglesearch";
	std::vector<std::string> worker_addrs; //or @foo for response files
	std::string_view checkpoint_file = ""; //TODO: split into resume file and path to save new checkpoints
	bool multiplayer = false;
	std::vector<std::string_view> gid_specs;
	for (int i = 1; i < argc; ++i) {
		if (argv[i] == "--db-user"sv)
			db_user = argv[++i];
		else if (argv[i] == "--db-pass"sv)
			db_pass = argv[++i];
		else if (argv[i] == "--db-host"sv)
			db_host = argv[++i];
		else if (argv[i] == "--db-port"sv)
			db_port = argv[++i];
		else if (argv[i] == "--db-name"sv)
			db_name = argv[++i];
		else if (argv[i] == "--worker"sv)
			worker_addrs.emplace_back(argv[++i]);
		else if (argv[i] == "--checkpoint"sv)
			checkpoint_file = argv[++i];
		else if (argv[i] == "--multiplayer"sv)
			multiplayer = true;
		else
			gid_specs.emplace_back(argv[i]);
	}

	if (worker_addrs.empty()) {
		fmt::print("ERROR: no worker address arguments given, exiting\n");
		return 1;
	}
	worker_addrs = processFilenameArgs(std::move(worker_addrs));
	if (worker_addrs.empty()) {
		fmt::print("ERROR: no worker addresses after processing response files, exiting\n");
		return 1;
	}

	GadgetSet spec = parse_gid_specs(gid_specs);

	std::string connect_str = format_connect_string(db_user, db_pass, db_host, db_port, db_name);
	pqxx::connection conn(connect_str);
	prepare_statements(conn);
	vector<uint64_t> initial = collect_initial_gadget_set(conn, spec);
	vector<uint64_t> needs_mirror = filter_ids_needing_mirror(conn, initial);
	for (auto i : needs_mirror)
		fmt::print("{} ", i);
	fmt::print("\n");

	return 0;
}