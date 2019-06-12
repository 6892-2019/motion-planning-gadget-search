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

#endif /* SELSERT_GADGET_BY_DATA_HPP */

