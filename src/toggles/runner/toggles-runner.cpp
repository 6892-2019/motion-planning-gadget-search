#include "precompiled.hpp"
#include "automaton.hpp"
#include "canonicalize.hpp"
#include "ops.hpp"
#include "../database.hpp"
#include "../rpc.hpp"
#include "../toggles-shared.hpp"
#include "stringutils.hpp"
#include "hopscotch/hopscotch_set.h"
#include "hopscotch/hopscotch_map.h"
#include "tsl/ordered_set.h"
#include "msgpack.hpp"
#include "farmhash/farmhash.h"
#include "lmdb++.h"
#include <yaml-cpp/yaml.h>
#include <pqxx/pqxx>
#include <cstdio>

using namespace automaton;
using std::uint64_t;
using std::size_t;
using std::pair;
using std::tuple;
using std::optional;
using std::nullopt;
using std::vector;
using std::unique_ptr;
using std::string_view;
using namespace std::literals::string_view_literals;

//forward declaration:
void write_output(const void* data, size_t size);

////TODO: make this SCCs::find
//unsigned int component_for_state(SCCs sccs, AutomatonBase::state_type state) {
//	for (unsigned int c : xrange(sccs.size()))
//		for (unsigned int s : make_range_for_pair(sccs.begin(c), sccs.end(c))) //TODO: add SCCs::range (name TBD)
//			if (s == state)
//				return c;
//	//TODO: add an SCCs method giving the number of states, so we can report here
//	throw std::logic_error(fmt::format("component_for_state failed: {} {}", state, sccs.size()));
//}

///**
// * Canonicalizes a gadget in SLLS format, returning in database row format.
// * Intended for use when loading human-readable gadget definitions into the
// * database.
// */
//vector<pair<OutputRow, optional<OutputRow>>> canonicalize_from_slls(SLLS gadget) {
//	unique_ptr<WorkingAutomaton> a = inflate_slls(gadget);
//	OutputRow row = deflate_outputrow(*a);
//	SCCs sccs = automaton::find_components(*a); //just computed this in deflate_outputrow, could try to save it
//	auto activealpha = a->active_alphabet_size();
//
//	vector<pair<unique_ptr<WorkingAutomaton>, OutputRow>> normals;
//	normals.emplace_back(std::move(a), std::move(row));
//	//When initializing the database with named gadgets, we want to try all
//	//initial states in the initial connected component.
//	unsigned int initial_component = component_for_state(sccs, 0);
//	for (auto state : make_range_for_pair(sccs.begin(initial_component), sccs.end(initial_component))) //TODO: SCCs::range
//		if (normals.front().first->accept(state)) {
//			unique_ptr<WorkingAutomaton> p = normals.front().first->clone();
//			p->swapStateNumbers(0, state);
//			canonicalize(*p, activealpha, false); //no mirroring
//			row = deflate_outputrow(*p);
//			normals.emplace_back(std::move(p), std::move(row));
//		}
//	std::sort(normals.begin(), normals.end(), [](const auto& l, const auto& r) {return l.second < r.second;});
//	normals.erase(std::unique(normals.begin(), normals.end(),
//			[](const auto& l, const auto& r) {return l.second == r.second;}), normals.end());
//
//	//It's plausible that only a subset of the states are chiral.
//	vector<pair<unique_ptr<WorkingAutomaton>, OutputRow>> mirrors;
//	for (const auto& n : normals) {
//		//We don't return the rotation, but we won't add a mirror provenance edge
//		//either, so the usual mirror machinery will fill it in later.  We just
//		//need the gadget up front so we can give it an appropriate name.
//		unique_ptr<WorkingAutomaton> p = mirror(*n.first).first;
//		row = deflate_outputrow(*p);
//		mirrors.emplace_back(std::move(p), std::move(row));
//	}
//
//	//Mirror order is the same as the normal order (mirrors aren't sorted).
//	//We're also just taking the first enantiomorph as 'normal', rather than
//	//the lexicographically lesser one.
//	vector<pair<OutputRow, optional<OutputRow>>> retval;
//	for (auto i : xrange(normals.size()))
//		if (normals[i].second != mirrors[i].second)
//			retval.emplace_back(std::move(normals[i].second), std::move(mirrors[i].second));
//		else
//			retval.emplace_back(std::move(normals[i].second), nullopt);
//	return retval;
//}


Finisher<ConnectProvenance> do_connect(vector<pair<std::uint64_t, vector<std::byte>>> inputs) {
	Finisher<ConnectProvenance> finisher;
	for (auto& i : inputs)
		connect(*encoding::decode(i.second), i.first, finisher);
	return finisher;
}

