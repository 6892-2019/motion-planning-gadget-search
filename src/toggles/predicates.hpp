// SPDX-License-Identifier: MIT
// Copyright 2019 Massachusetts Institute of Technology
// Copyright 2025 Jeffrey Bosboom
#ifndef PREDICATES_HPP
#define PREDICATES_HPP

#include <lmdb++.h>
#include <vector>
#include <utility>

//update SL predicates through given id (default max) using N threads (or using given executor)
bool update_predicates(lmdb::env& env, lmdb::dbi& predicates, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, std::uint64_t valid_before = std::numeric_limits<std::uint64_t>::max(),
		unsigned int threads = 1);

enum class PredicateKind {
	locations, states, uedges, dedges, total_edges, components
};
inline bool operator<(PredicateKind a, PredicateKind b) {
	return static_cast<int>(a) < static_cast<int>(b);
}
template<>
struct fmt::formatter<PredicateKind> : formatter<string_view> {
	template<typename FormatContext>
	auto format(const PredicateKind kind, FormatContext& ctx) const {
		string_view name;
		switch (kind) {
			case PredicateKind::locations: name = "locations"; break;
			case PredicateKind::states: name = "states"; break;
			case PredicateKind::uedges: name = "uedges"; break;
			case PredicateKind::dedges: name = "dedges"; break;
			case PredicateKind::total_edges: name = "edges"; break;
			case PredicateKind::components: name = "components"; break;
		}
		return fmt::formatter<string_view>::format(name, ctx);
	}
};
PredicateKind predicate_kind_from_string(std::string_view s);

//create and update new predicates to current validity using N threads
bool create_predicates(lmdb::env& env, lmdb::dbi& predicates, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index,
		std::vector<std::pair<unsigned int, PredicateKind>> less_than_or_equal_to, unsigned int threads = 1);

//get predicate (copy from DB)
std::vector<std::pair<std::uint64_t, std::uint64_t>> get_predicate(lmdb::env& env,
		lmdb::dbi& predicates, PredicateKind kind, unsigned int less_than_or_equal_to);
std::vector<std::pair<std::uint64_t, std::uint64_t>> get_predicate(lmdb::txn& txn,
		lmdb::dbi& predicates, PredicateKind kind, unsigned int less_than_or_equal_to);

//get equality predicate (difference between two predicates from DB)
//We could also offer range query, though if the upper bound is greater than
//we've computed, we'd fail.
std::vector<std::pair<std::uint64_t, std::uint64_t>> get_equal_predicate(lmdb::env& env,
		lmdb::dbi& predicates, PredicateKind kind, unsigned int equal_to);
std::vector<std::pair<std::uint64_t, std::uint64_t>> get_equal_predicate(lmdb::txn& txn,
		lmdb::dbi& predicates, PredicateKind kind, unsigned int equal_to);

//predicate difference (remove matching)
std::vector<std::pair<std::uint64_t, std::uint64_t>> subtract_predicate(lmdb::env& env, lmdb::dbi& predicates,
		PredicateKind kind, unsigned int less_than_or_equal_to,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);
std::vector<std::pair<std::uint64_t, std::uint64_t>> subtract_predicate(lmdb::txn& txn, lmdb::dbi& predicates,
		PredicateKind kind,	unsigned int less_than_or_equal_to,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);

//predicate intersection (retain only matching)
std::vector<std::pair<std::uint64_t, std::uint64_t>> intersect_predicate(lmdb::env& env, lmdb::dbi& predicates,
		PredicateKind kind, unsigned int less_than_or_equal_to,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);
std::vector<std::pair<std::uint64_t, std::uint64_t>> intersect_predicate(lmdb::txn& txn, lmdb::dbi& predicates,
		PredicateKind kind,	unsigned int less_than_or_equal_to,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);

#endif /* PREDICATES_HPP */

