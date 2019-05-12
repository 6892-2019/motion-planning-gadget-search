/*
 * File:   toggles-shared.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on March 14, 2019, 12:27 AM
 */

#ifndef TOGGLES_SHARED_HPP
#define TOGGLES_SHARED_HPP

#include "database.hpp"
#include <msgpack.hpp>
#include <lmdb++.h>

struct farmhash_hash {
	//std::hash<uint64_t> is the identity, and hopscotch doesn't like that.
	uint64_t operator()(uint64_t x) const noexcept {
		return farmhash::Fingerprint(x);
	}
	uint64_t operator()(const std::vector<std::byte>& x) const noexcept {
		return farmhash::Hash(reinterpret_cast<const char*>(x.data()), x.size());
	}
};


struct DatabaseOperationStatistics {
	std::size_t pruned_locally, pruned_database, novel_gadgets, edges;
	DatabaseOperationStatistics& operator+=(const DatabaseOperationStatistics& o) {
		pruned_locally += o.pruned_locally;
		pruned_database += o.pruned_database;
		novel_gadgets += o.novel_gadgets;
		edges += o.edges;
		return *this;
	}
	MSGPACK_DEFINE(pruned_locally, pruned_database, novel_gadgets, edges)
};

struct GadgetSet {
	std::vector<std::uint64_t> ids;
	std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges; //inclusive, exclusive
	std::vector<std::string> names;
	MSGPACK_DEFINE_ARRAY(ids, ranges, names)
};

GadgetSet parse_gid_specs(const std::vector<std::string_view>& specs);
std::string format_gadget_set(const GadgetSet& gs);
std::vector<std::uint64_t> collect_initial_gadget_set(lmdb::env& env, const GadgetSet& gs);
std::vector<std::uint64_t> collect_initial_gadget_set(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, lmdb::dbi& names, const GadgetSet& gs);



/**
 * A row (minus the primary key) of an edge in the connect_provenance table.  We
 * use smaller types to save space.
 */
struct ConnectProvenance {
	std::uint64_t input1;
	std::uint32_t output1;
	std::uint8_t connectPoint;
	std::uint8_t canonicalizePermutation;
	MSGPACK_DEFINE_ARRAY(input1, output1, connectPoint, canonicalizePermutation)
};
inline bool operator<(const ConnectProvenance& a, const ConnectProvenance& b) {
	return std::tie(a.input1, a.output1, a.connectPoint, a.canonicalizePermutation) <
			std::tie(b.input1, b.output1, b.connectPoint, b.canonicalizePermutation);
}
template<class Callable>
void database_invoke_apply(Callable&& inv, const ConnectProvenance& p) {
	inv(p.input1)((uint64_t)p.output1)((unsigned short)p.connectPoint)((unsigned short)p.canonicalizePermutation);
}

/**
 * A row (minus the primary key) of an edge in the combine_provenance table.  We
 * use smaller types to save space.
 */
struct CombineProvenance {
	std::uint64_t input1, input2;
	std::uint32_t output1;
	std::uint8_t splice, rotation, connectPoint;
	std::uint8_t canonicalizePermutation;
	MSGPACK_DEFINE_ARRAY(input1, input2, output1, splice, rotation, connectPoint, canonicalizePermutation)
};
inline bool operator<(const CombineProvenance& a, const CombineProvenance& b) {
	return std::tie(a.input1, a.input2, a.output1, a.splice, a.rotation, a.connectPoint, a.canonicalizePermutation) <
			std::tie(b.input1, b.input2, b.output1, b.splice, b.rotation, b.connectPoint, b.canonicalizePermutation);
}
template<class Callable>
void database_invoke_apply(Callable&& inv, const CombineProvenance& p) {
	inv(p.input1)(p.input2)((uint64_t)p.output1)
			((unsigned short)p.splice)((unsigned short)p.rotation)
			((unsigned short)p.connectPoint)((unsigned short)p.canonicalizePermutation);
}

struct SimpleProvenance {
	std::uint64_t input1;
	std::uint32_t output1;
	std::uint8_t canonicalizePermutation;
	MSGPACK_DEFINE_ARRAY(input1, output1, canonicalizePermutation)
};
inline bool operator==(const SimpleProvenance& a, const SimpleProvenance& b) {
	return std::tie(a.input1, a.output1, a.canonicalizePermutation) ==
			std::tie(b.input1, b.output1, b.canonicalizePermutation);
}
inline bool operator<(const SimpleProvenance& a, const SimpleProvenance& b) {
	return std::tie(a.input1, a.output1, a.canonicalizePermutation) <
			std::tie(b.input1, b.output1, b.canonicalizePermutation);
}
template<class Callable>
void database_invoke_apply(Callable&& inv, const SimpleProvenance& p) {
	inv(p.input1)((uint64_t)p.output1)((unsigned short)p.canonicalizePermutation);
}


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

std::vector<std::pair<std::uint64_t, std::uint64_t>> filter_completion(
		lmdb::env& env, lmdb::dbi& completions, std::string_view kind,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);
std::vector<std::pair<std::uint64_t, std::uint64_t>> union_completion(
		lmdb::env& env, lmdb::txn& txn, lmdb::dbi& completions, std::string_view kind,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);

#endif /* TOGGLES_SHARED_HPP */