Finisher<SimpleProvenance> do_close(vector<pair<std::uint64_t, vector<std::byte>>> inputs) {
	Finisher<SimpleProvenance> finisher;
	for (const auto& p : inputs) {
		SimpleProvenance prov;
		prov.input1 = p.first;
		unique_ptr<WorkingAutomaton> a = encoding::decode(p.second);
		//TODO: decode could return an encoding::Stats instead of querying the active alphabet again.
		auto activealpha = a->active_alphabet_size();
		bool possibly_changed = acceptingClosure(*a, activealpha);
		if (!possibly_changed) continue;
		prov.canonicalizePermutation = numeric_cast<std::uint8_t>(canonicalize(*a, activealpha, false));
		std::vector<std::byte> r = encoding::encode(*a);
		if (r != p.second)
			finisher(std::move(r), prov);
		//TODO: should always be more edges (with undirected counting twice) after
		//successful (modified the automaton) closure, never the same or fewer
	}
	return finisher;
}

Finisher<SimpleProvenance> do_mirror(vector<pair<std::uint64_t, vector<std::byte>>> inputs) {
	Finisher<SimpleProvenance> finisher;
	for (const auto& p : inputs) {
		SimpleProvenance prov;
		prov.input1 = p.first;
		unique_ptr<WorkingAutomaton> a = encoding::decode(p.second);
		//mirror copies.  We do need to know if mirroring changed the automaton,
		//but maybe there's a way to do that while reusing *a?
		pair<unique_ptr<WorkingAutomaton>, unsigned int> m = mirror(*a);
		if (*m.first == *a) continue;
		prov.canonicalizePermutation = numeric_cast<std::uint8_t>(m.second);
		finisher(encoding::encode(*m.first), prov);
	}
	return finisher;
}




//return type is just to satisfy rpc machinery; we don't special-case for void
//and msgpack can't handle nullptr_t
[[noreturn]] int do_batch_combine(vector<pair<uint64_t, vector<std::byte>>> inputs,
		vector<uint64_t> lefts, vector<uint64_t> rights, unsigned int precision) {
	tsl::hopscotch_map<std::uint64_t, vector<std::byte>, farmhash_hash> map;
	for (auto& p : inputs)
		map[p.first] = std::move(p.second);
	inputs.clear();
	inputs.shrink_to_fit();
	Finisher<CombineProvenance> finisher = do_combine(std::move(map), std::move(lefts), std::move(rights), precision);
	//TODO: We'd like to use the same sequence number here, but we don't have
	//access.  Introduce a seqno_t "strong typedef" that handler_adapter
	//recognizes and fills in (in addition to whatever other args are present).
	simple_buffer buf = pack_call(0, "batch-combine-commit", finisher.rows_.values_container(), finisher.prov_, finisher.pruned_);
	//By printing to stdout and exiting, we emit an RPC call rather than a
	//response.  If we threw an exception, though, the dispatcher will generate
	//an error response as normal, so we'll detect the failure when trying to
	//commit the results.
	write_output(buf.data(), buf.size());
	std::exit(0);
}

namespace {
vector<uint64_t> extract_first(const vector<pair<uint64_t, vector<std::byte>>>& inputs) {
	vector<uint64_t> input_gids;
	input_gids.reserve(inputs.size());
	for (const auto& p : inputs)
		input_gids.push_back(p.first);
	return input_gids;
}
}

[[noreturn]] int do_batch_connect(vector<pair<uint64_t, vector<std::byte>>> inputs) {
	vector<uint64_t> input_gids = extract_first(inputs);
	Finisher<ConnectProvenance> finisher = do_connect(std::move(inputs));
	simple_buffer buf = pack_call(0, "batch-connect-commit", input_gids,
			finisher.rows_.values_container(), finisher.prov_, finisher.pruned_);
	write_output(buf.data(), buf.size());
	std::exit(0);
}

[[noreturn]] int do_batch_close(vector<pair<uint64_t, vector<std::byte>>> inputs) {
	vector<uint64_t> input_gids = extract_first(inputs);
	Finisher<SimpleProvenance> finisher = do_close(std::move(inputs));
	simple_buffer buf = pack_call(0, "batch-close-commit", input_gids,
			finisher.rows_.values_container(), finisher.prov_, finisher.pruned_);
	write_output(buf.data(), buf.size());
	std::exit(0);
}

[[noreturn]] int do_batch_mirror(vector<pair<uint64_t, vector<std::byte>>> inputs) {
	vector<uint64_t> input_gids = extract_first(inputs);
	Finisher<SimpleProvenance> finisher = do_mirror(std::move(inputs));
	simple_buffer buf = pack_call(0, "batch-mirror-commit", input_gids,
			finisher.rows_.values_container(), finisher.prov_, finisher.pruned_);
	write_output(buf.data(), buf.size());
	std::exit(0);
}



