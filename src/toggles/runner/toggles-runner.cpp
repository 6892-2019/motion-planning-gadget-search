#include "precompiled.hpp"
#include "selsert-gadget-by-data.hpp"
#include "automaton.hpp"
#include "canonicalize.hpp"
#include "ops.hpp"
#include "provenance.hpp"
#include "../rpc.hpp"
#include "../toggles-shared.hpp"
#include "intervals.hpp"
#include "hopscotch/hopscotch_map.h"
#include <boost/container/static_vector.hpp>
#include "msgpack.hpp"
#include "farmhash/farmhash.h"
#include "lmdb++.h"
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




////return type is just to satisfy rpc machinery; we don't special-case for void
////and msgpack can't handle nullptr_t
//[[noreturn]] int do_batch_combine(vector<pair<uint64_t, vector<std::byte>>> inputs,
//		vector<uint64_t> lefts, vector<uint64_t> rights, unsigned int precision) {
//	tsl::hopscotch_map<std::uint64_t, vector<std::byte>, farmhash_hash> map;
//	for (auto& p : inputs)
//		map[p.first] = std::move(p.second);
//	inputs.clear();
//	inputs.shrink_to_fit();
//	Finisher<CombineProvenance> finisher = do_combine(std::move(map), std::move(lefts), std::move(rights), precision);
//	//TODO: We'd like to use the same sequence number here, but we don't have
//	//access.  Introduce a seqno_t "strong typedef" that handler_adapter
//	//recognizes and fills in (in addition to whatever other args are present).
//	simple_buffer buf = pack_call(0, "batch-combine-commit", finisher.rows_.values_container(), finisher.prov_, finisher.pruned_);
//	//By printing to stdout and exiting, we emit an RPC call rather than a
//	//response.  If we threw an exception, though, the dispatcher will generate
//	//an error response as normal, so we'll detect the failure when trying to
//	//commit the results.
//	write_output(buf.data(), buf.size());
//	std::exit(0);
//}

namespace {
//I guess this could be a generalized projection function...
template<class T>
auto extract_first(const vector<T>& inputs) {
	vector<typename std::tuple_element<0, T>::type> firsts;
	firsts.reserve(inputs.size());
	for (const auto& p : inputs)
		firsts.push_back(std::get<0>(p));
	return firsts;
}
}

//[[noreturn]] int do_batch_connect(vector<pair<uint64_t, vector<std::byte>>> inputs) {
//	vector<uint64_t> input_gids = extract_first(inputs);
//	Finisher<ConnectProvenance> finisher = do_connect(std::move(inputs));
//	simple_buffer buf = pack_call(0, "batch-connect-commit", input_gids,
//			finisher.rows_.values_container(), finisher.prov_, finisher.pruned_);
//	write_output(buf.data(), buf.size());
//	std::exit(0);
//}
//
//[[noreturn]] int do_batch_close(vector<pair<uint64_t, vector<std::byte>>> inputs) {
//	vector<uint64_t> input_gids = extract_first(inputs);
//	Finisher<SimpleProvenance> finisher = do_close(std::move(inputs));
//	simple_buffer buf = pack_call(0, "batch-close-commit", input_gids,
//			finisher.rows_.values_container(), finisher.prov_, finisher.pruned_);
//	write_output(buf.data(), buf.size());
//	std::exit(0);
//}
//
//[[noreturn]] int do_batch_mirror(vector<pair<uint64_t, vector<std::byte>>> inputs) {
//	vector<uint64_t> input_gids = extract_first(inputs);
//	Finisher<SimpleProvenance> finisher = do_mirror(std::move(inputs));
//	simple_buffer buf = pack_call(0, "batch-mirror-commit", input_gids,
//			finisher.rows_.values_container(), finisher.prov_, finisher.pruned_);
//	write_output(buf.data(), buf.size());
//	std::exit(0);
//}



static std::string g_database_path;

