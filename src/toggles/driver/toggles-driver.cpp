#include "precompiled.hpp"
#include "../database.hpp"
#include "../rpc.hpp"
#include "../toggles-shared.hpp"
#include "stringutils.hpp"
#include "ioutils.hpp"
#include "stopwatch.hpp"
#include "lmdb++.h"
#define BOOST_ASIO_SEPARATE_COMPILATION
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <system_error>
#include <future>

using std::vector;
using std::pair;
using std::uint64_t;
using namespace std::literals::string_view_literals;
namespace asio = boost::asio;
using asio::ip::tcp;

std::string build_filter_connectable_ids_query(std::size_t count) {
	vector<std::string> things;
	things.reserve(count);
	for (std::size_t i = 1; i <= count; ++i)
		things.push_back(fmt::format("(${}::int8)", i));
	return "select * from (values " +
			join(things, ", ") +
			") as maybe(id) join gadgets using (id) where locations >= 4";
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
	//TODO: Some other queries ask the database to deduplicate the results for
	//us, but in this case we need the inputs as well as the outputs.  But we
	//don't care which input corresponds to which output; we just need the two
	//sets.  Can we have the database dedup the outputs but preserve all inputs?
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
			") group by output1"; //group by is apparently faster than select distinct
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
	return "with lefts (lid) as (values\n" +
			left_params +
			"\n), rights (rid) as (values\n" +
			join(things, ", ") +
			"\n)\n" +
			"select array_agg(lid), right_ids from (\n"+
			"  select lefts.lid, array(select rid from rights where\n" +
			"    not exists (select 1 from combine_edges where\n" +
			"      input1 = lid and input2 = rid limit 1)\n" +
			"    and\n" +
			//can't select a sum because we might combine a gadget against itself
			"    (select locations from gadgets where id = lid) + (select locations from gadgets where id = rid)\n" +
			fmt::format("    <= {}\n", precision) +
			"  order by rid\n"
			") from lefts) as parent(lid, right_ids) where cardinality(right_ids) > 0 group by right_ids";
	//The aggregation below is "nicer" but slightly slower than the above query.
	//(This query doesn't have the empty array check.)
//	return "select array_agg(lid), rights from\n"
//			"(select lefts.lid, array_agg(rights.rid order by rights.rid) from\n"
//			"(values " + left_params + ") as lefts(lid) join\n"
//			"(values " + join(things, ", ") + ") as rights(rid)\n"
//			"on\n"
//			"not exists (\n"
//			"select 1 from combine_edges where input1 = lid and input2 = rid limit 1\n"
//		    ") and (select locations from gadgets where id = lid limit 1) + (select locations from gadgets where id = rid limit 1) <= " +
//			std::to_string(precision) +
//			" group by lefts.lid) as parent(lid, rights) group by rights";
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
	//Joining against the values lists is slower here.
	return "select output1 from combine_edges where input1 in (\n" +
			left_params +
			"\n) and input2 in (\n" +
			join(things, ", ") +
			"\n) group by output1"; //group by is apparently faster than select distinct
}

const std::pair<std::size_t, std::string_view> follow_close_edges_prepared[] = {
	{50000, "follow_close_50000"sv},
	{25000, "follow_close_25000"sv},
	{10000, "follow_close_10000"sv},
	{5000, "follow_close_5000"sv},
	{1000, "follow_close_1000"sv},
	{500, "follow_close_500"sv},
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
	for (const auto& p : follow_close_edges_prepared)
		conn.prepare(std::string(p.second), build_follow_close_edges_query(p.first));
	for (const auto& p : get_mirrors_prepared)
		conn.prepare(std::string(p.second), build_get_mirrors_query(p.first));
	for (const auto& p : filter_connect_prepared)
		conn.prepare(std::string(p.second), build_filter_connectable_ids_query(p.first));
	for (const auto& p : get_connects_prepared)
		conn.prepare(std::string(p.second), build_get_connects_query(p.first));
}

vector<std::pair<std::size_t, std::string>> required_combines_prepared;
const unsigned int required_combines_prepared_batch_sizes[] = {50000, 25000, 10000, 5000, 1000, 500};
vector<std::pair<std::size_t, std::string>> get_combines_prepared;
const unsigned int get_combines_prepared_batch_sizes[] = {50000, 25000, 10000, 5000, 1000, 500};

void prepare_combine_statements(ConnectionPool* pool, std::size_t right_count, unsigned int precision) {
	if (!required_combines_prepared.empty()) {
		assert(!get_combines_prepared.empty());
		return;
	}

	std::vector<std::tuple<std::string, std::size_t, std::size_t, unsigned int>> build_required_combines_data;
	for (unsigned int left_count : required_combines_prepared_batch_sizes) {
		std::string name = fmt::format("required_combines_{}", left_count);
		build_required_combines_data.emplace_back(name, left_count, right_count, precision);
		required_combines_prepared.emplace_back(left_count, std::move(name));
	}

	std::vector<std::tuple<std::string, std::size_t, std::size_t>> build_get_combines_data;
	for (unsigned int batch_size : get_combines_prepared_batch_sizes) {
		std::size_t left_count = std::max<std::size_t>(batch_size / right_count, 1);
		std::string name = fmt::format("get_combines_{}", left_count);
		build_get_combines_data.emplace_back(name, left_count, right_count);
		get_combines_prepared.emplace_back(left_count, std::move(name));
	}

	pool->register_setup("combine", [=](pqxx::connection& conn) {
		for (const auto& x : build_required_combines_data)
			conn.prepare(std::get<0>(x), build_required_combines_query(std::get<1>(x), std::get<2>(x), std::get<3>(x)));
		for (const auto& x : build_get_combines_data)
			conn.prepare(std::get<0>(x), build_get_combines_query(std::get<1>(x), std::get<2>(x)));
	});
}


