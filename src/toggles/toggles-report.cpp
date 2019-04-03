#include "precompiled.hpp"
#include "toggles-shared.hpp"
#include "stringutils.hpp"
#include "stopwatch.hpp"

using std::vector;
using std::pair;
using std::uint8_t;
using std::uint64_t;
using namespace std::literals::string_view_literals;

//grumble
namespace pqxx {
template<> struct string_traits<uint8_t> {
	static constexpr const char* name() noexcept {return "uint8_t";}
	static constexpr bool has_null() noexcept {return false;}
	static bool is_null() {return false;}
	[[noreturn]] static uint8_t null() {pqxx::internal::throw_null_conversion(name());}
	static void from_string(const char s[], uint8_t& t) {
		//We don't have the string length?!
		unsigned int x;
		string_traits<unsigned int>::from_string(s, x);
		t = numeric_cast<uint8_t>(x);
	}
	static std::string to_string(uint8_t x) {
		return std::to_string(x);
	}
};
}

enum class EdgeKind : unsigned char {
	combine = 0, connect = 1, close = 2, mirror = 3, source = 4
};
class AnyProv {
public:
	static AnyProv source(uint64_t output) {
		return {EdgeKind::source, output, std::numeric_limits<uint64_t>::max(),
				std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint8_t>::max(),
				std::numeric_limits<uint8_t>::max(), std::numeric_limits<uint8_t>::max(),
				std::numeric_limits<uint8_t>::max()};
	}
	static AnyProv combine(uint64_t input1, uint64_t input2, uint64_t output1,
			uint8_t splice, uint8_t rotation, uint8_t connectPoint,
			uint8_t canonicalizePermutation) {
		return {EdgeKind::combine, output1, input1, input2, splice, rotation, connectPoint, canonicalizePermutation};
	}
	static AnyProv combine(const pqxx::row& r) {
		return combine(r[0].as<uint64_t>(), r[1].as<uint64_t>(), r[2].as<uint64_t>(),
				r[3].as<uint8_t>(), r[4].as<uint8_t>(), r[5].as<uint8_t>(), r[6].as<uint8_t>());
	}
	static AnyProv connect(uint64_t input1, uint64_t output1,
			uint8_t connectPoint, uint8_t canonicalizePermutation) {
		return {EdgeKind::connect, output1, input1, std::numeric_limits<uint64_t>::max(),
				std::numeric_limits<uint8_t>::max(), std::numeric_limits<uint8_t>::max(),
				connectPoint, canonicalizePermutation};
	}
	static AnyProv connect(const pqxx::row& r) {
		return connect(r[0].as<uint64_t>(), r[1].as<uint64_t>(), r[2].as<uint8_t>(), r[3].as<uint8_t>());
	}
	static AnyProv close(uint64_t input1, uint64_t output1, uint8_t canonicalizePermutation) {
		return {EdgeKind::close, output1, input1, std::numeric_limits<uint64_t>::max(),
				std::numeric_limits<uint8_t>::max(), std::numeric_limits<uint8_t>::max(),
				std::numeric_limits<uint8_t>::max(), canonicalizePermutation};
	}
	static AnyProv close(const pqxx::row& r) {
		return close(r[0].as<uint64_t>(), r[1].as<uint64_t>(), r[2].as<uint8_t>());
	}
	static AnyProv mirror(uint64_t input1, uint64_t output1, uint8_t canonicalizePermutation) {
		return {EdgeKind::mirror, output1, input1, std::numeric_limits<uint64_t>::max(),
				std::numeric_limits<uint8_t>::max(), std::numeric_limits<uint8_t>::max(),
				std::numeric_limits<uint8_t>::max(), canonicalizePermutation};
	}
	static AnyProv mirror(const pqxx::row& r) {
		return mirror(r[0].as<uint64_t>(), r[1].as<uint64_t>(), r[2].as<uint8_t>());
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

std::string build_insert_closedset_query(std::size_t count, unsigned int generation) {
	vector<std::string> things;
	for (std::size_t i = 1; i <= count; ++i)
		things.push_back(fmt::format("(${}::int8, {}::int2)", i, generation));
	return "insert into closedset (id, gen) values " + join(things, ", ");
}

std::string build_follow_combine_query(unsigned int generation_start, unsigned int next_generation) {
	return fmt::format(
			"with edges as (select distinct on(output1) * from combine_edges\n"
			"  join closedset as c1 on c1.id = input1\n"
			"  join closedset as c2 on c2.id = input2\n"
			"  where (c1.gen >= {0} or c2.gen >= {0})\n"
			"    and not exists (select 1 from closedset where closedset.id = output1)\n"
			"),"
			"ins as (insert into closedset(id, gen) select edges.output1, {1} from edges)\n"
			//everything but the id
			"select input1, input2, output1, splice, rotation, connect_location, canonicalize_rotation from edges",
			generation_start, next_generation);
}

std::string build_follow_connect_query(unsigned int subgeneration_start, unsigned int next_generation) {
	return fmt::format(
			"with edges as (select distinct on(output1) * from connect_edges join closedset on (\n"
			"  closedset.id = input1 and\n"
			"  gen = {0}\n"
			"  and not exists (select 1 from closedset where closedset.id = output1 limit 1)\n"
			")),\n"
			"ins as (insert into closedset(id, gen) select edges.output1, {1} from edges)\n"
			"select input1, output1, connect_location, canonicalize_rotation from edges",
			subgeneration_start, next_generation);
}

std::string build_follow_close_query(unsigned int subgeneration_start) {
	return fmt::format(
			"with edges as (select distinct on(output1) * from close_edges join closedset on (\n"
			"  id = input1\n"
			"  and gen = {0}\n"
			"  and not exists (select 1 from closedset where id = output1)\n"
			")),\n"
			"ins as (insert into closedset(id, gen) select edges.output1, {0} from edges)\n"
			"select input1, output1, canonicalize_rotation from edges",
			subgeneration_start);
}

std::string build_follow_mirror_query(unsigned int subgeneration_start) {
	//select distinct doesn't just work here because of a/b symmetry
	return fmt::format(
			"with aedges as (select * from mirror_edges join closedset on (\n"
			"  id = a\n"
			"  and gen = {0}\n"
			"  and not exists (select 1 from closedset where id = b)\n"
			")),\n"
			"bedges as (select * from mirror_edges join closedset on (\n"
			"  id = b\n"
			"  and gen = {0}\n"
			"  and not exists (select 1 from closedset where id = a)\n"
			")),\n"
			"ains as (insert into closedset(id, gen) select aedges.b, {0} from aedges),\n"
			"bins as (insert into closedset(id, gen) select bedges.a, {0} from bedges)\n"
			"select a, b, canonicalize_rotation from aedges\n"
			"union all\n"
			"select b, a, canonicalize_rotation from bedges",
			subgeneration_start);
}

std::size_t do_stuff(pqxx::connection& conn, tsl::hopscotch_map<uint64_t, AnyProv, farmhash_hash>& prov,
		std::string query, AnyProv(*ctor)(const pqxx::row&), std::string_view op_name,
		unsigned int generation, unsigned int subgeneration) {
	Stopwatch stopwatch = Stopwatch::process();
	transaction trans(conn);
	pqxx::result res = trans.exec(query);
	std::size_t size = res.size();
	for (const pqxx::row& r : res) {
		AnyProv p = ctor(r);
		auto iter_bool = prov.try_emplace(p.output(), p);
		if (!iter_bool.second)
			throw std::logic_error(fmt::format("conflict while {}:\n  {}\n  {}",
					op_name, iter_bool.first->second, p));
	}
	trans.commit();
	fmt::print("{} {}.{} found {} in {}, closed size {}\n", op_name,
			generation, subgeneration, size, stopwatch.elapsed().hms(), prov.size());
	return size;
}

int main(int argc, char* argv[]) { //genbuild entrypoint
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

	tsl::hopscotch_map<uint64_t, AnyProv, farmhash_hash> prov, target_prov;
	vector<uint64_t> source_ids, target_ids;
	unsigned int generation_start = 0, subgeneration_start = 0;

	auto close_and_mirror = [&conn, multiplayer](tsl::hopscotch_map<uint64_t, AnyProv, farmhash_hash>& prov,
			unsigned int subgeneration_start, unsigned int generation, unsigned int subgeneration) {
		if (!multiplayer)
			do_stuff(conn, prov, build_follow_close_query(subgeneration_start),
					AnyProv::close, "closing", generation, subgeneration);
		do_stuff(conn, prov, build_follow_mirror_query(subgeneration_start),
				AnyProv::mirror, "mirroring", generation, subgeneration);
	};

	for (unsigned int generation = 0; ; generation++) {
		if (generation == 0) {
			source_ids = collect_initial_gadget_set(conn, source_set);
			std::sort(source_ids.begin(), source_ids.end());
			for (uint64_t i : source_ids)
				prov.try_emplace(i, AnyProv::source(i));
			target_ids = collect_initial_gadget_set(conn, target_set);
			std::sort(target_ids.begin(), target_ids.end());
			for (uint64_t i : target_ids)
				target_prov.try_emplace(i, AnyProv::source(i));

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

			transaction trans(conn);
			//Delete the target stuff and insert the sources.
			trans.exec("delete from closedset");
			trans.exec_params(build_insert_closedset_query(source_ids.size(), 0),
					pqxx::prepare::make_dynamic_params(source_ids));
			trans.commit();
		} else {
			std::size_t discovered = do_stuff(conn, prov,
					build_follow_combine_query(generation_start, subgeneration_start+1),
					AnyProv::combine, "combining", generation, 0);
			if (!discovered)
				break;
			generation_start = subgeneration_start = subgeneration_start+1;
		}

		close_and_mirror(prov, subgeneration_start, generation, 0);

		for (unsigned int subgeneration = 1; ; subgeneration++) {
			std::size_t discovered = do_stuff(conn, prov,
					build_follow_connect_query(subgeneration_start, subgeneration_start+1),
					AnyProv::connect, "connecting", generation, subgeneration);
			if (!discovered)
				break;
			++subgeneration_start;
			close_and_mirror(prov, subgeneration_start, generation, subgeneration);
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

		using prov_iterator = tsl::hopscotch_map<uint64_t, AnyProv, farmhash_hash>::iterator;
		for (prov_iterator target = target_prov.begin(); target != target_prov.end();) {
			prov_iterator leaf = prov.find(target->first);
			if (leaf != prov.end()) {
				vector<AnyProv> target_trace = toposort_provs(target_prov, target->first);
				fmt::print("Target trace:\n");
				for (const AnyProv& p : target_trace) {
					if (p.kind() == EdgeKind::source)
						fmt::print("{} {}\n", p, get_names(p.output()));
					else
						fmt::print("{}\n", p);
				}
				//Don't report finding it again in the future.
				target = target_prov.erase(target);

				vector<AnyProv> source_trace = toposort_provs(prov, leaf->first);
				fmt::print("Source trace:\n");
				for (const AnyProv& p : source_trace)
					if (p.kind() == EdgeKind::source)
						fmt::print("{} {}\n", p, get_names(p.output()));
					else
						fmt::print("{}\n", p);
			} else
				++target;
		}
	}
	return 0;
}