DatabaseOperationStatistics commit_combine_result(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, vector<pair<uint64_t, lmdb::dbi>>& edge_tables, lmdb::dbi& completions,
		vector<vector<std::byte>>&& gadgets, vector<CombineProvenance>&& prov, std::size_t pruned, std::size_t skipped) {
	std::size_t survivor_size = gadgets.size();
	std::size_t edge_count = prov.size();

	auto selsert_result = selsert_gadget_by_data(env, gadget_hashtable, gadget_index, std::move(gadgets));

	//The loop control below assumes provs isn't empty.  It can only be empty if
	//we skipped all the pairs.
	if (prov.empty()) {
		if (survivor_size != 0 || skipped == 0)
			fmt::print(stderr, "warning: skipping empty combine provs but there were {} gadgets; {} skipped\n", survivor_size, skipped);
		return {skipped, pruned, survivor_size - selsert_result.novel_size(), selsert_result.novel_size(), edge_count};
	}

	//We group by input2, then by input1.  Because we combine against each right
	//operand in sequence, we're usually not already sorted, so we don't check
	//is_sorted like we do in the other commit_*_result functions.
	std::sort(prov.begin(), prov.end(), InputGroupingProvCmp());

	//We commit edges and completions one right operand at a time (so we're only
	//touching one edge table at a time).  This is more to simplify managing
	//cursor lifetime than to keep transactions short, as we'll start another
	//immediately and we can't do anything useful if we're suspended.
	auto input2_sort = [](const CombineProvenance& a, const CombineProvenance& b){return a.input2 < b.input2;};
	for (auto block_first = prov.begin(), block_end = std::lower_bound(block_first, prov.end(), *block_first, input2_sort);
			block_first != prov.end();
			block_end = std::upper_bound(block_first, prov.end(), *block_first, input2_sort)) {
		uint64_t input2 = block_first->input2;
		auto edges_it = std::find_if(edge_tables.begin(), edge_tables.end(),
				[input2](const auto& p){return p.first == input2;});
		if (edges_it == edge_tables.end()) //TODO: unlikely
			throw std::logic_error(fmt::format("combine edge database for right operand {} not found; available databases: {}",
					input2, extract_first(edge_tables)));

		//Combines for a pair of operands always succeed, so completions is just
		//a summary of the edges.  (If we skipped a pair due to insufficient
		//precision, we didn't generate any edges, so we don't record any
		//completions and can come back for that pair later.)
		std::string completions_kind = fmt::format("combine-{}", input2);
		interval_accumulator<uint64_t> comp_input1(512);

		auto txn = lmdb::txn::begin(env);
		{
			lmdb::cursor cur = lmdb::cursor::open(txn, edges_it->second);
			//We could use static_vector with 512 here (16 splice * 16 rotation * 2 connect locations).
			vector<CombineEdge> buf;
			while (block_first != block_end) {
				buf.clear();
				//Find the block sharing the same input1.
				auto subblock_end = block_first;
				while (subblock_end != block_end && block_first->input1 == subblock_end->input1) {
					const CombineProvenance& p = *subblock_end;
					CombineEdge e;
					e.output = selsert_result.local_to_global[p.output1];
					e.splice = p.splice;
					e.rotation = p.rotation;
					e.connectPoint = p.connectPoint;
					e.canonicalizePermutation = p.canonicalizePermutation;
					buf.push_back(e);
					++subblock_end;
				}
				//For canonicalization purposes, sort the edges.  (We have to remap
				//through local_to_global before we can do this.)
				std::sort(buf.begin(), buf.end());
				std::string_view value(reinterpret_cast<char*>(buf.data()), buf.size()*sizeof(CombineEdge));
				if (!cur.put(lmdb::to_sv(block_first->input1), value, MDB_NOOVERWRITE)) {
					if (value.size() == 0 || value.size() % sizeof(CombineEdge) != 0)
						throw std::logic_error(fmt::format("combine edge data for key {}/{} has value length {} (not a multiple of {})",
								block_first->input1, block_first->input2, value.size(), sizeof(CombineEdge)));
					const CombineEdge* first = reinterpret_cast<const CombineEdge*>(value.data());
					const CombineEdge* last = first + value.size() / sizeof(CombineEdge);
					if (!std::equal(buf.cbegin(), buf.cend(), first, last))
						throw std::logic_error(fmt::format("differing combine edges from {}/{}: {} and {}",
								block_first->input1, block_first->input2, buf, make_range_for_pair(first, last)));
					edge_count -= buf.size(); //don't count edges already present
				}
				comp_input1(block_first->input1);
				block_first = subblock_end;
			}
		}
		union_completion(env, txn, completions, completions_kind, std::move(comp_input1).finish());
		txn.commit();
	}

	return {skipped, pruned, survivor_size - selsert_result.novel_size(), selsert_result.novel_size(), edge_count};
}

