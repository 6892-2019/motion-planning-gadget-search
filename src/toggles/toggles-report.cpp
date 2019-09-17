#include "precompiled.hpp"
#include "gadget-set.hpp"
#include "select-by-id.hpp"
#include "anyprov.hpp"
#include "rpc.hpp"
#include "stringutils.hpp"
#include "stopwatch.hpp"
#include "intervals.hpp"
#include "proj_compare.hpp"
#include "transform_reduce.hpp"
#include "tsl/ordered_set.h"
#include "task_parallel.hpp"
#include "ioutils.hpp"
#include <fmt/chrono.h>
#include <functional>

using std::vector;
using std::pair;
using std::uint8_t;
using std::uint64_t;
using namespace std::literals::string_view_literals;

/**
 * SkinnyProv stores just enough information to get the actual edge later.  For
 * connect, close and mirror edges, that's the other end of the edge; for
 * combines, it's the left input.  For connects we have to search for the edge
 * leading to the output; for combines we additionally have to search over all
 * allowed right inputs.  In both cases, if there are many edges, it doesn't
 * matter which we pick.
 */
class SkinnyProv {
public:
	SkinnyProv(uint64_t output, uint64_t input, EdgeKind kind) : output_(output),
			input_(input | (static_cast<uint64_t>(kind) << 56)) {
		//We should never get this high, but just in case, don't silently get
		//the wrong result.  (We could use just the highest 3 bits.)
		if (input > 0x00FFFFFFFFFFFFFF) [[unlikely]]
			throw std::runtime_error("input gadget id too large");
	}
	SkinnyProv(const SkinnyProv&) = default;
	SkinnyProv(SkinnyProv&&) = default;
	SkinnyProv& operator=(const SkinnyProv&) = default;
	SkinnyProv& operator=(SkinnyProv&&) = default;
	uint64_t output() const {
		return output_;
	}
	uint64_t input() const {
		return input_ & 0x00FFFFFFFFFFFFFF;
	}
	EdgeKind kind() const {
		return EdgeKind{numeric_cast<unsigned char>((input_ & 0xFF00000000000000) >> 56)};
	}
private:
	//The EdgeKind is stored in the high byte of input_, because we only check
	//the input when we've found something.
	std::uint64_t output_, input_;
};
bool operator<(const SkinnyProv& a, const SkinnyProv& b) {
	return a.output() < b.output();
}
bool operator==(const SkinnyProv& a, const SkinnyProv& b) {
	return a.output() == b.output();
}
bool operator<(const SkinnyProv& a, uint64_t b) {
	return a.output() < b;
}

using EdgeCache = tsl::hopscotch_map<uint64_t, AnyProv, farmhash_hash>;
using DeletedLocationsCache = tsl::hopscotch_map<uint64_t, vector<unsigned int>, farmhash_hash>;

vector<AnyProv> toposort_provs(const EdgeCache& prov, uint64_t root) {
	tsl::hopscotch_map<uint64_t, uint64_t, farmhash_hash> needs;
	tsl::hopscotch_map<uint64_t, vector<uint64_t>, farmhash_hash> releases;
	vector<uint64_t> stack;
	stack.push_back(root);
	while (!stack.empty()) {
		uint64_t cur = stack.back();
		stack.pop_back();
		const AnyProv& p = prov.at(cur);
		needs[cur] = 0;
		for (uint64_t i : p.inputs()) {
			++needs[cur];
			if (releases[i].empty()) //constructs if not existing
				stack.push_back(i);
			releases[i].push_back(cur);
		}
	}

	for (auto it = needs.begin(); it != needs.end(); ++it)
		if (!it->second)
			stack.push_back(it->first);
	assert(!stack.empty());
	std::sort(stack.begin(), stack.end(), std::greater<>());
	vector<uint64_t> released_now;
	vector<AnyProv> ret;
	while (!stack.empty()) {
		uint64_t cur = stack.back();
		stack.pop_back();
		ret.push_back(prov.at(cur));
		released_now.clear();
		for (uint64_t r : releases[cur]) {
			--needs[r];
			if (!needs[r])
				released_now.push_back(r);
		}
		std::sort(released_now.begin(), released_now.end(), std::greater<>());
		stack.insert(stack.end(), released_now.begin(), released_now.end());
	}
	return ret;
}

SkinnyProv find_sp(const vector<vector<SkinnyProv>>& provs, uint64_t output) {
	for (const vector<SkinnyProv>& prov : provs) {
		auto lb = std::lower_bound(prov.begin(), prov.end(), output);
		if (lb != prov.end() && lb->output() == output)
			return *lb;
	}
	throw std::logic_error(fmt::format("could not find SkinnyProv for {}", output));
}

template<class Edge>
std::optional<Edge> search_for_edge(lmdb::txn& txn, lmdb::dbi& edges, uint64_t input, uint64_t output) {
	std::optional<Edge> ret;
	visit_edges<Edge>(txn, edges, input, [&ret, output](uint64_t, const Edge& e) {
		if (e.output == output) {
			ret = e;
			return VisitEdgeResult::quit;
		}
		return VisitEdgeResult::proceed;
	});
	return ret;
}