std::string build_select_gadget_data_to_id(std::size_t rows) {
	assert(rows >= 1);
	vector<std::string> values;
	values.reserve(rows);
	values.push_back("  ($1::integer, $2::bytea)"); //first one is special to specify types
	for (unsigned int i = 1; i < rows; ++i)
		values.push_back(fmt::format("(${}, ${})", 2*i + 1, 2*i + 2));
	return "with input_rows (n, data) as (values\n" +
			join(values, ",\n  ") +
			"\n)\n" +
			"select input_rows.n, gadgets.id from gadgets join input_rows using (data);";
}

std::string build_insert_gadgets_query(std::size_t rows) {
	assert(rows >= 1);
	vector<std::string> values;
	values.reserve(rows);
	//first one is special to specify types
	values.push_back("  ($1::integer, $2::integer, $3::integer, $4::integer, $5::integer, $6::integer, $7::bytea)");
	for (unsigned int i = 1; i < rows; ++i)
		values.push_back(fmt::format("(${}, ${}, ${}, ${}, ${}, ${}, ${})",
				7*i+1, 7*i+2, 7*i+3, 7*i+4, 7*i+5, 7*i+6, 7*i+7));
	return "with input_rows (n, states, locations, uedges, dedges, components, data) as (values" +
			join(values, ",\n  ") +
			"\n), ins as (\n"
			"  insert into gadgets (states, locations, undirected_edges, directed_edges, components, data)\n"
			"  select states, locations, uedges, dedges, components, data from input_rows\n"
			"  returning gadgets.id, gadgets.data\n"
			")\n"
			"select input_rows.n, ins.id from input_rows join ins using (data);";
}

std::string build_insert_connect_edges_query(std::size_t rows) {
	assert(rows >= 1);
	vector<std::string> values;
	values.reserve(rows);
	values.push_back("  ($1::bigint, $2::bigint, $3::smallint, $4::smallint)");
	for (unsigned int i = 1; i < rows; ++i)
		values.push_back(fmt::format("(${}, ${}, ${}, ${})", 4*i + 1, 4*i + 2, 4*i+3, 4*i+4));
	return "insert into connect_edges (input1, output1, connect_location, canonicalize_rotation) values\n" +
			join(values, ",\n  ") + ";";
}

std::string build_insert_combine_edges_query(std::size_t rows) {
	assert(rows >= 1);
	vector<std::string> values;
	values.reserve(rows);
	values.push_back("  ($1::bigint, $2::bigint, $3::bigint, $4::smallint, $5::smallint, $6::smallint, $7::smallint)");
	for (unsigned int i = 1; i < rows; ++i)
		values.push_back(fmt::format("(${}, ${}, ${}, ${}, ${}, ${}, ${})",
				7*i+1, 7*i+2, 7*i+3, 7*i+4, 7*i+5, 7*i+6, 7*i+7));
	return "insert into combine_edges (input1, input2, output1, splice, rotation, connect_location, canonicalize_rotation) values\n" +
			join(values, ",\n  ") + ";";
}

std::string build_insert_simple_edges_query(std::size_t rows, std::string_view table_name, std::string_view column_name_list) {
	assert(rows >= 1);
	vector<std::string> values;
	values.reserve(rows);
	values.push_back("  ($1::bigint, $2::bigint, $3::smallint)");
	for (unsigned int i = 1; i < rows; ++i)
		values.push_back(fmt::format("(${}, ${}, ${})", 3*i + 1, 3*i + 2, 3*i+3));
	return fmt::format("insert into {} ({}) values\n", table_name, column_name_list) +
			join(values, ",\n  ") +
			"\n on conflict do nothing;";
}

std::string build_insert_close_edges_query(std::size_t rows) {
	return build_insert_simple_edges_query(rows, "close_edges", "input1, output1, canonicalize_rotation");
}
std::string build_insert_mirror_edges_query(std::size_t rows) {
	return build_insert_simple_edges_query(rows, "mirror_edges", "a, b, canonicalize_rotation");
}

std::string build_insert_completion_query(std::size_t rows, std::string_view table_name) {
	assert(rows >= 1);
	vector<std::string> values;
	values.reserve(rows);
	values.push_back("  (int8range($1::bigint, $2::bigint))");
	for (unsigned int i = 1; i < rows; ++i)
		values.push_back(fmt::format("(int8range(${}, ${}))", 2*i + 1, 2*i + 2));
	return fmt::format("insert into {} (r) values\n", table_name) +
			join(values, ",\n  ") + ";";
}
std::string build_insert_connect_completion_query(std::size_t rows) {
	return build_insert_completion_query(rows, "completed_connects");
}
std::string build_insert_close_completion_query(std::size_t rows) {
	return build_insert_completion_query(rows, "completed_closes");
}
std::string build_insert_mirror_completion_query(std::size_t rows) {
	return build_insert_completion_query(rows, "completed_mirrors");
}