DatabaseOperationStatistics do_combine_db(vector<pair<uint64_t, uint64_t>> left_intervals,
		vector<std::uint64_t> right_gids, unsigned int precision) {
	lmdb::env env = lmdb::env::create(); //TODO: flags?
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(g_database_path.c_str(), MDB_NORDAHEAD); //TODO: flags?
	lmdb::dbi gadget_hashtable, gadget_index, completions;
	vector<pair<uint64_t, lmdb::dbi>> edge_tables;
	{
		//We may have to create edge databases, though usually the driver will
		//create them for us, so try a read-only txn first.
		try {
			lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
			gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
			gadget_index = lmdb::dbi::open(txn, "gadget_index");
			completions = lmdb::dbi::open(txn, "completions");
			for (uint64_t i : right_gids)
				edge_tables.emplace_back(i, lmdb::dbi::open(txn, fmt::format("edges-combine-{}", i).c_str()));
			txn.commit();
		} catch (lmdb::not_found_error&) {
			lmdb::txn txn = lmdb::txn::begin(env);
			gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
			gadget_index = lmdb::dbi::open(txn, "gadget_index");
			completions = lmdb::dbi::open(txn, "completions");
			for (uint64_t i : right_gids)
				edge_tables.emplace_back(i, lmdb::dbi::open(txn, fmt::format("edges-combine-{}", i).c_str(),
						MDB_CREATE | MDB_INTEGERKEY));
			txn.commit();
		}
	}

	if (!std::is_sorted(right_gids.begin(), right_gids.end()))
		std::sort(right_gids.begin(), right_gids.end());
	vector<pair<uint64_t, uint64_t>> right_intervals = maximal_intervals(right_gids.begin(), right_gids.end());
	vector<pair<uint64_t, uint64_t>> input_intervals = interval_union(
			left_intervals.cbegin(), left_intervals.cend(), right_intervals.cbegin(), right_intervals.cend());
	//TODO: select_gadget_id_to_data should be templated on the result container so we can directly build this map
	vector<pair<std::uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(
			env, gadget_hashtable, gadget_index, std::move(input_intervals));
	tsl::hopscotch_map<std::uint64_t, vector<std::byte>, farmhash_hash> map;
	for (pair<std::uint64_t, vector<std::byte>>& p : inputs)
		map.try_emplace(p.first, std::move(p.second));
	Finisher<CombineProvenance> outputs = do_combine(std::move(map),
			std::move(left_intervals), std::move(right_gids), precision);
	return commit_combine_result(env, gadget_hashtable, gadget_index, edge_tables, completions,
			std::move(outputs.rows_).values_container(), std::move(outputs.prov_), outputs.pruned_, outputs.skipped_);
}


DatabaseOperationStatistics commit_connect_result(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, lmdb::dbi& edges, lmdb::dbi& completions,
		vector<pair<uint64_t, uint64_t>>&& input_intervals,	vector<vector<std::byte>>&& gadgets,
		vector<ConnectProvenance>&& prov, std::size_t pruned, std::size_t skipped) {
	std::size_t survivor_size = gadgets.size();
	std::size_t edge_count = prov.size();

	auto selsert_result = selsert_gadget_by_data(env, gadget_hashtable, gadget_index, std::move(gadgets));

	//Group provs by input1, if they aren't already.
	if (!std::is_sorted(prov.begin(), prov.end(), InputGroupingProvCmp()))
		std::sort(prov.begin(), prov.end(), InputGroupingProvCmp());

	auto txn = lmdb::txn::begin(env);
	{
		lmdb::cursor cur = lmdb::cursor::open(txn, edges);
		boost::container::static_vector<ConnectEdge, 16> buf;

		for (std::size_t i = 0; i < prov.size();) {
			buf.clear();
			//Find the block sharing the same input1.
			std::size_t j = i;
			while (j < prov.size() && prov[i].input1 == prov[j].input1) {
				const ConnectProvenance& p = prov[j];
				ConnectEdge e;
				e.output = selsert_result.local_to_global[p.output1];
				e.connectPoint = p.connectPoint;
				e.canonicalizePermutation = p.canonicalizePermutation;
				buf.push_back(e);
				++j;
			}
			//For canonicalization purposes, sort the edges.  (We have to remap
			//through local_to_global before we can do this.)
			std::sort(buf.begin(), buf.end());
			std::string_view value(reinterpret_cast<char*>(buf.data()), buf.size()*sizeof(ConnectEdge));
			if (!cur.put(lmdb::to_sv(prov[i].input1), value, MDB_NOOVERWRITE)) {
				if (value.size() == 0 || value.size() % sizeof(ConnectEdge) != 0)
					throw std::logic_error(fmt::format("connect edge data for key {} has value length {} (not a multiple of {})",
							prov[i].input1, value.size(), sizeof(ConnectEdge)));
				const ConnectEdge* first = reinterpret_cast<const ConnectEdge*>(value.data());
				const ConnectEdge* last = first + value.size() / sizeof(ConnectEdge);
				if (!std::equal(buf.cbegin(), buf.cend(), first, last))
					throw std::logic_error(fmt::format("differing connect edges from {}: {} and {}",
							prov[i].input1, buf, make_range_for_pair(first, last)));
				edge_count -= buf.size(); //don't count edges already present
			}
			i = j;
		}
	}

	union_completion(env, txn, completions, "connect", input_intervals);
	txn.commit();

	return {skipped, pruned, survivor_size - selsert_result.novel_size(), selsert_result.novel_size(), edge_count};
}

