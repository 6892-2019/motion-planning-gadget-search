#include "precompiled.hpp"
#include "gadget-encoding.hpp"
#include "../select-by-id.hpp"
#include "transform_reduce.hpp"
#include "intervals.hpp"
#include "stringutils.hpp"
#include "bitset.hpp"
#include <lmdb++.h>

using std::vector;
using std::array;
using std::uint64_t;
using std::pair;
using namespace std::literals::string_view_literals;
using automaton::bitset;
using encoding::GadgetEdge;

bool in_different_circular_partitions(unsigned int alphabet_size, unsigned int a, unsigned int b, unsigned int c, unsigned int d) {
	//TODO: could precompute these bitsets: two bitsets per a/b/length, check that c and d are set in different bitsets
	if (a > b)
		std::swap(a, b);
	bitset<16> set1, set2;
	set1.set(0, alphabet_size+1);
	set2.set(a, b+1);
	set1 &= ~set2;
	return (set1[c] && set2[d]) || (set1[d] && set2[c]);
}

void name_alternating_leaky_directed_crossovers(uint64_t gadget_id, encoding::Stats stats,
		const vector<GadgetEdge>& uedges, const vector<GadgetEdge>& dedges,
		vector<pair<std::string, uint64_t>>& results) {
	using StateLoc = pair<unsigned int, unsigned int>;
	tsl::hopscotch_map<StateLoc, vector<StateLoc>, farmhash_hash> graph;
	for (GadgetEdge e : uedges) {
		if (e.start != e.end && e.from != e.to) { //ignore nop edges if present
			graph[{e.start, e.from}].push_back({e.end, e.to});
			graph[{e.end, e.to}].push_back({e.start, e.from});
		}
	}
	for (GadgetEdge e : dedges)
		graph[{e.start, e.from}].push_back({e.end, e.to});

	for (unsigned int p = 0; p < stats.states; ++p)
		for (unsigned int a = 0; a < stats.locations; ++a) {
			const vector<StateLoc>& endpoints = graph[{p, a}];
			if (endpoints.size() != 1) continue;
			unsigned int q = endpoints[0].first, c = endpoints[0].second;
			if (c == a) continue;
			if (!graph[{q, c}].empty()) continue;
			for (unsigned int b = 0; b < stats.locations; ++b) {
				if (b == a || b == c) continue;
				const vector<StateLoc>& endpoints2 = graph[{q, b}];
				if (endpoints2.size() != 1) continue;
				unsigned int pp = endpoints2[0].first, d = endpoints2[0].second;
				if (d == a || d == b || d == c) continue;
				if (pp != p) continue; //not reusable
				if (!graph[{p, d}].empty()) continue;
				if (!in_different_circular_partitions(stats.locations, a, b, c, d)) continue;
				fmt::print("{} is an alternating leaky directed crossover: {} {}->{}, {} {}->{}\n",
						gadget_id, p, a, c, q, b, d);
				//Including the id ensures the assigned name is unique.  Including
				//the rest helps the human decode what is happening in the gadget.
				//TODO: We probably want to build the gadget where p is state 0
				//for the ease of graph drawing later (so we don't need to work
				//out the pre-traversals that make it a crossover).
				std::string name = fmt::format("z-alternating-leaky-directed-crossover-{}-{}-{}-{}-{}-{}-{}",
						gadget_id, p, a, c, q, b, d);
				results.emplace_back(std::move(name), gadget_id);
			}
		}
}

int scan_mode(std::string_view db_path, vector<std::string_view>& args) {
	unsigned int num_threads = 0;
	for (std::size_t i = 0; i < args.size(); ++i)
		if (args[i] == "--threads"sv)
			num_threads = from_string<unsigned int>(args[++i]);
		else {
			fmt::print(stderr, "ERROR: unknown option {}\n", args[i]);
			std::exit(2);
		}

	if (!num_threads)
		num_threads = std::thread::hardware_concurrency();

	lmdb::env env = lmdb::env::create();
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(std::string(db_path).c_str(), MDB_NORDAHEAD);
	lmdb::dbi gadget_hashtable, gadget_index, names_db;
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		names_db = lmdb::dbi::open(txn, "names");
		txn.commit();
	}

	//TODO: this is copied from predicates.cpp; we may want a select_gadget_*
	//method that returns a tuple<id, hash, data> but we don't want to be
	//making a copy of a whole slice of the table; would have to do load
	//balancing by making lots of chunks but then would have some empty chunks too
	//Scan over gadget_hashtable because we don't care about the encounter order.
	std::size_t chunk_size = std::numeric_limits<std::size_t>::max() / num_threads;
	vector<pair<std::size_t, std::size_t>> hash_ranges; //inclusive!
	for (unsigned int i = 0; i < num_threads; ++i)
		hash_ranges.emplace_back(chunk_size*i, chunk_size*(i+1)-1);
	//Ensure we cover the whole space even if it didn't divide evenly.
	hash_ranges.back().second = std::numeric_limits<std::size_t>::max();

	vector<pair<std::string, uint64_t>> names = transform_reduce(std::move(hash_ranges), num_threads,
	[&](pair<std::size_t, std::size_t> range) {
		vector<pair<std::string, uint64_t>> results;
		auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		lmdb::cursor cur = lmdb::cursor::open(txn, gadget_hashtable);
		std::string_view hash = lmdb::to_sv(range.first), gadget;
		if (cur.get(hash, gadget, MDB_SET_RANGE))
		while (lmdb::from_sv<uint64_t>(hash) <= range.second) {//yes, inclusive
			uint64_t id = lmdb::from_sv<uint64_t>(gadget.substr(gadget.size()-8, gadget.size()));
			gadget.remove_suffix(8);
			const std::byte* gadget_data = reinterpret_cast<const std::byte*>(gadget.data());
			encoding::Stats stats = encoding::stats(gadget_data);
			pair<vector<GadgetEdge>, vector<GadgetEdge>> edges = encoding::decode_to_slls(gadget_data, gadget.size());
			name_alternating_leaky_directed_crossovers(id, stats, edges.first, edges.second, results);
			if (!cur.get(hash, gadget, MDB_NEXT)) break;
		}
		txn.commit();
		return results;
	}, [](vector<pair<std::string, uint64_t>>&& left, vector<pair<std::string, uint64_t>>&& right) {
		vector<pair<std::string, uint64_t>> result(std::move(left));
		vector<pair<std::string, uint64_t>> rest(std::move(right)); //ensure memory is released
		result.insert(result.end(), std::move_iterator(rest.begin()), std::move_iterator(rest.end()));
		return result;
	});

	std::sort(names.begin(), names.end());
	{
		auto txn = lmdb::txn::begin(env);
		for (const pair<std::string, uint64_t>& p : names) {
			std::string_view data(reinterpret_cast<const char*>(&p.second), sizeof(p.second));
			if (!names_db.put(txn, p.first, data, MDB_NOOVERWRITE))
				if (lmdb::from_sv<uint64_t>(data) != p.second)
					throw std::runtime_error(fmt::format("failed to insert names {} -> {}", p.first, p.second));
		}
		txn.commit();
	}

	return 0;
}