uint64_t recover_combine_right(lmdb::env& env, vector<pair<uint64_t, lmdb::dbi>>& combine_skinny_edges, SkinnyProv p) {
	vector<pair<uint64_t, uint64_t>> singleton = {{p.input(), p.input()+1}};
	uint64_t ret = 0;
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	for (auto& db : combine_skinny_edges) {
		visit_skinny_edges(txn, db.second, singleton, [&ret, input2=db.first, p](uint64_t input, uint64_t output) {
			assert(input == p.input());
			if (output == p.output()) {
				ret = input2;
				return VisitEdgeResult::quit;
			}
			return VisitEdgeResult::proceed;
		});
		if (ret) break;
	}
	txn.commit();
	if (ret) return ret;
	throw std::logic_error(fmt::format("no combine right for {}/{}?", p.input(), p.output()));
}

void fill_cache(lmdb::env& env,
		vector<pair<uint64_t, lmdb::dbi>>& combine_edges, vector<pair<uint64_t, lmdb::dbi>>& combine_skinny_edges,
		lmdb::dbi& connect_edges, lmdb::dbi& connect_skinny_edges,
		lmdb::dbi& close_edges, lmdb::dbi& mirror_edges,
		const vector<pair<uint64_t, uint64_t>>& roots, const vector<vector<SkinnyProv>>& prov,
		EdgeCache& edge_cache, DeletedLocationsCache& delloc, std::string_view db_path, unsigned int threads) {
	vector<uint64_t> trace_lhs = interval_inflate(roots.begin(), roots.end());
	vector<pair<uint64_t, SkinnyProv>> combine_batch;
	vector<SkinnyProv> connect_batch, close_batch, mirror_batch;
	vector<uint64_t> newly_cached_connects; //keys into edge_cache; also combines because those include a connect
	for (std::size_t i = 0; i < trace_lhs.size(); ++i) { //note that we append during the loop
		if (edge_cache.count(trace_lhs[i]) ||
				//We'll always find it, but if we find it earlier, this one is a
				//duplicate.  This is a linear scan, so potentially quadratic,
				//but traces shouldn't get large enough for that to matter.
				std::find(trace_lhs.begin(), trace_lhs.end(), trace_lhs[i]) != trace_lhs.begin()+i)
			continue;
		SkinnyProv p = find_sp(prov, trace_lhs[i]);
		switch (p.kind()) {
			case EdgeKind::source:
				continue; //nothing to do; end of this branch of the trace
			case EdgeKind::combine: {
				uint64_t input2 = recover_combine_right(env, combine_skinny_edges, p);
				combine_batch.emplace_back(input2, p);
				newly_cached_connects.push_back(p.output());
				trace_lhs.push_back(p.input());
				trace_lhs.push_back(input2);
				}
				break;
			case EdgeKind::connect:
				connect_batch.push_back(p);
				newly_cached_connects.push_back(p.output());
				trace_lhs.push_back(p.input());
				break;
			case EdgeKind::close:
				close_batch.push_back(p);
				trace_lhs.push_back(p.input());
				break;
			case EdgeKind::mirror:
				mirror_batch.push_back(p);
				trace_lhs.push_back(p.input());
				break;
		}
	}

	if (!combine_batch.empty() || !connect_batch.empty() || !close_batch.empty() || !mirror_batch.empty()) {
		auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		//Close and mirror don't use skinny edges, so we always have the full edges.
		for (const SkinnyProv& p : close_batch) {
			if (std::optional<SimpleEdge> e = search_for_edge<SimpleEdge>(txn, close_edges, p.input(), p.output()))
				edge_cache.try_emplace(p.output(), AnyProv::close(p.input(), *e));
			else
				throw std::logic_error(fmt::format("no close edge for {}/{}", p.input(), p.output()));
		}
		for (const SkinnyProv& p : mirror_batch) {
			if (std::optional<SimpleEdge> e = search_for_edge<SimpleEdge>(txn, mirror_edges, p.input(), p.output()))
				edge_cache.try_emplace(p.output(), AnyProv::mirror(p.input(), *e));
			else
				throw std::logic_error(fmt::format("no mirror edge for {}/{}", p.input(), p.output()));
		}
		close_batch.clear();
		mirror_batch.clear();

		//Combine and connect may not have full edges.
		combine_batch.erase(std::remove_if(combine_batch.begin(), combine_batch.end(), [&](const pair<uint64_t, SkinnyProv>& p) {
			//TODO: the lambda here should be a utility in proj_compare.hpp
			auto db = std::find_if(combine_edges.begin(), combine_edges.end(), [p](const auto& q){return q.first == p.first;});
			if (db == combine_edges.end())
				throw std::logic_error(fmt::format("no edge database for combine right? {} {}",
						p.second.input(), p.first, p.second.output()));
			if (std::optional<CombineEdge> e = search_for_edge<CombineEdge>(txn, db->second, p.second.input(), p.second.output())) {
				edge_cache.try_emplace(p.second.output(), AnyProv::combine(p.second.input(), p.first, *e));
				return true;
			} else
				return false;
		}), combine_batch.end());
		connect_batch.erase(std::remove_if(connect_batch.begin(), connect_batch.end(), [&](const SkinnyProv& p) {
			if (std::optional<ConnectEdge> e = search_for_edge<ConnectEdge>(txn, connect_edges, p.input(), p.output())) {
				edge_cache.try_emplace(p.output(), AnyProv::connect(p.input(), *e));
				return true;
			} else
				return false;
		}), connect_batch.end());
		txn.commit();
	}

	vector<std::string> request_files, response_files;
	simple_buffer call_buf;
	//Group by combine right.
	std::sort(combine_batch.begin(), combine_batch.end(), proj_less<0>());
	if (!combine_batch.empty()) {
		for (auto first = combine_batch.begin(), last = std::upper_bound(first, combine_batch.end(), *first, proj_less<0>());
				first != combine_batch.end(); first = last, last = std::upper_bound(first, combine_batch.end(), *first, proj_less<0>())) {
			vector<uint64_t> rights = {first->first};
			interval_accumulator<uint64_t> lefts(64);
			for (auto i = first; i != last; ++i)
				lefts(i->second.input());

			call_buf.clear();
			pack_call(call_buf, numeric_cast<std::uint32_t>(request_files.size()),
					"combine-db-full", std::move(lefts).finish(), rights,
					//no limits on precision or states
					16, std::numeric_limits<unsigned int>::max());
			std::string request = make_temp_filename("toggles-report-combine-request", "msg"),
					response = make_temp_filename("toggles-report-combine-response", "msg");
			write_buffer(call_buf, request);
			request_files.push_back(request);
			response_files.push_back(response);
		}
	}
	if (!connect_batch.empty()) {
		interval_accumulator<uint64_t> operands(64);
		for (const SkinnyProv& p : connect_batch)
			operands(p.input());

		call_buf.clear();
		pack_call(call_buf, numeric_cast<std::uint32_t>(request_files.size()),
				"connect-db-full", std::move(operands).finish(),
				//no limit on states
				std::numeric_limits<unsigned int>::max());
		std::string request = make_temp_filename("toggles-report-connect-request", "msg"),
				response = make_temp_filename("toggles-report-connect-response", "msg");
		write_buffer(call_buf, request);
		request_files.push_back(request);
		response_files.push_back(response);
	}

	if (!request_files.empty()) {
		vector<std::function<void()>> tasks;
		tasks.reserve(request_files.size());
		for (std::size_t i = 0; i < request_files.size(); ++i)
			tasks.push_back([&, i](){
				std::string cmdline = fmt::format("toggles-runner.exe msgpack --db-path {} -i {} -o {}",
						db_path, request_files[i], response_files[i]);
				int rc = std::system(cmdline.c_str());
				if (rc)
					throw std::runtime_error(fmt::format("problem filling in edges {} {} {}", rc, i, request_files[i]));
				//There's some weirdness here where if there's a problem with
				//the response we just skip the remainder of the tasks, not
				//throwing or reporting errors.  Presumably read_buffer or
				//Response has some undefined behavior.
				Response resp = unpack_response(read_buffer(response_files[i]));
				if (!resp)
					throw std::runtime_error(fmt::format("edge-filling task {} returned error: {}", i, resp.error_as()));
				std::remove(request_files[i].c_str());
				std::remove(response_files[i].c_str());
			});
		//TODO: parallel_for with better interface? transform_reduce with trivial reducer?
		for (auto& t : tasks)
			t();
	}

	//Collect any missing edges.
	if (!combine_batch.empty() || !connect_batch.empty()) {
		auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		for (const pair<uint64_t, SkinnyProv>& p : combine_batch) {
			//TODO: copied from above, should be factored out
			auto db = std::find_if(combine_edges.begin(), combine_edges.end(), [p](const auto& q){return q.first == p.first;});
			if (db == combine_edges.end())
				throw std::logic_error(fmt::format("no edge database for combine right? {} {}",
						p.second.input(), p.first, p.second.output()));
			if (std::optional<CombineEdge> e = search_for_edge<CombineEdge>(txn, db->second, p.second.input(), p.second.output()))
				edge_cache.try_emplace(p.second.output(), AnyProv::combine(p.second.input(), p.first, *e));
			else
				throw std::logic_error(fmt::format("no combine edge (after filling) for {}/{}/{}",
						p.second.input(), p.first, p.second.output()));
		}
		for (const SkinnyProv& p : connect_batch) {
			if (std::optional<ConnectEdge> e = search_for_edge<ConnectEdge>(txn, connect_edges, p.input(), p.output()))
				edge_cache.try_emplace(p.output(), AnyProv::connect(p.input(), *e));
			else
				throw std::logic_error(fmt::format("no connect edge (after filling) for {}/{}", p.input(), p.output()));
		}
		txn.commit();
	}

	//We could cache these between runs, either in the database itself (no longer
	//using it read-only) or a side database (e.g., ~/.cache/toggles-report/{id}.mdb).
	//But reporting doesn't seem to be a bottleneck.
	if (!newly_cached_connects.empty()) {
		vector<AnyProv> requests;
		for (uint64_t o : newly_cached_connects)
			requests.push_back(edge_cache.at(o));

		simple_buffer rpcbuf;
		std::string temp_to = make_temp_filename("toggles-report-delloc-request", "msg"),
				temp_from = make_temp_filename("toggles-report-delloc-response", "msg");
		pack_call(rpcbuf, 42, "deleted-locations", requests);
		write_buffer(rpcbuf, temp_to);
		std::string cmdline = fmt::format("toggles-runner.exe msgpack --db-path {} -i {} -o {}",
				db_path, temp_to, temp_from);
		int retcode = std::system(cmdline.c_str());
		if (retcode)
			throw std::runtime_error(fmt::format("runner failed during deleted-locations: {}", retcode));
		rpcbuf.clear();
		read_buffer(rpcbuf, temp_from);
		Response resp = unpack_response(rpcbuf);
		if (!resp)
			throw std::runtime_error(fmt::format("deleted-locations returned error: {}\n{}\n{}",
					resp.error_as(), temp_to, temp_from));

		auto responses = resp.result_as<vector<pair<uint64_t, vector<unsigned int>>>>();
		for (auto& p : responses) {
			assert(std::find(newly_cached_connects.begin(), newly_cached_connects.end(), p.first) != newly_cached_connects.end());
			auto pair = delloc.try_emplace(p.first, std::move(p.second));
			if (!pair.second)
				throw std::runtime_error(fmt::format("dellocs conflict for {}: {} {}",
						p.first, *pair.first, p.second));
		}
		std::remove(temp_to.c_str());
		std::remove(temp_from.c_str());
	}

	if (!request_files.empty() || !newly_cached_connects.empty())
		if (unsigned int dead_count = check_for_stale_readers(env))
			fmt::print("cleaned up {} stale readers\n", dead_count);
}