DatabaseOperationStatistics do_connect_db(vector<pair<uint64_t, uint64_t>> input_intervals) {
	lmdb::env env = lmdb::env::create(); //TODO: flags?
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(g_database_path.c_str(), MDB_NORDAHEAD); //TODO: flags?
	lmdb::dbi gadget_hashtable, gadget_index, completions, connect_edges;
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		completions = lmdb::dbi::open(txn, "completions");
		connect_edges = lmdb::dbi::open(txn, "edges-connect");
		txn.commit();
	}

	vector<pair<uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(
			env, gadget_hashtable, gadget_index, input_intervals);
	Finisher<ConnectProvenance> outputs = do_connect(std::move(inputs));
	return commit_connect_result(env, gadget_hashtable, gadget_index, connect_edges, completions,
			std::move(input_intervals), std::move(outputs.rows_).values_container(),
			std::move(outputs.prov_), outputs.pruned_, outputs.skipped_);
}

DatabaseOperationStatistics commit_simple_result(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, lmdb::dbi& edges, lmdb::dbi& completions,
		std::string_view completion_kind, bool idempotent,
		vector<pair<uint64_t, uint64_t>>&& input_intervals,	vector<vector<std::byte>>&& gadgets,
		vector<SimpleProvenance>&& prov, std::size_t pruned, std::size_t skipped) {
	std::size_t survivor_size = gadgets.size();
	std::size_t edge_count = prov.size();

	auto selsert_result = selsert_gadget_by_data(env, gadget_hashtable, gadget_index, std::move(gadgets));

	//We don't need to group provs by input1 (as there's only one close edge
	//from a given gadget), but sorting improves insert performance.
	if (!std::is_sorted(prov.begin(), prov.end(), InputGroupingProvCmp()))
		std::sort(prov.begin(), prov.end(), InputGroupingProvCmp());

	auto txn = lmdb::txn::begin(env);
	{
		lmdb::cursor cur = lmdb::cursor::open(txn, edges);
		SimpleEdge e;
		for (SimpleProvenance& p : prov) {
			e.output = selsert_result.local_to_global[p.output1];
			e.canonicalizePermutation = p.canonicalizePermutation;
			std::string_view value = lmdb::to_sv(e);
			if (!cur.put(lmdb::to_sv(p.input1), value, MDB_NOOVERWRITE)) {
				//Having done some duplicate work is fine, so long as we got the
				//same result.  If not, either there's a bug in the code or the
				//database is corrupt.
				SimpleEdge exist = lmdb::from_sv<SimpleEdge>(value);
				if (e != exist)
					throw std::runtime_error(fmt::format("differing {} edges from {}: {}/{} and {}/{}",
							completion_kind, p.input1, e.output, e.canonicalizePermutation,
							exist.output, exist.canonicalizePermutation));
				--edge_count; //we didn't actually add this edge, don't count it
			}
		}
	}

	if (idempotent) {
		//Any target of a close edge cannot also be the source of a close edge,
		//so we can add completion for them.  (We can't sort local_to_global
		//until we're done with the above loop, though we could copy if we
		//really want to get this out of the transaction.)
		std::sort(selsert_result.local_to_global.begin(), selsert_result.local_to_global.end());
		auto stuff = maximal_intervals(selsert_result.local_to_global.cbegin(), selsert_result.local_to_global.cend());
		input_intervals = interval_union(input_intervals.cbegin(), input_intervals.cend(), stuff.cbegin(), stuff.cend());
	}

	union_completion(env, txn, completions, completion_kind, input_intervals);
	txn.commit();

	return {skipped, pruned, survivor_size - selsert_result.novel_size(), selsert_result.novel_size(), edge_count};
}

