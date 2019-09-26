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
#include "varint.hpp"

void jemalloc_tuning();

unsigned int check_for_stale_readers(lmdb::env& env);


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
		return format_to(ctx.out(), "[{}, {}, {}, {}, {}]",
				e.output, e.splice, e.rotation, e.connectPoint, e.canonicalizePermutation);
	}
};

template<>
struct formatter<ConnectEdge> {
	template<typename ParseContext>
	constexpr auto parse(ParseContext& ctx) {return ctx.begin();}
	template<typename FormatContext>
	auto format(const ConnectEdge& e, FormatContext& ctx) {
		return format_to(ctx.out(), "[{}, {}, {}]",
				e.output, e.connectPoint, e.canonicalizePermutation);
	}
};

template<>
struct formatter<SimpleEdge> {
	template<typename ParseContext>
	constexpr auto parse(ParseContext& ctx) {return ctx.begin();}
	template<typename FormatContext>
	auto format(const SimpleEdge& e, FormatContext& ctx) {
		return format_to(ctx.out(), "[{}, {}]", e.output, e.canonicalizePermutation);
	}
};
} //namespace fmt



enum class VisitEdgeResult {
	proceed, //visit the next edge, if any (continue)
	skip, //skip any remaining edges from this input (break)
	quit //stop the visitation (return)
};

namespace detail {
template<class Edge, class Action>
VisitEdgeResult visit_edges0(std::string_view key, std::string_view value, Action&& action) {
	//If we have to replace this pointer-based code for alignment etc.,
	//we can instead template this function on sizeof(Edge)
	//(Or maybe still template on Edge, but use sizeof/offsetof to
	//achieve the same.)
	if (value.size() == 0 || value.size() % sizeof(Edge) != 0)
		throw std::logic_error(fmt::format("edge data of type {} for key {} has value length {} (not a multiple of {})",
				//We want the dbi's name here, but I don't see how to get it.
				//The message won't distinguish close and mirror.
				typeid(Edge).name(), lmdb::from_sv<std::uint64_t>(key), value.size(), sizeof(Edge)));
	const Edge* first = reinterpret_cast<const Edge*>(value.data());
	const Edge* last = first + value.size() / sizeof(Edge);
	while (first != last) {
		VisitEdgeResult r = action(lmdb::from_sv<std::uint64_t>(key), *first++);
		if (r == VisitEdgeResult::skip) break;
		if (r == VisitEdgeResult::quit) return r;
	}
	return VisitEdgeResult::proceed;
}
} //end namespace detail

template<class Edge, class Action>
void visit_edges(lmdb::txn& txn, lmdb::dbi& edge_db,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources,
		Action&& action) {
	lmdb::cursor cur = lmdb::cursor::open(txn, edge_db);
	for (const std::pair<std::uint64_t, std::uint64_t>& p : sources) {
		std::string_view key = lmdb::to_sv(p.first), value;
		if (!cur.get(key, value, MDB_SET_RANGE))
			break; //reached end of database
		while (lmdb::from_sv<uint64_t>(key) < p.second) {
			VisitEdgeResult r = detail::visit_edges0<Edge>(key, value, std::forward<Action>(action));
			if (r == VisitEdgeResult::quit) return;
			//visit_edge handled skip, no handling necessary for proceed
			if (!cur.get(key, value, MDB_NEXT)) break;
		}
	}
}
template<class Edge, class Action>
void visit_edges(lmdb::env& env, lmdb::dbi& edge_db,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources,
		Action&& action) {
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	visit_edges<Edge>(txn, edge_db, sources, std::forward<Action>(action));
	txn.commit();
}

template<class Edge, class Action>
void visit_edges(lmdb::txn& txn, lmdb::dbi& edge_db, uint64_t input, Action&& action) {
	std::string_view key = lmdb::to_sv(input), value;
	if (edge_db.get(txn, lmdb::to_sv(input), value))
		//result is irrelevant here
		detail::visit_edges0<Edge>(key, value, std::forward<Action>(action));
}
template<class Edge, class Action>
void visit_edges(lmdb::env& env, lmdb::dbi& edge_db, uint64_t input, Action&& action) {
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	visit_edges<Edge>(txn, edge_db, input, std::forward<Action>(action));
	txn.commit();
}

namespace detail {
std::uint64_t decode_skinny_edge_page(std::string_view key, std::string_view value, std::vector<std::uint32_t>& offsets);
} //end namespace detail

template<class Action>
void visit_skinny_edges(lmdb::txn& txn, lmdb::dbi& edge_db,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources, Action&& action) {
	using std::uint64_t;
	using std::pair;
	lmdb::cursor cur = lmdb::cursor::open(txn, edge_db);
	//To avoid decoding the page over and over when we have small intervals, we
	//cache one decoded page header.
	uint64_t open_page_key = 0;
	uint64_t open_page_start = 0;
	std::vector<std::uint32_t> offsets;
	for (pair<uint64_t, uint64_t> p : sources) {
		//If the next page starts after this interval ends, there are no edges
		//from ids in this interval.
		if (p.second <= open_page_start) continue;
		uint64_t id = p.first;
		while (id < p.second) { //interval might span pages
			std::string_view key = lmdb::to_sv(id), value;
			if (!cur.get(key, value, MDB_SET_RANGE))
				//We've reached the end of the database.  No further ids have edges.
				return;
			if (lmdb::from_sv<uint64_t>(key) != open_page_key) {
				open_page_key = lmdb::from_sv<uint64_t>(key);
				open_page_start = detail::decode_skinny_edge_page(key, value, offsets);
			}
			//Skip to the start of the page (no edges for any skipped ids).  If
			//the start is beyond this interval, the following loop does nothing.
			id = std::max(id, open_page_start);

			const std::byte* page_base = reinterpret_cast<const std::byte*>(value.data());
			for (std::size_t offset = id - open_page_start; id < p.second && id <= open_page_key; ++offset, ++id) {
				const std::byte* first = page_base + offsets[offset], *last = page_base + offsets[offset+1];
				std::uint64_t output = 0;
				do {
					output += upv::read(first); //delta decode
					VisitEdgeResult r = action(id, output);
					if (r == VisitEdgeResult::quit) return;
					if (r == VisitEdgeResult::skip) break;
				} while (first != last);
			}
		}
	}
}
template<class Action>
void visit_skinny_edges(lmdb::env& env, lmdb::dbi& edge_db,
		const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources,
		Action&& action) {
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	visit_skinny_edges(txn, edge_db, sources, std::forward<Action>(action));
	txn.commit();
}

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
		lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources,
		unsigned int threads = 1);
extern template std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_edges<CombineEdge>(
		lmdb::env& env,	lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources,
		unsigned int threads);
extern template std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_edges<ConnectEdge>(
		lmdb::env& env,	lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources,
		unsigned int threads);
extern template std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_edges<SimpleEdge>(
		lmdb::env& env,	lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources,
		unsigned int threads);

//Not templated, because the database format doesn't change.  If we packed
//close/mirror edges, we'd have a separate follow_packed_edges function.
std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_skinny_edges(lmdb::env& env,
		lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources,
		unsigned int threads = 1);



struct DatabaseMetadata {
	uint64_t id;
	std::string creator_hostname;
	std::time_t creation_time;
	std::string creation_timestamp;
};

DatabaseMetadata read_meta(lmdb::env& env);



#endif /* TOGGLES_SHARED_HPP */

