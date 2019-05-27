#include "precompiled.hpp"
#include "toggles-shared.hpp"
#include "stringutils.hpp"
#include "stopwatch.hpp"
#include "intervals.hpp"
#include "tsl/ordered_set.h"

using std::vector;
using std::pair;
using std::uint8_t;
using std::uint64_t;
using namespace std::literals::string_view_literals;

enum class EdgeKind : unsigned char {
	combine = 0, connect = 1, close = 2, mirror = 3, source = 4
};
std::string_view name_for_kind(EdgeKind kind) {
	switch (kind) {
		case EdgeKind::combine: return "combine";
		case EdgeKind::connect: return "connect";
		case EdgeKind::close: return "close";
		case EdgeKind::mirror: return "mirror";
		case EdgeKind::source: return "source";
	}
	throw std::logic_error(fmt::format("bad kind: {}", static_cast<unsigned int>(kind)));
}
template<>
struct fmt::formatter<EdgeKind> : formatter<std::string_view> {
	template<typename FormatContext>
	auto format(const EdgeKind kind, FormatContext& ctx) {
		return fmt::formatter<std::string_view>::format(name_for_kind(kind), ctx);
	}
};


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
	SkinnyProv(uint64_t output, uint64_t input, EdgeKind kind) : output_(output), input_(input), kind_(kind) {}
	SkinnyProv(const SkinnyProv&) = default;
	SkinnyProv(SkinnyProv&&) = default;
	SkinnyProv& operator=(const SkinnyProv&) = default;
	SkinnyProv& operator=(SkinnyProv&&) = default;
	uint64_t output() const {
		return output_;
	}
	uint64_t input() const {
		return input_;
	}
	EdgeKind kind() const {
		return kind_;
	}
private:
	std::uint64_t output_, input_;
	EdgeKind kind_; //TODO: put in the high bits of the other fields
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

class AnyProv {
public:
	static AnyProv source(uint64_t output) {
		return {EdgeKind::source, output, std::numeric_limits<uint64_t>::max(),
				std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint8_t>::max(),
				std::numeric_limits<uint8_t>::max(), std::numeric_limits<uint8_t>::max(),
				std::numeric_limits<uint8_t>::max()};
	}
	static AnyProv combine(uint64_t input1, uint64_t input2, const CombineEdge& e) {
		return {EdgeKind::combine, e.output, input1, input2, e.splice, e.rotation, e.connectPoint, e.canonicalizePermutation};
	}
	static AnyProv connect(uint64_t input1, const ConnectEdge& e) {
		return {EdgeKind::connect, e.output, input1, std::numeric_limits<uint64_t>::max(),
				std::numeric_limits<uint8_t>::max(), std::numeric_limits<uint8_t>::max(),
				e.connectPoint, e.canonicalizePermutation};
	}
	static AnyProv close(uint64_t input1, const SimpleEdge& e) {
		return {EdgeKind::close, e.output, input1, std::numeric_limits<uint64_t>::max(),
				std::numeric_limits<uint8_t>::max(), std::numeric_limits<uint8_t>::max(),
				std::numeric_limits<uint8_t>::max(), e.canonicalizePermutation};
	}
	static AnyProv mirror(uint64_t input1, const SimpleEdge& e) {
		return {EdgeKind::mirror, e.output, input1, std::numeric_limits<uint64_t>::max(),
				std::numeric_limits<uint8_t>::max(), std::numeric_limits<uint8_t>::max(),
				std::numeric_limits<uint8_t>::max(), e.canonicalizePermutation};
	}

	EdgeKind kind() const {
		return kind_;
	}
	uint64_t output() const {
		return output1_;
	}
	uint64_t input1() const {
		return input1_;
	}
	uint64_t input2() const {
		assert(input2_ != std::numeric_limits<uint64_t>::max());
		return input2_;
	}
	uint8_t splice() const {
		assert(splice_ != std::numeric_limits<uint8_t>::max());
		return splice_;
	}
	uint8_t rotation() const {
		assert(rotation_ != std::numeric_limits<uint8_t>::max());
		return rotation_;
	}
	uint8_t connectPoint() const {
		assert(connectPoint_ != std::numeric_limits<uint8_t>::max());
		return connectPoint_;
	}
	uint8_t canonicalizePermutation() const {
		return canonicalizePermutation_;
	}

