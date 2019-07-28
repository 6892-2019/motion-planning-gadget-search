#include "precompiled.hpp"
#include "selsert-gadget-by-data.hpp"
#include "../gadget-encoding-stats.hpp"

using std::uint64_t;
using std::size_t;
using std::pair;
using std::vector;
using std::string_view;

namespace {
struct ProposedInsert {
	unsigned int local_index;
	size_t hash;
	vector<std::byte> data;
};
struct hash_order {
	bool operator()(const ProposedInsert& a, const ProposedInsert& b) const noexcept {
		return a.hash < b.hash;
	}
};

struct SortStats {
	encoding::Stats stats;
	//We break ties by hash for locality of later readers, and we do so
	//explicitly rather than rely std::stable_sort's order preservation because
	//std::stable_sort allocates more memory than explicitly storing the hash
	//costs.  It turns out to be handy, anyway.
	std::size_t hash;
	unsigned int local_index;
};
bool operator<(const SortStats& a, const SortStats& b) noexcept {
	//TODO: also try to group uedges == 0 and dedges == 0.
	return std::tie(a.stats.locations, a.stats.states, a.hash) <
			std::tie(b.stats.locations, b.stats.states, b.hash);
}

vector<ProposedInsert> hash_and_move(vector<vector<std::byte>>&& gadgets) {
	vector<ProposedInsert> ret;
	ret.reserve(gadgets.size());
	for (std::size_t i = 0; i < gadgets.size(); ++i) {
		std::size_t hash = farmhash::Fingerprint64(gadgets[i]);
		ret.push_back({numeric_cast<unsigned int>(i), hash, std::move(gadgets[i])}); //stats initialized later for survivors
	}
	vector<vector<std::byte>> ensure_memory_is_freed(std::move(gadgets));
	return ret;
}
}//anonymous namespace

/**
 * Returns the id of each of the given gadgets after inserting any gadgets not
 * already present.  The gadgets are assumed to be distinct.
 */
