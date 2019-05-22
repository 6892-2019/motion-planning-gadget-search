#include "precompiled.hpp"
#include "../database.hpp"
#include "../rpc.hpp"
#include "../toggles-shared.hpp"
#include "intervals.hpp"
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

vector<std::pair<std::size_t, std::string>> required_combines_prepared;
const unsigned int required_combines_prepared_batch_sizes[] = {50000, 25000, 10000, 5000, 1000, 500};

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

//Copied from toggles-shared because we need to record close edge inputs and I
//don't see a good way to templatize them together.  (constexpr if and a bool template param?)
template<class Edge>
pair<vector<pair<uint64_t, uint64_t>>, vector<pair<uint64_t, uint64_t>>> follow_edges_with_inputs(
		lmdb::env& env, lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources) {
	interval_accumulator<uint64_t> input_accum(256), output_accum(256);
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
			while (first != last)
				output_accum(first++->output);
			input_accum(lmdb::from_sv<uint64_t>(key));

			if (!cur.get(key, value, MDB_NEXT)) break;
		}
	}
	txn.commit();
	return {std::move(input_accum).finish(), std::move(output_accum).finish()};
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


template<typename T>
vector<T>& unmarshal_reinterpret(vector<T>& dest, lmdb::dbi& db, lmdb::txn& txn, std::string_view key, bool allow_empty) {
	std::string_view value;
	if (!db.get(txn, key, value))
		throw std::runtime_error("key \"{}\" not found", key);
	if ((value.size() == 0 && !allow_empty) || value.size() % sizeof(T) != 0)
		throw std::logic_error(fmt::format("key {} has value length {} (not a multiple of {}) {}",
				key, value.size(), sizeof(T), typeid(T).name()));
	const T* first = reinterpret_cast<const T*>(value.data());
	const T* last = first + value.size() / sizeof(T);
	dest.assign(first, last);
	return dest;
}
template<typename T>
vector<T> unmarshal_reinterpret(lmdb::dbi& db, lmdb::txn& txn, std::string_view key, bool allow_empty) {
	vector<T> dest;
	unmarshal_reinterpret(dest, db, txn, key, allow_empty);
	return dest;
}

template<typename T>
T unmarshal_from_string(lmdb::dbi& db, lmdb::txn& txn, std::string_view key) {
	std::string_view value;
	if (!db.get(txn, key, value))
		throw std::runtime_error("key \"{}\" not found", key);
	//TODO: from_string will throw on a parse problem, but we'll lose which key had the problem
	return from_string(value);
}

void marshal_nullseparated(lmdb::dbi& db, lmdb::txn& txn, std::string_view key, const vector<std::string>& data) {
	//We could use MDB_RESERVE here, but I am assuming this is uncommon code...
	for (const std::string& x : data)
		assert(x.find('\0') == std::string::npos);
	std::string value = join(data, "\0");
	if (!db.put(txn, key, value))
		//put only returns false if we passed MDB_NOOVERWRITE and the key existed
		throw std::logic_error("can't happen: put threw for key {}", key);
}

void unmarshal_nullseparated(vector<std::string>& data, lmdb::dbi& db, lmdb::txn& txn, std::string_view key, bool allow_empty) {
	std::string_view value;
	if (!db.get(txn, key, value))
		throw std::runtime_error("key \"{}\" not found", key);
	if (value.empty() && !allow_empty)
		throw std::runtime_error("key \"{}\" was empty", key);
	split(data, value, '\0');
}



