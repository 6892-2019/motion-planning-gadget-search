// SPDX-License-Identifier: MIT
// Copyright 2019 Massachusetts Institute of Technology
// Copyright 2025 Jeffrey Bosboom
#include "precompiled.hpp"
#include "selsert-gadget-by-data.hpp"
#include "../gadget-encoding-stats.hpp"
#include "../select-by-id.hpp"

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
	//std::tie doesn't bind rvalues, so we have to explicitly compute some things.
	//Single out uedges == 0 and dedges == 0 because those are natural queries;
	//the rest of the edge sorts aren't super useful.  These are reversed because
	//false sorts before true.
	bool a_undirected = a.stats.directed_edges != 0,
			a_directed = a.stats.undirected_edges != 0,
			b_undirected = b.stats.directed_edges != 0,
			b_directed = b.stats.undirected_edges != 0;
	auto a_total_edges = a.stats.undirected_edges + a.stats.directed_edges,
			b_total_edges = b.stats.undirected_edges + b.stats.directed_edges;
	return std::tie(a.stats.components, a.stats.locations, a.stats.states, a_undirected, a_directed, a.stats.undirected_edges, a.stats.directed_edges, a_total_edges, a.hash) <
			std::tie(b.stats.components, b.stats.locations, b.stats.states, b_undirected, b_directed, b.stats.undirected_edges, b.stats.directed_edges, b_total_edges, b.hash);
}

vector<ProposedInsert> hash_and_move(vector<vector<std::byte>>&& gadgets) {
	vector<ProposedInsert> ret;
	ret.reserve(gadgets.size());
	for (std::size_t i = 0; i < gadgets.size(); ++i) {
		std::size_t hash = contig_range_hash()(gadgets[i]);
		ret.push_back({numeric_cast<unsigned int>(i), hash, std::move(gadgets[i])}); //stats initialized later for survivors
	}
	vector<vector<std::byte>> ensure_memory_is_freed(std::move(gadgets));
	return ret;
}
}//anonymous namespace

void append_gadget_index(lmdb::txn& txn, lmdb::dbi& gadget_index, const std::vector<std::size_t>& hashes,
		std::uint64_t first_novel_id) {
	lmdb::cursor index_cur = lmdb::cursor::open(txn, gadget_index);
	std::string_view last_index_key, last_index_value;
	index_cur.get(last_index_key, last_index_value, MDB_LAST);

	//We pack hashes into pages to save space.  See the comment in
	//select_gadget_id_to_hash.
	std::size_t hashes_index = 0;
	//LMDB overflow pages have a 16-byte header.
	constexpr std::size_t index_page_bytes = (4096-16), index_page_size = index_page_bytes / sizeof(std::size_t);
	//If the previous page wasn't full, fill it.
	if (!last_index_value.empty() && last_index_value.size() != index_page_bytes) {
		//We can't actually append to the last open page; instead we
		//append a new page and delete the old one.
		std::size_t current_size = last_index_value.size() / sizeof(std::size_t);
		std::size_t new_elements = std::min(hashes.size(), index_page_size - current_size);
		std::uint64_t current_key = lmdb::from_sv<uint64_t>(last_index_key);
		std::uint64_t new_key = current_key + new_elements;
		//We're about to mutate the database, so we need to copy the old data.
		std::vector<char> old_data_bytes(last_index_value.begin(), last_index_value.end());
		std::size_t length = (current_size + new_elements) * sizeof(std::size_t);
		if (!index_cur.put_reserve(lmdb::to_sv(new_key), length, MDB_APPEND | MDB_NOOVERWRITE, [&](std::byte* dest, std::size_t length){
			std::memcpy(dest, old_data_bytes.data(), old_data_bytes.size());
			std::memcpy(dest + old_data_bytes.size(), hashes.data(), new_elements * sizeof(std::size_t));
		}))
			throw std::logic_error(fmt::format("failed to append while extending index page: {} {} {} {}",
					current_key, current_size, new_key, length));
		if (!index_cur.get(last_index_key, MDB_SET))
			throw std::logic_error(fmt::format("failed to position for deletion? {}", current_key));
		index_cur.del(); //throws on failure
		hashes_index += new_elements;
	}

	while (hashes_index < hashes.size()) {
		std::size_t new_elements = std::min(hashes.size() - hashes_index, index_page_size);
		std::size_t new_key = first_novel_id + hashes_index + new_elements - 1;
		std::size_t length = new_elements * sizeof(std::size_t);
		if (!index_cur.put_reserve(lmdb::to_sv(new_key), length, MDB_APPEND | MDB_NOOVERWRITE, [&](std::byte* dest, std::size_t length) {
			std::memcpy(dest, hashes.data() + hashes_index, new_elements * sizeof(std::size_t));
		}))
			throw std::logic_error(fmt::format("failed to append new index page: {} {} {} {}",
					hashes_index, new_key, new_elements, length));
		hashes_index += new_elements;
	}
}

