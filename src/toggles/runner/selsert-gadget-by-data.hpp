// SPDX-License-Identifier: MIT
// Copyright 2019 Massachusetts Institute of Technology
#ifndef SELSERT_GADGET_BY_DATA_HPP
#define SELSERT_GADGET_BY_DATA_HPP

#include <vector>
#include <utility>
#include "lmdb++.h"

struct SelsertGadgetByDataResult {
	//TODO: local_to_global could be a dynarray to allow allocating without initializing it
	std::vector<std::uint64_t> local_to_global;
	std::pair<std::uint64_t, std::uint64_t> novel_global_ids;
	std::size_t early_pruned, late_pruned;
	std::size_t novel_size() const {
		return novel_global_ids.second - novel_global_ids.first;
	}
};

SelsertGadgetByDataResult selsert_gadget_by_data(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, std::vector<std::vector<std::byte>>&& gadgets);

//This is a hack to share some code between the "regular" selsert and the new
//firsthalf/secondhalf selsert.  Not for general consumption.
//Last argument could be a span<std::size_t> (we do exploit contiguity).
void append_gadget_index(lmdb::txn& txn, lmdb::dbi& gadget_index, const std::vector<std::size_t>& hashes,
		std::uint64_t first_novel_id);

#endif /* SELSERT_GADGET_BY_DATA_HPP */