const std::string_view preferred_names[] = {
	"diode"sv,
	"wire"sv,

	"diode-diode-parallel"sv,
	"diode-diode-antiparallel-r"sv,
	"diode-diode-antiparallel-s"sv,
	"diode-diode-crossing"sv,

	"wire-wire-noncrossing"sv,
	"wire-wire-crossing"sv,

	"diode-wire-noncrossing-r"sv,
	"diode-wire-noncrossing-s"sv,
	"diode-wire-crossing"sv,
};

struct TargetStuff {
	vector<pair<uint64_t, uint64_t>> intervals;
	EdgeCache edge_cache;
	tsl::hopscotch_map<uint64_t, vector<std::string>, farmhash_hash> inv_names;
};

TargetStuff target_stuff(lmdb::env& env) {
	//Our targets are always the full set of named gadgets.  Because there are a
	//bounded number of edges there, we eagerly fill the target edge cache.  We
	//also build an id -> names map and the target set for later intersection
	//checking.
	EdgeCache edge_cache;
	//We'll have many repeated strings we could try to share, maybe by indices
	//into another vector?
	tsl::hopscotch_map<uint64_t, vector<std::string>, farmhash_hash> inv_names;

	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	lmdb::dbi names = lmdb::dbi::open(txn, "names");
	{
		lmdb::cursor cur = lmdb::cursor::open(txn, names);
		std::string_view key, value;
		if (!cur.get(key, value, MDB_FIRST))
			throw std::logic_error("no names?");
		do {
			//TODO: this was copied from follow_edges; see also unmarshal_reinterpret
			if (value.size() == 0 || value.size() % sizeof(uint64_t) != 0)
				throw std::logic_error(fmt::format("name key {} has value length {} (not a multiple of {})",
						//We want the dbi's name here, but I don't see how to get it.
						//The message won't distinguish close and mirror.
						key, value.size(), sizeof(uint64_t)));
			if (value.size() != sizeof(uint64_t))
				//We only want to report singleton names.  Every gadget has at
				//least one singleton name (see runner's sync.cpp).
				continue;
			inv_names[lmdb::from_sv<uint64_t>(value)].push_back(std::string(key));
		} while (cur.get(key, value, MDB_NEXT));
	}
	vector<uint64_t> stable_iteration;
	vector<std::string_view> found_preferred_names;
	for (auto it = inv_names.begin(); it != inv_names.end(); ++it) {
		stable_iteration.push_back(it->first);
		edge_cache.insert_or_assign(it->first, AnyProv::source(it->first));

		for (const std::string& name : it->second) {
			auto pref = std::find(std::begin(preferred_names), std::end(preferred_names), name);
			if (pref != std::end(preferred_names))
				found_preferred_names.push_back(*pref);
		}
		if (!found_preferred_names.empty()) {
			if (found_preferred_names.size() > 1)
				throw std::logic_error(fmt::format("found multiple preferred names? {} {}", it->first, found_preferred_names));
			it.value().clear();
			it.value().push_back(std::string(found_preferred_names.front()));
			found_preferred_names.clear();
		} else
			std::sort(it.value().begin(), it.value().end());
	}

	lmdb::dbi close_edges = lmdb::dbi::open(txn, "edges-close");
	for (uint64_t i : stable_iteration)
		visit_edges<SimpleEdge>(txn, close_edges, i, [&](uint64_t input, const SimpleEdge& e) {
			assert(input == i);
			if (!edge_cache.count(e.output))
				edge_cache.insert_or_assign(e.output, AnyProv::close(i, e));
			return VisitEdgeResult::proceed;
		});

	stable_iteration.clear();
	for (auto it = edge_cache.begin(); it != edge_cache.end(); ++it)
		stable_iteration.push_back(it->first);
	lmdb::dbi mirror_edges = lmdb::dbi::open(txn, "edges-mirror");
	for (uint64_t i : stable_iteration)
		visit_edges<SimpleEdge>(txn, mirror_edges, i, [&](uint64_t input, const SimpleEdge& e) {
			assert(input == i);
			if (!edge_cache.count(e.output))
				edge_cache.insert_or_assign(e.output, AnyProv::mirror(i, e));
			return VisitEdgeResult::proceed;
		});

	txn.commit();
	interval_accumulator<uint64_t> accum(512);
	for (auto it = edge_cache.begin(); it != edge_cache.end(); ++it)
		accum(it->first);
	return {std::move(accum).finish(), std::move(edge_cache), std::move(inv_names)};
}