	vector<uint64_t> inputs() const {
		//Returning a vector isn't great for perf, but is convenient.
		vector<uint64_t> r;
		if (input1_ != std::numeric_limits<uint64_t>::max())
			r.push_back(input1_);
		if (input2_ != std::numeric_limits<uint64_t>::max())
			r.push_back(input2_);
		return r;
	}
private:
	AnyProv(EdgeKind kind, uint64_t output1, uint64_t input1, uint64_t input2,
			uint8_t splice, uint8_t rotation, uint8_t connectPoint,
			uint8_t canonicalizeRotation) : output1_(output1), input1_(input1),
					input2_(input2), splice_(splice), rotation_(rotation),
					connectPoint_(connectPoint), canonicalizePermutation_(canonicalizeRotation), kind_(kind) {}
	std::uint64_t output1_, input1_, input2_;
	std::uint8_t splice_, rotation_, connectPoint_;
	std::uint8_t canonicalizePermutation_;
	//TODO: steal two bits from one of the other fields, or encode using special values of unused fields
	EdgeKind kind_;
};

template<>
struct fmt::formatter<AnyProv> {
	template<typename ParseContext>
	constexpr auto parse(ParseContext& ctx) {return ctx.begin();}
	template<typename FormatContext>
	auto format(const AnyProv& p, FormatContext& ctx) {
		switch (p.kind()) {
			case EdgeKind::combine:
				return fmt::format_to(ctx.out(), "{} = combine {} splice {:d} with {} rotate {:d} connect at {:d} @{:d}",
						p.output(), p.input1(), p.splice(), p.input2(), p.rotation(),
						p.connectPoint(), p.canonicalizePermutation());
			case EdgeKind::connect:
				return fmt::format_to(ctx.out(), "{} = connect {} at {:d} @{:d}",
						p.output(), p.input1(), p.connectPoint(), p.canonicalizePermutation());
			case EdgeKind::close:
				return fmt::format_to(ctx.out(), "{} = close {} @{:d}",
						p.output(), p.input1(), p.canonicalizePermutation());
			case EdgeKind::mirror:
				return fmt::format_to(ctx.out(), "{} = mirror {} @{:d}",
						p.output(), p.input1(), p.canonicalizePermutation());
			case EdgeKind::source:
				return fmt::format_to(ctx.out(), "{} = source", p.output());
			default:
				//TODO: We'd like to dump the other members to help track down
				//the corruption, but we'd hit the asserts in the methods.
				//Figure out how friending a future full specialization works,
				//then print the members directly.
				return fmt::format_to(ctx.out(), "unknown AnyProv kind {}", static_cast<unsigned char>(p.kind()));
		}
	}
};

using EdgeCache = tsl::hopscotch_map<uint64_t, AnyProv, farmhash_hash>;

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

