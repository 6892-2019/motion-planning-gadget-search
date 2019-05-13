#include "precompiled.hpp"
#include "automaton.hpp"
#include "canonicalize.hpp"
#include "ops.hpp"
#include "provenance.hpp"
#include "../database.hpp"
#include "../rpc.hpp"
#include "../toggles-shared.hpp"
#include "stringutils.hpp"
#include "intervals.hpp"
#include "hopscotch/hopscotch_set.h"
#include "hopscotch/hopscotch_map.h"
#include "tsl/ordered_set.h"
#include "tsl/ordered_map.h"
#include <boost/container/static_vector.hpp>
#include "msgpack.hpp"
#include "farmhash/farmhash.h"
#include "lmdb++.h"
#include <yaml-cpp/yaml.h>
//#include <pqxx/pqxx>
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

//namespace {
//vector<uint64_t> extract_first(const vector<pair<uint64_t, vector<std::byte>>>& inputs) {
//	vector<uint64_t> input_gids;
//	input_gids.reserve(inputs.size());
//	for (const auto& p : inputs)
//		input_gids.push_back(p.first);
//	return input_gids;
//}
//}

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



struct SelsertGadgetByDataResult {
	//TODO: local_to_global could be a dynarray to allow allocating without initializing it
	vector<std::uint64_t> local_to_global;
	pair<uint64_t, uint64_t> novel_global_ids;
	std::size_t early_pruned, late_pruned;
	std::size_t novel_size() const {
		return novel_global_ids.second - novel_global_ids.first;
	}
};
/**
 * Returns the global gadget id of each of the given rows, inserting the row if
 * not already present.  The vector of ids matches the order of the rows.
 */
