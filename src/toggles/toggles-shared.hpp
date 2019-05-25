/*
 * File:   toggles-shared.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on March 14, 2019, 12:27 AM
 */

#ifndef TOGGLES_SHARED_HPP
#define TOGGLES_SHARED_HPP

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
	uint64_t operator()(const std::vector<unsigned long>& x) const noexcept {
		return farmhash::Hash(reinterpret_cast<const char*>(x.data()), x.size());
	}
};


struct DatabaseOperationStatistics {
	std::size_t skipped, pruned_locally, pruned_database, novel_gadgets, edges;
	DatabaseOperationStatistics& operator+=(const DatabaseOperationStatistics& o) {
		skipped += o.skipped;
		pruned_locally += o.pruned_locally;
		pruned_database += o.pruned_database;
		novel_gadgets += o.novel_gadgets;
		edges += o.edges;
		return *this;
	}
	MSGPACK_DEFINE(skipped, pruned_locally, pruned_database, novel_gadgets, edges)
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
std::vector<std::pair<std::uint64_t, std::uint64_t>> filter_completion(
		lmdb::env& env, lmdb::txn& txn, lmdb::dbi& completions, std::string_view kind,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);
std::vector<std::pair<std::uint64_t, std::uint64_t>> intersect_completion(
		lmdb::env& env, lmdb::txn& txn, lmdb::dbi& completions, std::string_view kind,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);
std::vector<std::pair<std::uint64_t, std::uint64_t>> union_completion(
		lmdb::env& env, lmdb::txn& txn, lmdb::dbi& completions, std::string_view kind,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& intervals);


//These are the "right halves" of edges stored as (arrays of) values in the database.
struct __attribute__((__packed__)) CombineEdge {
	std::uint64_t output;
	std::uint8_t splice, rotation, connectPoint;
	std::uint8_t canonicalizePermutation;
};
inline bool operator<(const CombineEdge& a, const CombineEdge& b) {
	return std::tie(a.output, a.splice, a.rotation, a.connectPoint, a.canonicalizePermutation) <
			std::tie(b.output, b.splice, b.rotation, b.connectPoint, b.canonicalizePermutation);
}
inline bool operator==(const CombineEdge& a, const CombineEdge& b) {
	return std::tie(a.output, a.splice, a.rotation, a.connectPoint, a.canonicalizePermutation) ==
			std::tie(b.output, b.splice, b.rotation, b.connectPoint, b.canonicalizePermutation);
}
inline bool operator!=(const CombineEdge& a, const CombineEdge& b) {
	return !(a == b);
}

struct __attribute__((__packed__)) ConnectEdge {
	std::uint64_t output;
	std::uint8_t connectPoint;
	std::uint8_t canonicalizePermutation;
};
inline bool operator<(const ConnectEdge& a, const ConnectEdge& b) {
	return std::tie(a.output, a.connectPoint, a.canonicalizePermutation) <
			std::tie(b.output, b.connectPoint, b.canonicalizePermutation);
}
inline bool operator==(const ConnectEdge& a, const ConnectEdge& b) {
	return std::tie(a.output, a.connectPoint, a.canonicalizePermutation) ==
			std::tie(b.output, b.connectPoint, b.canonicalizePermutation);
}
inline bool operator!=(const ConnectEdge& a, const ConnectEdge& b) {
	return !(a == b);
}

struct __attribute__((__packed__)) SimpleEdge {
	std::uint64_t output;
	std::uint8_t canonicalizePermutation;
};
inline bool operator<(const SimpleEdge& a, const SimpleEdge& b) {
	return std::tie(a.output, a.canonicalizePermutation) <
			std::tie(b.output, b.canonicalizePermutation);
}
inline bool operator==(const SimpleEdge& a, const SimpleEdge& b) {
	return std::tie(a.output, a.canonicalizePermutation) ==
			std::tie(b.output, b.canonicalizePermutation);
}
inline bool operator!=(const SimpleEdge& a, const SimpleEdge& b) {
	return !(a == b);
}

//We could replace these formatters with specializations of std::tuple_element,
//std::tuple_size, and an appropriate get implementation somewhere.
namespace fmt {
template<>
struct formatter<CombineEdge> {
	template<typename ParseContext>
	constexpr auto parse(ParseContext& ctx) {return ctx.begin();}
	template<typename FormatContext>
	auto format(const CombineEdge& e, FormatContext& ctx) {
		return format_to(ctx.begin(), "[{}, {}, {}, {}, {}]",
				e.output, e.splice, e.rotation, e.connectPoint, e.canonicalizePermutation);
	}
};

template<>
struct formatter<ConnectEdge> {
	template<typename ParseContext>
	constexpr auto parse(ParseContext& ctx) {return ctx.begin();}
	template<typename FormatContext>
	auto format(const ConnectEdge& e, FormatContext& ctx) {
		return format_to(ctx.begin(), "[{}, {}, {}]",
				e.output, e.connectPoint, e.canonicalizePermutation);
	}
};

template<>
struct formatter<SimpleEdge> {
	template<typename ParseContext>
	constexpr auto parse(ParseContext& ctx) {return ctx.begin();}
	template<typename FormatContext>
	auto format(const SimpleEdge& e, FormatContext& ctx) {
		return format_to(ctx.begin(), "[{}, {}]", e.output, e.canonicalizePermutation);
	}
};
} //namespace fmt



/**
 * Follows edges in an edge database from the source intervals, returning target
 * intervals (the deduplicated union of all targets).  This function is declared
 * here and defined in the corresponding object file because the runner's sync
 * mode wants to follow close edges, and the implementation only depends on the
 * location of the 'output' member of the edge struct (actually, because it's
 * always the first member, only on the size of the struct).
 */
template<class Edge>
std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_edges(lmdb::env& env,
		lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources);
extern template std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_edges<CombineEdge>(
		lmdb::env& env,	lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources);
extern template std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_edges<ConnectEdge>(
		lmdb::env& env,	lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources);
extern template std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_edges<SimpleEdge>(
		lmdb::env& env,	lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources);

#endif /* TOGGLES_SHARED_HPP */