//This is basically a workaround for NetBeans' choking on structured bindings.
//If I ever stop using it, this can just be a pair.
struct SelsertGadgetByDataResult {
	vector<std::uint64_t> local_to_global, novel_global_ids;
};
/**
 * Returns the global gadget id of each of the given rows, inserting the row if
 * not already present.  The vector of ids matches the order of the rows.
 */
SelsertGadgetByDataResult selsert_gadget_by_data(pqxx::connection& conn, transaction& trans, vector<vector<std::byte>>&& rows) {
	//We want multiple runners to select and insert gadgets in a consistent
	//order to reduce serialization failures, but we also need local_to_global
	//in the same order.  So we sort an array of indices, then use that order.
	vector<unsigned int> indices(rows.size());
	std::iota(indices.begin(), indices.end(), 0u);
	std::sort(indices.begin(), indices.end(), [&rows](unsigned int a, unsigned int b) {
		return rows[a] < rows[b];
	});

	//This duplicates some logic from batch_parameterized.  This function is
	//hard to fit into that format because we're using the indices array and
	//because there are effectively two different implementations for
	//database_invoke_apply for OutputRow (one for select, another for insert).
	vector<std::uint64_t> local_to_global(rows.size(), std::numeric_limits<std::uint64_t>::max());
//	std::size_t pending_insert_count = 0;
//	std::size_t batch_base = 0;
//	while (batch_base < indices.size()) {
//		std::size_t batch_size = std::min<std::size_t>(65535/2, indices.size() - batch_base);
//		pqxx::internal::parameterized_invocation inv = trans.parameterized(build_select_gadget_data_to_id(batch_size));
//		for (std::size_t offset = 0; offset < batch_size; ++offset) {
//			unsigned int i = indices[batch_base + offset];
//			inv(i)(pqxx::binarystring(rows[i].edges.data(), rows[i].edges.size()));
//		}
//		pqxx::result already_have = inv.exec();
//		for (const pqxx::row& r : already_have)
//			local_to_global[r[0].as<std::size_t>()] = r[1].as<std::uint64_t>();
//		pending_insert_count += batch_size - already_have.size();
//		batch_base += batch_size;
//	}

	vector<std::uint64_t> novel_global_ids;
//	novel_global_ids.reserve(pending_insert_count);
//	batch_base = 0;
//	std::size_t index = 0;
//	while (batch_base < pending_insert_count) {
//		std::size_t batch_size = std::min<std::size_t>(65535/7, pending_insert_count - batch_base);
//		pqxx::internal::parameterized_invocation inv = trans.parameterized(
//				build_insert_gadgets_query(batch_size));
//		for (std::size_t batch_offset = 0; batch_offset < batch_size; ++index /* not batch_offset */) {
//			unsigned int i = indices[index];
//			if (local_to_global[i] == std::numeric_limits<std::uint64_t>::max()) {
//				const OutputRow& r = rows[i];
//				inv(i)(r.states)(r.locations)(r.uedges)(r.dedges)(r.sccs)(pqxx::binarystring(r.edges.data(), r.edges.size()));
//				++batch_offset; //made progress on this batch
//			}
//		}
//		pqxx::result inserted = inv.exec();
//		for (const pqxx::row& r : inserted) {
//			std::uint64_t gid = r[1].as<std::uint64_t>();
//			local_to_global[r[0].as<std::size_t>()] = gid;
//			novel_global_ids.push_back(gid);
//		}
//		batch_base += batch_size;
//	}
//	//Should have filled in everything now.
//	assert(std::find(local_to_global.begin(), local_to_global.end(),
//			std::numeric_limits<std::uint64_t>::max()) == local_to_global.end());
	return {std::move(local_to_global), std::move(novel_global_ids)};
}
//as above, but executes as its own transaction
SelsertGadgetByDataResult selsert_gadget_by_data(pqxx::connection& conn, vector<vector<std::byte>>&& rows) {
	return retry_db_operation([&]() {
		transaction trans(conn);
		SelsertGadgetByDataResult res = selsert_gadget_by_data(conn, trans, std::move(rows));
		trans.commit();
		return std::move(res);
	}, 10, "selsert_gadget_by_data");
}

vector<pair<std::uint64_t, std::uint64_t>> maximal_ranges(const vector<std::uint64_t>& data) {
	assert(std::is_sorted(data.begin(), data.end()));
	vector<pair<std::uint64_t, std::uint64_t>> ranges;
	auto first = data.begin(), last = data.begin();
	//Build maximal ranges, first inclusive and last exclusive.
	while (true) {
		if (last+1 == data.end()) {
			ranges.emplace_back(*first, *last + 1);
			break;
		} else if (*(last+1) - *last != 1) {
			ranges.emplace_back(*first, *last + 1);
			first = last = last+1;
		} else
			++last;
	}
	assert(std::is_sorted(ranges.begin(), ranges.end()));
	return ranges;
}

