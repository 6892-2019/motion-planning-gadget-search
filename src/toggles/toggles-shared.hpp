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

void jemalloc_tuning();

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