class SearchState {
public:
	SearchState() {}
	static SearchState resume(lmdb::env& checkpoint_env, lmdb::txn& txn, lmdb::dbi& main_db) {
		SearchState s;
		unmarshal_reinterpret(s.closed_, main_db, txn, "state.closed", true);
		unmarshal_reinterpret(s.curgen_, main_db, txn, "state.curgen", true);
		unmarshal_reinterpret(s.subgen_, main_db, txn, "state.subgen", true);
		unmarshal_reinterpret(s.prev_subgen_, main_db, txn, "state.prev_subgen", true);
		return s;
	}
	void operator()(uint64_t id) {
		std::array<pair<uint64_t, uint64_t>, 1> singleton = {{id, id+1}};
		subgen_ = interval_union(subgen_.begin(), subgen_.end(), singleton.cbegin(), singleton.cend());
	}
	void operator()(const vector<pair<uint64_t, uint64_t>>& ids) {
		//Add to the subgen those ids we haven't put in closed_.
		vector<pair<uint64_t, uint64_t>> novel = interval_difference(
				ids.begin(), ids.end(), closed_.cbegin(), closed_.cend());
		subgen_ = interval_union(subgen_.begin(), subgen_.end(), novel.begin(), novel.end());
	}
	void operator()(vector<pair<uint64_t, uint64_t>>&& ids_rref) {
		//Enforce it actually moves so it gets deallocated promptly.
		vector<pair<uint64_t, uint64_t>> ids(std::move(ids_rref));
		//Now it's an lvalue so we can just delegate.
		//TODO: implement modifying interval_ functions so we can reuse its storage
		this->operator()(ids);
	}
	std::size_t subgeneration_size() const {
		return interval_size(subgen_);
	}
	std::size_t generation_size() const {
		return interval_size(curgen_) + subgeneration_size();
	}
	std::size_t closed_size() const {
		return interval_size(closed_) + subgeneration_size();
	}
	//TODO: to avoid repeated size calculations when reporting, and to report
	//the number of intervals in each group, we might want to return a stats
	//structure instead.  (could also return average interval size, etc.)
	/**
	 * Gives access to the current subgeneration.  Do not append to the state
	 * while iterating this view.
	 * @return an iterator range spanning the current subgeneration
	 */
	const vector<pair<uint64_t, uint64_t>>& subgeneration() const {
		return subgen_;
	}
	const vector<pair<uint64_t, uint64_t>>& prev_subgeneration() const {
		return prev_subgen_;
	}
	/**
	 * Erases the ids in the given set from the current subgeneration.  They're
	 * still in the closed set.
	 */
	void erase_from_subgeneration(const vector<pair<uint64_t, uint64_t>>& to_be_erased) {
		//Everything we're erasing should be in subgen_ already.
		assert(interval_intersection(subgen_.cbegin(), subgen.cend(), to_be_erased.cbegin(), to_be_erased.cend()) == to_be_erased);
		//Erase it, but record the removed elements in the tenured closed set.
		closed_ = interval_union(closed_.begin(), closed_.end(), to_be_erased.cbegin(), to_be_erased.cend());
		subgen_ = interval_difference(subgen_.begin(), subgen_.end(), to_be_erased.begin(), to_be_erased.end());
	}
	/**
	 * Begin a new generation.
	 * @return the previous generation
	 */
	vector<pair<uint64_t, uint64_t>> flip_generation() {
		curgen_ = interval_union(curgen_.begin(), curgen_.end(), subgen_.begin(), subgen.end());
		vector<pair<uint64_t, uint64_t>> prev = std::move(curgen_);
		curgen_.clear(); //make moved-from vector suitable for insertion again
		subgen_.clear();
		return prev;
	}
	/**
	 * Begin a new subgeneration.
	 */
	void flip_subgeneration() {
		closed_ = interval_union(closed_.begin(), closed_.end(), subgen_.cbegin(), subgen.cend());
		curgen_ = interval_union(curgen_.begin(), curgen_.end(), subgen_.cbegin(), subgen.cend());
		prev_subgen_ = std::move(subgen_);
		subgen_.clear();
	}

private:
	/**
	 * The set of all gadget ids encountered so far, divided into a "tenured"
	 * closed set and the current subgeneration (exclusively).  Ids are added
	 * to subgen_ in operator() and promoted from subgen_ to closed_ in
	 * erase_from_subgeneration and flip_subgeneration.
	 *
	 * A generation is all gadgets since the previous combine; a subgeneration
	 * is all gadgets discovered since the previous connect.
	 */
	vector<pair<uint64_t, uint64_t>> closed_, subgen_;
	/**
	 * The current generation, except those in subgen_.  Only updated when flipping.
	 */
	vector<pair<uint64_t, uint64_t>> curgen_;
	/**
	 * The previous subgeneration.  This is included in curgen_.  Updated upon
	 * flipping.
	 */
	vector<pair<uint64_t, uint64_t>> prev_subgen_;
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