template<typename T>
struct fits_in {
	bool operator()(T t) const noexcept {
		return t <= std::numeric_limits<T>::max();
	}
};

static std::string g_database_connect_string;

DatabaseOperationStatistics commit_connect_result(pqxx::connection& conn,
		vector<std::uint64_t>&& input_gids, //for completion data
		vector<vector<std::byte>>&& rows, vector<ConnectProvenance>&& prov, std::size_t pruned) {
	std::sort(input_gids.begin(), input_gids.end());
	vector<pair<std::uint64_t, std::uint64_t>> completed_ranges = maximal_ranges(std::move(input_gids));
	std::size_t survivor_size = rows.size();
	std::size_t edge_count = prov.size();
	auto selsert_result = selsert_gadget_by_data(conn, std::move(rows));
	std::size_t novel_gadgets_size = selsert_result.novel_global_ids.size();
	const vector<std::uint64_t>& local_to_global = selsert_result.local_to_global;

	if (std::all_of(local_to_global.begin(), local_to_global.end(), fits_in<decltype(ConnectProvenance::output1)>())) {
		for (ConnectProvenance& p : prov)
			p.output1 = static_cast<std::uint32_t>(local_to_global[p.output1]);
		std::sort(prov.begin(), prov.end());

		retry_db_operation([&]() {
			transaction trans(conn);
			batch_parameterized(conn, trans, build_insert_connect_edges_query, 65535/4, std::move(prov));
			batch_parameterized(conn, trans, build_insert_connect_completion_query, 65535/2, std::move(completed_ranges));
			trans.commit();
			return nullptr;
		}, 10, "commit_connect_result inserting provs");
	} else {
		//We use larger types than necessary because pqxx doesn't want to string/unstring uint8_t.
		vector<std::tuple<uint64_t, uint64_t, std::uint16_t, std::uint16_t>> edges;
		for (const ConnectProvenance& p : prov)
			edges.emplace_back(p.input1, local_to_global[p.output1], p.connectPoint, p.canonicalizePermutation);
		std::sort(edges.begin(), edges.end());

		retry_db_operation([&]() {
			transaction trans(conn);
			batch_parameterized(conn, trans, build_insert_connect_edges_query, 65535/4, std::move(edges));
			batch_parameterized(conn, trans, build_insert_connect_completion_query, 65535/2, std::move(completed_ranges));
			trans.commit();
			return nullptr;
		}, 10, "commit_connect_result inserting edges");
	}
	return {pruned, survivor_size - novel_gadgets_size, novel_gadgets_size, edge_count};
}

DatabaseOperationStatistics do_connect_db(vector<std::uint64_t> input_gids) {
	pqxx::connection conn(g_database_connect_string);
	vector<pair<std::uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(conn, input_gids);
	Finisher outputs = do_connect(std::move(inputs));
	return commit_connect_result(conn, std::move(input_gids), std::move(outputs.rows_).values_container(),
			std::move(outputs.prov_), outputs.pruned_);
}

DatabaseOperationStatistics commit_combine_result(pqxx::connection& conn, vector<vector<std::byte>>&& rows,
		vector<CombineProvenance>&& prov, std::size_t pruned) {
	std::size_t survivor_size = rows.size(), edge_count = prov.size();
	auto selsert_result = selsert_gadget_by_data(conn, std::move(rows));
	const vector<std::uint64_t>& local_to_global = selsert_result.local_to_global;
	//We only need the size here, so clean up.
	std::size_t novel_gadgets_size = selsert_result.novel_global_ids.size();
	selsert_result.novel_global_ids.clear();
	selsert_result.novel_global_ids.shrink_to_fit();

	//We want to insert edges in sorted order to reduce serialization failures.
	//We can use the Provenance if the new ids fit; otherwise we have to copy.
	if (std::all_of(local_to_global.begin(), local_to_global.end(), fits_in<decltype(CombineProvenance::output1)>())) {
		for (CombineProvenance& p : prov)
			p.output1 = static_cast<std::uint32_t>(local_to_global[p.output1]);
		std::sort(prov.begin(), prov.end());
		batch_parameterized(conn, build_insert_combine_edges_query, 65535/7, std::move(prov), "commit_combine_result inserting provs");
	} else {
		//We use larger types than necessary because pqxx doesn't want to string/unstring uint8_t.
		vector<std::tuple<uint64_t, uint64_t, uint64_t, std::uint16_t, std::uint16_t, std::uint16_t, std::uint16_t>> edges;
		for (const CombineProvenance& p : prov)
			edges.emplace_back(p.input1, p.input2, local_to_global[p.output1], p.splice, p.rotation, p.connectPoint, p.canonicalizePermutation);
		std::sort(edges.begin(), edges.end());
		batch_parameterized(conn, build_insert_combine_edges_query, 65535/7, std::move(edges), "commit_combine_result inserting tuples");
	}
	return {pruned, survivor_size - novel_gadgets_size, novel_gadgets_size, edge_count};
}