/**
 * Returns the id of each of the given gadgets after inserting any gadgets not
 * already present.  The gadgets are assumed to be distinct.
 */
SelsertGadgetByDataResult selsert_gadget_by_data(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, vector<vector<std::byte>>&& gadgets) {
	SelsertGadgetByDataResult ret;
	ret.local_to_global.resize(gadgets.size(), std::numeric_limits<std::uint64_t>::max());
	ret.early_pruned = ret.late_pruned = ret.novel_global_ids.first = ret.novel_global_ids.second = 0;
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

	std::vector<std::size_t> hashes;
	hashes.reserve(sorted_stats.size());

	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr);
		{//extra scope for write cursors
			//Do late pruning.
			lmdb::cursor hashtable_cur = lmdb::cursor::open(txn, gadget_hashtable);
			auto [new_end, pruned, self_collision] = pruning_loop(txn, hashtable_cur);
			pending.erase(new_end, pending.end());
			if (pending.empty())
				return ret;
			assert(std::is_sorted(pending.begin(), pending.end(), hash_order()));
			ret.late_pruned = pruned;
			//We now know we don't collide with anything in the database, but we
			//might collide with one of the other elements we're going to insert.
			if (self_collision) //TODO: unlikely
				resolve_self_collision(txn, hashtable_cur);

			//There's a slight inefficiency here: we're opening and closing a
			//cursor here and then opening another index cursor later when
			//appending to the index.  This costs one malloc and some stores.
			uint64_t last_id = get_current_max_gadget_id(txn, gadget_index);

			//Because we've late-pruned, we know exactly what will be inserted,
			//so we can assign ids according to the stats sort rank (skipping
			//late-pruned gadgets).
			ret.novel_global_ids.first = last_id + 1;
			ret.novel_global_ids.second = ret.novel_global_ids.first + pending.size();
			for (const SortStats& ss : sorted_stats)
				if (ret.local_to_global[ss.local_index] == std::numeric_limits<std::uint64_t>::max()) {
					ret.local_to_global[ss.local_index] = ++last_id; //last_id is inclusive, so pre-increment
					hashes.push_back(ss.hash);
				}
			assert(ret.novel_global_ids.second == last_id+1); //last_id is inclusive, intervals' second is exclusive

			for (ProposedInsert& pi : pending) {
				std::string_view data(reinterpret_cast<const char*>(pi.data.data()), pi.data.size());
				//Constructing a string_view to nullptr is technically undefined
				//behavior.  We have to const_cast it later again anyway, so
				//string_view is just the wrong abstraction for MDB_RESERVE.
				//TODO: rewrite lmdbxx using std::span (hah)
				auto length = pi.data.size() + sizeof(ret.local_to_global[pi.local_index]);
				if (!hashtable_cur.put_reserve(lmdb::to_sv(pi.hash), length, MDB_NOOVERWRITE, [&](std::byte* dest, std::size_t length) {
					std::memcpy(dest, pi.data.data(), pi.data.size());
					std::memcpy(dest + pi.data.size(), &ret.local_to_global[pi.local_index],
						sizeof(ret.local_to_global[pi.local_index]));
				}))
					throw std::logic_error(fmt::format("can't happen: collision after late-pruning for hash {}", pi.hash));
			}

			append_gadget_index(txn, gadget_index, hashes, ret.novel_global_ids.first);
		}
		txn.commit();
	}
	//Should have filled in everything now.
	assert(std::find(ret.local_to_global.begin(), ret.local_to_global.end(),
			std::numeric_limits<std::uint64_t>::max()) == ret.local_to_global.end());
	return ret;
}