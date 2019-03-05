#include "precompiled.hpp"
#include "database.hpp"
#include "rpc.hpp"
#include "stringutils.hpp"
#include "ioutils.hpp"
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
		for (std::size_t i = 0; i < workers_.size() && generator->next(buffers_[i]); ++i)
			sockets_[i].async_connect(workers_[i], [=](const boost::system::error_code& ec){on_connect(i, ec);});
		ctx_.run();
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
				dispatch_write(index);
			//TODO: We might want to shrink the buffer if we're going idle.
		} else
			throw std::system_error(ec, fmt::format("reading from worker {} at endpoint {}:{}, transferred {}, previously transferred {}",
					index, workers_[index].address().to_string(), workers_[index].port(), bytes_transferred, buffers_[index].size()));
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
				fmt::print("worker {}: {}", response.seq(), response.result_as<std::string>());
			else
				fmt::print(stderr, "worker {} ping error: {}", response.seq(), response.error_as());
		}
	} generator(manager.size());
	manager.run(&generator);
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
	WorkerManager manager(worker_addrs);
	ping_all_workers(manager);

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