DatabaseOperationStatistics do_combine_db(vector<std::uint64_t> left_gids, vector<std::uint64_t> right_gids, unsigned int precision) {
	vector<std::uint64_t> input_gids;
	input_gids.reserve(left_gids.size() + right_gids.size());
	input_gids.insert(input_gids.end(), left_gids.begin(), left_gids.end());
	input_gids.insert(input_gids.end(), right_gids.begin(), right_gids.end());
	std::sort(input_gids.begin(), input_gids.end());
	input_gids.erase(std::unique(input_gids.begin(), input_gids.end()), input_gids.end());

	pqxx::connection conn(g_database_connect_string);

	//TODO: select_gadget_id_to_data should be templated on the result container so we can directly build this map
	vector<pair<std::uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(conn, input_gids);
	tsl::hopscotch_map<std::uint64_t, vector<std::byte>, farmhash_hash> map;
	for (pair<std::uint64_t, vector<std::byte>>& p : inputs)
		map.try_emplace(p.first, std::move(p.second));
	Finisher outputs = do_combine(std::move(map), left_gids, right_gids, precision);
	return commit_combine_result(conn, std::move(outputs.rows_).values_container(), std::move(outputs.prov_), outputs.pruned_);
}

DatabaseOperationStatistics commit_close_result(pqxx::connection& conn,
		vector<std::uint64_t>&& input_gids, //for completion data
		vector<vector<std::byte>>&& rows, vector<SimpleProvenance>&& prov, std::size_t pruned) {
	std::size_t survivor_size = rows.size();
	std::size_t edge_count = prov.size();

	auto selsert_result = selsert_gadget_by_data(conn, std::move(rows));
	std::size_t novel_gadgets_size = selsert_result.novel_global_ids.size();
	const vector<std::uint64_t>& local_to_global = selsert_result.local_to_global;
	const vector<std::uint64_t>& novel_global_ids = selsert_result.novel_global_ids;

	//Closure is idempotent, so we've also finished for any new gadgets.
	input_gids.insert(input_gids.end(), novel_global_ids.begin(), novel_global_ids.end());
	std::sort(input_gids.begin(), input_gids.end());
	vector<pair<std::uint64_t, std::uint64_t>> completed_ranges = maximal_ranges(std::move(input_gids));

	if (std::all_of(local_to_global.begin(), local_to_global.end(), fits_in<decltype(SimpleProvenance::output1)>())) {
		for (SimpleProvenance& p : prov)
			p.output1 = static_cast<std::uint32_t>(local_to_global[p.output1]);
		std::sort(prov.begin(), prov.end());

		retry_db_operation([&]() {
			transaction trans(conn);
			batch_parameterized(conn, trans, build_insert_close_edges_query, 65535/3, std::move(prov));
			batch_parameterized(conn, trans, build_insert_close_completion_query, 65535/2, std::move(completed_ranges));
			trans.commit();
			return nullptr;
		}, 10, "commit_close_result inserting provs");
	} else {
		//We use larger types than necessary because pqxx doesn't want to string/unstring uint8_t.
		vector<std::tuple<uint64_t, uint64_t, std::uint16_t>> edges;
		for (const SimpleProvenance& p : prov)
			edges.emplace_back(p.input1, local_to_global[p.output1], p.canonicalizePermutation);
		std::sort(edges.begin(), edges.end());

		retry_db_operation([&]() {
			transaction trans(conn);
			batch_parameterized(conn, trans, build_insert_close_edges_query, 65535/3, std::move(edges));
			batch_parameterized(conn, trans, build_insert_close_completion_query, 65535/2, std::move(completed_ranges));
			trans.commit();
			return nullptr;
		}, 10, "commit_close_result inserting edges");
	}
	return {pruned, survivor_size - novel_gadgets_size, novel_gadgets_size, edge_count};
}

DatabaseOperationStatistics do_close_db(vector<std::uint64_t> input_gids) {
	pqxx::connection conn(g_database_connect_string);
	vector<pair<std::uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(conn, input_gids);
	Finisher<SimpleProvenance> outputs = do_close(std::move(inputs));
	return commit_close_result(conn, std::move(input_gids), std::move(outputs.rows_).values_container(),
			std::move(outputs.prov_), outputs.pruned_);
}

