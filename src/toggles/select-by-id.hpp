#ifndef SELECT_BY_ID_HPP
#define SELECT_BY_ID_HPP

#include "gadget-encoding-stats.hpp"
#include <lmdb++.h>
#include <vector>
#include <utility>

/**
 * Returns the current maximum gadget id (inclusive).  The return value is
 * immediately stale, but monotonically increasing (so it will never be an
 * underestimate).
 */
std::uint64_t get_current_max_gadget_id(lmdb::env& env, lmdb::dbi& gadget_index);
/**
 * Returns the current maximum gadget id (inclusive).  The return value is
 * immediately stale, but monotonically increasing (so it will never be an
 * underestimate).
 */
std::uint64_t get_current_max_gadget_id(lmdb::env& env);
/**
 * Returns the current maximum gadget id (inclusive).  The return value is
 * immediately stale, but monotonically increasing (so it will never be an
 * underestimate).
 */
std::uint64_t get_current_max_gadget_id(lmdb::txn& txn, lmdb::dbi& gadget_index);
/**
 * Returns the current maximum gadget id (inclusive).  The return value is
 * immediately stale, but monotonically increasing (so it will never be an
 * underestimate).
 */
std::uint64_t get_current_max_gadget_id(lmdb::txn& txn);



std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> select_gadget_id_to_data(
		lmdb::env& env, const std::vector<std::uint64_t>& gids);
std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> select_gadget_id_to_data(
		lmdb::env& env, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index,
		const std::vector<std::uint64_t>& gids);
std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> select_gadget_id_to_data(
		lmdb::env& env, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& gid_intervals);
std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> select_gadget_id_to_data(
		lmdb::env& env, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& gid_intervals);

//Offering this here (and pulling in gadget-encoding-stats.hpp) lets us keep
//select_gadget_id_to_value out of this header, but is somewhat unsatisfying as
//predicates.cpp is the only natural client.
std::vector<std::pair<std::uint64_t, encoding::Stats>> select_gadget_id_to_stats(
		lmdb::env& env, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& gid_intervals);

#endif /* SELECT_BY_ID_HPP */