SelsertGadgetByDataResult selsert_gadget_by_data(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, vector<vector<std::byte>>&& gadgets) {
	SelsertGadgetByDataResult ret;
	ret.local_to_global.resize(gadgets.size(), std::numeric_limits<std::uint64_t>::max());
	ret.early_pruned = ret.late_pruned = 0;

	vector<std::uint64_t> hashes(gadgets.size(), std::numeric_limits<std::uint64_t>::max());
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		for (std::size_t i = 0; i < gadgets.size(); ++i) {
			std::string_view data(reinterpret_cast<const char*>(gadgets[i].data()), gadgets[i].size());
			hashes[i] = farmhash::Fingerprint64(data.data(), data.size());
			std::string_view existing;
			while (gadget_hashtable.get(txn, lmdb::to_sv(hashes[i]), existing))
				if (data == existing.substr(0, existing.size()-8)) { //skip the appended ID (remove_suffix is a mutator)
					gadgets[i].clear(); //don't bother with shrink-to-fit as we won't touch that memory again anyway
					hashes[i] = std::numeric_limits<std::uint64_t>::max();
					ret.local_to_global[i] = lmdb::from_sv<std::uint64_t>(existing.substr(existing.size()-8));
					++ret.early_pruned;
					break;
				} else
					++hashes[i]; //linear probing
			//hashes[i] now contains the proposed insert point for absent gadgets
			//and max() for present ones (so they'll be sorted to the end).  It's
			//fine if some gadget actually hashes to max(), we'll still check it
			//later, and those may not be the final insert positions anyway.
		}
		txn.commit();
	}

	//Inserting in sorted order is faster (though as this is a hash table, we'll
	//probably end up rewriting the whole tree anyway).  But we also need to
	//fill in local_to_global, so we need to remember the initial order.
	vector<unsigned int> indices(gadgets.size());
	std::iota(indices.begin(), indices.end(), 0u);
	std::sort(indices.begin(), indices.end(), [&hashes, &gadgets](unsigned int a, unsigned int b) {
		if (hashes[a] != hashes[b])
			return hashes[a] < hashes[b];
		//We want to sort all the empty gadgets (which were present) to the end
		//so we don't have to check them during the write transaction.  Those
		//gadgets' hashes were set to max(), but some gadget might have hashed
		//to max(), and we need to check the absent one before exiting.  Thus we
		//sort by reverse-length.  It's probably cheaper to always do this for
		//equal hashes than to test for max() specifically.
		return gadgets[a].size() > gadgets[b].size();
	});

	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr);
		{
			//We need an extra scope to ensure the cursor is destroyed before the
			//transaction commits or aborts.
			lmdb::cursor index_cur = lmdb::cursor::open(txn, gadget_index);
			std::string_view last_id_view;
			std::uint64_t last_id;
			if (index_cur.get(last_id_view, MDB_LAST))
				last_id = lmdb::from_sv<std::uint64_t>(last_id_view);
			else
				last_id = 0; //empty index; starting at 0 means first key will be 1
			ret.novel_global_ids.first = ret.novel_global_ids.second = last_id + 1;

			//We'll try to insert at the proposed insert point, but some other
			//transaction may have written there as well (or ourselves if we have
			//duplicates), so we may still need to linear-probe here.
			for (unsigned int i : indices) {
				if (gadgets[i].empty()) {
					//Per the sort above, all remaining gadgets are empty, so we are done.
					//It's awkward to write an assertion here -- the relevant
					//span is over the rest of the *indices*, not gadgets.begin()+i
					//to gadgets.end().
					break;
				}

				std::string_view data(reinterpret_cast<const char*>(gadgets[i].data()), gadgets[i].size());
				//Constructing a string_view to nullptr is technically undefined
				//behavior.  We have to const_cast it later again anyway, so
				//string_view is just the wrong abstraction for MDB_RESERVE.
				std::string_view existing(nullptr, gadgets[i].size()+8);
				while (!gadget_hashtable.put(txn, lmdb::to_sv(hashes[i]), existing, MDB_NOOVERWRITE | MDB_RESERVE))
					if (data == existing.substr(0, existing.size()-8)) { //did someone insert in the meantime?
						ret.local_to_global[i] = lmdb::from_sv<std::uint64_t>(existing.substr(existing.size()-8));
						++ret.late_pruned;
						goto labeled_continue;
					} else {
						++hashes[i]; //linear probing
						existing = std::string_view(nullptr, gadgets[i].size()+8);
					}
				//We successfully inserted.  Copy into the reserved space.
				std::memcpy(const_cast<char*>(existing.begin()), gadgets[i].data(), gadgets[i].size());
				++last_id;
				std::memcpy(const_cast<char*>(existing.begin()) + gadgets[i].size(), &last_id, sizeof(last_id));
				if (!index_cur.put(lmdb::to_sv(last_id), lmdb::to_sv(hashes[i]), MDB_NOOVERWRITE | MDB_APPEND))
					throw std::runtime_error(fmt::format("failed to append to index: index {} key {} hash {}",
							i, last_id, hashes[i]));
				ret.local_to_global[i] = last_id;
				ret.novel_global_ids.second++;

				labeled_continue: ;
			}
		}
		txn.commit();
	}
	//Should have filled in everything now.
	assert(std::find(ret.local_to_global.begin(), ret.local_to_global.end(),
			std::numeric_limits<std::uint64_t>::max()) == ret.local_to_global.end());
	//We may have modified gadgets, so it's not safe for the caller to use anyway.
	vector<vector<std::byte>> ensure_memory_is_freed(std::move(gadgets));
	return ret;
}

vector<pair<std::uint64_t, std::uint64_t>> maximal_ranges(vector<std::uint64_t>&& data) {
	vector<std::uint64_t> ensure_memory_is_freed(std::move(data));
	return maximal_intervals(ensure_memory_is_freed.begin(), ensure_memory_is_freed.end());
}

static std::string g_database_path;