//find all the possible combine rights, but don't open any databases
vector<pair<uint64_t, uint64_t>> find_all_combine_rights(lmdb::env& env) {
	const std::string_view edges_combine_prefix = "edges-skinny-combine-"sv;
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	lmdb::dbi main = lmdb::dbi::open(txn, nullptr);
	lmdb::cursor cur = lmdb::cursor::open(txn, main);
	std::string_view key = edges_combine_prefix;
	if (!cur.get(key, MDB_SET_RANGE))
		throw std::logic_error("no combine edge subdatabases?");

	interval_accumulator<uint64_t> accum(64);
	//no starts_with yet
	while (key.compare(0, edges_combine_prefix.size(), edges_combine_prefix) == 0) {
		key.remove_prefix(edges_combine_prefix.size());
		accum(from_string<uint64_t>(key));
		if (!cur.get(key, MDB_NEXT)) break;
	}
	txn.commit();
	return std::move(accum).finish();
}

struct EdgeVisitor {
	EdgeKind kind;
	const vector<pair<uint64_t, uint64_t>>* closed;
	vector<SkinnyProv> followed;
	//TODO: don't care if this is ordered_set or some other set
	//vector_ordered_set (could use deque here, I guess)
	tsl::ordered_set<uint64_t, farmhash_hash, std::equal_to<uint64_t>, std::allocator<uint64_t>, std::vector<uint64_t>> already_added;
	template<class Edge>
	VisitEdgeResult operator()(uint64_t input, const Edge& e) {
		return (*this)(input, e.output);
	}
	VisitEdgeResult operator()(uint64_t input, uint64_t output) {
		if (!interval_contains(*closed, output) && already_added.insert(output).second)
			followed.emplace_back(output, input, kind);
		return VisitEdgeResult::proceed;
	}
};