	//for the convenience of resume()
	Search(lmdb::env&& database, RuntimeOptions runtime_opts) :
			database_(std::move(database)), runtime_opts_(runtime_opts) {}

public:
	Search(vector<std::string>&& cmdline_specs, GadgetSet&& source_specs,
			unsigned int precision, bool multiplayer, RuntimeOptions runtime_opts,
			lmdb::env&& database, std::optional<lmdb::env>&& checkpoint) :
			generation_(0), subgeneration_(0), phase_(Phase::collect_initial),
			cmdline_specs_(std::move(cmdline_specs)),
			source_specs_(std::move(source_specs)), precision_(precision), multiplayer_(multiplayer),
			database_(std::move(database)), workers_(nullptr), checkpoint_(std::move(checkpoint)),
			runtime_opts_(runtime_opts), generation_stopwatch_(Stopwatch::process()),
			subgeneration_stopwatch_(Stopwatch::process()) {}
	static Search resume(lmdb::env&& checkpoint, lmdb::env&& database, RuntimeOptions runtime_opts) {
		Search s(std::move(database, runtime_opts));
		{
			auto txn = lmdb::txn::begin(checkpoint, nullptr, MDB_RDONLY);
			lmdb::dbi root = lmdb::dbi::open(txn, nullptr);
			s.state_ = SearchState::resume(checkpoint, txn, root);
			unmarshal_reinterpret(s.combine_rights_, root, txn, "combine_rights", true);
			unmarshal_reinterpret(s.unary_needs_, root, txn, "unary_needs", true);
			throw std::logic_error("TODO: unmarshal combine_needs_"); //we're going to change combine_needs_ soon anyway, implement this later
			s.generation_ = unmarshal_from_string<unsigned int>(root, txn, "generation");
			s.subgeneration_ = unmarshal_from_string<unsigned int>(root, txn, "subgeneration");
			//using the string name of the enum would be more flexible...
			s.phase_ = Phase{unmarshal_from_string<unsigned int>(root, txn, "phase")};
			unmarshal_nullseparated(s.cmdline_specs_, root, txn, "cmdline_specs", true);
			unmarshal_reinterpret(s.source_specs_.ids, root, txn, "source_specs.ids", true);
			unmarshal_reinterpret(s.source_specs_.ranges, root, txn, "source_specs.ranges", true);
			unmarshal_nullseparated(s.source_specs_.names, root, txn, "source_specs.names", true);
			s.precision_ = unmarshal_from_string<unsigned int>(root, txn, "precision");
			s.multiplayer_ = bool(unmarshal_from_string<unsigned int>(root, txn, "multiplayer"));
			txn.commit();
		}
		*s.checkpoint_ = std::move(checkpoint);
		return s;
	}

	/**
	 * @return true if the search completed; false if we should write a checkpoint
	 */
	bool execute(WorkerManager* workers) {
		open_subdatabases();
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

		workers_ = nullptr;
		return control == Control::stop;
	}

private:
	Control collect_initial() {
		generation_stopwatch_.reset();
		Stopwatch stopwatch = Stopwatch::process();
		vector<uint64_t> initial = collect_initial_gadget_set(database_, source_specs_);
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
		open_combine_subdatabases();
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
		open_combine_subdatabases();
		Stopwatch stopwatch = Stopwatch::process();

		vector<vector<pair<uint64_t, uint64_t>>> incoming;
		for (const auto& p : edges_combine_)
			incoming.push_back(follow_edges<CombineEdge>(database_, p.second, unary_needs_));
		//binary merge tree
		//TODO: move this to intervals.hpp as multiway union?  but we also want
		//fork-join-ish stuff here and that won't generalize well
		while (incoming.size() > 1) {
			for (std::size_t i = 0; i < incoming.size()-1; ++i) {
				incoming[i] = interval_union(incoming[i].begin(), incoming[i].end(), incoming[i+1].begin(), incoming[i+1].end());
				incoming[i+1].clear();
			}
			incoming.erase(std::partition(incoming.begin(), incoming.end(), [](const auto& x){return !x.empty();}), incoming.end());
		}
		//We might have found nothing; ensure incoming.front() always exists.
		if (incoming.empty())
			incoming.push_back();
		fmt::print("Followed combine edges to {} gadgets in {}\n", interval_size(incoming.front()), stopwatch.elapsed().hms());
		state_(std::move(incoming.front()));

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
		filter_unary("close", state_.subgeneration(), state_.subgeneration_end());
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
		pair<vector<pair<uint64_t, uint64_t>>, vector<pair<uint64_t, uint64_t>>> close_edges =
				follow_edges_with_inputs<SimpleEdge>(database_, edges_close_, state_.subgeneration());
		fmt::print("Followed close edges from {} gadgets to {} gadgets in {}\n",
				interval_size(close_edges.first), interval_size(close_edges.second), stopwatch.elapsed().hms());

		state_.erase_from_subgeneration(std::move(close_edges.first));
		state_(std::move(close_edges.second));
		phase_ = Phase::discover_needs_mirror;
		return Control::proceed;
	}