DatabaseOperationStatistics commit_mirror_result(pqxx::connection& conn,
		vector<std::uint64_t>&& input_gids, //for completion data
		vector<vector<std::byte>>&& rows, vector<SimpleProvenance>&& prov, std::size_t pruned) {
	std::size_t survivor_size = rows.size();
	std::size_t edge_count = prov.size();

	auto selsert_result = selsert_gadget_by_data(conn, std::move(rows));
	const vector<std::uint64_t>& local_to_global = selsert_result.local_to_global;
	const vector<std::uint64_t>& novel_global_ids = selsert_result.novel_global_ids;
	std::size_t novel_gadgets_size = novel_global_ids.size();

	//Mirror is undirected, so we've also finished for any new gadgets.
	input_gids.insert(input_gids.end(), novel_global_ids.begin(), novel_global_ids.end());
	std::sort(input_gids.begin(), input_gids.end());
	vector<pair<std::uint64_t, std::uint64_t>> completed_ranges = maximal_ranges(std::move(input_gids));

	//We have to sort the edge's vertices after remapping.  That means our check
	//for reusing the provs is stricter.  We don't need to deduplicate due "on
	//conflict do nothing", but it is probably faster to do so if it saves us a
	//database round-trip.
	if (std::all_of(local_to_global.begin(), local_to_global.end(), fits_in<decltype(SimpleProvenance::output1)>()) &&
			//p.input1 also fits in output1 (no clean way without a lambda)
			std::all_of(prov.begin(), prov.end(), [](const SimpleProvenance& p) {
				return p.input1 <= std::numeric_limits<decltype(SimpleProvenance::output1)>::max();
			})) {
		for (SimpleProvenance& p : prov) {
			p.output1 = static_cast<std::uint32_t>(local_to_global[p.output1]);
			if (p.input1 > p.output1) {
				//We can't just swap because they aren't the same size.
				std::size_t x = p.input1;
				p.input1 = p.output1;
				p.output1 = static_cast<std::uint32_t>(x);
			}
		}
		std::sort(prov.begin(), prov.end());
		prov.erase(std::unique(prov.begin(), prov.end()), prov.end());

		retry_db_operation([&]() {
			transaction trans(conn);
			batch_parameterized(conn, trans, build_insert_mirror_edges_query, 65535/3, std::move(prov));
			batch_parameterized(conn, trans, build_insert_mirror_completion_query, 65535/2, std::move(completed_ranges));
			trans.commit();
			return nullptr;
		}, 10, "commit_mirror_result inserting provs");
	} else {
		//We use larger types than necessary because pqxx doesn't want to string/unstring uint8_t.
		vector<std::tuple<uint64_t, uint64_t, std::uint16_t>> edges;
		for (const SimpleProvenance& p : prov) {
			uint64_t output = local_to_global[p.output1];
			edges.emplace_back(std::min(p.input1, output), std::max(p.input1, output), p.canonicalizePermutation);
		}
		std::sort(edges.begin(), edges.end());
		edges.erase(std::unique(edges.begin(), edges.end()), edges.end());

		retry_db_operation([&]() {
			transaction trans(conn);
			batch_parameterized(conn, trans, build_insert_mirror_edges_query, 65535/3, std::move(edges));
			batch_parameterized(conn, trans, build_insert_mirror_completion_query, 65535/2, std::move(completed_ranges));
			trans.commit();
			return nullptr;
		}, 10, "commit_mirror_result inserting edges");
	}
	return {pruned, survivor_size - novel_gadgets_size, novel_gadgets_size, edge_count};
}

DatabaseOperationStatistics do_mirror_db(vector<std::uint64_t> input_gids) {
	pqxx::connection conn(g_database_connect_string);
	vector<pair<std::uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(conn, input_gids);
	Finisher<SimpleProvenance> outputs = do_mirror(std::move(inputs));
	return commit_mirror_result(conn, std::move(input_gids), std::move(outputs.rows_).values_container(),
			std::move(outputs.prov_), outputs.pruned_);
}