DatabaseOperationStatistics commit_close_result(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, lmdb::dbi& close_edges, lmdb::dbi& completions,
		vector<pair<uint64_t, uint64_t>>&& input_intervals,	vector<vector<std::byte>>&& gadgets,
		vector<SimpleProvenance>&& prov, std::size_t pruned, std::size_t skipped) {
	return commit_simple_result(env, gadget_hashtable, gadget_index, close_edges, completions, "close", true,
			std::move(input_intervals), std::move(gadgets), std::move(prov), pruned, skipped);
}

//declared in sync.cpp, extracted for the benefit of sync_mode
DatabaseOperationStatistics do_close_db0(vector<pair<uint64_t, uint64_t>> input_intervals,
		lmdb::env& env, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index, lmdb::dbi& close_edges,
		lmdb::dbi& completions) {
	vector<pair<uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(
			env, gadget_hashtable, gadget_index, input_intervals);
	Finisher<SimpleProvenance> outputs = do_close(std::move(inputs));
	return commit_close_result(env, gadget_hashtable, gadget_index, close_edges, completions,
			std::move(input_intervals), std::move(outputs.rows_).values_container(),
			std::move(outputs.prov_), outputs.pruned_, outputs.skipped_);
}

DatabaseOperationStatistics do_close_db(vector<pair<uint64_t, uint64_t>> input_intervals) {
	lmdb::env env = lmdb::env::create(); //TODO: flags?
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(g_database_path.c_str(), MDB_NORDAHEAD); //TODO: flags?
	lmdb::dbi gadget_hashtable, gadget_index, completions, close_edges;
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		completions = lmdb::dbi::open(txn, "completions");
		close_edges = lmdb::dbi::open(txn, "edges-close");
		txn.commit();
	}
	return do_close_db0(std::move(input_intervals), env, gadget_hashtable, gadget_index, close_edges, completions);
}

DatabaseOperationStatistics commit_mirror_result(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, lmdb::dbi& mirror_edges, lmdb::dbi& completions,
		vector<pair<uint64_t, uint64_t>>&& input_intervals,	vector<vector<std::byte>>&& gadgets,
		vector<SimpleProvenance>&& prov, std::size_t pruned, std::size_t skipped) {
	return commit_simple_result(env, gadget_hashtable, gadget_index, mirror_edges, completions, "mirror", false,
			std::move(input_intervals), std::move(gadgets), std::move(prov), pruned, skipped);
}

//TODO: There's a lot of duplication between close and mirror (and maybe also
//connect in the future).  Can we parameterize/templatize them together?

//declared in sync.cpp, extracted for the benefit of sync_mode
DatabaseOperationStatistics do_mirror_db0(vector<pair<uint64_t, uint64_t>> input_intervals,
		lmdb::env& env, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index, lmdb::dbi& mirror_edges,
		lmdb::dbi& completions) {
	vector<pair<uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(
			env, gadget_hashtable, gadget_index, input_intervals);
	Finisher<SimpleProvenance> outputs = do_mirror(std::move(inputs));
	return commit_mirror_result(env, gadget_hashtable, gadget_index, mirror_edges, completions,
			std::move(input_intervals), std::move(outputs.rows_).values_container(),
			std::move(outputs.prov_), outputs.pruned_, outputs.skipped_);
}

DatabaseOperationStatistics do_mirror_db(vector<pair<uint64_t, uint64_t>> input_intervals) {
	lmdb::env env = lmdb::env::create(); //TODO: flags?
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(g_database_path.c_str(), MDB_NORDAHEAD); //TODO: flags?
	lmdb::dbi gadget_hashtable, gadget_index, completions, mirror_edges;
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		completions = lmdb::dbi::open(txn, "completions");
		mirror_edges = lmdb::dbi::open(txn, "edges-mirror");
		txn.commit();
	}
	return do_mirror_db0(std::move(input_intervals), env, gadget_hashtable, gadget_index, mirror_edges, completions);
}



