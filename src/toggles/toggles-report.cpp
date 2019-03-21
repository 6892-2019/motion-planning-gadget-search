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
private:
	AnyProv(EdgeKind kind, uint64_t output1, uint64_t input1, uint64_t input2,
			uint8_t splice, uint8_t rotation, uint8_t connectPoint,
			uint8_t canonicalizeRotation) : output1_(output1), input1_(input1),
					input2_(input2), splice_(splice), rotation_(rotation),
					connectPoint_(connectPoint), canonicalizePermutation_(canonicalizeRotation) {}
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

std::string build_insert_closedset_query(std::size_t count, unsigned int generation) {
	vector<std::string> things;
	for (std::size_t i = 1; i <= count; ++i)
		things.push_back(fmt::format("(${}::int8, {}::int2)", i, generation));
	return "insert into closedset (id, gen) values " + join(things, ", ");
}

std::string build_follow_combine_query(unsigned int generation_start, unsigned int next_generation) {
	return fmt::format(
			"with edges as (select distinct on(output1) * from combine_edges join closedset on ((\n"
			"      input1 = closedset.id\n"
			"    and \n"
			"      input2 in (select id from closedset where gen >= {0})\n"
			"  or\n"
			"      input2 = closedset.id\n"
			"    and\n"
			"      input1 in (select id from closedset where gen >= {0})\n"
			") and not exists (select 1 from closedset where id = output1 limit 1)\n"
			")),\n"
			"generation (gen) as (values ({1})),\n"
			"ins as (insert into closedset(id, gen) select edges.output1, generation.gen from edges cross join generation)\n"
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
			"nextgen(gen) as (values ({1})),\n"
			"ins as (insert into closedset(id, gen) select edges.output1, nextgen.gen from edges cross join nextgen)\n"
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
			"generation (gen) as (values ({0})),\n"
			"ins as (insert into closedset(id, gen) select edges.output1, generation.gen from edges cross join generation)\n"
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
			"generation (gen) as (values ({0})),\n"
			"ains as (insert into closedset(id, gen) select aedges.b, generation.gen from aedges cross join generation),\n"
			"bins as (insert into closedset(id, gen) select bedges.a, generation.gen from bedges cross join generation)\n"
			"select a, b, canonicalize_rotation from aedges\n"
			"union all\n"
			"select b, a, canonicalize_rotation from bedges",
			subgeneration_start);
}

std::size_t do_stuff(pqxx::connection& conn, tsl::hopscotch_map<uint64_t, AnyProv>& prov,
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

	tsl::hopscotch_map<uint64_t, AnyProv> prov;
	vector<uint64_t> source_ids, target_ids;
	unsigned int generation_start = 0, subgeneration_start = 0;

	auto close_and_mirror = [&](unsigned int generation, unsigned int subgeneration) {
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
				prov.try_emplace(i, AnyProv::source(i)); //default-construct the variant
			target_ids = collect_initial_gadget_set(conn, target_set);
			std::sort(target_ids.begin(), target_ids.end());

			transaction trans(conn);
			trans.exec("create temporary table closedset ("
					//can't add "references gadgets" because temp tables can't reference perm tables
					"id bigint primary key,"
					"gen smallint"
					") on commit preserve rows");
			trans.exec("create index on closedset(gen) include(id)");
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

		close_and_mirror(generation, 0);

		for (unsigned int subgeneration = 1; ; subgeneration++) {
			std::size_t discovered = do_stuff(conn, prov,
					build_follow_connect_query(subgeneration_start, subgeneration_start+1),
					AnyProv::connect, "connecting", generation, subgeneration);
			if (!discovered)
				break;
			++subgeneration_start;
			close_and_mirror(generation, subgeneration);
		}

		{
			transaction trans(conn);
			trans.exec("analyze closedset");
			trans.commit();
		}

//		fmt::print("{}\n", prov.size());
		for (const auto& p : prov)
			fmt::print("({}, 0), ", p.first);
		fmt::print("\n");
	}
	return 0;
}