struct merge_unique_vectors {
template<typename T>
vector<T> operator()(vector<T>&& left, vector<T>&& right) const {
	vector<T> result;
	//This is an overestimate, but at most by max(left.size(), right.size()),
	//because we know each vector is already uniqued.  Empirically, this
	//improved utilization from ~.75 to ~.95 without requiring a big vector copy
	//(as in shrink_to_fit()).
	result.reserve(left.size() + right.size());
	merge_unique(left.begin(), left.end(), right.begin(), right.end(), std::back_inserter(result));
	return result;
}
};

vector<pair<uint64_t, uint64_t>> build_output_intervals(const vector<SkinnyProv>& provs, unsigned int num_threads) {
	vector<pair<std::size_t, std::size_t>> intervalize_tasks;
	for (std::size_t i = 0; i < provs.size(); i += 30000)
		intervalize_tasks.emplace_back(i, std::min(i + 30000, provs.size()));
	return transform_reduce(std::move(intervalize_tasks), num_threads, [&provs](pair<std::size_t, std::size_t> p) {
		interval_accumulator<uint64_t> accum(512);
		for (std::size_t i = p.first; i < p.second; ++i)
			accum(provs[i].output());
		return std::move(accum).finish();
	}, [](vector<pair<uint64_t, uint64_t>> left, vector<pair<uint64_t, uint64_t>> right) {
		return interval_union(left.begin(), left.end(), right.begin(), right.end());
	});
}

template<class Edge>
pair<vector<SkinnyProv>, vector<pair<uint64_t, uint64_t>>> discover_through_edges(
		lmdb::env& env, lmdb::dbi& edge_db, EdgeKind kind, const vector<pair<uint64_t, uint64_t>>& possible,
		const vector<pair<uint64_t, uint64_t>>& closed, unsigned int num_threads) {
	auto chunks = interval_chunk(possible.begin(), possible.end(), 10000);
	vector<SkinnyProv> provs = transform_reduce(std::move(chunks), num_threads, [&](vector<pair<uint64_t, uint64_t>> chunk) {
		auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		EdgeVisitor visitor = {.kind = kind, .closed = &closed, .followed = {}, .already_added = {}};
		visit_edges<Edge>(txn, edge_db, chunk, visitor);
		txn.commit();
		std::sort(visitor.followed.begin(), visitor.followed.end());
		return std::move(visitor.followed);
	}, merge_unique_vectors());
	auto discovered = build_output_intervals(provs, num_threads);
	return {std::move(provs), std::move(discovered)};
}

pair<vector<SkinnyProv>, vector<pair<uint64_t, uint64_t>>> discover_through_skinny_edges(
		lmdb::env& env, lmdb::dbi& edge_db, EdgeKind kind, const vector<pair<uint64_t, uint64_t>>& possible,
		const vector<pair<uint64_t, uint64_t>>& closed, unsigned int num_threads) {
	auto chunks = interval_chunk(possible.begin(), possible.end(), 25000);
	vector<SkinnyProv> provs = transform_reduce(std::move(chunks), num_threads, [&](vector<pair<uint64_t, uint64_t>> chunk) {
		auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		EdgeVisitor visitor = {.kind = kind, .closed = &closed, .followed = {}, .already_added = {}};
		visit_skinny_edges(txn, edge_db, chunk, visitor);
		txn.commit();
		std::sort(visitor.followed.begin(), visitor.followed.end());
		return std::move(visitor.followed);
	}, merge_unique_vectors());
	auto discovered = build_output_intervals(provs, num_threads);
	return {std::move(provs), std::move(discovered)};
}

