#include "precompiled.hpp"
#include "database.hpp"
#include "rpc.hpp"
#include "toggles-shared.hpp"
#include "stringutils.hpp"
#include "ioutils.hpp"
#include "stopwatch.hpp"
#define BOOST_ASIO_SEPARATE_COMPILATION
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <regex>
#include <system_error>

using std::vector;
using std::pair;
using std::uint64_t;
using namespace std::literals::string_view_literals;
namespace asio = boost::asio;
using asio::ip::tcp;

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
	//Also filter out any gadgets too small to connect.
	return build_filter_ids_range_table(count, "completed_connects") +
			"and (select locations from gadgets where gadgets.id = maybe.id) >= 4";
}

std::string build_get_mirrors_query(std::size_t count) {
	vector<std::string> things;
	things.reserve(count);
	for (std::size_t i = 1; i <= count; ++i)
		things.push_back(fmt::format("${}::int8", i));
	std::string parameters = join(things, ", ");
	return "select b from mirror_edges where a in (" +
			parameters +
			")\n" +
			"union all\n" +
			"select a from mirror_edges where b in (" +
			parameters +
			")";
}

std::string build_follow_close_edges_query(std::size_t count) {
	vector<std::string> things;
	things.reserve(count);
	for (std::size_t i = 1; i <= count; ++i)
		things.push_back(fmt::format("${}::int8", i));
	return "select input1, output1 from close_edges where input1 in (" +
			join(things, ", ") +
			")";
}

std::string build_get_connects_query(std::size_t count) {
	vector<std::string> things;
	things.reserve(count);
	for (std::size_t i = 1; i <= count; ++i)
		things.push_back(fmt::format("${}::int8", i));
	return "select output1 from connect_edges where input1 in (" +
			join(things, ", ") +
			")";
}

std::string build_required_combines_query(std::size_t left_count, std::size_t right_count, unsigned int precision) {
	vector<std::string> things;
	things.reserve(std::max(left_count, right_count));
	for (std::size_t i = 1; i <= left_count; ++i)
		things.push_back(fmt::format("(${}::int8)", i));
	std::string left_params = join(things, ", ");
	things.clear();
	for (std::size_t i = left_count + 1; i <= left_count + right_count; ++i)
		things.push_back(fmt::format("(${}::int8)", i));
	//TODO: we actually only care to get back rows with nonempty array, but I
	//can't see how to filter them out.
	return "with lefts (lid) as (values\n" +
			left_params +
			"\n), rights (rid) as (values\n" +
			join(things, ", ") +
			"\n)\n" +
			"select lefts.lid, array(select rid from rights where\n" +
			"  not exists (select 1 from combine_edges where\n" +
			"    input1 = lid and input2 = rid limit 1)\n" +
			"  and\n" +
			//can't select a sum because we might combine a gadget against itself
			"  (select locations from gadgets where id = lid) + (select locations from gadgets where id = rid)\n" +
			fmt::format("    <= {}\n", precision) +
			") from lefts";
}

std::string build_get_combines_query(std::size_t left_count, std::size_t right_count) {
	vector<std::string> things;
	things.reserve(std::max(left_count, right_count));
	for (std::size_t i = 1; i <= left_count; ++i)
		things.push_back(fmt::format("(${}::int8)", i));
	std::string left_params = join(things, ", ");
	things.clear();
	for (std::size_t i = left_count + 1; i <= left_count + right_count; ++i)
		things.push_back(fmt::format("(${}::int8)", i));
	return "select output1 from combine_edges where input1 in (\n" +
			left_params +
			"\n) and input2 in (\n" +
			join(things, ", ") +
			"\n) group by output1"; //group by is apparently faster than select distinct
}

const std::pair<std::size_t, std::string_view> filter_close_prepared[] = {
	{50000, "filter_close_50000"sv},
	{25000, "filter_close_25000"sv},
	{10000, "filter_close_10000"sv},
	{5000, "filter_close_5000"sv},
	{1000, "filter_close_1000"sv},
	{500, "filter_close_500"sv},
};
const std::pair<std::size_t, std::string_view> follow_close_edges_prepared[] = {
	{50000, "follow_close_50000"sv},
	{25000, "follow_close_25000"sv},
	{10000, "follow_close_10000"sv},
	{5000, "follow_close_5000"sv},
	{1000, "follow_close_1000"sv},
	{500, "follow_close_500"sv},
};
const std::pair<std::size_t, std::string_view> filter_mirrors_prepared[] = {
	{50000, "filter_mirrors_50000"sv},
	{25000, "filter_mirrors_25000"sv},
	{10000, "filter_mirrors_10000"sv},
	{5000, "filter_mirrors_5000"sv},
	{1000, "filter_mirrors_1000"sv},
	{500, "filter_mirrors_500"sv},
};
const std::pair<std::size_t, std::string_view> get_mirrors_prepared[] = {
	{50000, "get_mirrors_50000"sv},
	{25000, "get_mirrors_25000"sv},
	{10000, "get_mirrors_10000"sv},
	{5000, "get_mirrors_5000"sv},
	{1000, "get_mirrors_1000"sv},
	{500, "get_mirrors_500"sv},
};
const std::pair<std::size_t, std::string_view> filter_connect_prepared[] = {
	{50000, "filter_connect_50000"sv},
	{25000, "filter_connect_25000"sv},
	{10000, "filter_connect_10000"sv},
	{5000, "filter_connect_5000"sv},
	{1000, "filter_connect_1000"sv},
	{500, "filter_connect_500"sv},
};
const std::pair<std::size_t, std::string_view> get_connects_prepared[] = {
	{50000, "get_connects_50000"sv},
	{25000, "get_connects_25000"sv},
	{10000, "get_connects_10000"sv},
	{5000, "get_connects_5000"sv},
	{1000, "get_connects_1000"sv},
	{500, "get_connects_500"sv},
};