DatabaseOperationStatistics do_batch_combine_commit(vector<vector<std::byte>> rows, vector<CombineProvenance> prov, std::size_t pruned) {
	pqxx::connection conn(g_database_connect_string);
	return commit_combine_result(conn, std::move(rows), std::move(prov), pruned);
}
DatabaseOperationStatistics do_batch_connect_commit(vector<uint64_t> input_gids, vector<vector<std::byte>> rows,
		vector<ConnectProvenance> prov, std::size_t pruned) {
	pqxx::connection conn(g_database_connect_string);
	return commit_connect_result(conn, std::move(input_gids), std::move(rows), std::move(prov), pruned);
}
DatabaseOperationStatistics do_batch_close_commit(vector<uint64_t> input_gids, vector<vector<std::byte>> rows,
		vector<SimpleProvenance> prov, std::size_t pruned) {
	pqxx::connection conn(g_database_connect_string);
	return commit_close_result(conn, std::move(input_gids), std::move(rows), std::move(prov), pruned);
}
DatabaseOperationStatistics do_batch_mirror_commit(vector<uint64_t> input_gids, vector<vector<std::byte>> rows,
		vector<SimpleProvenance> prov, std::size_t pruned) {
	pqxx::connection conn(g_database_connect_string);
	return commit_mirror_result(conn, std::move(input_gids), std::move(rows), std::move(prov), pruned);
}



/**
 * @return a string containing various information about this worker
 */
std::string do_ping() {
	//maybe information about the parent process (might be socat)
	//hostname
	//try to get information about stdin/stdout if they're sockets
	//information about slurm environment variables (if any)
	return "pong";
}



const std::pair<string_view, handler_ptr> handlers[] = {
	{"ping"sv, &handler_adapter<do_ping>},

//	{"canonicalize"sv, &handler_adapter<canonicalize_from_slls>},

	{"connect-db"sv, &handler_adapter<do_connect_db>},
	{"combine-db"sv, &handler_adapter<do_combine_db>},
	{"close-db"sv, &handler_adapter<do_close_db>},
	{"mirror-db"sv, &handler_adapter<do_mirror_db>},

	{"batch-combine"sv, &handler_adapter<do_batch_combine>},
	{"batch-combine-commit"sv, &handler_adapter<do_batch_combine_commit>},
	{"batch-connect"sv, &handler_adapter<do_batch_connect>},
	{"batch-connect-commit"sv, &handler_adapter<do_batch_connect_commit>},
	{"batch-close"sv, &handler_adapter<do_batch_close>},
	{"batch-close-commit"sv, &handler_adapter<do_batch_close_commit>},
	{"batch-mirror"sv, &handler_adapter<do_batch_mirror>},
	{"batch-mirror-commit"sv, &handler_adapter<do_batch_mirror_commit>},
};



vector<char> exhaust_stdin() {
	vector<char> data;
	data.resize(4096, 0);
	size_t index = 0;
	while (true) {
		size_t count = data.size() - index;
		size_t bytes_read = std::fread(&data[index], sizeof(unsigned char), count, stdin);
		if (bytes_read != count) {
			if (std::feof(stdin)) {
				data.resize(index + bytes_read);
				return data;
			}
			if (std::ferror(stdin)) {
				auto savederrno = errno;
				fmt::print(stderr, "error reading from stdin: {} ({}), after reading {} before and {} last\n",
						strerror(savederrno), savederrno, index, bytes_read);
				std::exit(1);
			}
		} else {
			index += bytes_read;
			data.resize(std::min(data.size() * 2, data.size() + 1024*1024*1024), 0);
		}
	}
	//We always return out of the loop or exit(1).
}

msgpack::object_handle read_input() {
	vector<char> input = exhaust_stdin();
	//TODO: optionally decompress the message (based on a command-line option)
	return msgpack::unpack(input.data(), input.size());
}

void write_output(const void* data, size_t size) {
	//TODO: optional compression based on command-line option
	size_t index = 0;
	while (index < size) {
		size_t count = size - index;
		size_t bytes_written = std::fwrite(reinterpret_cast<const char*>(data) + index, sizeof(char), count, stdout);
		if (bytes_written != count) {
			if (std::ferror(stdout)) {
				auto savederrno = errno;
				fmt::print(stderr, "error writing to stdout: {} ({}), after writing {} before and {} last\n",
						strerror(savederrno), savederrno, index, bytes_written);
				std::exit(1);
			} else
				//Unusual enough to be worth remarking about.
				fmt::print(stderr, "Short fwrite? index {}, count {}, wrote {} (short by {})\n",
						index, count, bytes_written, (count - bytes_written));
		}
		index += bytes_written;
	}
	std::fflush(stdout);
}

int main(int argc, char* argv[]) { //genbuild {'entrypoint': True, 'ldflags': '-lpqxx -lpq -llmdb -lyaml-cpp'}
	std::string_view db_user = "jbosboom", db_pass = "", db_host = "127.0.0.1",
			db_port = "5432", db_name = "togglesearch";
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
		else {
			fmt::print(stderr, "ERROR: unknown option {}\n", argv[i]);
			std::exit(2);
		}
	}

	g_database_connect_string = format_connect_string(db_user, db_pass, db_host, db_port, db_name);

	simple_buffer response = dispatch(read_input(), std::begin(handlers), std::end(handlers));
	write_output(response.data(), response.size());

	return 0;
}