//DatabaseOperationStatistics commit_combine_result(pqxx::connection& conn, vector<vector<std::byte>>&& rows,
//		vector<CombineProvenance>&& prov, std::size_t pruned) {
//	std::size_t survivor_size = rows.size(), edge_count = prov.size();
//	auto selsert_result = selsert_gadget_by_data(conn, std::move(rows));
//	const vector<std::uint64_t>& local_to_global = selsert_result.local_to_global;
//	//We only need the size here, so clean up.
//	std::size_t novel_gadgets_size = selsert_result.novel_global_ids.size();
//	selsert_result.novel_global_ids.clear();
//	selsert_result.novel_global_ids.shrink_to_fit();
//
//	//We want to insert edges in sorted order to reduce serialization failures.
//	//We can use the Provenance if the new ids fit; otherwise we have to copy.
//	if (std::all_of(local_to_global.begin(), local_to_global.end(), fits_in<decltype(CombineProvenance::output1)>())) {
//		for (CombineProvenance& p : prov)
//			p.output1 = static_cast<std::uint32_t>(local_to_global[p.output1]);
//		std::sort(prov.begin(), prov.end());
//		batch_parameterized(conn, build_insert_combine_edges_query, 65535/7, std::move(prov), "commit_combine_result inserting provs");
//	} else {
//		//We use larger types than necessary because pqxx doesn't want to string/unstring uint8_t.
//		vector<std::tuple<uint64_t, uint64_t, uint64_t, std::uint16_t, std::uint16_t, std::uint16_t, std::uint16_t>> edges;
//		for (const CombineProvenance& p : prov)
//			edges.emplace_back(p.input1, p.input2, local_to_global[p.output1], p.splice, p.rotation, p.connectPoint, p.canonicalizePermutation);
//		std::sort(edges.begin(), edges.end());
//		batch_parameterized(conn, build_insert_combine_edges_query, 65535/7, std::move(edges), "commit_combine_result inserting tuples");
//	}
//	return {pruned, survivor_size - novel_gadgets_size, novel_gadgets_size, edge_count};
//}
//
//DatabaseOperationStatistics do_combine_db(vector<std::uint64_t> left_gids, vector<std::uint64_t> right_gids, unsigned int precision) {
//	vector<std::uint64_t> input_gids;
//	input_gids.reserve(left_gids.size() + right_gids.size());
//	input_gids.insert(input_gids.end(), left_gids.begin(), left_gids.end());
//	input_gids.insert(input_gids.end(), right_gids.begin(), right_gids.end());
//	std::sort(input_gids.begin(), input_gids.end());
//	input_gids.erase(std::unique(input_gids.begin(), input_gids.end()), input_gids.end());
//
//	pqxx::connection conn(g_database_connect_string);
//
//	//TODO: select_gadget_id_to_data should be templated on the result container so we can directly build this map
//	vector<pair<std::uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(conn, input_gids);
//	tsl::hopscotch_map<std::uint64_t, vector<std::byte>, farmhash_hash> map;
//	for (pair<std::uint64_t, vector<std::byte>>& p : inputs)
//		map.try_emplace(p.first, std::move(p.second));
//	Finisher outputs = do_combine(std::move(map), left_gids, right_gids, precision);
//	return commit_combine_result(conn, std::move(outputs.rows_).values_container(), std::move(outputs.prov_), outputs.pruned_);
//}


DatabaseOperationStatistics commit_connect_result(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, lmdb::dbi& edges, lmdb::dbi& completions,
		vector<pair<uint64_t, uint64_t>>&& input_intervals,	vector<vector<std::byte>>&& gadgets,
		vector<ConnectProvenance>&& prov, std::size_t pruned) {
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
				--edge_count; //we didn't actually add this edge, don't count it
			}
			i = j;
		}
	}

	union_completion(env, txn, completions, "connect", input_intervals);
	txn.commit();

	return {pruned, survivor_size - selsert_result.novel_size(), selsert_result.novel_size(), edge_count};
}

DatabaseOperationStatistics do_connect_db(vector<pair<uint64_t, uint64_t>> input_intervals) {
	lmdb::env env = lmdb::env::create(); //TODO: flags?
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(g_database_path.c_str()); //TODO: flags?
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
			std::move(outputs.prov_), outputs.pruned_);
}