void prepare_statements(pqxx::connection& conn) {
	for (const auto& p : filter_close_prepared)
		conn.prepare(std::string(p.second), build_filter_ids_needing_close(p.first));
	for (const auto& p : follow_close_edges_prepared)
		conn.prepare(std::string(p.second), build_follow_close_edges_query(p.first));
	for (const auto& p : filter_mirrors_prepared)
		conn.prepare(std::string(p.second), build_filter_ids_needing_mirror(p.first));
	for (const auto& p : get_mirrors_prepared)
		conn.prepare(std::string(p.second), build_get_mirrors_query(p.first));
	for (const auto& p : filter_connect_prepared)
		conn.prepare(std::string(p.second), build_filter_ids_needing_connect(p.first));
	for (const auto& p : get_connects_prepared)
		conn.prepare(std::string(p.second), build_get_connects_query(p.first));
}

vector<std::pair<std::size_t, std::string>> required_combines_prepared;
const unsigned int required_combines_prepared_batch_sizes[] = {50000, 25000, 10000, 5000, 1000, 500};
vector<std::pair<std::size_t, std::string>> get_combines_prepared;
const unsigned int get_combines_prepared_batch_sizes[] = {50000, 25000, 10000, 5000, 1000, 500};

void prepare_combine_statements(pqxx::connection& conn, std::size_t right_count, unsigned int precision) {
	for (unsigned int left_count : required_combines_prepared_batch_sizes) {
		std::string name = fmt::format("required_combines_{}", left_count);
		conn.prepare(name, build_required_combines_query(left_count, right_count, precision));
		required_combines_prepared.emplace_back(left_count, std::move(name));
	}
	for (unsigned int batch_size : get_combines_prepared_batch_sizes) {
		std::size_t left_count = std::max<std::size_t>(batch_size / right_count, 1);
		std::string name = fmt::format("get_combines_{}", left_count);
		conn.prepare(name, build_get_combines_query(left_count, right_count));
		get_combines_prepared.emplace_back(left_count, std::move(name));
	}
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

vector<uint64_t> id_to_id_db_op(pqxx::connection& conn,
		const vector<uint64_t>::const_iterator ids_begin,
		const vector<uint64_t>::const_iterator ids_end,
		const std::pair<std::size_t, std::string_view>* prepared_begin,
		const std::pair<std::size_t, std::string_view>* prepared_end,
		std::string(*query_func)(std::size_t)) {
	ro_transaction trans(conn);
	vector<uint64_t> result;
	vector<uint64_t>::const_iterator cur = ids_begin;
	for (const auto& p : make_range_for_pair(prepared_begin, prepared_end)) {
		while (numeric_cast<std::size_t>(std::distance(cur, ids_end)) >= p.first) {
			pqxx::result rows = trans.exec_prepared(std::string(p.second),
					pqxx::prepare::make_dynamic_params(cur, cur + p.first));
			for (const auto& r : rows)
				result.push_back(r[0].as<uint64_t>());
			cur += p.first;
		}
	}
	if (cur != ids_end) {
		pqxx::result rows = trans.exec_params(query_func(std::distance(cur, ids_end)),
				pqxx::prepare::make_dynamic_params(cur, ids_end));
		for (const auto& r : rows)
			result.push_back(r[0].as<uint64_t>());
	}
	trans.commit();
	return result;
}

//vector<uint64_t> id_to_id_db_op(pqxx::connection& conn, const std::vector<uint64_t>& ids,
//		const std::pair<std::size_t, std::string_view>* prepared_begin,
//		const std::pair<std::size_t, std::string_view>* prepared_end,
//		std::string(*query_func)(std::size_t)) {
//	return id_to_id_db_op(conn, ids.cbegin(), ids.cend(), prepared_begin, prepared_end, query_func);
//}

vector<uint64_t> filter_ids_needing_close(pqxx::connection& conn, const vector<uint64_t>::const_iterator ids_begin,
		const vector<uint64_t>::const_iterator ids_end) {
	return retry_db_operation([&]() {
		return id_to_id_db_op(conn, ids_begin, ids_end,
				std::begin(filter_close_prepared), std::end(filter_close_prepared),
				&build_filter_ids_needing_close);
	}, 10, "filter_ids_needing_close");
}

pair<tsl::hopscotch_set<uint64_t>, vector<uint64_t>> follow_close_edges(pqxx::connection& conn,
		const vector<uint64_t>::const_iterator ids_begin, const vector<uint64_t>::const_iterator ids_end) {
	return retry_db_operation([&]() {
		ro_transaction trans(conn);
		//inputs aren't duplicated, but quickly removing them from the SearchState requires having them in a set
		tsl::hopscotch_set<uint64_t> inputs;
		//outputs might be duplicated, but we'll dedup when adding them to the SearchState.
		vector<uint64_t> outputs;
		vector<uint64_t>::const_iterator cur = ids_begin;
		for (const auto& p : follow_close_edges_prepared) {
			while (numeric_cast<std::size_t>(std::distance(cur, ids_end)) >= p.first) {
				pqxx::result rows = trans.exec_prepared(std::string(p.second),
						pqxx::prepare::make_dynamic_params(cur, cur + p.first));
				for (const auto& r : rows) {
					inputs.insert(r[0].as<uint64_t>());
					outputs.push_back(r[1].as<uint64_t>());
				}
				cur += p.first;
			}
		}
		if (cur != ids_end) {
			pqxx::result rows = trans.exec_params(build_follow_close_edges_query(std::distance(cur, ids_end)),
					pqxx::prepare::make_dynamic_params(cur, ids_end));
			for (const auto& r : rows) {
				inputs.insert(r[0].as<uint64_t>());
				outputs.push_back(r[1].as<uint64_t>());
			}
		}
		trans.commit();
		return std::make_pair(std::move(inputs), std::move(outputs));
	}, 10, "follow_close_edges");
}

vector<uint64_t> filter_ids_needing_mirror(pqxx::connection& conn, const vector<uint64_t>::const_iterator ids_begin,
		const vector<uint64_t>::const_iterator ids_end) {
	return retry_db_operation([&]() {
		return id_to_id_db_op(conn, ids_begin, ids_end,
				std::begin(filter_mirrors_prepared), std::end(filter_mirrors_prepared),
				&build_filter_ids_needing_mirror);
	}, 10, "filter_ids_needing_mirror");
}

vector<uint64_t> get_mirrors(pqxx::connection& conn, const vector<uint64_t>::const_iterator ids_begin,
		const vector<uint64_t>::const_iterator ids_end) {
	return retry_db_operation([&]() {
		return id_to_id_db_op(conn, ids_begin, ids_end,
				std::begin(get_mirrors_prepared), std::end(get_mirrors_prepared),
				&build_get_mirrors_query);
	}, 10, "get_mirrors");
}

vector<uint64_t> filter_ids_needing_connect(pqxx::connection& conn, const vector<uint64_t>::const_iterator ids_begin,
		const vector<uint64_t>::const_iterator ids_end) {
	return retry_db_operation([&]() {
		return id_to_id_db_op(conn, ids_begin, ids_end,
				std::begin(filter_connect_prepared), std::end(filter_connect_prepared),
				&build_filter_ids_needing_connect);
	}, 10, "filter_ids_needing_connect");
}

vector<uint64_t> get_connects(pqxx::connection& conn, const vector<uint64_t>::const_iterator ids_begin,
		const vector<uint64_t>::const_iterator ids_end) {
	return retry_db_operation([&]() {
		return id_to_id_db_op(conn, ids_begin, ids_end,
				std::begin(get_connects_prepared), std::end(get_connects_prepared),
				&build_get_connects_query);
	}, 10, "get_connects");
}

struct vector_hash {
	std::size_t operator()(const std::vector<uint64_t>& v) const {
		return farmhash::Hash(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(v.front()));
	}
};

//Note the key is the list of right ids and the value is the list of left ids (they're swapped).
using combines_map = tsl::hopscotch_map<vector<uint64_t>, vector<uint64_t>, vector_hash>;
/**
 * Finds required combines.
 * @return a map of lists of right ids to the left ids that need to be combined against them
 */
combines_map find_required_combines(pqxx::connection& conn, const std::vector<uint64_t>& left_ids,
		const std::vector<uint64_t>& right_ids, unsigned int precision) {
	return retry_db_operation([&]() {
		ro_transaction trans(conn);
		combines_map result;
		vector<uint64_t> temp_key;
		std::pair<pqxx::array_parser::juncture, std::string> array_element;
		auto process_rows = [&](const pqxx::result& rows) {
			for (const auto& r : rows) {
				pqxx::array_parser parser = r[1].as_array();
				temp_key.clear();
				while ((array_element = parser.get_next()).first != pqxx::array_parser::done)
					if (array_element.first == pqxx::array_parser::string_value)
						temp_key.push_back(to_uint64(array_element.second));
				if (!temp_key.empty()) {
					std::sort(temp_key.begin(), temp_key.end());
					auto it = result.find(temp_key);
					if (it == result.end())
						it = result.try_emplace(std::move(temp_key)).first;
					it.value().push_back(r[0].as<uint64_t>());
				}
			}
		};

		std::size_t cur = 0;
		for (const auto& p : required_combines_prepared) {
			while (left_ids.size() - cur >= p.first) {
				pqxx::result rows = trans.exec_prepared(std::string(p.second),
						pqxx::prepare::make_dynamic_params(left_ids.begin()+cur, left_ids.begin()+cur+p.first),
						pqxx::prepare::make_dynamic_params(right_ids.begin(), right_ids.end()));
				process_rows(rows);
				cur += p.first;
			}
		}
		if (left_ids.size() - cur > 0) {
			pqxx::result rows = trans.exec_params(build_required_combines_query(left_ids.size() - cur, right_ids.size(), precision),
					pqxx::prepare::make_dynamic_params(left_ids.begin()+cur, left_ids.end()),
					pqxx::prepare::make_dynamic_params(right_ids.begin(), right_ids.end()));
			process_rows(rows);
		}
		trans.commit();
		return result;
	});
}

vector<uint64_t> get_combines(pqxx::connection& conn, const std::vector<uint64_t>& left_ids,
		const std::vector<uint64_t>& right_ids, unsigned int precision) {
	return retry_db_operation([&]() {
		ro_transaction trans(conn);
		vector<uint64_t> result;
		std::size_t cur = 0;
		for (const auto& p : get_combines_prepared) {
			while (left_ids.size() - cur >= p.first) {
				pqxx::result rows = trans.exec_prepared(std::string(p.second),
						pqxx::prepare::make_dynamic_params(left_ids.begin()+cur, left_ids.begin()+cur+p.first),
						pqxx::prepare::make_dynamic_params(right_ids.begin(), right_ids.end()));
				for (const auto& r : rows)
					result.push_back(r[0].as<uint64_t>());
				cur += p.first;
			}
		}
		if (left_ids.size() - cur > 0) {
			pqxx::result rows = trans.exec_params(build_get_combines_query(left_ids.size() - cur, right_ids.size()),
					pqxx::prepare::make_dynamic_params(left_ids.begin()+cur, left_ids.end()),
					pqxx::prepare::make_dynamic_params(right_ids.begin(), right_ids.end()));
			for (const auto& r : rows)
				result.push_back(r[0].as<uint64_t>());
		}
		trans.commit();
		return result;
	});
}

struct WorkGenerator {
	virtual bool next(simple_buffer& buffer) = 0;
	//caller should unpack into rpc Response object and use sequence number to
	//match with produced work
	virtual void process(simple_buffer& response) = 0;
};

struct DelegateGenerator : WorkGenerator {
	DelegateGenerator(std::function<bool(simple_buffer&)> next, std::function<void(simple_buffer&)> process)
			: next_(std::move(next)), process_(std::move(process)) {}
	bool next(simple_buffer& buffer) override {return next_(buffer);}
	void process(simple_buffer& response) override {return process_(response);}
private:
	std::function<bool(simple_buffer&)> next_;
	std::function<void(simple_buffer&)> process_;
};

class WorkerManager {
public:
	WorkerManager(const vector<std::string>& worker_addrs) : ctx_(), generator_(nullptr) {
		tcp::resolver resolver(ctx_);
		//glibc's getaddrinfo does no caching, so we have to.
		tsl::hopscotch_map<std::string, asio::ip::address> dns_cache;
		for (const std::string& addr : worker_addrs) {
			auto parts = rpartition(addr, ':');
			std::string& host = std::get<0>(parts), port = std::get<2>(parts);
			if (host.empty() || port.empty())
				throw std::runtime_error("bad worker address: "+addr);
			auto resolution = dns_cache.find(host);
			if (resolution == dns_cache.end()) {
				auto result = resolver.resolve(tcp::v4(), host, "");
				if (result.empty())
					throw std::runtime_error(fmt::format("unable to resolve {} from worker address {}", host, addr));
				if (result.size() > 1)
					fmt::print("warning: got {} results for host {}\n", result.size(), host);
				resolution = dns_cache.insert(std::pair(host, (*result.begin()).endpoint().address())).first;
			}
			workers_.emplace_back(resolution->second, to_ushort(port));
			sockets_.emplace_back(ctx_);
			buffers_.emplace_back();
		}
	}
	std::size_t size() const {
		return workers_.size();
	}
	void run(WorkGenerator* generator) {
		generator_ = generator;
		//Try to launch one task per worker.  Finish callbacks will pull further
		//work items when the worker's current item finishes.
		for (std::size_t i = 0; i < workers_.size(); ++i) {
			buffers_[i].clear();
			if (generator->next(buffers_[i]))
				dispatch_connect(i);
			else
				break;
		}
		ctx_.restart(); //seems safe to call this even the first time around
		ctx_.run();
		generator_ = nullptr;
	}
	void run(std::function<bool(simple_buffer&)> next, std::function<void(simple_buffer&)> process) {
		DelegateGenerator generator(std::move(next), std::move(process));
		run(&generator);
	}
private:
	asio::io_context ctx_;
	vector<tcp::endpoint> workers_;
	vector<tcp::socket> sockets_;
	vector<simple_buffer> buffers_; //a write buffer while writing, then a read buffer while reading
	WorkGenerator* generator_; //a non-owning pointer to the current generator, or nullptr

	void on_connect(std::size_t index, const boost::system::error_code& ec) {
		if (ec)
			throw std::system_error(ec, fmt::format("connecting to worker {} at endpoint {}:{}",
					index, workers_[index].address().to_string(), workers_[index].port()));
		dispatch_write(index);
	}

	void after_write(std::size_t index, const boost::system::error_code& ec, std::size_t bytes_transferred) {
		if (ec)
			throw std::system_error(ec, fmt::format("writing to worker {} at endpoint {}:{}, transferred {}",
					index, workers_[index].address().to_string(), workers_[index].port(), bytes_transferred));
		sockets_[index].shutdown(tcp::socket::shutdown_send); //send EOF signal to worker
		buffers_[index].clear();
		dispatch_read(index);
	}

	void after_read(std::size_t index, const boost::system::error_code& ec, std::size_t bytes_transferred) {
		if (!ec) {
			//If we completed without error, we filled the buffer but didn't reach EOF.
			//Enlarge the buffer and resume reading.
			buffers_[index].size(buffers_[index].size() + bytes_transferred);
			if (buffers_[index].size() != buffers_[index].capacity())
				throw std::runtime_error(fmt::format(
						"no error but buffer not full while reading from worker {} at endpoint {}:{}, transferred {}, previously transferred {}, capacity {}",
						index, workers_[index].address().to_string(), workers_[index].port(), bytes_transferred, buffers_[index].size(), buffers_[index].capacity()));
			buffers_[index].grow(buffers_[index].size() * 2); //TODO: growth policy?
			dispatch_read(index);
		} else if (ec == asio::error::eof) {
			//EOF means we read all the data available.
			buffers_[index].size(buffers_[index].size() + bytes_transferred);
			generator_->process(buffers_[index]);
			quiet_close(index);

			buffers_[index].clear();
			if (generator_->next(buffers_[index]))
				dispatch_connect(index);
			//TODO: We might want to shrink the buffer if we're going idle.
		} else
			throw std::system_error(ec, fmt::format("reading from worker {} at endpoint {}:{}, transferred {}, previously transferred {}",
					index, workers_[index].address().to_string(), workers_[index].port(), bytes_transferred, buffers_[index].size()));
	}

	void dispatch_connect(std::size_t index) {
		sockets_[index].async_connect(workers_[index], [=](const boost::system::error_code& ec){on_connect(index, ec);});
	}
	void dispatch_write(std::size_t index) {
		asio::async_write(sockets_[index], asio::const_buffer(buffers_[index].data(), buffers_[index].size()),
				[=](const boost::system::error_code& ec, std::size_t bytes){after_write(index, ec, bytes);});
	}
	void dispatch_read(std::size_t index) {
		auto& buf = buffers_[index];
		asio::async_read(sockets_[index],
				//asio's buffer view starts after the existing data, if any
				asio::mutable_buffer(reinterpret_cast<char*>(buf.data()) + buf.size(), buf.capacity() - buf.size()),
				[=](const boost::system::error_code& ec, std::size_t bytes){after_read(index, ec, bytes);});
	}
	/**
	 * Closes a socket, ignoring errors we don't care about.  (There's an
	 * inherent race between us closing the socket and the other end closing it,
	 * so we can't check for errors first.)
	 */
	void quiet_close(std::size_t index) {
		boost::system::error_code ec;
		sockets_[index].shutdown(tcp::socket::shutdown_receive, ec);
		if (ec && ec != asio::error::not_connected)
			throw std::system_error(ec, fmt::format("closing worker {} at endpoint {}:{}",
					index, workers_[index].address().to_string(), workers_[index].port()));
		sockets_[index].close();
	}
};

void ping_all_workers(WorkerManager& manager) {
	struct PingGenerator : public WorkGenerator {
		PingGenerator(std::size_t worker_count) : i(0), max(worker_count) {}
		std::size_t i, max;
		bool next(simple_buffer& buffer) override {
			if (i < max) {
				buffer.clear();
				pack_call(buffer, numeric_cast<std::uint32_t>(i), "ping");
				++i;
				return true;
			}
			return false;
		}
		void process(simple_buffer& buffer) override {
			Response response = unpack_response(buffer);
			if (response)
				fmt::print("worker {}: {}\n", response.seq(), response.result_as<std::string>());
			else
				fmt::print(stderr, "worker {} ping error: {}", response.seq(), response.error_as());
		}
	} generator(manager.size());
	manager.run(&generator);
}

DatabaseOperationStatistics do_unary_operation(WorkerManager& manager, std::string_view operation, const vector<uint64_t>& operands) {
	std::uint32_t seqno = 0;
	std::size_t offset = 0;
	const std::size_t batch_size = 5000; //TODO: should scale against number of workers, be customizable
	//TODO: figure out how to pack a range without copying (probably a custom type with a pack method?)
	vector<uint64_t> range;
	DatabaseOperationStatistics overall_stats = {};
	bool error_happened = false;
	manager.run([&](simple_buffer& buffer) {
		if (error_happened) return false; //stop generating work, but let existing issued work finish
		if (!(offset < operands.size())) return false;
		range.clear();
		std::size_t end = std::min(offset + batch_size, operands.size());
		range.insert(range.end(), operands.begin()+offset, operands.begin()+end);
		pack_call(buffer, seqno++, operation, range);
		offset = end;
		return true;
	}, [&](simple_buffer& buffer) {
		Response resp = unpack_response(buffer);
		if (!resp) {
			fmt::print("ERROR: task {} failed: {}\n", resp.seq(), resp.error_as());
			error_happened = true;
		} else {
			DatabaseOperationStatistics stats = resp.result_as<DatabaseOperationStatistics>();
			fmt::print("task {} completed: {} locally pruned, {} globally pruned, {} discovered, {} edges\n",
					resp.seq(), stats.pruned_locally, stats.pruned_database, stats.novel_gadgets, stats.edges);
			overall_stats += stats;
		}
	});
	if (error_happened)
		throw std::runtime_error("one or more tasks failed; exiting to prevent generating a corrupt checkpoint");
	return overall_stats;
}

DatabaseOperationStatistics do_combine_operation(WorkerManager& manager, const combines_map& operands, unsigned int precision) {
	const std::size_t batch_size = 5000; //TODO: should scale against number of workers, be customizable
	std::uint32_t seqno = 0;
	combines_map::const_iterator cur = operands.begin();
	std::size_t offset = 0;
	vector<uint64_t> lefts; //rights is just cur->first
	DatabaseOperationStatistics overall_stats = {};
	bool error_happened = false;
	manager.run([&](simple_buffer& buffer) {
		if (error_happened) return false; //stop generating work, but let existing issued work finish
		if (cur == operands.end()) return false;
		lefts.clear();
		std::size_t lefts_size = std::max<std::size_t>(batch_size / cur->first.size(), 1);
		std::size_t end = std::min(offset + lefts_size, cur->second.size());
		lefts.insert(lefts.end(), cur->second.begin()+offset, cur->second.begin()+end);
		pack_call(buffer, seqno++, "combine-db", lefts, cur->first, precision);
		offset = end;
		if (offset == cur->second.size()) {
			offset = 0;
			++cur;
		}
		return true;
	}, [&](simple_buffer& buffer) {
		Response resp = unpack_response(buffer);
		if (!resp) {
			fmt::print("ERROR: task {} failed: {}\n", resp.seq(), resp.error_as());
			error_happened = true;
		} else {
			DatabaseOperationStatistics stats = resp.result_as<DatabaseOperationStatistics>();
			fmt::print("task {} completed: {} locally pruned, {} globally pruned, {} discovered, {} edges\n",
					resp.seq(), stats.pruned_locally, stats.pruned_database, stats.novel_gadgets, stats.edges);
			overall_stats += stats;
		}
	});
	if (error_happened)
		throw std::runtime_error("one or more tasks failed; exiting to prevent generating a corrupt checkpoint");
	return overall_stats;
}


class SearchState {
public:
	SearchState() : subgen_start_(0) {}
	bool operator()(uint64_t id) {
		if (closed_.insert(id).second) {
			curgen_.push_back(id);
			return true;
		}
		return false;
	}
	bool operator()(const vector<uint64_t>& ids) {
		bool modified = false;
		for (uint64_t x : ids)
			modified |= (*this)(x);
		return modified;
	}
	std::size_t subgeneration_size() const {
		return curgen_.size() - subgen_start_;
	}
	std::size_t generation_size() const {
		return curgen_.size();
	}
	std::size_t closed_size() const {
		return closed_.size();
	}
	/**
	 * Gives access to the current subgeneration.  Do not append to the state
	 * while iterating this view.
	 * @return an iterator range spanning the current subgeneration
	 */
	auto subgeneration_view() const {
		return make_range_for_pair(subgeneration_begin(), subgeneration_end());
	}
	vector<uint64_t>::const_iterator subgeneration_begin() const {
		return curgen_.cbegin() + subgen_start_;
	}
	vector<uint64_t>::const_iterator subgeneration_end() const {
		return curgen_.cend();
	}
	/**
	 * Erases the ids in the given set from the current subgeneration.  They're
	 * still in the closed set.
	 */
	void erase_from_subgeneration(tsl::hopscotch_set<uint64_t>& to_be_erased) {
		curgen_.erase(std::remove_if(curgen_.begin()+subgen_start_, curgen_.end(),
				[&](uint64_t i){return to_be_erased.count(i);}), curgen_.end());
	}
	/**
	 * Begin a new generation.
	 * @return the previous generation
	 */
	vector<uint64_t> flip_generation() {
		vector<uint64_t> prev = std::move(curgen_);
		curgen_.clear(); //make moved-from vector suitable for insertion again
		subgen_start_ = 0;
		return prev;
	}
	/**
	 * Begin a new subgeneration.  Don't use the returned iterator range after
	 * doing any further appending to the state (the pointed-to vector may have
	 * to reallocate).
	 * @return an iterator range over the elements in the previous subgeneration
	 */
	auto flip_subgeneration() {
		auto prev_subgen = make_range_for_pair(curgen_.cbegin() + subgen_start_, curgen_.cend());
		subgen_start_ = curgen_.size();
		return prev_subgen;
	}
private:
	/**
	 * The set of all gadget ids encountered so far, including those in the
	 * current generation.
	 */
	tsl::hopscotch_set<uint64_t> closed_;
	/**
	 * The current generation: gadgets discovered since the previous combine.
	 */
	vector<uint64_t> curgen_;
	/**
	 * The start index of the current subgeneration: gadgets discovered since
	 * the previous connect.
	 */
	std::size_t subgen_start_;
};

int main(int argc, char* argv[]) { //genbuild entrypoint
	std::string_view db_user = "jbosboom", db_pass = "", db_host = "127.0.0.1",
			db_port = "5432", db_name = "togglesearch";
	std::vector<std::string> worker_addrs; //or @foo for response files
	std::string_view checkpoint_file = ""; //TODO: split into resume file and path to save new checkpoints
	bool multiplayer = false;
	std::vector<std::string_view> gid_specs;
	unsigned int precision = 8;
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
		else if (argv[i] == "--precision"sv)
			precision = to_uint(argv[++i]);
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
	WorkerManager manager(worker_addrs);
	ping_all_workers(manager);

	GadgetSet spec = parse_gid_specs(gid_specs);
	fmt::print("Gadget spec:");
	for (uint64_t id : spec.ids)
		fmt::print(" {}", id);
	for (pair<uint64_t, uint64_t> p : spec.ranges)
		fmt::print(" [{},{})", p.first, p.second);
	for (std::string& n : spec.names)
		fmt::print(" {}", n);
	fmt::print("\n");

	std::string connect_str = format_connect_string(db_user, db_pass, db_host, db_port, db_name);
	pqxx::connection conn(connect_str);
	prepare_statements(conn);

	SearchState state;

	auto filter_unary = [&](auto filter_fxn, auto first, auto last, std::string_view log_name) {
		Stopwatch stopwatch = Stopwatch::process();
		vector<uint64_t> needs = filter_fxn(conn, first, last);
		fmt::print("Found {} of {} gadgets needing {} in {}\n",
				needs.size(), std::distance(first, last), log_name, stopwatch.elapsed().hms());
		//Sorting here means tasks will contain consecutive ids more often,
		//reducing fragmentation in the completion tables.
		std::sort(needs.begin(), needs.end());
		return needs;
	};
	auto operate_unary = [&](std::vector<uint64_t>&& ids, std::string_view operation_name, std::string_view log_name) {
		Stopwatch stopwatch = Stopwatch::process();
		DatabaseOperationStatistics stats = do_unary_operation(manager, operation_name, ids);
		fmt::print("{} operation completed in {}: {} locally pruned, {} globally pruned, {} novel gadgets, {} edges\n",
				log_name, stopwatch.elapsed().hms(), stats.pruned_locally, stats.pruned_database, stats.novel_gadgets, stats.edges);
	};
	auto follow_unary_simple = [&](auto follow_fxn, auto first, auto last, std::string_view log_name) {
		Stopwatch stopwatch = Stopwatch::process();
		vector<uint64_t> ids = follow_fxn(conn, first, last);
		fmt::print("Followed {} edges to {} gadgets in {}\n", log_name, ids.size(), stopwatch.elapsed().hms());
		return ids;
	};

	auto close_and_mirror = [&]() {
		if (!multiplayer) {
			vector<uint64_t> needs_close = filter_unary(filter_ids_needing_close, state.subgeneration_begin(), state.subgeneration_end(), "close");
			if (needs_close.size())
				operate_unary(std::move(needs_close), "close-db", "Close");

			Stopwatch stopwatch = Stopwatch::process();
			pair<tsl::hopscotch_set<uint64_t>, vector<uint64_t>> close_edges = follow_close_edges(conn, state.subgeneration_begin(), state.subgeneration_end());
			fmt::print("Followed close edges from {} gadgets to {} gadgets in {}\n",
					close_edges.first.size(), close_edges.second.size(), stopwatch.elapsed().hms());
			state.erase_from_subgeneration(close_edges.first);
			state(close_edges.second);
		}

		vector<uint64_t> needs_mirror = filter_unary(filter_ids_needing_mirror, state.subgeneration_begin(), state.subgeneration_end(), "mirror");
		if (needs_mirror.size())
			operate_unary(std::move(needs_mirror), "mirror-db", "Mirror");
		state(follow_unary_simple(get_mirrors, state.subgeneration_begin(), state.subgeneration_end(), "mirror"));
	};

	vector<uint64_t> combine_rights;
	for (std::size_t generation = 0; ; ++generation) {
		Stopwatch generation_stopwatch = Stopwatch::process();
		if (generation == 0) {
			Stopwatch stopwatch = Stopwatch::process();
			vector<uint64_t> initial = collect_initial_gadget_set(conn, spec);
			fmt::print("Collected {} initial gadgets in {}ms\n", initial.size(), stopwatch.elapsed().millis());
			state(initial);
		} else {
			vector<uint64_t> combine_lefts = state.flip_generation();
			if (combine_lefts.empty()) {
				fmt::print("previous generation was empty, exiting\n");
				break;
			}

			Stopwatch stopwatch = Stopwatch::process();
			combines_map needs_combine = find_required_combines(conn, combine_lefts, combine_rights, precision);
			//This is sloppy because it doesn't list the number of pairs to be combined.
			fmt::print("Found {} of {} gadgets needing combine in {}\n",
					needs_combine.size(), combine_lefts.size(), stopwatch.elapsed().hms());

			if (needs_combine.size()) {
				//like operate_unary, but not quite
				stopwatch.reset();
				DatabaseOperationStatistics stats = do_combine_operation(manager, needs_combine, precision);
				fmt::print("Combine operation completed in {}: {} locally pruned, {} globally pruned, {} novel gadgets, {} edges\n",
						stopwatch.elapsed().hms(), stats.pruned_locally, stats.pruned_database, stats.novel_gadgets, stats.edges);
			}

			stopwatch.reset();
			vector<uint64_t> combines = get_combines(conn, combine_lefts, combine_rights, precision);
			fmt::print("Followed combine edges to {} gadgets in {}\n", combines.size(), stopwatch.elapsed().hms());
			state(std::move(combines));
		}

		close_and_mirror();
		//I suppose we could wait until after any subgenerations (connects) to
		//choose the set of combine rights, but this matches how the old
		//generational search worked.
		if (generation == 0) {
			combine_rights.assign(state.subgeneration_begin(), state.subgeneration_end());
			std::sort(combine_rights.begin(), combine_rights.end());
			fmt::print("Combine rights ({}:", combine_rights.size());
			for (uint64_t id : combine_rights)
				fmt::print(" {}", id);
			fmt::print("\n");
			prepare_combine_statements(conn, combine_rights.size(), precision);
		}

		Stopwatch::Result generation_elapsed = generation_stopwatch.elapsed();
		fmt::print("Finished combine {}.{} in {}; curgen size {}, closed size {}, max resident {:.2f} GiB (+{:.2f})\n",
				generation, 0, generation_elapsed.hms(), state.generation_size(), state.closed_size(),
				generation_elapsed.absolute().highwaterGibibytes(), generation_elapsed.highwaterGibibytes());

		for (std::size_t subgeneration = 1; ; ++subgeneration) {
			Stopwatch subgeneration_stopwatch = Stopwatch::process();
			auto prev_subgen = state.flip_subgeneration();
			using std::begin; using std::end;
			if (begin(prev_subgen) == end(prev_subgen))
				break;

			vector<uint64_t> needs_connect = filter_unary(filter_ids_needing_connect, begin(prev_subgen), end(prev_subgen), "connect");
			if (needs_connect.size())
				operate_unary(std::move(needs_connect), "connect-db", "Connect");

			vector<uint64_t> connects = follow_unary_simple(get_connects, begin(prev_subgen), end(prev_subgen), "connect");
			state(connects);
			close_and_mirror();

			Stopwatch::Result subgeneration_elapsed = subgeneration_stopwatch.elapsed();
			fmt::print("Finished connect {}.{} in {}; subgen size {}, curgen size {}, closed size {}, max resident {:.2f} GiB (+{:.2f})\n",
					generation, subgeneration, subgeneration_elapsed.hms(), state.subgeneration_size(),
					state.generation_size(), state.closed_size(),
					subgeneration_elapsed.absolute().highwaterGibibytes(), subgeneration_elapsed.highwaterGibibytes());
		}

		generation_elapsed = generation_stopwatch.elapsed();
		fmt::print("Finished generation {} in {}; curgen size {}, closed size {}, max resident {:.2f} GiB (+{:.2f})\n",
				generation, generation_elapsed.hms(), state.generation_size(), state.closed_size(),
				generation_elapsed.absolute().highwaterGibibytes(), generation_elapsed.highwaterGibibytes());
	}

	return 0;
}