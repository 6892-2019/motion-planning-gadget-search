#include "precompiled.hpp"
#include "../toggles-shared.hpp"
#include "../select-by-id.hpp"
#include "../gadget-set.hpp"
#include "gadget-encoding.hpp"
#include "proj_compare.hpp"
#include <hopscotch/hopscotch_map.h>

using std::vector;
using std::uint64_t;
using std::pair;
using encoding::GadgetEdge;

auto invert_names(lmdb::env& env, lmdb::dbi& names) {
	tsl::hopscotch_map<uint64_t, vector<std::string>, farmhash_hash> singletons, groups;
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
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
		if (value.size() == sizeof(uint64_t))
			singletons[lmdb::from_sv<uint64_t>(value)].push_back(std::string(key));
		else
			for (std::size_t i = 0; i < value.size(); i += sizeof(uint64_t))
				groups[lmdb::from_sv<uint64_t>(value.substr(i, sizeof(uint64_t)))].push_back(std::string(key));
	} while (cur.get(key, value, MDB_NEXT));
	txn.commit();
	return std::pair(std::move(singletons), std::move(groups));
}

struct is_nop {
	bool operator()(const GadgetEdge& e) const noexcept {
		return e.start == e.end && e.from == e.to;
	}
};

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

	auto [singletons, groups] = invert_names(env, names_db);
	vector<uint64_t> all_ids = collect_initial_gadget_set(env, gadget_hashtable, gadget_index, names_db, gadget_set);
	vector<pair<uint64_t, vector<std::byte>>> all_data = select_gadget_id_to_data(env, gadget_hashtable, gadget_index, all_ids);
	std::sort(all_data.begin(), all_data.end(), proj_less<0>());
	for (const pair<uint64_t, vector<std::byte>>& gadget : all_data) {
		encoding::Stats stats = encoding::stats(gadget.second.data());
		fmt::print("{}: {} locations, {} states, {} uedges, {} dedges, {} components, {} bytes\n",
				gadget.first, stats.locations, stats.states, stats.undirected_edges,
				stats.directed_edges, stats.components, gadget.second.size());

		if (auto it = singletons.find(gadget.first); it != singletons.end())
			fmt::print("  names: {}\n", fmt::join(it->second, ", "));
		if (auto it = groups.find(gadget.first); it != groups.end())
			fmt::print("  groups: {}\n", fmt::join(it->second, ", "));

		pair<vector<GadgetEdge>, vector<GadgetEdge>> slls = encoding::decode_to_slls(gadget.second.data(), gadget.second.size());
		vector<GadgetEdge> nops;
		std::copy_if(slls.first.begin(), slls.first.end(), std::back_inserter(nops), is_nop());
		slls.first.erase(std::remove_if(slls.first.begin(), slls.first.end(), is_nop()), slls.first.end());
		if (slls.first.size())
			fmt::print("  undirected edges: {}\n", fmt::join(slls.first, ", "));
		if (slls.second.size())
			fmt::print("  directed edges: {}\n", fmt::join(slls.second, ", "));
		if (nops.size())
			fmt::print("  nop edges: {}\n", fmt::join(nops, ", "));

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