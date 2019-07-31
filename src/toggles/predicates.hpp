#ifndef PREDICATES_HPP
#define PREDICATES_HPP

#include <lmdb++.h>
#include <vector>
#include <utility>

//update SL predicates through given id (default max) using N threads (or using given executor)
bool update_SL_predicates(lmdb::env& env, lmdb::dbi& predicates, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, std::uint64_t valid_before = std::numeric_limits<std::uint64_t>::max(),
		unsigned int threads = 1);
//create and update new state predicate to current validity using N threads
bool create_state_predicate(lmdb::env& env, lmdb::dbi& predicates, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, unsigned int less_than_or_equal_to, unsigned int threads = 1);
//get predicate (copy from DB)
std::vector<std::pair<std::uint64_t, std::uint64_t>> get_location_predicate(lmdb::env& env, lmdb::dbi& predicates, unsigned int max_locations);
std::vector<std::pair<std::uint64_t, std::uint64_t>> get_location_predicate(lmdb::txn& txn, lmdb::dbi& predicates, unsigned int max_locations);
std::vector<std::pair<std::uint64_t, std::uint64_t>> get_state_predicate(lmdb::env& env, lmdb::dbi& predicates, unsigned int max_states);
std::vector<std::pair<std::uint64_t, std::uint64_t>> get_state_predicate(lmdb::txn& txn, lmdb::dbi& predicates, unsigned int max_states);
//get equality predicate (difference between two predicates from DB)
//We could also offer range query, though if the upper bound is greater than
//we've computed, we'd fail.  So locations is fine, but states is not.
std::vector<std::pair<std::uint64_t, std::uint64_t>> get_equal_location_predicate(lmdb::env& env, lmdb::dbi& predicates, unsigned int locations);
std::vector<std::pair<std::uint64_t, std::uint64_t>> get_equal_location_predicate(lmdb::txn& txn, lmdb::dbi& predicates, unsigned int locations);
//calling these requires the states-1 predicate to also exist.  Reasonable for
//1-8? states, not for more than that.  caller's responsibility to create first.
std::vector<std::pair<std::uint64_t, std::uint64_t>> get_equal_state_predicate(lmdb::env& env, lmdb::dbi& predicates, unsigned int states);
std::vector<std::pair<std::uint64_t, std::uint64_t>> get_equal_state_predicate(lmdb::txn& txn, lmdb::dbi& predicates, unsigned int states);
//predicate_difference (for excluding impossible combines)
std::vector<std::pair<std::uint64_t, std::uint64_t>> subtract_location_predicate(lmdb::env& env, lmdb::dbi& predicates,
		unsigned int max_locations,	const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);
std::vector<std::pair<std::uint64_t, std::uint64_t>> subtract_location_predicate(lmdb::txn& txn, lmdb::dbi& predicates,
		unsigned int max_locations,	const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);
std::vector<std::pair<std::uint64_t, std::uint64_t>> subtract_state_predicate(lmdb::env& env, lmdb::dbi& predicates,
		unsigned int max_states, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);
std::vector<std::pair<std::uint64_t, std::uint64_t>> subtract_state_predicate(lmdb::txn& txn, lmdb::dbi& predicates,
		unsigned int max_states, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);
//predicate_intersection (for retaining only possible connects)
std::vector<std::pair<std::uint64_t, std::uint64_t>> intersect_location_predicate(lmdb::env& env, lmdb::dbi& predicates,
		unsigned int max_locations, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);
std::vector<std::pair<std::uint64_t, std::uint64_t>> intersect_location_predicate(lmdb::txn& txn, lmdb::dbi& predicates,
		unsigned int max_locations, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);
std::vector<std::pair<std::uint64_t, std::uint64_t>> intersect_state_predicate(lmdb::env& env, lmdb::dbi& predicates,
		unsigned int max_states, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);
std::vector<std::pair<std::uint64_t, std::uint64_t>> intersect_state_predicate(lmdb::txn& txn, lmdb::dbi& predicates,
		unsigned int max_states, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);

#endif /* PREDICATES_HPP */

