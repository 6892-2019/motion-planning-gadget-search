#ifndef LMDB_INTERVAL_LIST_HPP
#define LMDB_INTERVAL_LIST_HPP

#include <lmdb++.h>
#include <vector>
#include <utility>
#include <string_view>

/**
 * Returns a view to an interval list in the database, or a pair of nullptr if
 * the key does not exist.  Throws if the value has the wrong size.
 */
std::pair<const std::pair<std::uint64_t, std::uint64_t>*, const std::pair<std::uint64_t, std::uint64_t>*>
view_interval_list(lmdb::txn& txn, lmdb::dbi& database, std::string_view key);

std::vector<std::pair<std::uint64_t, std::uint64_t>> subtract_interval_list(
		lmdb::txn& txn, lmdb::dbi& database, std::string_view key,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);

std::vector<std::pair<std::uint64_t, std::uint64_t>> intersect_interval_list(
		lmdb::txn& txn, lmdb::dbi& database, std::string_view key,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);

std::vector<std::pair<std::uint64_t, std::uint64_t>> write_interval_list_union(
		lmdb::txn& txn, lmdb::dbi& database, std::string_view key,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);

#endif /* LMDB_INTERVAL_LIST_HPP */