DatabaseOperationStatistics commit_simple_result(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, lmdb::dbi& edges, lmdb::dbi& completions,
		std::string_view completion_kind, bool idempotent,
		vector<pair<uint64_t, uint64_t>>&& input_intervals,	vector<vector<std::byte>>&& gadgets,
		vector<SimpleProvenance>&& prov, std::size_t pruned) {
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

	return {pruned, survivor_size - selsert_result.novel_size(), selsert_result.novel_size(), edge_count};
}

DatabaseOperationStatistics commit_close_result(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, lmdb::dbi& close_edges, lmdb::dbi& completions,
		vector<pair<uint64_t, uint64_t>>&& input_intervals,	vector<vector<std::byte>>&& gadgets,
		vector<SimpleProvenance>&& prov, std::size_t pruned) {
	return commit_simple_result(env, gadget_hashtable, gadget_index, close_edges, completions, "close", true,
			std::move(input_intervals), std::move(gadgets), std::move(prov), pruned);
}

//extracted for the benefit of sync_mode
DatabaseOperationStatistics do_close_db0(vector<pair<uint64_t, uint64_t>> input_intervals,
		lmdb::env& env, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index, lmdb::dbi& close_edges,
		lmdb::dbi& completions) {
	vector<pair<uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(
			env, gadget_hashtable, gadget_index, input_intervals);
	Finisher<SimpleProvenance> outputs = do_close(std::move(inputs));
	return commit_close_result(env, gadget_hashtable, gadget_index, close_edges, completions,
			std::move(input_intervals), std::move(outputs.rows_).values_container(),
			std::move(outputs.prov_), outputs.pruned_);
}

DatabaseOperationStatistics do_close_db(vector<pair<uint64_t, uint64_t>> input_intervals) {
	lmdb::env env = lmdb::env::create(); //TODO: flags?
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(g_database_path.c_str()); //TODO: flags?
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
		vector<SimpleProvenance>&& prov, std::size_t pruned) {
	return commit_simple_result(env, gadget_hashtable, gadget_index, mirror_edges, completions, "mirror", false,
			std::move(input_intervals), std::move(gadgets), std::move(prov), pruned);
}

//TODO: There's a lot of duplication between close and mirror (and maybe also
//connect in the future).  Can we parameterize/templatize them together?

//extracted for the benefit of sync_mode
DatabaseOperationStatistics do_mirror_db0(vector<pair<uint64_t, uint64_t>> input_intervals,
		lmdb::env& env, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index, lmdb::dbi& mirror_edges,
		lmdb::dbi& completions) {
	vector<pair<uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(
			env, gadget_hashtable, gadget_index, input_intervals);
	Finisher<SimpleProvenance> outputs = do_mirror(std::move(inputs));
	return commit_mirror_result(env, gadget_hashtable, gadget_index, mirror_edges, completions,
			std::move(input_intervals), std::move(outputs.rows_).values_container(),
			std::move(outputs.prov_), outputs.pruned_);
}

