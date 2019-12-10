#include "precompiled.hpp"
#include "gadget-encoding.hpp"
#include "../select-by-id.hpp"
#include "transform_reduce.hpp"
#include "intervals.hpp"
#include "stringutils.hpp"
#include "bitset.hpp"

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
				fmt::print("{} {} {} {} {}\n", gadget_id, a, b, c, d);
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
	lmdb::dbi gadget_hashtable, gadget_index;
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		txn.commit();
	}

	vector<pair<uint64_t, uint64_t>> every_gadget_ever = {{1, get_current_max_gadget_id(env, gadget_index)+1}};
	vector<vector<pair<uint64_t, uint64_t>>> tasks = interval_chunk(
			every_gadget_ever.begin(), every_gadget_ever.end(), 250000);
	vector<pair<std::string, uint64_t>> names = transform_reduce(std::move(tasks), num_threads,
	[&](vector<pair<uint64_t, uint64_t>> task) {
		vector<pair<std::string, uint64_t>> results;
		vector<pair<uint64_t, vector<std::byte>>> data = select_gadget_id_to_data(env, gadget_hashtable, gadget_index, task);
		while (!data.empty()) {
			encoding::Stats stats = encoding::stats(data.back().second.data());
			pair<vector<GadgetEdge>, vector<GadgetEdge>> edges = encoding::decode_to_slls(
					data.back().second.data(), data.back().second.size());
			name_alternating_leaky_directed_crossovers(data.back().first, stats, edges.first, edges.second, results);
			data.pop_back();
		}
		return results;
	}, [](vector<pair<std::string, uint64_t>>&& left, vector<pair<std::string, uint64_t>>&& right) {
		vector<pair<std::string, uint64_t>> result(std::move(left));
		vector<pair<std::string, uint64_t>> rest(std::move(right)); //ensure memory is released
		result.insert(result.end(), std::move_iterator(rest.begin()), std::move_iterator(rest.end()));
		return result;
	});

	return 0;
}