//DatabaseOperationStatistics do_batch_combine_commit(vector<vector<std::byte>> rows, vector<CombineProvenance> prov, std::size_t pruned) {
//	pqxx::connection conn(g_database_connect_string);
//	return commit_combine_result(conn, std::move(rows), std::move(prov), pruned);
//}
//DatabaseOperationStatistics do_batch_connect_commit(vector<uint64_t> input_gids, vector<vector<std::byte>> rows,
//		vector<ConnectProvenance> prov, std::size_t pruned) {
//	pqxx::connection conn(g_database_connect_string);
//	return commit_connect_result(conn, std::move(input_gids), std::move(rows), std::move(prov), pruned);
//}
//DatabaseOperationStatistics do_batch_close_commit(vector<uint64_t> input_gids, vector<vector<std::byte>> rows,
//		vector<SimpleProvenance> prov, std::size_t pruned) {
//	pqxx::connection conn(g_database_connect_string);
//	return commit_close_result(conn, std::move(input_gids), std::move(rows), std::move(prov), pruned);
//}
//DatabaseOperationStatistics do_batch_mirror_commit(vector<uint64_t> input_gids, vector<vector<std::byte>> rows,
//		vector<SimpleProvenance> prov, std::size_t pruned) {
//	pqxx::connection conn(g_database_connect_string);
//	return commit_mirror_result(conn, std::move(input_gids), std::move(rows), std::move(prov), pruned);
//}



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
//
//	{"batch-combine"sv, &handler_adapter<do_batch_combine>},
//	{"batch-combine-commit"sv, &handler_adapter<do_batch_combine_commit>},
//	{"batch-connect"sv, &handler_adapter<do_batch_connect>},
//	{"batch-connect-commit"sv, &handler_adapter<do_batch_connect_commit>},
//	{"batch-close"sv, &handler_adapter<do_batch_close>},
//	{"batch-close-commit"sv, &handler_adapter<do_batch_close_commit>},
//	{"batch-mirror"sv, &handler_adapter<do_batch_mirror>},
//	{"batch-mirror-commit"sv, &handler_adapter<do_batch_mirror_commit>},
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

int msgpack_mode(std::string_view db_path, std::string_view input_file, std::string_view output_file) {
	if (input_file != "-"sv) {
		if (!std::freopen(std::string(input_file).c_str(), "rb", stdin)) {
			auto savederrno = errno;
			fmt::print(stderr, "unable to reopen stdin from {}: {} ({})\n", input_file, strerror(savederrno), errno);
			std::exit(1);
		}
	}
	if (output_file != "-"sv) {
		if (!std::freopen(std::string(output_file).c_str(), "wbx", stdout)) {
			auto savederrno = errno;
			fmt::print(stderr, "unable to reopen stdout from {}: {} ({})\n", output_file, strerror(savederrno), errno);
			std::exit(1);
		}
	}

	simple_buffer response = dispatch(read_input(), std::begin(handlers), std::end(handlers));
	write_output(response.data(), response.size());
	return 0;
}




//defined in sync.cpp
int sync_mode(std::string_view db_path, const vector<std::string_view>& files);

int main(int argc, char* argv[]) { //genbuild {'entrypoint': True, 'ldflags': '-llmdb -lyaml-cpp'}
	std::string_view mode = "unknown-mode";
	std::string_view db_path = "/bad-db-path-arg", input_file = "-", output_file = "-";
	vector<std::string_view> positionals;
	for (int i = 1; i < argc; ++i) {
		if (i == 1)
			mode = argv[i];
		else if (argv[i] == "--db-path"sv)
			db_path = argv[++i];
		else if (argv[i] == "--input-file"sv || argv[i] == "--input"sv || argv[i] == "-i"sv)
			input_file = argv[++i];
		else if (argv[i] == "--output-file"sv || argv[i] == "--output"sv || argv[i] == "-o"sv)
			output_file = argv[++i];
		else if (argv[i][0] == '-') {
			fmt::print(stderr, "ERROR: unknown option {}\n", argv[i]);
			std::exit(2);
		} else
			positionals.push_back(argv[i]);
	}

	g_database_path = std::string(db_path);

	if (mode == "sync"sv) {
		return sync_mode(db_path, positionals);
	} else if (mode == "msgpack"sv) {
		if (!positionals.empty()) {
			fmt::print(stderr, "ERROR: msgpack mode takes no positional arguments, but some passed: {}\n", positionals);
			std::exit(2);
		}
		return msgpack_mode(db_path, input_file, output_file);
	} else {
		fmt::print(stderr, "ERROR: unknown mode {}\n", mode);
		std::exit(2);
	}
	return 0;
}