void assign_interval_union(vector<pair<uint64_t, uint64_t>>& left, const vector<pair<uint64_t, uint64_t>>& right) {
	left = interval_union(left.begin(), left.end(), right.begin(), right.end());
}

int main(int argc, char* argv[]) { //genbuild {'entrypoint': True, 'ldflags': '-llmdb'}
	setvbuf(stdout, nullptr, _IOLBF, 0); //line buffering

	std::string_view db_path = "jbosboom";
	bool multiplayer = false, skip_mirror = false, combine_all = false;
	unsigned int num_threads = 1;
	std::vector<std::string_view> source_specs;
	for (int i = 1; i < argc; ++i) {
		if (argv[i] == "--db-path"sv)
			db_path = argv[++i];
		else if (argv[i] == "--multiplayer"sv)
			multiplayer = true;
		else if (argv[i] == "--skip-mirror"sv)
			skip_mirror = true;
		else if (argv[i] == "--combine-all"sv)
			//Combine against any reachable gadget, not just the initial set.
			//When we reach a new gadget that's been used as the right operand
			//of a combine, we'll try all gadgets in the closed set on the left,
			//and any newly-reached gadgets will be processed as normal.  This
			//breaks the generational aspect of the search -- paths with the
			//fewest combines are no longer assured.
			combine_all = true;
		else if (argv[i] == "--db-threads"sv || argv[i] == "--num-threads"sv || argv[i] == "--threads"sv)
			num_threads = to_uint(argv[++i]);
		else
			source_specs.emplace_back(argv[i]);
	}

	lmdb::env env = lmdb::env::create();
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(std::string(db_path).c_str(), MDB_RDONLY | MDB_NORDAHEAD);
	lmdb::dbi edges_connect, edges_skinny_connect, edges_close, edges_mirror, completions, meta_db;
	vector<pair<uint64_t, lmdb::dbi>> edges_combine, edges_skinny_combine; //lazily-initialized later when we know what we're using
	{
		auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		edges_connect = lmdb::dbi::open(txn, "edges-connect");
		edges_skinny_connect = lmdb::dbi::open(txn, "edges-skinny-connect");
		edges_close = lmdb::dbi::open(txn, "edges-close");
		edges_mirror = lmdb::dbi::open(txn, "edges-mirror");
		completions = lmdb::dbi::open(txn, "completions");
		meta_db = lmdb::dbi::open(txn, "meta");
		txn.commit();
	}

	{
		std::time_t now = std::time(nullptr);
		fmt::print("Report on {} started at {:%F %T %Z}\n", db_path, *std::localtime(&now));

		auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		std::string_view database_id, creation_timestamp, creator_hostname;
		meta_db.get(txn, "id_bytes", database_id);
		meta_db.get(txn, "creation_timestamp", creation_timestamp);
		meta_db.get(txn, "creator_hostname", creator_hostname);
		txn.commit();
		fmt::print("Database ID {:x}, created on {} at {}\n", lmdb::from_sv<uint64_t>(database_id),
				creator_hostname, creation_timestamp);
	}

	GadgetSet source_set = parse_gid_specs(source_specs);
	fmt::print("Source spec: {}\n", format_gadget_set(source_set));
	fmt::print("\n");

	TargetStuff target = target_stuff(env);
	const vector<pair<uint64_t, uint64_t>> possible_combine_rights = find_all_combine_rights(env);

	//Minimal provenance information.  All vectors are sorted.  There's no
	//correspondence between the various vectors and generations; we're mostly
	//just avoiding sorting all the data over and over, on the assumption that
	//we're printing tracebacks infrequently.
	vector<vector<SkinnyProv>> prov;
	//We store most edges as SkinnyProv, only getting the full edge data when
	//we're going to print a derivation.
	EdgeCache edge_cache;
	DeletedLocationsCache dellocs;
	//The closed set and intervals of gadgets pending processing.  We always
	//close and mirror if anything is awaiting such, then connect, and only if
	//neither is possible do we combine.  (This is nearly equivalent to the
	//generational search done by the driver, except that we connect before the
	//first combine while the driver doesn't.)
	vector<pair<uint64_t, uint64_t>> closed, awaiting_combine, awaiting_connect, awaiting_closemirror;

	vector<uint64_t> source_ids = collect_initial_gadget_set(env, source_set);
	std::sort(source_ids.begin(), source_ids.end());
	for (uint64_t i : source_ids)
		edge_cache.try_emplace(i, AnyProv::source(i));
	closed = maximal_intervals(source_ids.begin(), source_ids.end());
	awaiting_closemirror = awaiting_connect = awaiting_combine = closed;

	interval_accumulator<uint64_t> printed_in_traces(512);
	auto print_trace = [&target, &dellocs, &printed_in_traces](const vector<AnyProv>& trace, bool print_dellocs) {
		for (const AnyProv& p : trace) {
			if (p.kind() == EdgeKind::source) {
				auto it = target.inv_names.find(p.output());
				if (it == target.inv_names.end())
					fmt::print("  {} <names not found?>\n", p);
				else
					fmt::print("  {} {{{}}}\n", p, fmt::join(target.inv_names.at(p.output()), ", "));
			} else {
				auto dels = print_dellocs ? dellocs.find(p.output()) : dellocs.end();
				if (dels != dellocs.end()) {
					const vector<unsigned int>& deletions = dels->second;
					std::string line = fmt::to_string(p);
					line.insert(line.find('@'), fmt::format("delete {} ", fmt::join(deletions, ",")));
					fmt::print("  {}\n", line);
				} else
					fmt::print("  {}\n", p);
			}
			printed_in_traces(p.output());
		}
	};

	//We might not use a particular input for anything, but we still consider it
	//"mentioned" for reporting purposes.
	for (uint64_t i : source_ids)
		printed_in_traces(i);

	vector<std::string> targets_found;
	auto record_closed = [&](const vector<pair<uint64_t, uint64_t>>& discovered) {
		closed = interval_union(closed.begin(), closed.end(), discovered.cbegin(), discovered.cend());

		vector<pair<uint64_t, uint64_t>> found = interval_intersection(
				target.intervals.cbegin(), target.intervals.cend(), discovered.cbegin(), discovered.cend());
		if (!found.empty()) {
			fill_cache(env, edges_combine, edges_skinny_combine,
					edges_connect, edges_skinny_connect,
					edges_close, edges_mirror, found, prov, edge_cache, dellocs, db_path, num_threads);

			for (const pair<uint64_t, uint64_t>& p : found)
				for (uint64_t root = p.first; root < p.second; ++root) {
					vector<AnyProv> target_trace = toposort_provs(target.edge_cache, root),
							source_trace = toposort_provs(edge_cache, root);
					//There's no need to tell me X builds X.
					if (target_trace != source_trace) {
						fmt::print("Target trace:\n");
						print_trace(std::move(target_trace), false);
						fmt::print("Source trace:\n");
						print_trace(std::move(source_trace), true);
						fmt::print("\n");

						std::string suffix = "";
						for (const AnyProv& q : target_trace)
							if (q.kind() == EdgeKind::close)
								suffix += "closed";
							else if (q.kind() == EdgeKind::mirror) {
								if (!suffix.empty())
									suffix += ", ";
								suffix += "mirrored";
							}
						if (!suffix.empty())
							suffix = " [" + suffix + "]";
						auto it = target.inv_names.find(target_trace.front().output());
						if (it == target.inv_names.end())
							targets_found.push_back(fmt::format("{}{} ({})", root, suffix, root));
						else
							for (const auto& name : it->second)
								targets_found.push_back(fmt::format("{}{} ({})", name, suffix, root));
					}
				}
		}

		if (combine_all) {
			//TODO: if combine_all, check for newly-reachable combine rights; put
			//the new rights in the pool and in a special queue for full closed set
			//processing.
			throw std::logic_error("TODO: implement combine_all");
		}
	};

	auto discover_whats_possible = [&](std::string_view kind, vector<pair<uint64_t, uint64_t>>& intervals) {
		auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		vector<pair<uint64_t, uint64_t>> possible = intersect_completion(env, txn, completions, kind, intervals);
		txn.commit();
		return possible;
	};

	unsigned int step_count = 1;
	while (!awaiting_closemirror.empty() || !awaiting_connect.empty() || !awaiting_combine.empty()) {
		Stopwatch stopwatch = Stopwatch::process();
		if (!awaiting_closemirror.empty()) {
			if (!multiplayer) {
				vector<pair<uint64_t, uint64_t>> possible = discover_whats_possible("close", awaiting_closemirror);
				auto [provs, discovered] = discover_through_edges<SimpleEdge>(env, edges_close, EdgeKind::close, possible, closed, num_threads);
				if (!provs.empty())
					prov.push_back(std::move(provs));
				if (!discovered.empty()) //avoid copying if nothing found (especially for close)
					task_parallel(num_threads,
							std::bind_front(record_closed, std::cref(discovered)),
							//Outputs of close get mirrored, so put them in closemirror immediately.
							std::bind_front(assign_interval_union, std::ref(awaiting_closemirror), std::cref(discovered)),
							//They also get connected and combined.
							std::bind_front(assign_interval_union, std::ref(awaiting_connect), std::cref(discovered)),
							std::bind_front(assign_interval_union, std::ref(awaiting_combine), std::cref(discovered))
					);
			}

			if (!skip_mirror) {
				vector<pair<uint64_t, uint64_t>> possible = discover_whats_possible("mirror", awaiting_closemirror);
				auto [provs, discovered] = discover_through_edges<SimpleEdge>(env, edges_mirror, EdgeKind::mirror, possible, closed, num_threads);
				if (!provs.empty())
					prov.push_back(std::move(provs));
				if (!discovered.empty()) //avoid copying if nothing found (should be uncommon for mirror...)
					task_parallel(num_threads,
							std::bind_front(record_closed, std::cref(discovered)),
							std::bind_front(assign_interval_union, std::ref(awaiting_connect), std::cref(discovered)),
							std::bind_front(assign_interval_union, std::ref(awaiting_combine), std::cref(discovered))
					);
			}

			awaiting_closemirror.clear();

			if (edges_combine.empty()) {
				//Fix the initial combine rights pool as anything reachable in
				//one closemirror from the initial gadgets and having combine edges.
				vector<pair<uint64_t, uint64_t>> rights = interval_intersection(closed.cbegin(), closed.cend(),
						possible_combine_rights.cbegin(), possible_combine_rights.cend());
				if (rights.empty())
					throw std::runtime_error(fmt::format("no closemirror-reachable combine rights? possible rights are {}", possible_combine_rights));
				vector<pair<uint64_t, encoding::Stats>> right_stat_sort = select_gadget_id_to_stats(env, rights);
				std::sort(right_stat_sort.begin(), right_stat_sort.end(), [](const auto& a, const auto&b) {
					return std::tie(a.second.locations, a.second.states, a.first) <
							std::tie(b.second.locations, b.second.states, b.first);
				});

				auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
				for (const auto& p : right_stat_sort) {
					edges_combine.emplace_back(p.first, lmdb::dbi::open(txn, fmt::format("edges-combine-{}", p.first).c_str()));
					edges_skinny_combine.emplace_back(p.first, lmdb::dbi::open(txn, fmt::format("edges-skinny-combine-{}", p.first).c_str()));
				}
				txn.commit();
			}
		} else if (!awaiting_connect.empty()) {
			vector<pair<uint64_t, uint64_t>> possible = discover_whats_possible("connect", awaiting_connect);
			auto [provs, discovered] = discover_through_skinny_edges(env, edges_skinny_connect, EdgeKind::connect, possible, closed, num_threads);
			if (!provs.empty())
				prov.push_back(std::move(provs));
			if (!discovered.empty())
				task_parallel(num_threads,
						std::bind_front(record_closed, std::cref(discovered)),
						std::bind_front(assign_interval_union, std::ref(awaiting_closemirror), std::cref(discovered)),
						std::bind_front(assign_interval_union, std::ref(awaiting_combine), std::cref(discovered))
				);
			awaiting_connect = std::move(discovered); //i.e., if empty, clear
		} else if (!awaiting_combine.empty()) {
			vector<pair<uint64_t, uint64_t>> awaiting_combine_next;
			for (pair<uint64_t, lmdb::dbi>& right : edges_skinny_combine) {
				vector<pair<uint64_t, uint64_t>> possible = discover_whats_possible(
						fmt::format("combine-{}", right.first), awaiting_combine);
				auto [provs, discovered] = discover_through_skinny_edges(env, right.second, EdgeKind::combine, possible, closed, num_threads);
				if (!provs.empty())
					prov.push_back(std::move(provs));
				if (!discovered.empty())
					task_parallel(num_threads,
							std::bind_front(record_closed, std::cref(discovered)),
							std::bind_front(assign_interval_union, std::ref(awaiting_closemirror), std::cref(discovered)),
							std::bind_front(assign_interval_union, std::ref(awaiting_connect), std::cref(discovered)),
							std::bind_front(assign_interval_union, std::ref(awaiting_combine_next), std::cref(discovered))
					);
			}
			awaiting_combine = std::move(awaiting_combine_next);
		} else
			throw std::logic_error("can't happen: nothing to do?");

		Stopwatch::Result elapsed = stopwatch.elapsed();
		fmt::print("==> Step {}: {} user, {} sys, {} wall ({:.2f}), {:.2f} GiB ({:.2f}, {})\n",
				step_count++, elapsed.userSeconds(), elapsed.systemSeconds(), elapsed.hms(), elapsed.utilization(),
				elapsed.absolute().highwaterGibibytes(), elapsed.highwaterGibibytes(), elapsed.hardFaults());
		auto print_stats_line = [&](const auto& list, std::string_view name) {
			std::size_t size = interval_size(list), count = list.size();
			double avg_width = ((double)size)/((double)count); //cast both to avoid imprecision warning
			std::string width_field = std::isnan(avg_width) ? "" : fmt::format("{:5.1f}", avg_width);
			if (width_field.size() > 5) width_field = "big";
			fmt::print("--> {:>9}: {:11d} {:11d} {:>5} {:7d} MiB\n",
					name, size, count, width_field, list.capacity() * sizeof(list.front()) / (1024*1024));
		};
		print_stats_line(closed, "closed");
		print_stats_line(awaiting_closemirror, "closemirr");
		print_stats_line(awaiting_connect, "connect");
		print_stats_line(awaiting_combine, "combine");

		std::size_t prov_total_bytes = 0, prov_total_capacity = 0;
		for (const auto& p : prov) {
			prov_total_bytes += p.size() * sizeof(p.front());
			prov_total_capacity += p.capacity() * sizeof(p.front());
		}
		fmt::print("--> provenance: {:6.2f} GiB {:6.2f} GiB {:.2f}\n",
				((double)prov_total_bytes) / (1024*1024*1024),
				((double)prov_total_capacity) / (1024*1024*1024),
				((double)prov_total_bytes) / ((double)prov_total_capacity));

		fmt::print("\n");
	}

	fmt::print("==> REPORT COMPLETED\n");
	std::sort(targets_found.begin(), targets_found.end());
	fmt::print("--> {} targets found:\n", targets_found.size());
	for (const std::string& t : targets_found)
		fmt::print("   {}\n", t);
	auto printed_in_traces_intervals = std::move(printed_in_traces).finish();
	vector<uint64_t> all_gadgets_mentioned = interval_inflate(printed_in_traces_intervals.begin(), printed_in_traces_intervals.end());
	fmt::print("--> {} gadgets mentioned: {}\n", all_gadgets_mentioned.size(), fmt::join(all_gadgets_mentioned, " "));

	return 0;
}