DatabaseOperationStatistics do_mirror_db(vector<pair<uint64_t, uint64_t>> input_intervals) {
	lmdb::env env = lmdb::env::create(); //TODO: flags?
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(g_database_path.c_str()); //TODO: flags?
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

//	{"connect-db"sv, &handler_adapter<do_connect_db>},
//	{"combine-db"sv, &handler_adapter<do_combine_db>},
//	{"close-db"sv, &handler_adapter<do_close_db>},
//	{"mirror-db"sv, &handler_adapter<do_mirror_db>},
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



////TODO: make this SCCs::find
unsigned int component_for_state(SCCs sccs, AutomatonBase::state_type state) {
	for (unsigned int c : xrange(sccs.size()))
		for (unsigned int s : make_range_for_pair(sccs.begin(c), sccs.end(c))) //TODO: add SCCs::range (name TBD)
			if (s == state)
				return c;
	//TODO: add an SCCs method giving the number of states, so we can report here
	throw std::logic_error(fmt::format("component_for_state failed: {} {}", state, sccs.size()));
}

/**
 * Canonicalizes a gadget in SLLS format, returning in database row format.
 * Intended for use when loading human-readable gadget definitions into the
 * database.
 */
vector<pair<vector<std::byte>, optional<vector<std::byte>>>> canonicalize_from_slls(
		vector<encoding::GadgetEdge> uedges, vector<encoding::GadgetEdge> dedges) {
	unique_ptr<WorkingAutomaton> a = encoding::inflate_slls(uedges, dedges);
	vector<std::byte> row = encoding::encode(*a);
	//just computed these in encoding::encode, could try to save them
	SCCs sccs = automaton::find_components(*a);
	auto activealpha = a->active_alphabet_size();

	vector<pair<unique_ptr<WorkingAutomaton>, vector<std::byte>>> normals;
	normals.emplace_back(std::move(a), std::move(row));
	//When initializing the database with named gadgets, we want to try all
	//initial states in the initial connected component.
	unsigned int initial_component = component_for_state(sccs, 0);
	for (auto state : make_range_for_pair(sccs.begin(initial_component), sccs.end(initial_component))) //TODO: SCCs::range
		if (normals.front().first->accept(state)) {
			unique_ptr<WorkingAutomaton> p = normals.front().first->clone();
			p->swapStateNumbers(0, state);
			canonicalize(*p, activealpha, false); //no mirroring
			row = encoding::encode(*p);
			normals.emplace_back(std::move(p), std::move(row));
		}
	std::sort(normals.begin(), normals.end(), [](const auto& l, const auto& r) {return l.second < r.second;});
	normals.erase(std::unique(normals.begin(), normals.end(),
			[](const auto& l, const auto& r) {return l.second == r.second;}), normals.end());

	//It's plausible that only a subset of the states are chiral.
	vector<pair<unique_ptr<WorkingAutomaton>, vector<std::byte>>> mirrors;
	for (const auto& n : normals) {
		//We don't return the rotation, but we won't add a mirror provenance edge
		//either, so the usual mirror machinery will fill it in later.  We just
		//need the gadget up front so we can give it an appropriate name.
		unique_ptr<WorkingAutomaton> p = mirror(*n.first).first;
		row = encoding::encode(*p);
		mirrors.emplace_back(std::move(p), std::move(row));
	}

	//Mirror order is the same as the normal order (mirrors aren't sorted).
	//We're also just taking the first enantiomorph as 'normal', rather than
	//the lexicographically lesser one.
	vector<pair<vector<std::byte>, optional<vector<std::byte>>>> retval;
	for (auto i : xrange(normals.size()))
		if (normals[i].second != mirrors[i].second)
			retval.emplace_back(std::move(normals[i].second), std::move(mirrors[i].second));
		else
			retval.emplace_back(std::move(normals[i].second), nullopt);
	return retval;
}

namespace YAML {
template<>
struct convert<encoding::GadgetEdge> {
	static Node encode(const encoding::GadgetEdge& e) {
		Node node;
		node.push_back(e.start);
		node.push_back(e.from);
		node.push_back(e.to);
		node.push_back(e.end);
		return node;
	}
	static bool decode(const Node& node, encoding::GadgetEdge& e) {
		if (!node.IsSequence() || node.size() != 4)
			return false;
		e.start = node[0].as<unsigned int>();
		e.from = node[1].as<unsigned int>();
		e.to = node[2].as<unsigned int>();
		e.end = node[3].as<unsigned int>();
		return true;
	}
};
}

int sync_mode(std::string_view db_path, const vector<std::string_view>& files) {
	vector<vector<std::byte>> canonicals;
	tsl::ordered_map<std::string, vector<std::size_t>> naming;
	for (std::string_view filename : files) {
		YAML::Node toplevel = YAML::LoadFile(std::string(filename));
		YAML::Node gadgets = toplevel["gadgets"];
		for (auto it = gadgets.begin(); it != gadgets.end(); ++it) {
			std::string gadget_name = it->first.as<std::string>();
			vector<encoding::GadgetEdge> uedges, dedges;
			if (it->second["uedges"])
				uedges = it->second["uedges"].as<vector<encoding::GadgetEdge>>();
			if (it->second["dedges"])
				dedges = it->second["dedges"].as<vector<encoding::GadgetEdge>>();
			if (uedges.empty() && dedges.empty()) {
				fmt::print(stderr, "no edges for gadget {} in {}\n", gadget_name, filename);
				return 1;
			}

			vector<pair<vector<std::byte>, optional<vector<std::byte>>>> morphs =
					canonicalize_from_slls(std::move(uedges), std::move(dedges));
			//We are chiral if any state has enantiomorphs.
			bool chiral = std::any_of(morphs.begin(), morphs.end(), [](const auto& q){return q.second.has_value();});
			std::size_t group_start = canonicals.size();
			if (morphs.size() == 1 && !chiral) {
				canonicals.push_back(std::move(morphs[0].first));
				//singleton group -- we'll install the usual group name later
			} else if (morphs.size() == 1 && chiral) {
				auto& p = morphs[0];
				naming["r-"+gadget_name] = {canonicals.size()};
				canonicals.push_back(std::move(p.first));
				naming["s-"+gadget_name] = {canonicals.size()};
				canonicals.push_back(std::move(*p.second));
			} else if (morphs.size() > 1 && !chiral)
				for (std::size_t i = 0; i < morphs.size(); ++i) {
					naming[fmt::format("{}-{}", gadget_name, i)] = {canonicals.size()};
					canonicals.push_back(std::move(morphs[i].first));
				}
			else if (morphs.size() > 1 && chiral)
				for (std::size_t i = 0; i < morphs.size(); ++i) {
					naming[fmt::format("r-{}-{}", gadget_name, i)] = {canonicals.size()};
					canonicals.push_back(std::move(morphs[i].first));
					if (morphs[i].second) {
						naming[fmt::format("s-{}-{}", gadget_name, i)] = {canonicals.size()};
						canonicals.push_back(std::move(*morphs[i].second));
					} else
						naming[fmt::format("s-{}-{}", gadget_name, i)] = naming.at(fmt::format("r-{}-{}", gadget_name, i));
				}
			else
				throw std::logic_error("empty morphs somehow?");
			vector<std::size_t> whole_group_indices(canonicals.size() - group_start);
			std::iota(whole_group_indices.begin(), whole_group_indices.end(), group_start);
			naming[gadget_name] = std::move(whole_group_indices);
		}

		YAML::Node aliases = toplevel["aliases"];
		for (auto it = aliases.begin(); it != aliases.end(); ++it) {
			std::string source = it->first.as<std::string>(), target = it->second.as<std::string>();
			if (naming.count(source)) {
				fmt::print(stderr, "alias {} (intended for {}) in {} already names a gadget", source, target, filename);
				return 1;
			}
			if (!naming.count(target)) {
				//An alias can reference another alias, but only if the referent
				//is defined first.
				fmt::print(stderr, "alias target {} (from {}) in {} doesn't name a gadget", target, source, filename);
				return 1;
			}
			naming[source] = naming[target];
		}
	}
	std::size_t canonicals_size = canonicals.size();

	lmdb::env env = lmdb::env::create(); //TODO: flags?
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(std::string(db_path).c_str()); //TODO: flags?
	lmdb::dbi gadget_hashtable, gadget_index, names_db, completions, close_edges, mirror_edges;
	{
		lmdb::txn txn = lmdb::txn::begin(env);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable", MDB_CREATE | MDB_INTEGERKEY);
		//TODO: can we pass MDB_INTEGERDUP without MDB_DUPSORT/FIXED?  We won't
		//have duplicates, but all our keys are binary integers.
		gadget_index = lmdb::dbi::open(txn, "gadget_index", MDB_CREATE | MDB_INTEGERKEY);
		//We could use duplicate integer keys here, but because we aren't adding
		//or removing any keys, it's more convenient for our code to just store
		//byte arrays.  There are few enough names that compression isn't useful.
		names_db = lmdb::dbi::open(txn, "names", MDB_CREATE);

		//We also open some databases we don't use here, just to ensure they
		//exist when the database starts.  Combine-related databases are created
		//on demand (because they are specific to the right operand).

		//The completions database holds keys named "close", "mirror", "connect"
		//and "combine-{}" whose values are an interval list.
		completions = lmdb::dbi::open(txn, "completions", MDB_CREATE);

		mirror_edges = lmdb::dbi::open(txn, "edges-mirror", MDB_CREATE | MDB_INTEGERKEY);
		close_edges = lmdb::dbi::open(txn, "edges-close", MDB_CREATE | MDB_INTEGERKEY);
		lmdb::dbi::open(txn, "edges-combine", MDB_CREATE | MDB_INTEGERKEY);

		//TODO: driver's work-tracking things? or leave those for the driver?
		txn.commit();
	}

	auto selsert_result = selsert_gadget_by_data(env, gadget_hashtable, gadget_index, std::move(canonicals));
	//Punning a bit on this vector: in the map, it's indices into canonicals,
	//but we're about to remap it to gadget ids.
	std::deque<pair<std::string, std::vector<uint64_t>>> sorted_names = std::move(naming).values_container();
	for (auto& p : sorted_names)
		for (std::size_t i = 0; i < p.second.size(); ++i)
			p.second[i] = selsert_result.local_to_global[p.second[i]];
	//TODO: this compare-tupleish-by-nth-element also appears in the driver,
	//and is probably worth elevating to a named utility function/lambda.
	std::sort(sorted_names.begin(), sorted_names.end(), [](const auto& a, const auto& b) {
		return std::get<0>(a) < std::get<0>(b);
	});

	{
		lmdb::txn txn = lmdb::txn::begin(env);
		names_db.drop(txn);
		for (const pair<std::string, vector<std::uint64_t>>& p : sorted_names)
			if (!names_db.put(txn, p.first,
					std::string_view(reinterpret_cast<const char*>(p.second.data()), p.second.size()*sizeof(std::uint64_t)),
					MDB_APPEND | MDB_NOOVERWRITE))
				throw std::runtime_error(fmt::format("failed to insert names {} -> {}", p.first, p.second));
		txn.commit();
	}
	fmt::print("loaded {} gadgets ({} novel) and {} names\n",
			canonicals_size, selsert_result.novel_size(), sorted_names.size());

	//Now close and mirror all gadgets (even non-novel ones) that need it, for
	//the benefit of the reporter.
	std::sort(selsert_result.local_to_global.begin(), selsert_result.local_to_global.end());
	vector<pair<uint64_t, uint64_t>> named_gadgets = maximal_ranges(std::move(selsert_result.local_to_global));
	auto needs_close = filter_completion(env, completions, "close", named_gadgets);
	DatabaseOperationStatistics close_stats = do_close_db0(std::move(needs_close),
			env, gadget_hashtable, gadget_index, close_edges, completions);
	fmt::print("close: {} locally pruned, {} globally pruned, {} discovered, {} edges\n",
			close_stats.pruned_locally, close_stats.pruned_database, close_stats.novel_gadgets, close_stats.edges);

	auto followed_close = follow_edges<SimpleEdge>(env, close_edges, named_gadgets);
	fmt::print("followed close edges to {} gadgets\n", interval_size(followed_close.cbegin(), followed_close.cend()));

	auto desire_mirror = interval_union(named_gadgets.cbegin(), named_gadgets.cend(),
			followed_close.cbegin(), followed_close.cend());
	auto needs_mirror = filter_completion(env, completions, "mirror", desire_mirror);
	DatabaseOperationStatistics mirror_stats = do_mirror_db0(std::move(needs_mirror),
			env, gadget_hashtable, gadget_index, mirror_edges, completions);
	fmt::print("mirror: {} locally pruned, {} globally pruned, {} discovered, {} edges\n",
			mirror_stats.pruned_locally, mirror_stats.pruned_database, mirror_stats.novel_gadgets, mirror_stats.edges);

	return 0;
}



int main(int argc, char* argv[]) { //genbuild {'entrypoint': True, 'ldflags': '-lpqxx -lpq -llmdb -lyaml-cpp'}
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