vector<uint64_t> id_to_id_db_op0(pqxx::connection& conn,
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

vector<vector<uint64_t>> id_to_id_db_op(ConnectionPool& pool,
		const vector<uint64_t>::const_iterator ids_begin,
		const vector<uint64_t>::const_iterator ids_end,
		const std::pair<std::size_t, std::string_view>* prepared_begin,
		const std::pair<std::size_t, std::string_view>* prepared_end,
		std::string(*query_func)(std::size_t)) {
	std::size_t total_size = numeric_cast<std::size_t>(std::distance(ids_begin, ids_end));
	std::size_t max_threads = total_size /
			std::max_element(prepared_begin, prepared_end, [](const auto& a, const auto& b) {
				return std::get<0>(a) < std::get<0>(b);
			})->first;
	max_threads = std::min<std::size_t>(max_threads, pool.capacity());
	if (max_threads <= 1) {
		ConnectionLease lease = pool.checkout();
		vector<vector<uint64_t>> results;
		results.push_back(id_to_id_db_op0(*lease, ids_begin, ids_end, prepared_begin, prepared_end, query_func));
		return std::move(results);
	}

	std::size_t batch_size = (total_size + (max_threads - 1)) / max_threads;
	vector<std::future<vector<uint64_t>>> futures;
	for (std::size_t i = 0; i < total_size; i += batch_size) {
		auto first = ids_begin + i, last = ids_begin + std::min(i+batch_size, total_size);
		futures.push_back(std::async(std::launch::async, [&pool, first, last, prepared_begin, prepared_end, query_func]() {
			ConnectionLease lease = pool.checkout();
			return id_to_id_db_op0(*lease, first, last, prepared_begin, prepared_end, query_func);
		}));
	}

	vector<vector<uint64_t>> results;
	for (std::size_t i = 0; i < futures.size(); ++i)
		results.push_back(futures[i].get());
	return std::move(results);
}

vector<pair<uint64_t, uint64_t>> get_all_completion_ranges(pqxx::connection& conn, std::string_view tablename) {
	std::string query = fmt::format("select lower(r), upper(r) from {}", tablename);
	std::string reporting_name = fmt::format("get_all_completion_ranges {}", tablename);
	return retry_db_operation([&]() {
		vector<pair<uint64_t, uint64_t>> ranges;
		ro_transaction trans(conn);
		pqxx::result res = trans.exec(query);
		for (const pqxx::row& r : res)
			ranges.emplace_back(r[0].as<uint64_t>(), r[1].as<uint64_t>());
		trans.commit();

		std::sort(ranges.begin(), ranges.end());
		return std::move(ranges);
	}, 10, reporting_name);
}

vector<uint64_t> filter_ids_needing_close(ConnectionPool& pool,
		const vector<uint64_t>::iterator ids_begin,
		const vector<uint64_t>::iterator ids_end) {
	std::sort(ids_begin, ids_end);
	ConnectionLease conn = pool.checkout();
	vector<pair<uint64_t, uint64_t>> ranges = get_all_completion_ranges(*conn, "completed_closes");
	//The result is the range [ids_begin, needy_end).  It would be nice to find
	//a way to use it without copying, but we'll need unary_needs_ for other
	//purposes anyway, so...
	return {ids_begin, partition_on_range_exclusion(ids_begin, ids_end, ranges.cbegin(), ranges.cend())};
}

pair<tsl::hopscotch_set<uint64_t, farmhash_hash>, vector<uint64_t>> follow_close_edges0(pqxx::connection& conn,
		const vector<uint64_t>::const_iterator ids_begin, const vector<uint64_t>::const_iterator ids_end) {
	return retry_db_operation([&]() {
		ro_transaction trans(conn);
		//inputs aren't duplicated, but quickly removing them from the SearchState requires having them in a set
		tsl::hopscotch_set<uint64_t, farmhash_hash> inputs;
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

vector<pair<tsl::hopscotch_set<uint64_t, farmhash_hash>, vector<uint64_t>>> follow_close_edges(ConnectionPool& pool,
		const vector<uint64_t>::const_iterator ids_begin, const vector<uint64_t>::const_iterator ids_end) {
	std::size_t total_size = numeric_cast<std::size_t>(std::distance(ids_begin, ids_end));
	std::size_t max_threads = total_size /
			std::max_element(std::begin(follow_close_edges_prepared), std::end(follow_close_edges_prepared),
			[](const auto& a, const auto& b) {
				return std::get<0>(a) < std::get<0>(b);
			})->first;
	max_threads = std::min<std::size_t>(max_threads, pool.capacity());
	if (max_threads <= 1) {
		ConnectionLease lease = pool.checkout();
		vector<pair<tsl::hopscotch_set<uint64_t, farmhash_hash>, vector<uint64_t>>> results;
		results.push_back(follow_close_edges0(*lease, ids_begin, ids_end));
		return std::move(results);
	}

	std::size_t batch_size = (total_size + (max_threads - 1)) / max_threads;
	vector<std::future<pair<tsl::hopscotch_set<uint64_t, farmhash_hash>, vector<uint64_t>>>> futures;
	for (std::size_t i = 0; i < total_size; i += batch_size) {
		auto first = ids_begin + i, last = ids_begin + std::min(i+batch_size, total_size);
		futures.push_back(std::async(std::launch::async, [&pool, first, last]() {
			ConnectionLease lease = pool.checkout();
			return follow_close_edges0(*lease, first, last);
		}));
	}

	vector<pair<tsl::hopscotch_set<uint64_t, farmhash_hash>, vector<uint64_t>>> results;
	for (std::size_t i = 0; i < futures.size(); ++i)
		results.push_back(futures[i].get());
	return std::move(results);
}

vector<uint64_t> filter_ids_needing_mirror(ConnectionPool& pool,
		const vector<uint64_t>::iterator ids_begin,
		const vector<uint64_t>::iterator ids_end) {
	std::sort(ids_begin, ids_end);
	ConnectionLease conn = pool.checkout();
	vector<pair<uint64_t, uint64_t>> ranges = get_all_completion_ranges(*conn, "completed_mirrors");
	//The result is the range [ids_begin, needy_end).  It would be nice to find
	//a way to use it without copying, but we'll need unary_needs_ for other
	//purposes anyway, so...
	return {ids_begin, partition_on_range_exclusion(ids_begin, ids_end, ranges.cbegin(), ranges.cend())};
}

vector<vector<uint64_t>> get_mirrors(ConnectionPool& pool, const vector<uint64_t>::const_iterator ids_begin,
		const vector<uint64_t>::const_iterator ids_end) {
	return retry_db_operation([&]() {
		return id_to_id_db_op(pool, ids_begin, ids_end,
				std::begin(get_mirrors_prepared), std::end(get_mirrors_prepared),
				&build_get_mirrors_query);
	}, 10, "get_mirrors");
}

vector<uint64_t> filter_ids_needing_connect(ConnectionPool& pool,
		const vector<uint64_t>::iterator ids_begin,
		const vector<uint64_t>::iterator ids_end) {
	std::sort(ids_begin, ids_end);
	ConnectionLease lease = pool.checkout();
	vector<pair<uint64_t, uint64_t>> ranges = get_all_completion_ranges(*lease, "completed_connects");
	pool.checkin(std::move(lease));
	vector<uint64_t>::iterator new_end = partition_on_range_exclusion(ids_begin, ids_end, ranges.cbegin(), ranges.cend());

	//Also filter out any gadgets too small to connect.
	vector<vector<uint64_t>> results = retry_db_operation([&]() {
		return id_to_id_db_op(pool, ids_begin, new_end,
				std::begin(filter_connect_prepared), std::end(filter_connect_prepared),
				&build_filter_connectable_ids_query);
	}, 10, "filter_ids_needing_connect, filtering connectable ids");

	std::size_t total_size = 0;
	for (const vector<uint64_t>& r : results)
		total_size += r.size();
	results[0].reserve(total_size);
	for (std::size_t i = 1; i < results.size(); ++i)
		results[0].insert(results[0].end(), results[i].begin(), results[i].end());
	return std::move(results[0]);
}

vector<vector<uint64_t>> get_connects(ConnectionPool& pool, const vector<uint64_t>::const_iterator ids_begin,
		const vector<uint64_t>::const_iterator ids_end) {
	return retry_db_operation([&]() {
		return id_to_id_db_op(pool, ids_begin, ids_end,
				std::begin(get_connects_prepared), std::end(get_connects_prepared),
				&build_get_connects_query);
	}, 10, "get_connects");
}

//struct vector_hash {
//	std::size_t operator()(const std::vector<uint64_t>& v) const {
//		return farmhash::Hash(reinterpret_cast<const char*>(v.data()), v.size() * sizeof(v.front()));
//	}
//};

void sort_and_deduplicate(vector<pair<vector<uint64_t>, vector<uint64_t>>>& records) {
	if (records.empty()) return;

	std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) {
		return std::get<0>(a) < std::get<0>(b);
	});

	using iter = vector<pair<vector<uint64_t>, vector<uint64_t>>>::iterator;
	iter head = records.begin(), last_committed = records.begin();
	while (++head != records.end())
		if (head->first == last_committed->first) {
			last_committed->second.insert(last_committed->second.end(),
					//If we generalize this to arbitrary types for algoutils,
					//this should be a move_iterator.
					head->second.begin(), head->second.end());
			//These'll be deallocated later, of course, but as we're growing the
			//survivors we should free these eagerly.
			head->second.clear();
			head->second.shrink_to_fit();
		} else if (++last_committed != head) //commit, and avoid moving last_committed onto itself
			*last_committed = std::move(*head);
	++last_committed;

	records.erase(last_committed, records.end());
}

/**
 * Finds required combines.
 * @return pairs of right ids and left ids needing to be combined against them (i.e., backwards)
 */
vector<pair<vector<uint64_t>, vector<uint64_t>>> find_required_combines0(pqxx::connection& conn,
		std::vector<uint64_t>::const_iterator left_ids_first, std::vector<uint64_t>::const_iterator left_ids_last,
		const std::vector<uint64_t>& right_ids, unsigned int precision) {
	vector<pair<vector<uint64_t>, vector<uint64_t>>> result = retry_db_operation([&]() {
		ro_transaction trans(conn);
		vector<pair<vector<uint64_t>, vector<uint64_t>>> records;
		std::pair<pqxx::array_parser::juncture, std::string> array_element;
		auto process_rows = [&](const pqxx::result& rows) {
			for (const auto& r : rows) {
				vector<uint64_t> lefts, rights;
				{
					pqxx::array_parser parser = r[0].as_array();
					while ((array_element = parser.get_next()).first != pqxx::array_parser::done)
						if (array_element.first == pqxx::array_parser::string_value)
							lefts.push_back(to_uint64(array_element.second));
				}
				{
					pqxx::array_parser parser = r[1].as_array();
					while ((array_element = parser.get_next()).first != pqxx::array_parser::done)
						if (array_element.first == pqxx::array_parser::string_value)
							rights.push_back(to_uint64(array_element.second));
				}
				assert(!lefts.empty());
				assert(!rights.empty());
				assert(std::is_sorted(rights.begin(), rights.end()));
				records.emplace_back(std::move(rights), std::move(lefts));
			}
		};

		vector<uint64_t>::const_iterator cur = left_ids_first;
		for (const auto& p : required_combines_prepared) {
			while (numeric_cast<std::size_t>(std::distance(cur, left_ids_last)) >= p.first) {
				pqxx::result rows = trans.exec_prepared(std::string(p.second),
						pqxx::prepare::make_dynamic_params(cur, cur+p.first),
						pqxx::prepare::make_dynamic_params(right_ids.begin(), right_ids.end()));
				process_rows(rows);
				cur += p.first;
			}
		}
		std::size_t epilogue_size = numeric_cast<std::size_t>(std::distance(cur, left_ids_last));
		if (epilogue_size) {
			pqxx::result rows = trans.exec_params(build_required_combines_query(epilogue_size, right_ids.size(), precision),
					pqxx::prepare::make_dynamic_params(cur, left_ids_last),
					pqxx::prepare::make_dynamic_params(right_ids.begin(), right_ids.end()));
			process_rows(rows);
		}
		trans.commit();
		return records;
	}, 10, "find_required_combines");

	//Because we (may have) made multiple queries, we may have duplicates.
	sort_and_deduplicate(result);
	return std::move(result);
}

vector<pair<vector<uint64_t>, vector<uint64_t>>> find_required_combines(ConnectionPool& pool,
		const std::vector<uint64_t>& left_ids,
		const std::vector<uint64_t>& right_ids, const unsigned int precision) {
	std::size_t max_threads = left_ids.size() /
			//insure against changing the batch sizes somehow
			*std::max_element(std::begin(required_combines_prepared_batch_sizes), std::end(required_combines_prepared_batch_sizes));
	max_threads = std::min<std::size_t>(max_threads, pool.capacity());
	if (max_threads <= 1) {
		ConnectionLease lease = pool.checkout();
		return find_required_combines0(*lease, left_ids.cbegin(), left_ids.cend(), right_ids, precision);
	}

	//We give a near-equal division, so each thread will have "straggling" queries.
	//We could try to give most threads more full batches and leave one thread
	//with less, but irregularly-shaped, work.
	std::size_t batch_size = (left_ids.size() + (max_threads - 1)) / max_threads;
	vector<std::future<vector<pair<vector<uint64_t>, vector<uint64_t>>>>> futures;
	for (std::size_t i = 0; i < left_ids.size(); i += batch_size) {
		auto first = left_ids.cbegin()+i, last = left_ids.cbegin() + std::min(i+batch_size, left_ids.size());
		futures.push_back(std::async(std::launch::async, [&pool, first, last, &right_ids, precision]() {
			ConnectionLease lease = pool.checkout();
			return find_required_combines0(*lease, first, last, right_ids, precision);
		}));
	}

	vector<pair<vector<uint64_t>, vector<uint64_t>>> result = futures.front().get();
	for (std::size_t i = 1; i < futures.size(); ++i) {
		vector<pair<vector<uint64_t>, vector<uint64_t>>> more = futures[i].get();
		result.insert(result.end(), std::move_iterator(more.begin()), std::move_iterator(more.end()));
	}
	sort_and_deduplicate(result);
	return std::move(result);
}

vector<uint64_t> get_combines0(pqxx::connection& conn,
		std::vector<uint64_t>::const_iterator left_ids_first, std::vector<uint64_t>::const_iterator left_ids_last,
		const std::vector<uint64_t>& right_ids, unsigned int precision) {
	return retry_db_operation([&]() {
		ro_transaction trans(conn);
		vector<uint64_t> result;
		vector<uint64_t>::const_iterator cur = left_ids_first;
		for (const auto& p : get_combines_prepared) {
			while (numeric_cast<std::size_t>(std::distance(cur, left_ids_last)) >= p.first) {
				pqxx::result rows = trans.exec_prepared(std::string(p.second),
						pqxx::prepare::make_dynamic_params(cur, cur+p.first),
						pqxx::prepare::make_dynamic_params(right_ids.begin(), right_ids.end()));
				for (const auto& r : rows)
					result.push_back(r[0].as<uint64_t>());
				cur += p.first;
			}
		}
		std::size_t epilogue_size = numeric_cast<std::size_t>(std::distance(cur, left_ids_last));
		if (epilogue_size) {
			pqxx::result rows = trans.exec_params(build_get_combines_query(epilogue_size, right_ids.size()),
					pqxx::prepare::make_dynamic_params(cur, left_ids_last),
					pqxx::prepare::make_dynamic_params(right_ids.begin(), right_ids.end()));
			for (const auto& r : rows)
				result.push_back(r[0].as<uint64_t>());
		}
		trans.commit();
		return result;
	}, 10, "get_combines0");
}

vector<vector<uint64_t>> get_combines(ConnectionPool& pool, const std::vector<uint64_t>& left_ids,
		const std::vector<uint64_t>& right_ids, const unsigned int precision) {
	std::size_t max_threads = left_ids.size() /
			//insure against changing the batch sizes somehow
			*std::max_element(std::begin(get_combines_prepared_batch_sizes), std::end(get_combines_prepared_batch_sizes));
	max_threads = std::min<std::size_t>(max_threads, pool.capacity());
	if (max_threads <= 1) {
		ConnectionLease lease = pool.checkout();
		vector<vector<uint64_t>> results;
		results.push_back(get_combines0(*lease, left_ids.cbegin(), left_ids.cend(), right_ids, precision));
		return std::move(results);
	}

	std::size_t batch_size = (left_ids.size() + (max_threads - 1)) / max_threads;
	vector<std::future<vector<uint64_t>>> futures;
	for (std::size_t i = 0; i < left_ids.size(); i += batch_size) {
		auto first = left_ids.cbegin()+i, last = left_ids.cbegin() + std::min(i+batch_size, left_ids.size());
		futures.push_back(std::async(std::launch::async, [&pool, first, last, &right_ids, precision]() {
			ConnectionLease lease = pool.checkout();
			return get_combines0(*lease, first, last, right_ids, precision);
		}));
	}

	vector<vector<uint64_t>> results;
	for (std::size_t i = 0; i < futures.size(); ++i)
		results.push_back(futures[i].get());
	return std::move(results);
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

struct UnaryBatcher {
	vector<uint64_t>::const_iterator head, last;
	std::size_t size_;
	UnaryBatcher(const vector<uint64_t>& vec, std::size_t batch_size) : head(vec.begin()), last(vec.end()), size_(batch_size) {}
	explicit operator bool() const {return head != last;}
	pair<vector<uint64_t>::const_iterator, vector<uint64_t>::const_iterator> operator()() {
		std::size_t actual_size = std::min(size_, static_cast<std::size_t>(last-head));
		pair<vector<uint64_t>::const_iterator, vector<uint64_t>::const_iterator> ret(head, head+actual_size);
		head += actual_size;
		return ret;
	}
	/**
	 * @return the number of batches remaining
	 */
	std::size_t size() const {
		return (std::distance(head, last) + (size_-1))/ size_; //round up
	}
};

DatabaseOperationStatistics do_unary_operation(WorkerManager& manager, std::string_view operation, UnaryBatcher operands) {
	std::uint32_t seqno = 0;
	//TODO: figure out how to pack a range without copying (probably a custom type with a pack method?)
	vector<uint64_t> range;
	DatabaseOperationStatistics overall_stats = {};
	bool error_happened = false;
	manager.run([&](simple_buffer& buffer) {
		if (error_happened) return false; //stop generating work, but let existing issued work finish
		if (!operands) return false;
		range.clear();
		auto iters = operands();
		range.insert(range.end(), iters.first, iters.second);
		pack_call(buffer, seqno++, operation, range);
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

void write_unary_batch_tasks(pqxx::connection& conn, std::string_view operation, UnaryBatcher batcher, const std::string& directory) {
	vector<uint64_t> fetches;
	simple_buffer buffer;
	for (std::uint32_t seqno = 0; batcher; ++seqno) {
		auto batch = batcher();
		fetches.assign(batch.first, batch.second);
		vector<pair<std::uint64_t, vector<std::byte>>> gadget_data = select_gadget_id_to_data(conn, fetches);
		pack_call(buffer, seqno, operation, gadget_data);
		write_buffer(buffer, fmt::format("{}/{}.msg", directory, seqno));
		buffer.clear();
	}
}

struct CombineBatcher {
	//backwards input: rights first, lefts second
	vector<pair<vector<uint64_t>, vector<uint64_t>>>::const_iterator head, last;
	vector<uint64_t>::const_iterator subhead; //position in head->second
	std::size_t size_;
	CombineBatcher(const vector<pair<vector<uint64_t>, vector<uint64_t>>>& vec, std::size_t batch_size)
			: head(vec.cbegin()), last(vec.cend()), subhead(), size_(batch_size) {
		if (head != last) //can't initialize in mem-init-list due to this check
			subhead = head->second.cbegin();
	}
	explicit operator bool() const {return head != last;}
	//iterator range of lefts, pointer to vector of rights
	pair<pair<vector<uint64_t>::const_iterator, vector<uint64_t>::const_iterator>, const vector<uint64_t>*>
	operator()() {
		std::size_t lefts_size = std::max<std::size_t>(size_ / head->first.size(), 1);
		lefts_size = std::min(lefts_size, static_cast<std::size_t>(head->second.cend() - subhead));
		pair<vector<uint64_t>::const_iterator, vector<uint64_t>::const_iterator> lefts(subhead, subhead + lefts_size);
		pair<pair<vector<uint64_t>::const_iterator, vector<uint64_t>::const_iterator>, const vector<uint64_t>*> ret(lefts, &head->first);
		subhead += lefts_size;
		if (subhead == head->second.cend()) {
			++head;
			if (head != last)
				subhead = head->second.cbegin();
		}
		return ret;
	}
	std::size_t size() const {
		//This is only approximate, but should be good enough for making batching decisions.
		std::size_t s = 0;
		for (auto i = head; i != last; ++i)
			s += i->first.size() * i->second.size();
		return (s + (size_-1)) / size_;
	}
};

DatabaseOperationStatistics do_combine_operation(WorkerManager& manager, CombineBatcher operands, unsigned int precision) {
	std::uint32_t seqno = 0;
	vector<uint64_t> lefts; //TODO: pack iter-range without copying it first
	DatabaseOperationStatistics overall_stats = {};
	bool error_happened = false;
	manager.run([&](simple_buffer& buffer) {
		if (error_happened) return false; //stop generating work, but let existing issued work finish
		if (!operands) return false;
		lefts.clear();
		pair<pair<vector<uint64_t>::const_iterator, vector<uint64_t>::const_iterator>, const vector<uint64_t>*> batch = operands();
		lefts.insert(lefts.end(), batch.first.first, batch.first.second);
		pack_call(buffer, seqno++, "combine-db", lefts, *batch.second, precision);
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

void write_combine_batch_tasks(pqxx::connection& conn, CombineBatcher batcher, unsigned int precision, const std::string& directory) {
	vector<uint64_t> fetches;
	simple_buffer buffer;
	for (std::uint32_t seqno = 0; batcher; ++seqno) {
		pair<pair<vector<uint64_t>::const_iterator, vector<uint64_t>::const_iterator>, const vector<uint64_t>*> batch = batcher();
		fetches.clear();
		fetches.insert(fetches.end(), batch.first.first, batch.first.second);
		fetches.insert(fetches.end(), batch.second->begin(), batch.second->end());
		//sort-unique is optional here because the database will effectively do it for us.
		std::sort(fetches.begin(), fetches.end());
		fetches.erase(std::unique(fetches.begin(), fetches.end()), fetches.end());
		vector<pair<std::uint64_t, vector<std::byte>>> gadget_data = select_gadget_id_to_data(conn, fetches);

		fetches.assign(batch.first.first, batch.first.second); //packing iterator-range would save this copy
		pack_call(buffer, seqno, "batch-combine", gadget_data, fetches, *batch.second, precision);
		write_buffer(buffer, fmt::format("{}/{}.msg", directory, seqno));
		buffer.clear();
	}
}


class SearchState {
public:
	class Serialized;
	SearchState() : subgen_start_(0), prev_subgen_start_(0) {}
	SearchState(const Serialized& s) : closed_(s.closed.begin(), s.closed.end()),
			curgen_(s.curgen), subgen_start_(s.subgen_start), prev_subgen_start_(s.prev_subgen_start) {
		if (prev_subgen_start_ > subgen_start_ || subgen_start_ > curgen_.size())
			throw std::runtime_error(fmt::format("prev_subgen_start_ {} subgen_start_ {} curgen_.size() {} while restoring",
					prev_subgen_start_, subgen_start_, curgen_.size()));
	}
	SearchState(Serialized&& s) : closed_(s.closed.begin(), s.closed.end()),
			curgen_(std::move(s.curgen)), subgen_start_(s.subgen_start), prev_subgen_start_(s.prev_subgen_start) {
		if (prev_subgen_start_ > subgen_start_ || subgen_start_ > curgen_.size())
			throw std::runtime_error(fmt::format("prev_subgen_start_ {} subgen_start_ {} curgen_.size() {} while restoring",
					prev_subgen_start_, subgen_start_, curgen_.size()));
	}
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
	bool operator()(vector<uint64_t>&& ids_rref) {
		//Enforce it actually moves so it gets deallocated promptly.
		vector<uint64_t> ids(std::move(ids_rref));
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
	vector<uint64_t>::iterator subgeneration_begin() {
		return curgen_.begin() + subgen_start_;
	}
	vector<uint64_t>::iterator subgeneration_end() {
		return curgen_.end();
	}
	vector<uint64_t>::const_iterator subgeneration_begin() const {
		return curgen_.cbegin() + subgen_start_;
	}
	vector<uint64_t>::const_iterator subgeneration_end() const {
		return curgen_.cend();
	}
	vector<uint64_t>::iterator prev_subgeneration_begin() {
		return curgen_.begin() + prev_subgen_start_;
	}
	vector<uint64_t>::iterator prev_subgeneration_end() {
		return subgeneration_begin();
	}
	vector<uint64_t>::const_iterator prev_subgeneration_begin() const {
		return curgen_.cbegin() + prev_subgen_start_;
	}
	vector<uint64_t>::const_iterator prev_subgeneration_end() const {
		return subgeneration_begin();
	}
	/**
	 * Erases the ids in the given set from the current subgeneration.  They're
	 * still in the closed set.
	 */
	void erase_from_subgeneration(tsl::hopscotch_set<uint64_t, farmhash_hash>& to_be_erased) {
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
		prev_subgen_start_ = subgen_start_ = 0;
		return prev;
	}
	/**
	 * Begin a new subgeneration.
	 */
	void flip_subgeneration() {
		prev_subgen_start_ = subgen_start_;
		subgen_start_ = curgen_.size();
	}

	/**
	 * Serialization proxy for SearchState.  The vectors are sorted (curgen's
	 * parts separately) so the on-disk representation is deterministic, at the
	 * cost of making the resumed state not exactly the same as the suspended
	 * state (in which they were not sorted).
	 *
	 * TODO: consider delta-compressing the vectors.  (may help to break curgen
	 * into two parts rather than deal with the discontinuity in sorting)
	 * TODO: Serialized maybe should have constructors taking SearchState&& and
	 * const SearchState&
	 */
	struct Serialized {
		vector<uint64_t> closed;
		vector<uint64_t> curgen;
		std::size_t subgen_start, prev_subgen_start;
		void canonicalize() {
			std::sort(closed.begin(), closed.end());
			std::sort(curgen.begin(), curgen.begin()+prev_subgen_start);
			std::sort(curgen.begin()+prev_subgen_start, curgen.begin()+subgen_start);
			std::sort(curgen.begin()+subgen_start, curgen.end());
		}
		MSGPACK_DEFINE_ARRAY(closed, curgen, subgen_start, prev_subgen_start)
	};
	Serialized serialized() const & {
		Serialized s;
		s.closed.assign(closed_.begin(), closed_.end());
		s.curgen = curgen_;
		s.subgen_start = subgen_start_;
		s.prev_subgen_start = prev_subgen_start_;
		s.canonicalize();
		return s;
	}
	Serialized serialized() && {
		Serialized s;
		//can't just move from the set, unfortunately.
		s.closed.assign(closed_.begin(), closed_.end());
		//TODO: figure out how to free closed_'s memory (no shrink_to_fit(), rehash(0) will iterate the map)
		s.curgen = std::move(curgen_);
		s.subgen_start = subgen_start_;
		s.prev_subgen_start = prev_subgen_start_;
		s.canonicalize();
		return s;
	}
private:
	/**
	 * The set of all gadget ids encountered so far, including those in the
	 * current generation.
	 */
	tsl::hopscotch_set<uint64_t, farmhash_hash> closed_;
	/**
	 * The current generation: gadgets discovered since the previous combine.
	 */
	vector<uint64_t> curgen_;
	/**
	 * The start index of the current subgeneration: gadgets discovered since
	 * the previous connect.
	 */
	std::size_t subgen_start_;
	std::size_t prev_subgen_start_;
};

/**
 * Various control options used by a search.  These options can be changed from
 * run to run even when resuming from a checkpoint.
 */
struct RuntimeOptions {
	/**
	 * The number of left-right pairs in each combine task.
	 */
	std::size_t combine_pairs_per_task;
	std::size_t connect_gadgets_per_task, close_gadgets_per_task, mirror_gadgets_per_task;
	/**
	 * When allowing batch operation, the number of tasks required to trigger
	 * writing tasks and suspending.  Below this threshold the tasks will be run
	 * on workers.  (The idea is that early generations are small and fast so
	 * batching isn't useful.)
	 *
	 * When not in batch mode, this is max(), so batching will never be invoked.
	 */
	std::size_t combine_task_batch_threshold, connect_task_batch_threshold,
			close_task_batch_threshold, mirror_task_batch_threshold;
	/**
	 * The directory to write batch tasks into.
	 */
	std::string batch_task_directory;
};

class Search {
private:
	/**
	 * The states of the generational search state machine.  The flow is
	 * combine -> close -> mirror -> connect -> close -> mirror -> connect -> ...
	 * with discover_needs_connect branching to the next combine step when the
	 * previous subgeneration was empty, and with the first combine replaced by
	 * collecting the initial gadget set.
	 */
	enum class Phase {
		collect_initial,

		begin_generation,
		discover_needs_combine,
		compute_combine,
		follow_combine,

		discover_needs_close,
		compute_close,
		follow_close,

		discover_needs_mirror,
		compute_mirror,
		follow_mirror,

		begin_subgeneration,
		discover_needs_connect,
		compute_connect,
		follow_connect,
	};

	enum class Control {
		/**
		 * Continue with the next phase.  ("continue" is a keyword.)
		 */
		proceed,
		/**
		 * Create a new checkpoint and exit.
		 */
		suspend,
		/**
		 * The search has completed; no more gadgets can be made.  (Storing the
		 * closed set might be useful for future "can make X?" queries.)
		 */
		stop,
	};

public:
	class Serialized;
	Search(vector<std::string>&& cmdline_specs, GadgetSet&& source_specs,
			unsigned int precision, bool multiplayer, RuntimeOptions runtime_opts) :
			generation_(0), subgeneration_(0), phase_(Phase::collect_initial),
			cmdline_specs_(std::move(cmdline_specs)),
			source_specs_(std::move(source_specs)), precision_(precision), multiplayer_(multiplayer),
			conn_pool_(nullptr), workers_(nullptr), runtime_opts_(runtime_opts),
			generation_stopwatch_(Stopwatch::process()), subgeneration_stopwatch_(Stopwatch::process()) {}
	Search(Serialized&& s, RuntimeOptions runtime_opts) : state_(std::move(s.state)), combine_rights_(std::move(s.combine_rights)),
			unary_needs_(std::move(s.unary_needs)), combine_needs_(std::move(s.combine_needs)),
			generation_(s.generation), subgeneration_(s.subgeneration), phase_{s.phase},
			cmdline_specs_(std::move(s.cmdline_specs)), source_specs_(std::move(s.source_specs)),
			precision_(s.precision), multiplayer_(s.multiplayer), conn_pool_(nullptr), workers_(nullptr), runtime_opts_(runtime_opts),
			generation_stopwatch_(Stopwatch::process()), subgeneration_stopwatch_(Stopwatch::process()) {}

	/**
	 * @return true if the search completed; false if we should write a checkpoint
	 */
	bool execute(ConnectionPool& pool, WorkerManager* workers) {
		conn_pool_ = &pool;
		workers_ = workers;

		Control control = Control::proceed;
		while (control == Control::proceed) {
			switch (phase_) {
				case Phase::collect_initial: control = collect_initial(); break;
				case Phase::begin_generation: control = begin_generation(); break;
				case Phase::discover_needs_combine: control = discover_needs_combine(); break;
				case Phase::compute_combine: control = compute_combine(); break;
				case Phase::follow_combine: control = follow_combine(); break;
				case Phase::discover_needs_close: control = discover_needs_close(); break;
				case Phase::compute_close: control = compute_close(); break;
				case Phase::follow_close: control = follow_close(); break;
				case Phase::discover_needs_mirror: control = discover_needs_mirror(); break;
				case Phase::compute_mirror: control = compute_mirror(); break;
				case Phase::follow_mirror: control = follow_mirror(); break;
				case Phase::begin_subgeneration: control = begin_subgeneration(); break;
				case Phase::discover_needs_connect: control = discover_needs_connect(); break;
				case Phase::compute_connect: control = compute_connect(); break;
				case Phase::follow_connect: control = follow_connect(); break;
			}
		}

		conn_pool_ = nullptr;
		workers_ = nullptr;
		return control == Control::stop;
	}

private:
	Control collect_initial() {
		generation_stopwatch_.reset();
		Stopwatch stopwatch = Stopwatch::process();
		ConnectionLease conn = conn_pool_->checkout();
		vector<uint64_t> initial = collect_initial_gadget_set(*conn, source_specs_);
		fmt::print("Collected {} initial gadgets in {}ms\n", initial.size(), stopwatch.elapsed().millis());
		state_(std::move(initial));
		phase_ = Phase::discover_needs_close;
		return Control::proceed;
	}

	Control begin_generation() {
		unary_needs_ = state_.flip_generation();
		if (unary_needs_.empty()) {
			fmt::print("previous generation was empty, exiting\n");
			return Control::stop;
		}
		++generation_;
		subgeneration_ = 0;
		generation_stopwatch_.reset();
		subgeneration_stopwatch_.reset();
		phase_ = Phase::discover_needs_combine;
		return Control::proceed;
	}

	Control discover_needs_combine() {
		prepare_combine_statements(conn_pool_, combine_rights_.size(), precision_);
		Stopwatch stopwatch = Stopwatch::process();
		combine_needs_ = find_required_combines(*conn_pool_, unary_needs_, combine_rights_, precision_);
		std::size_t needy_lefts = 0, needy_pairs = 0;
		for (const pair<vector<uint64_t>, vector<uint64_t>>& p : combine_needs_) {
			needy_lefts += p.second.size();
			needy_pairs += p.second.size() * p.first.size();
		}
		fmt::print("Found {} of {} lefts needing combine ({} total pairs) in {}\n",
				needy_lefts, unary_needs_.size(), needy_pairs, stopwatch.elapsed().hms());

		phase_ = Phase::compute_combine;
		return Control::proceed;
	}

	Control compute_combine() {
		if (combine_needs_.size()) {
			Stopwatch stopwatch = Stopwatch::process();
			CombineBatcher batcher(combine_needs_, runtime_opts_.combine_pairs_per_task);
			if (batcher.size() < runtime_opts_.combine_task_batch_threshold) {
				DatabaseOperationStatistics stats = do_combine_operation(*workers_, std::move(batcher), precision_);
				fmt::print("Combine operation completed in {}: {} locally pruned, {} globally pruned, {} novel gadgets, {} edges\n",
						stopwatch.elapsed().hms(), stats.pruned_locally, stats.pruned_database, stats.novel_gadgets, stats.edges);
			} else {
				std::size_t task_count = batcher.size();
				ConnectionLease conn = conn_pool_->checkout();
				write_combine_batch_tasks(*conn, std::move(batcher), precision_,
						runtime_opts_.batch_task_directory);
				//We could try a special resume state that only rechecks combine_needs_.
				combine_needs_.clear();
				fmt::print("wrote {} combine tasks in {}\n", task_count, stopwatch.elapsed().hms());
				phase_ = Phase::discover_needs_combine;
				return Control::suspend;
			}
		}
		phase_ = Phase::follow_combine;
		return Control::proceed;
	}

	Control follow_combine() {
		prepare_combine_statements(conn_pool_, combine_rights_.size(), precision_);
		Stopwatch stopwatch = Stopwatch::process();
		vector<vector<uint64_t>> combines = get_combines(*conn_pool_, unary_needs_, combine_rights_, precision_);
		std::size_t total_size = 0;
		for (const vector<uint64_t>& x : combines)
			total_size += x.size();
		fmt::print("Followed combine edges to {} gadgets in {}\n", total_size, stopwatch.elapsed().hms());
		for (vector<uint64_t>& x : combines)
			state_(std::move(x));

		unary_needs_.clear();
		unary_needs_.shrink_to_fit();
		combine_needs_.clear();
		combine_needs_.shrink_to_fit();
		phase_ = Phase::discover_needs_close;
		return Control::proceed;
	}

	Control discover_needs_close() {
		if (multiplayer_) {
			phase_ = Phase::discover_needs_mirror;
			return Control::proceed;
		}
		filter_unary(filter_ids_needing_close, state_.subgeneration_begin(), state_.subgeneration_end(), "close");
		phase_ = Phase::compute_close;
		return Control::proceed;
	}

	Control compute_close() {
		assert(!multiplayer_);
		Control control = Control::proceed;
		if (unary_needs_.size())
			control = operate_unary("close", "Close", runtime_opts_.close_gadgets_per_task, runtime_opts_.close_task_batch_threshold);
		//If we decide to use a separate resume phase to check fewer possible
		//needs, we'd preserve unary_needs_ here.
		unary_needs_.clear();
		unary_needs_.shrink_to_fit();
		phase_ = control == Control::proceed ? Phase::follow_close : Phase::discover_needs_close;
		return control;
	}

	Control follow_close() {
		assert(!multiplayer_);
		Stopwatch stopwatch = Stopwatch::process();
		vector<pair<tsl::hopscotch_set<uint64_t, farmhash_hash>, vector<uint64_t>>> close_edges
				= follow_close_edges(*conn_pool_, state_.subgeneration_begin(), state_.subgeneration_end());
		std::size_t source_size = 0, target_size = 0;
		for (const auto& p : close_edges) {
			source_size += p.first.size();
			target_size += p.second.size();
		}
		fmt::print("Followed close edges from {} gadgets to {} gadgets in {}\n",
				source_size, target_size, stopwatch.elapsed().hms());

		//erase_from_subgeneration scans the subgeneration, so we want to merge the sets.
		tsl::hopscotch_set<uint64_t, farmhash_hash> source_set = std::move(close_edges[0].first);
		for (std::size_t i = 1; i < close_edges.size(); ++i)
			source_set.insert(close_edges[i].first.begin(), close_edges[i].first.end());
		state_.erase_from_subgeneration(source_set);
		for (auto& p : close_edges)
			state_(std::move(p.second));
		phase_ = Phase::discover_needs_mirror;
		return Control::proceed;
	}

	Control discover_needs_mirror() {
		filter_unary(filter_ids_needing_mirror, state_.subgeneration_begin(), state_.subgeneration_end(), "mirror");
		phase_ = Phase::compute_mirror;
		return Control::proceed;
	}

	Control compute_mirror() {
		Control control = Control::proceed;
		if (unary_needs_.size())
			control = operate_unary("mirror", "Mirror", runtime_opts_.mirror_gadgets_per_task, runtime_opts_.mirror_task_batch_threshold);
		//If we decide to use a separate resume phase to check fewer possible
		//needs, we'd preserve unary_needs_ here.
		unary_needs_.clear();
		unary_needs_.shrink_to_fit();
		phase_ = control == Control::proceed ? Phase::follow_mirror : Phase::discover_needs_mirror;
		return control;
	}

	Control follow_mirror() {
		follow_unary_simple(get_mirrors, state_.subgeneration_begin(), state_.subgeneration_end(), "mirror");

		std::string_view step_type = subgeneration_ == 0 ? "combine"sv : "connect"sv;
		Stopwatch::Result elapsed = subgeneration_stopwatch_.elapsed();
		//printing the subgen size is redundant for combine
		fmt::print("Finished {} {}.{} in {}; subgen size {}, curgen size {}, closed size {}, max resident {:.2f} GiB (+{:.2f})\n",
				step_type, generation_, subgeneration_, elapsed.hms(),
				state_.subgeneration_size(), state_.generation_size(), state_.closed_size(),
				elapsed.absolute().highwaterGibibytes(), elapsed.highwaterGibibytes());

		phase_ = Phase::begin_subgeneration;
		return Control::proceed;
	}

	Control begin_subgeneration() {
		//I suppose we could wait until after any subgenerations (connects) to
		//choose the set of combine rights, but this matches how the old
		//generational search worked.
		if (generation_ == 0 && subgeneration_ == 0) {
			combine_rights_.assign(state_.subgeneration_begin(), state_.subgeneration_end());
			std::sort(combine_rights_.begin(), combine_rights_.end());
			fmt::print("Combine rights ({}):", combine_rights_.size());
			for (uint64_t id : combine_rights_)
				fmt::print(" {}", id);
			fmt::print("\n");
		}

		state_.flip_subgeneration();
		if (state_.prev_subgeneration_begin() == state_.prev_subgeneration_end()) {
			Stopwatch::Result elapsed = generation_stopwatch_.elapsed();
			fmt::print("Finished generation {} in {}; curgen size {}, closed size {}, max resident {:.2f} GiB (+{:.2f})\n",
					generation_, elapsed.hms(), state_.generation_size(), state_.closed_size(),
					elapsed.absolute().highwaterGibibytes(), elapsed.highwaterGibibytes());
			phase_ = Phase::begin_generation;
			return Control::proceed;
		}

		++subgeneration_;
		subgeneration_stopwatch_.reset();
		phase_ = Phase::discover_needs_connect;
		return Control::proceed;
	}

	Control discover_needs_connect() {
		filter_unary(filter_ids_needing_connect, state_.prev_subgeneration_begin(), state_.prev_subgeneration_end(), "connect");
		phase_ = Phase::compute_connect;
		return Control::proceed;
	}

	Control compute_connect() {
		Control control = Control::proceed;
		if (unary_needs_.size())
			control = operate_unary("connect", "Connect", runtime_opts_.connect_gadgets_per_task, runtime_opts_.connect_task_batch_threshold);
		//If we decide to use a separate resume phase to check fewer possible
		//needs, we'd preserve unary_needs_ here.
		unary_needs_.clear();
		unary_needs_.shrink_to_fit();
		phase_ = control == Control::proceed ? Phase::follow_connect : Phase::discover_needs_connect;
		return control;
	}

	Control follow_connect() {
		follow_unary_simple(get_connects, state_.prev_subgeneration_begin(), state_.prev_subgeneration_end(), "connect");

		phase_ = Phase::discover_needs_close;
		return Control::proceed;
	}

	template<class FilterFunc, class Iter>
	void filter_unary(FilterFunc filter_func, Iter first, Iter last, std::string_view log_name) {
		Stopwatch stopwatch = Stopwatch::process();
		if (!unary_needs_.empty())
			throw std::logic_error(fmt::format("called filter_unary for {} but unary_needs_ not empty\n", log_name));
		unary_needs_ = filter_func(*conn_pool_, first, last);
		fmt::print("Found {} of {} gadgets needing {} in {}\n",
				unary_needs_.size(), std::distance(first, last), log_name, stopwatch.elapsed().hms());
		//Sorting here means tasks will contain consecutive ids more often,
		//reducing fragmentation in the completion tables.
		std::sort(unary_needs_.begin(), unary_needs_.end());
	}

	Control operate_unary(std::string_view operation_name, std::string_view log_name,
			std::size_t gadgets_per_task, std::size_t batch_threshold) {
		Stopwatch stopwatch = Stopwatch::process();
		UnaryBatcher batcher(unary_needs_, gadgets_per_task);
		if (batcher.size() < batch_threshold) {
			std::string operation_cmd = fmt::format("{}-db", operation_name);
			DatabaseOperationStatistics stats = do_unary_operation(*workers_, operation_cmd, batcher);
			fmt::print("{} operation completed in {}: {} locally pruned, {} globally pruned, {} novel gadgets, {} edges\n",
					log_name, stopwatch.elapsed().hms(), stats.pruned_locally, stats.pruned_database, stats.novel_gadgets, stats.edges);
			return Control::proceed;
		} else {
			std::size_t task_count = batcher.size();
			std::string operation_cmd = fmt::format("batch-{}", operation_name);
			ConnectionLease conn = conn_pool_->checkout();
			write_unary_batch_tasks(*conn, operation_cmd, batcher, runtime_opts_.batch_task_directory);
			fmt::print("wrote {} {} tasks in {}\n", task_count, log_name, stopwatch.elapsed().hms());
			return Control::suspend;
		}
	}

	template<class FilterFunc, class Iter>
	void follow_unary_simple(FilterFunc follow_func, Iter first, Iter last, std::string_view log_name) {
		Stopwatch stopwatch = Stopwatch::process();
		vector<vector<uint64_t>> ids = follow_func(*conn_pool_, first, last);
		std::size_t total_size = 0;
		for (const vector<uint64_t>& x : ids)
			total_size += x.size();
		fmt::print("Followed {} edges to {} gadgets in {}\n", log_name, total_size, stopwatch.elapsed().hms());
		for (vector<uint64_t>& x : ids)
			state_(std::move(x));
	}


	SearchState state_;
	vector<uint64_t> combine_rights_;
	vector<uint64_t> unary_needs_; //also holds combine lefts during combine phases
	//first element is a list of rights, second is a list of lefts (it's backwards)
	vector<pair<vector<uint64_t>, vector<uint64_t>>> combine_needs_;
	unsigned int generation_;
	unsigned int subgeneration_;
	Phase phase_;

	//These are options that cannot be changed when loading from a checkpoint
	//(as opposed to, e.g., worker addresses).
	vector<std::string> cmdline_specs_;
	GadgetSet source_specs_;
	unsigned int precision_;
	bool multiplayer_;

	//These are provided at runtime and not stored with the checkpoint.  They
	//could be passed around through all functions, but are instead here for
	//convenience.
	ConnectionPool* conn_pool_;
	WorkerManager* workers_; //may be nullptr if no worker args given (must write tasks)

	RuntimeOptions runtime_opts_;

	//Timings from these aren't particularly useful when operating in batch mode.
	Stopwatch generation_stopwatch_, subgeneration_stopwatch_;
public:
	struct Serialized {
		SearchState::Serialized state;
		vector<uint64_t> combine_rights;
		vector<uint64_t> unary_needs;
		vector<pair<vector<uint64_t>, vector<uint64_t>>> combine_needs;
		unsigned int generation;
		unsigned int subgeneration;
		//MSGPACK_ADD_ENUM doesn't work for private enums
		std::underlying_type_t<Phase> phase;
		vector<std::string> cmdline_specs;
		GadgetSet source_specs;
		unsigned int precision;
		bool multiplayer;
		Serialized() = default; //for msgpack
		Serialized(const Search& s) : state(s.state_.serialized()), combine_rights(s.combine_rights_),
				unary_needs(s.unary_needs_), combine_needs(s.combine_needs_),
				generation(s.generation_), subgeneration(s.subgeneration_),
				phase(static_cast<std::underlying_type_t<Phase>>(s.phase_)),
				cmdline_specs(s.cmdline_specs_), source_specs(s.source_specs_), precision(s.precision_),
				multiplayer(s.multiplayer_) {
			canonicalize();
		}
		Serialized(Search&& s) : state(std::move(s.state_).serialized()), combine_rights(std::move(s.combine_rights_)),
				unary_needs(std::move(s.unary_needs_)), combine_needs(std::move(s.combine_needs_)),
				generation(s.generation_), subgeneration(s.subgeneration_),
				phase(static_cast<std::underlying_type_t<Phase>>(s.phase_)),
				cmdline_specs(std::move(s.cmdline_specs_)), source_specs(std::move(s.source_specs_)),
				precision(s.precision_), multiplayer(s.multiplayer_) {
			canonicalize();
		}

		//We're not really worried about format-ABI issues because the state can
		//be regenerated from the database, so we can use msgpack's packing
		//rather than writing our own versionable packing.
		MSGPACK_DEFINE_ARRAY(state, combine_rights, unary_needs, combine_needs,
				generation, subgeneration, phase, cmdline_specs, source_specs,
				precision, multiplayer)
	private:
		void canonicalize() {
			//TODO: I think these could all be asserts, but they're cheap enough
			assert(std::is_sorted(combine_rights.begin(), combine_rights.end()));
			if (!std::is_sorted(unary_needs.begin(), unary_needs.end()))
				std::sort(unary_needs.begin(), unary_needs.end());
			for (auto& p : combine_needs) {
				if (!std::is_sorted(p.first.begin(), p.first.end()))
					std::sort(p.first.begin(), p.first.end());
				if (!std::is_sorted(p.second.begin(), p.second.end()))
					std::sort(p.second.begin(), p.second.end());
			}
			if (!std::is_sorted(combine_needs.begin(), combine_needs.end()))
				std::sort(combine_needs.begin(), combine_needs.end());
			//let cmdline_specs, source_specs keep their original order
		}
	};
	Serialized serialize() const & {
		return {*this};
	}
	Serialized serialize() && {
		return {std::move(*this)};
	}
};

int main(int argc, char* argv[]) { //genbuild {'entrypoint': True, 'ldflags': '-lpqxx -lpq -l:libboost_system.a -llmdb'}
	std::string_view db_path, checkpoint_db_path;
	unsigned int num_db_threads = 1;
	std::vector<std::string> worker_addrs; //or @foo for response files
	bool multiplayer = false;
	std::vector<std::string_view> gid_specs;
	unsigned int precision = 8;
	RuntimeOptions runtime_opts;
	runtime_opts.combine_pairs_per_task = 5000;
	runtime_opts.connect_gadgets_per_task = runtime_opts.close_gadgets_per_task
			= runtime_opts.mirror_gadgets_per_task = 5000;
	runtime_opts.combine_task_batch_threshold = runtime_opts.connect_task_batch_threshold
			= runtime_opts.close_task_batch_threshold = runtime_opts.mirror_task_batch_threshold
			= std::numeric_limits<std::size_t>::max();
	for (int i = 1; i < argc; ++i) {
		if (argv[i] == "--db-path"sv)
			db_path = argv[++i];
		else if (argv[i] == "--checkpoint-db-path"sv)
			checkpoint_db_path = argv[++i];
		else if (argv[i] == "--db-threads"sv)
			num_db_threads = to_uint(argv[++i]);
		else if (argv[i] == "--worker"sv)
			worker_addrs.emplace_back(argv[++i]);
		else if (argv[i] == "--multiplayer"sv)
			multiplayer = true;
		else if (argv[i] == "--precision"sv)
			precision = to_uint(argv[++i]);

		else if (argv[i] == "--gadgets-per-task"sv)
			runtime_opts.combine_pairs_per_task = runtime_opts.connect_gadgets_per_task
					= runtime_opts.close_gadgets_per_task = runtime_opts.mirror_gadgets_per_task = to_uint64(argv[++i]);
		else if (argv[i] == "--combine-gadgets-per-task"sv || argv[i] == "--combine-pairs-per-task"sv)
			runtime_opts.combine_pairs_per_task = to_uint64(argv[++i]);
		else if (argv[i] == "--connect-gadgets-per-task"sv)
			runtime_opts.connect_gadgets_per_task = to_uint64(argv[++i]);
		else if (argv[i] == "--close-gadgets-per-task"sv)
			runtime_opts.close_gadgets_per_task = to_uint64(argv[++i]);
		else if (argv[i] == "--mirror-gadgets-per-task"sv)
			runtime_opts.mirror_gadgets_per_task = to_uint64(argv[++i]);

		else if (argv[i] == "--batch-task-threshold"sv || argv[i] == "--task-batch-threshold"sv)
			runtime_opts.combine_task_batch_threshold = runtime_opts.connect_task_batch_threshold
					= runtime_opts.close_task_batch_threshold = runtime_opts.mirror_task_batch_threshold = to_uint64(argv[++i]);
		else if (argv[i] == "--combine-batch-task-threshold"sv || argv[i] == "--combine-task-batch-threshold"sv)
			runtime_opts.combine_task_batch_threshold = to_uint64(argv[++i]);
		else if (argv[i] == "--connect-batch-task-threshold"sv || argv[i] == "--connect-task-batch-threshold"sv)
			runtime_opts.connect_task_batch_threshold = to_uint64(argv[++i]);
		else if (argv[i] == "--close-batch-task-threshold"sv || argv[i] == "--close-task-batch-threshold"sv)
			runtime_opts.close_task_batch_threshold = to_uint64(argv[++i]);
		else if (argv[i] == "--mirror-batch-task-threshold"sv || argv[i] == "--mirror-task-batch-threshold"sv)
			runtime_opts.mirror_task_batch_threshold = to_uint64(argv[++i]);
		else if (argv[i] == "--batch-task-directory"sv)
			runtime_opts.batch_task_directory = argv[++i];

		else
			gid_specs.emplace_back(argv[i]);
	}

	if (db_path.empty()) {
		fmt::print("ERROR: must specify --db-path\n");
		return 1;
	}

	//TODO: if the batch task thresholds are all 0, it's fine to have no workers
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
	fmt::print("Gadget spec: {}\n", format_gadget_set(spec));

	lmdb::env data_env = lmdb::env::create();
	data_env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	data_env.set_max_dbs(64);
	data_env.open(std::string(db_path).c_str()); //TODO: flags?
	uint64_t database_id = 0;
	{
		lmdb::txn txn = lmdb::txn::begin(data_env, nullptr, MDB_RDONLY);
		lmdb::dbi meta = lmdb::dbi::open(txn, "meta");
		std::string_view id_target;
		if (!meta.get(txn, "id_bytes", id_target)) {
			fmt::print("ERROR: database {} doesn't have an id?\n", db_path);
			return 1;
		}
		database_id = lmdb::from_sv<uint64_t>(id_target);
		txn.commit();
	}

	std::optional<Search> search; //just for lazy init
	if (!checkpoint_db_path.empty()) {
		lmdb::env checkpoint_env = lmdb::env::create(MDB_NOSUBDIR);
		data_env.set_mapsize(5UL * 1024 * 1024 * 1024);
		data_env.set_max_dbs(1);
		data_env.open(std::string(checkpoint_db_path).c_str()); //TODO: flags?
		lmdb::txn txn = lmdb::txn::begin(checkpoint_env);
		lmdb::dbi checkpoint_root = lmdb::dbi::open(txn, nullptr);
		std::string_view id_target;
		if (checkpoint_root.get(txn, "parent_id_bytes", id_target)) {
			uint64_t parent_id = lmdb::from_sv<uint64_t>(id_target);
			if (parent_id != database_id) {
				fmt::print("ERROR: checkpoint database {} is from id {}, but parent {} has id {}\n",
						checkpoint_db_path, parent_id, db_path, database_id);
				return 1;
			}
			txn.commit();
			search.emplace(std::move(checkpoint_env), runtime_opts);
		} else {
			if (checkpoint_root.size(txn) != 0) {
				fmt::print("ERROR: checkpoint database {} doesn't have parent id, but also isn't empty\n", checkpoint_db_path);
				return 1;
			}
			checkpoint_root.put(txn, "parent_id_bytes", lmdb::to_sv(database_id));
			checkpoint_root.put(txn, "parent_id", fmt::to_string(database_id));
			txn.commit();
			//run the normal ctor, but also give it the environment
			search.emplace(vector<std::string>(gid_specs.begin(), gid_specs.end()), std::move(spec),
					precision, multiplayer, std::move(checkpoint_env), runtime_opts);
		}
	} else
		//no checkpoint environment available
		search.emplace(vector<std::string>(gid_specs.begin(), gid_specs.end()), std::move(spec),
			precision, multiplayer, runtime_opts);

	if (!search->execute(pool, &manager)) {
		//TODO: if we have a checkpoint database, we're going to take checkpoints
		//continuously, not just when suspending, so this logic is unnecessary
		if (suspend_checkpoint.empty())
			fmt::print(stderr, "ERROR: would suspend, but --suspend-checkpoint not passed\n");
		else {
			simple_buffer buf;
			msgpack::pack(buf, std::move(*search).serialize());
			write_buffer(buf, std::string(suspend_checkpoint));
		}
	}

	return 0;
}