void fill_cache(lmdb::env& env, vector<pair<uint64_t, lmdb::dbi>>& combine_edges,
		lmdb::dbi& connect_edges, lmdb::dbi& close_edges, lmdb::dbi& mirror_edges,
		const vector<pair<uint64_t, uint64_t>>& roots, const vector<vector<SkinnyProv>>& prov,
		EdgeCache& edge_cache) {
	//This batching logic was quite helpful for postgres, but is probably less
	//helpful with lmdb because queries are local.
	vector<uint64_t> frontier = interval_inflate(roots.begin(), roots.end());
	//We don't need to know the other end for close and mirror lookups, but it
	//avoids special-casing in search_for_edge and adds some error-checking.
	vector<SkinnyProv> combine_batch, connect_batch, close_batch, mirror_batch;
	while (!frontier.empty()) {
		combine_batch.clear();
		connect_batch.clear();
		close_batch.clear();
		mirror_batch.clear();

		for (std::size_t i = 0; i < frontier.size(); ++i) {
			if (edge_cache.count(frontier[i]))
				continue;
			SkinnyProv p = find_sp(prov, frontier[i]);
			switch (p.kind()) {
				case EdgeKind::combine: combine_batch.push_back(p); break;
				case EdgeKind::connect: connect_batch.push_back(p); break;
				//We "look through" close and mirror edges so we terminate in
				//fewer iterations.
				case EdgeKind::close:
					close_batch.push_back(p);
					frontier.push_back(p.input());
					break;
				case EdgeKind::mirror:
					mirror_batch.push_back(p);
					frontier.push_back(p.input());
					break;
				case EdgeKind::source:
					//nothing to do
					break;
			}
		}
		frontier.clear();

		if (combine_batch.empty() || connect_batch.empty() || close_batch.empty() || mirror_batch.empty()) {
			vector<AnyProv> edges;
			{
				auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
				for (const SkinnyProv& p : combine_batch) {
					//We have to search all the combine databases.
					std::optional<CombineEdge> e;
					uint64_t input2 = 0;
					for (pair<uint64_t, lmdb::dbi>& db : combine_edges) {
						e = search_for_edge<CombineEdge>(txn, db.second, p.input(), p.output());
						if (e) {
							input2 = db.first;
							break;
						}
					}
					if (!e)
						throw std::logic_error(fmt::format("no combine edge for {}/{}", p.input(), p.output()));
					edges.push_back(AnyProv::combine(p.input(), input2, *e));
				}
				for (const SkinnyProv& p : connect_batch) {
					std::optional<ConnectEdge> e = search_for_edge<ConnectEdge>(txn, connect_edges, p.input(), p.output());
					if (!e)
						throw std::logic_error(fmt::format("no connect edge for {}/{}", p.input(), p.output()));
					edges.push_back(AnyProv::connect(p.input(), *e));
				}
				for (const SkinnyProv& p : close_batch) {
					std::optional<SimpleEdge> e = search_for_edge<SimpleEdge>(txn, close_edges, p.input(), p.output());
					if (!e)
						throw std::logic_error(fmt::format("no close edge for {}/{}", p.input(), p.output()));
					edges.push_back(AnyProv::close(p.input(), *e));
				}
				for (const SkinnyProv& p : mirror_batch) {
					std::optional<SimpleEdge> e = search_for_edge<SimpleEdge>(txn, close_edges, p.input(), p.output());
					if (!e)
						throw std::logic_error(fmt::format("no mirror edge for {}/{}", p.input(), p.output()));
					edges.push_back(AnyProv::mirror(p.input(), *e));
				}
				txn.commit();
			}

			for (const AnyProv& p : edges) {
				auto pair = edge_cache.try_emplace(p.output(), p);
				if (!pair.second)
					throw std::runtime_error(fmt::format("conflict for {}: {} {}",
							p.output(), *pair.first, p));
				const vector<uint64_t>& inputs = p.inputs();
				for (uint64_t i : inputs)
					frontier.push_back(i);
			}
		}
	}
}

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
			const uint64_t* first = reinterpret_cast<const uint64_t*>(value.data());
			const uint64_t* last = first + value.size() / sizeof(uint64_t);
			for (const uint64_t* e = first; e != last; ++e)
				inv_names[*e].push_back(std::string(key));
		} while (cur.get(key, value, MDB_NEXT));
	}
	vector<uint64_t> stable_iteration;
	for (auto it = inv_names.begin(); it != inv_names.end(); ++it) {
		stable_iteration.push_back(it->first);
		edge_cache.insert_or_assign(it->first, AnyProv::source(it->first));
		std::sort(it.value().begin(), it.value().end());
	}

	lmdb::dbi close_edges = lmdb::dbi::open(txn, "edges-close");
	for (uint64_t i : stable_iteration)
		visit_edges<SimpleEdge>(txn, close_edges, i, [&](uint64_t input, const SimpleEdge& e) {
			assert(input == i);
			if (!edge_cache.count(i))
				edge_cache.insert_or_assign(i, AnyProv::close(i, e));
			return VisitEdgeResult::proceed;
		});

	stable_iteration.clear();
	for (auto it = edge_cache.begin(); it != edge_cache.end(); ++it)
		stable_iteration.push_back(it->first);
	lmdb::dbi mirror_edges = lmdb::dbi::open(txn, "edges-mirror");
	for (uint64_t i : stable_iteration)
		visit_edges<SimpleEdge>(txn, mirror_edges, i, [&](uint64_t input, const SimpleEdge& e) {
			assert(input == i);
			if (!edge_cache.count(i))
				edge_cache.insert_or_assign(i, AnyProv::mirror(i, e));
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
	const std::string_view edges_combine_prefix = "edges-combine-"sv;
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

int main(int argc, char* argv[]) { //genbuild {'entrypoint': True, 'ldflags': '-llmdb'}
	std::string_view db_path = "jbosboom";
	bool multiplayer = false, combine_all = false;
	std::vector<std::string_view> source_specs;
	for (int i = 1; i < argc; ++i) {
		if (argv[i] == "--db-path"sv)
			db_path = argv[++i];
		else if (argv[i] == "--multiplayer"sv)
			multiplayer = true;
		else if (argv[i] == "--combine-all"sv)
			//Combine against any reachable gadget, not just the initial set.
			//When we reach a new gadget that's been used as the right operand
			//of a combine, we'll try all gadgets in the closed set on the left,
			//and any newly-reached gadgets will be processed as normal.  This
			//breaks the generational aspect of the search -- paths with the
			//fewest combines are no longer assured.
			combine_all = true;
		else
			source_specs.emplace_back(argv[i]);
	}

	GadgetSet source_set = parse_gid_specs(source_specs);
	fmt::print("Source spec: {}\n", format_gadget_set(source_set));

	lmdb::env env = lmdb::env::create();
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(std::string(db_path).c_str(), MDB_RDONLY);
	lmdb::dbi edges_connect, edges_close, edges_mirror, completions;
	vector<pair<uint64_t, lmdb::dbi>> edges_combine; //lazily-initialized later when we know what we're using
	{
		auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		edges_connect = lmdb::dbi::open(txn, "edges-connect");
		edges_close = lmdb::dbi::open(txn, "edges-close");
		edges_mirror = lmdb::dbi::open(txn, "edges-mirror");
		completions = lmdb::dbi::open(txn, "completions");
		txn.commit();
	}

	TargetStuff target = target_stuff(env);
	const vector<pair<uint64_t, uint64_t>> possible_combine_rights = find_all_combine_rights(env);

	//Minimal provenance information.  prov.back() is the current generation,
	//the only vector being appended to.  The other generations are kept sorted
	//for binary-search-based lookups.  There's no correspondence between the
	//various vectors and generations; we're mostly just avoiding sorting all
	//the data over and over, on the assumption that we're printing tracebacks
	//infrequently.
	vector<vector<SkinnyProv>> prov;
	prov.emplace_back();
	//We store most edges as SkinnyProv, only getting the full edge data when
	//we're going to print a derivation.
	EdgeCache edge_cache;
	//The closed set and intervals of gadgets pending processing.  We always
	//close and mirror if anything is awaiting such, then connect, and only if
	//neither is possible do we combine.  (This is nearly equivalent to the
	//generational search done by the driver, except that we connect before the
	//first combine while the driver doesn't.)
	vector<pair<uint64_t, uint64_t>> closed, awaiting_combine, awaiting_connect, awaiting_closemirror;
	//For logging purposes only -- not actually controlling anything.
	unsigned int generation = 0, subgeneration = 0;

	vector<uint64_t> source_ids = collect_initial_gadget_set(env, source_set);
	std::sort(source_ids.begin(), source_ids.end());
	for (uint64_t i : source_ids)
		edge_cache.try_emplace(i, AnyProv::source(i));
	closed = maximal_intervals(source_ids.begin(), source_ids.end());
	awaiting_closemirror = closed;

	auto print_trace = [&target](const vector<AnyProv>& trace) {
		for (const AnyProv& p : trace) {
			if (p.kind() == EdgeKind::source) {
				auto it = target.inv_names.find(p.output());
				if (it == target.inv_names.end())
					fmt::print("{} <names not found?>\n", p);
				else
					fmt::print("{} {}\n", p, target.inv_names.at(p.output()));
			} else
				fmt::print("{}\n", p);
		}
	};

	auto record_closed = [&](const vector<pair<uint64_t, uint64_t>>& discovered) {
		closed = interval_union(closed.begin(), closed.end(), discovered.cbegin(), discovered.cend());

		vector<pair<uint64_t, uint64_t>> found = interval_intersection(
				target.intervals.cbegin(), target.intervals.cend(), discovered.cbegin(), discovered.cend());
		if (!found.empty()) {
			std::sort(prov.back().begin(), prov.back().end());
			fill_cache(env, edges_combine, edges_connect, edges_close, edges_mirror, found, prov, edge_cache);
			for (const pair<uint64_t, uint64_t>& p : found)
				for (uint64_t root = p.first; root < p.second; ++root) {
					fmt::print("Target trace:\n");
					print_trace(toposort_provs(target.edge_cache, root));
					fmt::print("Source trace:\n");
					print_trace(toposort_provs(edge_cache, root));
				}
		}

		//TODO: if combine_all, check for newly-reachable combine rights; put
		//the new rights in the pool and in a special queue for full closed set
		//processing.
	};

	while (!awaiting_closemirror.empty() || !awaiting_connect.empty() || !awaiting_combine.empty()) {
		auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		if (!awaiting_closemirror.empty()) {
			if (!multiplayer) {
				vector<pair<uint64_t, uint64_t>> possible = intersect_completion(
						env, txn, completions, "close", awaiting_closemirror);
				//vector_ordered_set (could use deque here, I guess)
				tsl::ordered_set<uint64_t, farmhash_hash, std::equal_to<uint64_t>, std::allocator<uint64_t>, std::vector<uint64_t>> already_added;
				visit_edges<SimpleEdge>(txn, edges_close, possible, [&](uint64_t input, const SimpleEdge& e) {
					if (!interval_contains(closed, e.output) && already_added.insert(e.output).second)
						prov.back().emplace_back(e.output, input, EdgeKind::close);
					return VisitEdgeResult::proceed;
				});
				vector<uint64_t> novel = std::move(already_added).values_container();
				std::sort(novel.begin(), novel.end());
				vector<pair<uint64_t, uint64_t>> discovered = maximal_intervals(novel.begin(), novel.end());
				if (!discovered.empty()) { //avoid copying if nothing found (especially for close)
					record_closed(discovered);
					//Outputs of close get mirrored, so put them in closemirror immediately.
					awaiting_closemirror = interval_union(awaiting_closemirror.begin(), awaiting_closemirror.end(),
							discovered.begin(), discovered.end());
					//They also get connected and combined.
					awaiting_connect = interval_union(awaiting_connect.begin(), awaiting_connect.end(),
							discovered.cbegin(), discovered.cend());
					awaiting_combine = interval_union(awaiting_combine.begin(), awaiting_combine.end(),
							discovered.cbegin(), discovered.cend());
				}
			}

			vector<pair<uint64_t, uint64_t>> possible = intersect_completion(
						env, txn, completions, "mirror", awaiting_closemirror);
			//vector_ordered_set (could use deque here, I guess)
			tsl::ordered_set<uint64_t, farmhash_hash, std::equal_to<uint64_t>, std::allocator<uint64_t>, std::vector<uint64_t>> already_added;
			visit_edges<SimpleEdge>(txn, edges_mirror, possible, [&](uint64_t input, const SimpleEdge& e) {
				if (!interval_contains(closed, e.output) && already_added.insert(e.output).second)
					prov.back().emplace_back(e.output, input, EdgeKind::mirror);
				return VisitEdgeResult::proceed;
			});
			vector<uint64_t> novel = std::move(already_added).values_container();
			std::sort(novel.begin(), novel.end());
			vector<pair<uint64_t, uint64_t>> discovered = maximal_intervals(novel.begin(), novel.end());
			if (!discovered.empty()) { //avoid copying if nothing found (should be uncommon for mirror...)
				record_closed(discovered);
				awaiting_connect = interval_union(awaiting_connect.begin(), awaiting_connect.end(),
						discovered.cbegin(), discovered.cend());
				awaiting_combine = interval_union(awaiting_combine.begin(), awaiting_combine.end(),
						discovered.cbegin(), discovered.cend());
			}

			awaiting_closemirror.clear();

			if (edges_combine.empty()) {
				//Fix the initial combine rights pool as anything reachable in
				//one closemirror from the initial gadgets and having combine edges.
				vector<pair<uint64_t, uint64_t>> rights = interval_intersection(closed.cbegin(), closed.cend(),
						possible_combine_rights.cbegin(), possible_combine_rights.cend());
				if (rights.empty())
					throw std::runtime_error(fmt::format("no closemirror-reachable combine rights? possible rights are {}", possible_combine_rights));
				for (const pair<uint64_t, uint64_t>& p : rights)
					for (uint64_t r = p.first; r < p.second; ++r)
						edges_combine.emplace_back(r, lmdb::dbi::open(txn, fmt::format("edges-combine-{}", r).c_str()));
				//TODO: make tuple comparator a utility function (proj_compare)
				std::sort(edges_combine.begin(), edges_combine.end(), [](const auto& a, const auto& b) {
					return std::get<0>(a) < std::get<0>(b);
				});
			}
		} else if (!awaiting_connect.empty()) {
			vector<pair<uint64_t, uint64_t>> possible = intersect_completion(
					env, txn, completions, "connect", awaiting_connect);
			//vector_ordered_set (could use deque here, I guess)
			tsl::ordered_set<uint64_t, farmhash_hash, std::equal_to<uint64_t>, std::allocator<uint64_t>, std::vector<uint64_t>> already_added;
			visit_edges<ConnectEdge>(txn, edges_connect, possible, [&](uint64_t input, const ConnectEdge& e) {
				if (!interval_contains(closed, e.output) && already_added.insert(e.output).second)
					prov.back().emplace_back(e.output, input, EdgeKind::connect);
				return VisitEdgeResult::proceed;
			});
			vector<uint64_t> novel = std::move(already_added).values_container();
			std::sort(novel.begin(), novel.end());
			vector<pair<uint64_t, uint64_t>> discovered = maximal_intervals(novel.begin(), novel.end());
			if (!discovered.empty()) {
				record_closed(discovered);
				awaiting_closemirror = interval_union(awaiting_closemirror.begin(), awaiting_closemirror.end(),
						discovered.begin(), discovered.end());
				awaiting_combine = interval_union(awaiting_combine.begin(), awaiting_combine.end(),
						discovered.cbegin(), discovered.cend());
				awaiting_connect = std::move(discovered);
			}
		} else if (!awaiting_combine.empty()) {
			vector<pair<uint64_t, uint64_t>> awaiting_combine_next;
			for (pair<uint64_t, lmdb::dbi>& right : edges_combine) {
				vector<pair<uint64_t, uint64_t>> possible = intersect_completion(
					env, txn, completions, fmt::format("combine-{}", right.first), awaiting_combine);
				//vector_ordered_set (could use deque here, I guess)
				tsl::ordered_set<uint64_t, farmhash_hash, std::equal_to<uint64_t>, std::allocator<uint64_t>, std::vector<uint64_t>> already_added;
				visit_edges<CombineEdge>(txn, right.second, possible, [&](uint64_t input, const CombineEdge& e) {
					if (!interval_contains(closed, e.output) && already_added.insert(e.output).second)
						prov.back().emplace_back(e.output, input, EdgeKind::combine);
					return VisitEdgeResult::proceed;
				});
				vector<uint64_t> novel = std::move(already_added).values_container();
				std::sort(novel.begin(), novel.end());
				vector<pair<uint64_t, uint64_t>> discovered = maximal_intervals(novel.begin(), novel.end());
				if (!discovered.empty()) {
					record_closed(discovered);
					awaiting_closemirror = interval_union(awaiting_closemirror.begin(), awaiting_closemirror.end(),
							discovered.begin(), discovered.end());
					awaiting_connect = interval_union(awaiting_connect.begin(), awaiting_connect.end(),
							discovered.cbegin(), discovered.cend());
					awaiting_combine_next = interval_union(awaiting_combine_next.begin(), awaiting_combine_next.end(),
							discovered.cbegin(), discovered.cend());
				}
			}
			awaiting_combine = std::move(awaiting_combine_next);
		} else
			throw std::logic_error("can't happen: nothing to do?");
		txn.commit();
		if (!prov.empty()) {
			std::sort(prov.begin(), prov.end());
			prov.emplace_back();
		}
	}

	return 0;
}