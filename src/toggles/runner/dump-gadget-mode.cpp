#include "precompiled.hpp"
#include "../toggles-shared.hpp"
#include "gadget-encoding.hpp"
#include "proj_compare.hpp"

using std::vector;
using std::uint64_t;
using std::pair;
using encoding::GadgetEdge;

int dump_gadget_mode(std::string_view db_path, const vector<std::string_view>& gadget_spec) {
	GadgetSet gadget_set = parse_gid_specs(gadget_spec);

	lmdb::env env = lmdb::env::create();
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(std::string(db_path).c_str(), MDB_NORDAHEAD);

	lmdb::dbi gadget_hashtable, gadget_index, names_db, completions, close_edges, mirror_edges;
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		names_db = lmdb::dbi::open(txn, "names");
		completions = lmdb::dbi::open(txn, "completions");
		close_edges = lmdb::dbi::open(txn, "edges-close");
		mirror_edges = lmdb::dbi::open(txn, "edges-mirror");
		txn.commit();
	}

	vector<uint64_t> all_ids = collect_initial_gadget_set(env, gadget_hashtable, gadget_index, names_db, gadget_set);
	vector<pair<uint64_t, vector<std::byte>>> all_data = select_gadget_id_to_data(env, gadget_hashtable, gadget_index, all_ids);
	std::sort(all_data.begin(), all_data.end(), proj_less<0>());
	for (const pair<uint64_t, vector<std::byte>>& gadget : all_data) {
		encoding::Stats stats = encoding::stats(gadget.second.data());
		fmt::print("{}: {} locations, {} states, {} uedges, {} dedges, {} components, {} bytes\n",
				gadget.first, stats.locations, stats.states, stats.undirected_edges,
				stats.directed_edges, stats.components, gadget.second.size());

		//TODO: inverse names lookup (maybe sync mode should generate that map,
		//as we're using it in a couple places now)

		pair<vector<GadgetEdge>, vector<GadgetEdge>> slls = encoding::decode_to_slls(gadget.second.data(), gadget.second.size());
		//TODO: print nop edges on their own line (filter them from uedges)
		if (slls.first.size())
			fmt::print("  undirected edges: {}\n", slls.first);
		if (slls.second.size())
			fmt::print("  directed edges: {}\n", slls.second);

		vector<pair<uint64_t, uint64_t>> singleton = {{gadget.first, gadget.first+1}};
		vector<pair<uint64_t, uint64_t>> close_target = follow_edges<SimpleEdge>(env, close_edges, singleton);
		if (!close_target.empty())
			fmt::print("  closes to {}\n", close_target.front().first);
		//TODO: the close edges table is small enough to search the values and
		//print "closed from"
		vector<pair<uint64_t, uint64_t>> mirror_target = follow_edges<SimpleEdge>(env, mirror_edges, singleton);
		if (!mirror_target.empty())
			fmt::print("  mirrors to {}\n", mirror_target.front().first);

		//TODO: list outgoing connect and combine edges (we can't usefully do the inverse here)
	}

	return 0;
}