	Control discover_needs_mirror() {
		filter_unary("mirror", state_.subgeneration(), state_.subgeneration_end());
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
		follow_unary_simple(&follow_edges<SimpleEdge>, edges_mirror_, state_.subgeneration(), "mirror");

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
		filter_unary("connect", state_.prev_subgeneration());
		//We used to filter out gadgets with < 4 locations here, but for now
		//we'll defer that to task-writing time or to the runner so we don't
		//have to touch the gadget data.  (It wouldn't be done in filter_unary
		//anyway now that we aren't emitting SQL.)
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
		follow_unary_simple(&follow_edges<ConnectEdge>, edges_connect_, state_.prev_subgeneration(), "connect");
		phase_ = Phase::discover_needs_close;
		return Control::proceed;
	}

	void filter_unary(std::string_view completions_key, const vector<pair<uint64_t, uint64_t>>& candidates) {
		Stopwatch stopwatch = Stopwatch::process();
		if (!unary_needs_.empty())
			throw std::logic_error(fmt::format("called filter_unary for {} but unary_needs_ not empty\n", completions_key));
		unary_needs_ = filter_completion(database_, completions_, completions_key, candidates);
		fmt::print("Found {} of {} gadgets needing {} in {}\n",
				interval_size(unary_needs_), candidates.size(), completions_key, stopwatch.elapsed().hms());
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

	//We call this with either follow_edges<ConnectEdge> or follow_edges<SimpleEdge>.
	using FollowEdgeFunc = decltype(&follow_edges<SimpleEdge>);
	void follow_unary_simple(FollowEdgeFunc follow_func, lmdb::dbi& edge_db, const vector<pair<uint64_t, uint64_t>>& intervals, std::string_view log_name) {
		Stopwatch stopwatch = Stopwatch::process();
		vector<pair<uint64_t, uint64_t>> targets = follow_func(database_, edge_db, intervals);
		fmt::print("Followed {} edges to {} gadgets in {}\n", log_name, interval_size(targets), stopwatch.elapsed().hms());
		state_(std::move(targets));
	}

	void open_subdatabases() {
		if (gadget_hashtable_.handle() != std::numeric_limits<MDB_dbi>::max())
			return; //already initialized
		//These databases should all exist if the database was initialized
		//properly, so we use a read-only transaction.
		auto txn = lmdb::txn::begin(database_, nullptr, MDB_RDONLY);
		gadget_hashtable_ = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index_ = lmdb::dbi::open(txn, "gadget_index");
		edges_connect_ = lmdb::dbi::open(txn, "edges-connect");
		edges_close_ = lmdb::dbi::open(txn, "edges-close");
		edges_mirror_ = lmdb::dbi::open(txn, "edges-mirror");
		completions_ = lmdb::dbi::open(txn, "completions");
		txn.commit();
	}

	void open_combine_subdatabases() {
		if (!edges_combine_.empty()) return;
		//We speculatively use a read-only transaction in the hope these
		//databases already exist; otherwise we take the write lock and create them.
		try {
			auto txn = lmdb::txn::begin(database_, nullptr, MDB_RDONLY);
			for (uint64_t g : combine_rights_)
				edges_combine_.emplace_back(g, lmdb::dbi::open(txn, fmt::format("edges-combine-{}", g).c_str()));
			txn.commit();
		} catch (lmdb::not_found_error&) {
			auto txn = lmdb::txn::begin(database_);
			for (uint64_t g : combine_rights_)
				edges_combine_.emplace_back(g, lmdb::dbi::open(txn, fmt::format("edges-combine-{}", g).c_str(),
						MDB_CREATE | MDB_INTEGERKEY));
			txn.commit();
		} //let other errors propagate
		assert(std::is_sorted(edges_combine_.begin(), edges_combine_.end()));
	}


	SearchState state_;
	vector<uint64_t> combine_rights_;
	vector<pair<uint64_t, uint64_t>> unary_needs_; //also holds combine lefts during combine phases
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

	//Things below here are not saved in the checkpoint.  They could be passed
	//around most everywhere, but are saved here for convenience.
	lmdb::env database_;
	lmdb::dbi gadget_hashtable_, gadget_index_, edges_connect_, edges_close_, edges_mirror_, completions_;
	vector<pair<uint64_t, lmdb::dbi>> edges_combine_; //sorted
	std::optional<lmdb::env> checkpoint_;
	WorkerManager* workers_; //may be nullptr if no worker args given (must write tasks)

	RuntimeOptions runtime_opts_;

	//Timings from these aren't particularly useful when operating in batch mode.
	Stopwatch generation_stopwatch_, subgeneration_stopwatch_;
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
					precision, multiplayer, runtime_opts, std::move(data_env), std::move(checkpoint_env));
		}
	} else
		//no checkpoint environment available
		search.emplace(vector<std::string>(gid_specs.begin(), gid_specs.end()), std::move(spec),
			precision, multiplayer, runtime_opts, std::move(data_env), std::nullopt);

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