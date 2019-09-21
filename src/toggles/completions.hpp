#ifndef COMPLETIONS_HPP
#define COMPLETIONS_HPP

#include <vector>
#include <utility>
#include <string_view>
#include <cstdint>
#include <lmdb++.h>

std::vector<std::pair<std::uint64_t, std::uint64_t>> subtract_completion(
		lmdb::txn& txn, lmdb::dbi& completions, const std::string_view kind,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);
std::vector<std::pair<std::uint64_t, std::uint64_t>> subtract_completion(
		lmdb::env& env, lmdb::dbi& completions, const std::string_view kind,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);
std::vector<std::pair<std::uint64_t, std::uint64_t>> intersect_completion(
		lmdb::txn& txn, lmdb::dbi& completions, const std::string_view kind,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);
bool record_completion(lmdb::txn& txn, lmdb::dbi& completions, const std::string_view kind,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals,
		bool allow_overlap = false);
bool compact_completion(lmdb::txn& txn, lmdb::dbi& completions, const std::string_view kind);

#endif /* COMPLETIONS_HPP */

