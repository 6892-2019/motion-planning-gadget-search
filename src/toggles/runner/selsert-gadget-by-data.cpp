#include "precompiled.hpp"
#include "selsert-gadget-by-data.hpp"

using std::uint64_t;
using std::size_t;
using std::pair;
using std::vector;
using std::string_view;

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
			//This duplicates toggles-share's get_current_max_gadget_id, but
			//we're going to keep using the cursor.
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