SelsertGadgetByDataResult selsert_gadget_by_data(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, vector<vector<std::byte>>&& gadgets) {
	SelsertGadgetByDataResult ret;
	ret.local_to_global.resize(gadgets.size(), std::numeric_limits<std::uint64_t>::max());
	ret.early_pruned = ret.late_pruned = 0;
	vector<ProposedInsert> pending = hash_and_move(std::move(gadgets));

	auto pruning_loop = [&](lmdb::txn& txn, lmdb::cursor& cur) {
		vector<ProposedInsert>::iterator new_end = pending.begin();
		std::size_t pruned = 0;
		bool self_collision = false;
		for (vector<ProposedInsert>::iterator i = pending.begin(); i != pending.end(); ++i) {
			//Because you can't compare std::byte with char, grumble...
			const char* data_begin = reinterpret_cast<const char*>(i->data.data()),
					*data_end = data_begin + i->data.size();
			std::string_view key = lmdb::to_sv(i->hash), existing;
			while (cur.get(key, existing, MDB_SET))
				//skip the appended ID (remove_suffix is a mutator)
				if (std::equal(data_begin, data_end, existing.begin(), existing.end()-8)) {
					ret.local_to_global[i->local_index] = lmdb::from_sv<std::uint64_t>(existing.substr(existing.size()-8));
					++pruned;
					goto labeled_continue; //don't increment new_end
				} else
					++(i->hash); //linear probing

			//i->hash now contains the proposed insert point.  Are we using it already?
			self_collision |= (i != pending.begin() && i->hash <= (i-1)->hash);

			if (i != new_end) //avoid vector's self-move-assignment
				*new_end = std::move(*i);
			++new_end;
			labeled_continue: ;
		}
		return std::tuple(new_end, pruned, self_collision);
	};

	auto resolve_self_collision = [&](lmdb::txn& txn, lmdb::cursor& cur) {
		do {
			//Resolve the self-collisions.
			assert(pending.size() >= 2); //can't self-collide with one element
			for (std::size_t i = 0; i < pending.size()-1; ++i)
				if (pending[i+1].hash <= pending[i].hash)
					pending[i+i].hash = pending[i].hash + 1;
			//After resolving the collisions, we might be colliding with the
			//database again!  But we know we can't prune any more (because
			//we would have already late-pruned those elements).
		} while (std::get<2>(pruning_loop(txn, cur)));
	};

	//Reads in sorted order are faster too.
	std::sort(pending.begin(), pending.end(), hash_order());
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		lmdb::cursor cur = lmdb::cursor::open(txn, gadget_hashtable);
		//We can't resolve self-collisions during early pruning because one or
		//the other of the colliding elements might get inserted by some other
		//process, so we can't know which element's hash to increment.
		auto [new_end, pruned, self_collisions_ignored_during_early_pruning] = pruning_loop(txn, cur);
		txn.commit();
		pending.erase(new_end, pending.end());
		ret.early_pruned = pruned;
	}
	assert(std::is_sorted(pending.begin(), pending.end(), hash_order()));
	if (pending.empty())
		return ret;

	//Decode stats before potentially blocking on the write transaction, at the
	//cost of another pass through the data and doing the decoding for late prunes.
	vector<SortStats> sorted_stats;
	for (ProposedInsert& pi : pending)
		sorted_stats.push_back({encoding::stats(pi.data.data()), pi.hash, pi.local_index});
	std::sort(sorted_stats.begin(), sorted_stats.end());

	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr);
		{//extra scope for write cursors
			//Do late pruning.
			lmdb::cursor hashtable_cur = lmdb::cursor::open(txn, gadget_hashtable);
			auto [new_end, pruned, self_collision] = pruning_loop(txn, hashtable_cur);
			pending.erase(new_end, pending.end());
			assert(std::is_sorted(pending.begin(), pending.end(), hash_order()));
			ret.late_pruned = pruned;
			//We now know we don't collide with anything in the database, but we
			//might collide with one of the other elements we're going to insert.
			if (self_collision) //TODO: unlikely
				resolve_self_collision(txn, hashtable_cur);

			//This duplicates toggles-share's get_current_max_gadget_id, but
			//we're going to keep using the cursor.
			lmdb::cursor index_cur = lmdb::cursor::open(txn, gadget_index);
			std::string_view last_id_view;
			std::uint64_t last_id;
			if (index_cur.get(last_id_view, MDB_LAST))
				last_id = lmdb::from_sv<std::uint64_t>(last_id_view);
			else
				last_id = 0; //empty index; starting at 0 means first key will be 1

			//Because we've late-pruned, we know exactly what will be inserted,
			//so we can assign ids according to the stats sort rank (skipping
			//late-pruned gadgets).
			ret.novel_global_ids.first = last_id + 1;
			ret.novel_global_ids.second = ret.novel_global_ids.first + pending.size();
			for (const SortStats& ss : sorted_stats)
				if (ret.local_to_global[ss.local_index] == std::numeric_limits<std::uint64_t>::max()) {
					ret.local_to_global[ss.local_index] = ++last_id; //last_id is inclusive, so pre-increment
					if (!index_cur.put(lmdb::to_sv(ret.local_to_global[ss.local_index]), lmdb::to_sv(ss.hash), MDB_APPEND | MDB_NOOVERWRITE))
						throw std::runtime_error(fmt::format("failed to append to index: index {} key {} hash {}",
								ss.local_index, ret.local_to_global[ss.local_index], ss.hash));
				}
			assert(ret.novel_global_ids.second == last_id+1); //last_id is inclusive, intervals' second is exclusive

			for (ProposedInsert& pi : pending) {
				std::string_view data(reinterpret_cast<const char*>(pi.data.data()), pi.data.size());
				//Constructing a string_view to nullptr is technically undefined
				//behavior.  We have to const_cast it later again anyway, so
				//string_view is just the wrong abstraction for MDB_RESERVE.
				std::string_view target(nullptr, pi.data.size()+8);
				if (!hashtable_cur.put(lmdb::to_sv(pi.hash), target, MDB_RESERVE | MDB_NOOVERWRITE))
					throw std::logic_error(fmt::format("can't happen: collision after late-pruning for hash {}", pi.hash));
				std::memcpy(const_cast<char*>(target.begin()), pi.data.data(), pi.data.size());
				std::memcpy(const_cast<char*>(target.begin()) + pi.data.size(), &ret.local_to_global[pi.local_index],
						sizeof(ret.local_to_global[pi.local_index]));
			}
		}
		txn.commit();
	}
	//Should have filled in everything now.
	assert(std::find(ret.local_to_global.begin(), ret.local_to_global.end(),
			std::numeric_limits<std::uint64_t>::max()) == ret.local_to_global.end());
	return ret;
}