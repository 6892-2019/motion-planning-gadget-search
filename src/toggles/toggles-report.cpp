#include "precompiled.hpp"
#include "toggles-shared.hpp"
#include "stringutils.hpp"
#include "stopwatch.hpp"

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

vector<AnyProv> toposort_provs(const tsl::hopscotch_map<uint64_t, AnyProv, farmhash_hash>& prov, uint64_t root) {
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
	std::string_view value;
	if (!edges.get(txn, lmdb::to_sv(input), value))
		return std::nullopt;
	//TODO: this was copied from follow_edges; see also unmarshal_reinterpret
	if (value.size() == 0 || value.size() % sizeof(Edge) != 0)
		throw std::logic_error(fmt::format("edge data of type {} for key {} has value length {} (not a multiple of {})",
				//We want the dbi's name here, but I don't see how to get it.
				//The message won't distinguish close and mirror.
				typeid(Edge).name(), input, value.size(), sizeof(Edge)));
	const Edge* first = reinterpret_cast<const Edge*>(value.data());
	const Edge* last = first + value.size() / sizeof(Edge);
	for (const Edge* e = first; e != last; ++first)
		if (e->output == output)
			return *e;
	return std::nullopt;
}

void fill_cache(lmdb::env& env, vector<pair<uint64_t, lmdb::dbi>>& combine_edges,
		lmdb::dbi& connect_edges, lmdb::dbi& close_edges, lmdb::dbi& mirror_edges,
		tsl::hopscotch_map<uint64_t, AnyProv, farmhash_hash>& edge_cache,
		const vector<uint64_t>& roots, const vector<vector<SkinnyProv>>& prov) {
	//This batching logic was quite helpful for postgres, but is probably less
	//helpful with lmdb because queries are local.
	vector<uint64_t> frontier(roots.begin(), roots.end());
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
					edges.push_back(AnyProv::connect(p.input(), e));
				}
				for (const SkinnyProv& p : close_batch) {
					std::optional<SimpleEdge> e = search_for_edge<SimpleEdge>(txn, close_edges, p.input(), p.output());
					if (!e)
						throw std::logic_error(fmt::format("no close edge for {}/{}", p.input(), p.output()));
					edges.push_back(AnyProv::close(p.input(), e));
				}
				for (const SkinnyProv& p : mirror_batch) {
					std::optional<SimpleEdge> e = search_for_edge<SimpleEdge>(txn, close_edges, p.input(), p.output());
					if (!e)
						throw std::logic_error(fmt::format("no mirror edge for {}/{}", p.input(), p.output()));
					edges.push_back(AnyProv::mirror(p.input(), e));
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
void fill_cache(lmdb::env& env, vector<pair<uint64_t, lmdb::dbi>>& combine_edges,
		lmdb::dbi& connect_edges, lmdb::dbi& close_edges, lmdb::dbi& mirror_edges,
		tsl::hopscotch_map<uint64_t, AnyProv, farmhash_hash>& edge_cache,
		const vector<uint64_t>& roots, const vector<SkinnyProv>& prov) {
	vector<vector<SkinnyProv>> nested_prov;
	nested_prov.emplace_back(prov);
	fill_cache(env, combine_edges, connect_edges, close_edges, mirror_edges, edge_cache, roots, nested_prov);
}

int main(int argc, char* argv[]) { //genbuild {'entrypoint': True, 'ldflags': '-lpqxx -lpq'}
	std::string_view db_user = "jbosboom", db_pass = "", db_host = "127.0.0.1",
			db_port = "5432", db_name = "togglesearch";
	bool multiplayer = false;
	std::vector<std::string_view> source_specs, target_specs;
	std::vector<std::string_view>* active_spec = &source_specs;
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
		else if (argv[i] == "--multiplayer"sv)
			multiplayer = true;
		else if (argv[i] == "--"sv)
			if (active_spec == &target_specs) {
				fmt::print("ERROR: passed -- twice?\n");
				std::exit(1);
			} else
				active_spec = &target_specs;
		else
			active_spec->emplace_back(argv[i]);
	}

	GadgetSet source_set = parse_gid_specs(source_specs);
	fmt::print("Source spec: {}\n", format_gadget_set(source_set));
	GadgetSet target_set = parse_gid_specs(target_specs);
	fmt::print("Target spec: {}\n", format_gadget_set(target_set));

	std::string connect_str = format_connect_string(db_user, db_pass, db_host, db_port, db_name);
	pqxx::connection conn(connect_str);

	//Minimal provenance information.  prov.back() is the current generation,
	//the only vector being appended to.  The other generations are kept sorted
	//for binary-search-based lookups.
	vector<vector<SkinnyProv>> prov;
	vector<SkinnyProv> target_prov; //only one generation here
	prov.emplace_back();
	//We store most edges as SkinnyProv, only getting the full edge data when
	//we're going to print a derivation.
	tsl::hopscotch_map<uint64_t, AnyProv, farmhash_hash> edge_cache, target_edge_cache;
	vector<uint64_t> source_ids, target_ids;
	unsigned int generation_start = 0, subgeneration_start = 0;

	auto close_and_mirror = [&conn, multiplayer](vector<SkinnyProv>& prov,
			unsigned int subgeneration_start, unsigned int generation, unsigned int subgeneration) {
		if (!multiplayer)
			do_stuff_temptable(conn, prov, build_follow_close_into_table_query(subgeneration_start),
					EdgeKind::close, subgeneration_start, generation, subgeneration);
		do_stuff_temptable(conn, prov, build_follow_mirror_into_table_query(subgeneration_start),
				EdgeKind::mirror, subgeneration_start, generation, subgeneration);
	};

	for (unsigned int generation = 0; ; generation++) {
		if (generation == 0) {
			source_ids = collect_initial_gadget_set(conn, source_set);
			std::sort(source_ids.begin(), source_ids.end());
			for (uint64_t i : source_ids)
				edge_cache.try_emplace(i, AnyProv::source(i));
			vector<uint64_t> target_ids = collect_initial_gadget_set(conn, target_set);
			//What we really want is a std::set_difference that works like std::unique
			//(moving the subtracted elements to the end of the vector), but in lieu of
			//doing that properly, we're abusing the edge cache.
			target_ids.erase(std::remove_if(target_ids.begin(), target_ids.end(),
					[&edge_cache](const auto& i){return edge_cache.count(i);}), target_ids.end());
			if (target_ids.empty()) {
				fmt::print("ERROR: all target ids were specified in source ids, exiting\n");
				std::exit(3);
			}
			std::sort(target_ids.begin(), target_ids.end());
			for (uint64_t i : target_ids)
				target_edge_cache.try_emplace(i, AnyProv::source(i));

			{
				transaction trans(conn);
				trans.exec("create temporary table closedset ("
						//can't add "references gadgets" because temp tables can't reference perm tables
						"id bigint primary key not null,"
						"gen smallint not null"
						") on commit preserve rows");
				//We want to close and mirror the targets in the same way as sources
				//so we can recognize when we've build a closed/mirrored gadget.
				trans.exec_params(build_insert_closedset_query(target_ids.size(), 0),
						pqxx::prepare::make_dynamic_params(target_ids));
				trans.commit();
			}

			//do_stuff starts its own transaction so we have to do this outside.
			close_and_mirror(target_prov, 0, 0, 0);
			std::sort(target_prov.begin(), target_prov.end());

			transaction trans(conn);
			//Delete the target stuff and insert the sources.
			trans.exec("delete from closedset");
			trans.exec_params(build_insert_closedset_query(source_ids.size(), 0),
					pqxx::prepare::make_dynamic_params(source_ids));
			trans.commit();
		} else {
//			std::size_t discovered = do_stuff(conn, prov,
//					build_follow_combine_query(generation_start, subgeneration_start+1),
//					AnyProv::combine, "combining", generation, 0);
			std::size_t discovered = do_stuff_temptable(conn, prov.back(),
					build_follow_combine_into_table_query(generation_start),
					EdgeKind::combine, subgeneration_start+1, generation, 0);
			if (!discovered)
				break;
			generation_start = subgeneration_start = subgeneration_start+1;
		}

		close_and_mirror(prov.back(), subgeneration_start, generation, 0);

		for (unsigned int subgeneration = 1; ; subgeneration++) {
//			std::size_t discovered = do_stuff(conn, prov,
//					build_follow_connect_query(subgeneration_start, subgeneration_start+1),
//					AnyProv::connect, "connecting", generation, subgeneration);
			std::size_t discovered = do_stuff_temptable(conn, prov.back(),
					build_follow_connect_into_table_query(subgeneration_start), EdgeKind::connect,
					subgeneration_start+1, generation, subgeneration);
			if (!discovered)
				break;
			++subgeneration_start;
			close_and_mirror(prov.back(), subgeneration_start, generation, subgeneration);
		}

		{
			transaction trans(conn);
			trans.exec("analyze closedset");
			trans.commit();
		}

		auto get_names = [&](uint64_t id) -> std::string {
			ro_transaction trans(conn);
			pqxx::result res = trans.exec_params("select name from names where gadget_id = $1 and "
					"not exists (select 1 from names as n2 where n2.gadget_id != $1 and n2.name = names.name) order by name", id);
			vector<std::string_view> names;
			for (const pqxx::row& r : res)
				names.push_back(r[0].c_str());
			return join(names, ", ");
		};

		std::sort(prov.back().begin(), prov.back().end());
		using prov_iterator = vector<SkinnyProv>::const_iterator;
		vector<pair<uint64_t, uint64_t>> source_target_pairs;
		vector<uint64_t> source_roots, target_roots;
		for (prov_iterator target = target_prov.begin(); target != target_prov.end(); ++target) {
			prov_iterator lb = std::lower_bound(prov.back().cbegin(), prov.back().cend(), *target);
			if (*lb == *target) {
				source_target_pairs.emplace_back(lb->output(), target->output());
				source_roots.emplace_back(lb->output());
				target_roots.emplace_back(target->output());
			}
		}

		if (!source_target_pairs.empty()) {
			fill_cache(conn, edge_cache, source_roots, prov);
			fill_cache(conn, target_edge_cache, target_roots, target_prov);

			for (const pair<uint64_t, uint64_t>& p : source_target_pairs) {
				vector<AnyProv> target_trace = toposort_provs(target_edge_cache, p.second);
				fmt::print("Target trace:\n");
					for (const AnyProv& p : target_trace) {
						if (p.kind() == EdgeKind::source)
							fmt::print("{} {}\n", p, get_names(p.output()));
						else
							fmt::print("{}\n", p);
					}
				vector<AnyProv> source_trace = toposort_provs(edge_cache, p.first);
				fmt::print("Source trace:\n");
					for (const AnyProv& p : source_trace)
						if (p.kind() == EdgeKind::source)
							fmt::print("{} {}\n", p, get_names(p.output()));
						else
							fmt::print("{}\n", p);

				//Don't report finding it again in the future.
				target_prov.erase(std::lower_bound(target_prov.begin(), target_prov.end(), p.second));
			}
		}

		prov.emplace_back();
	}
	return 0;
}