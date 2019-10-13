#ifndef AUTOMATON_REGEXESQUE_HPP
#define AUTOMATON_REGEXESQUE_HPP

#include "automaton.hpp"
#include "automaton-regexesque-basic.hpp"

namespace automaton {

/**
 * Returns an Automaton that accepts any single character.
 */
template<unsigned int N>
Automaton<N> any() {
	Automaton<N> a;
	a.addState();
	a.addState();
	a.setAccept(1);
	for (typename Automaton<N>::symbol_type s = 0; s < N; ++s)
		a.addTrans(0, s, 1);
	a.minimal_ = a.canonical_ = true;
	return a;
}

namespace detail {
void do_lit(AutomatonBase& a, std::initializer_list<symbol_type> symbols);
}

/**
 * Returns an Automaton that accepts only the string containing just the given
 * symbol(s).
 */
template<unsigned int N>
Automaton<N> lit(std::initializer_list<typename Automaton<N>::symbol_type> symbols) {
	Automaton<N> a;
	detail::do_lit(a, symbols);
	a.minimal_ = true;
	if (a.state_size() <= 2)
		a.canonical_ = true;
	return a;
}

/**
 * Returns an Automaton that accepts only the string containing just the given
 * symbol(s).
 */
template<unsigned int N, typename... Symbols>
Automaton<N> lit(Symbols... symbols) {
	return lit<N>({numeric_cast<typename Automaton<N>::symbol_type>(symbols)...});
}

namespace detail {

template<class A, typename enabled = std::enable_if_t<std::is_base_of_v<AutomatonBase, A>>>
struct alphabet_size_trait : std::integral_constant<unsigned int, 0> {};
template<unsigned int N>
struct alphabet_size_trait<Automaton<N>> : std::integral_constant<unsigned int, Automaton<N>::alphabet_size_v> {};

template<class ...Automata>
constexpr unsigned int deduce_size() {
	if constexpr (!sizeof...(Automata))
		throw "cannot deduce from empty pack";
	unsigned int sizes[sizeof...(Automata)] = {alphabet_size_trait<Automata>::value...};
	unsigned int current = 0;
	for (unsigned int i : sizes) {
		if (current == 0)
			current = i; //i may be 0, but that's a no-op
		if (current != 0 && i != 0 && i != current)
			throw "size mismatch";
	}
	if (current == 0)
		throw "no size provided";
	return current;
}

template<class ForwardIterator, class = std::void_t<typename std::iterator_traits<ForwardIterator>::iterator_category>>
AutomatonBase::state_type total_states(ForwardIterator begin, ForwardIterator end) {
	return std::accumulate(begin, end, 0u,
			[](AutomatonBase::state_type x, const AutomatonBase& a) {
				return x + a.state_size();
			});
}

template<unsigned int N, class Source, class = std::enable_if_t<std::is_base_of<AutomatonBase, std::decay_t<Source>>::value>>
void cat_once(Automaton<N>& target, Source&& source) {
	auto base = target.append(std::forward<Source>(source));
	//Wire the previous automaton's accept states to the current initial
	//state (base), modifying them to not accept.
	//TODO: addEpsilon may cause p to become accepting again, so we have
	//to scan from 0 each time.  We should probably keep a set of
	//accepting /state indices to avoid the repeated scanning.
	for (typename Automaton<N>::state_type p = 0; p < base; ++p) {
		if (target.accept(p)) {
			target.setAccept(p, false);
			target.addEpsilon(p, base);
		}
	}
}

template<unsigned int N, class Source, class = std::enable_if_t<std::is_base_of<AutomatonBase, std::decay_t<Source>>::value>>
void alt_once(Automaton<N>& target, Source&& source) {
	auto base = target.append(std::forward<Source>(source));
	target.addEpsilon(0, base);
}
} //namespace detail

template<class ForwardIterator, unsigned int N = std::iterator_traits<ForwardIterator>::value_type::alphabet_size_v>
Automaton<N> cat(ForwardIterator begin, ForwardIterator end) {
	if (begin == end)
		//the empty string is the identity element for concatenation
		return epsilon<N>();

	Automaton<N> a;
	a.reserve(detail::total_states(begin, end));
	std::for_each(begin, end, [&](auto& v){detail::cat_once(a, v);});
	return a;
}
template<unsigned int N>
Automaton<N> cat(std::initializer_list<Automaton<N>> list) {
	return cat(list.begin(), list.end());
}

template<unsigned int N>
Automaton<N> cat() {
	return epsilon<N>();
}

template<typename... Automata>
auto cat(Automata&&... rest) {
	constexpr unsigned int N = detail::deduce_size<std::decay_t<Automata>...>();
	return cat<N, Automata...>(std::forward<Automata>(rest)...);
}
template<unsigned int N, typename... Automata>
Automaton<N> cat(Automata&&... rest) {
	Automaton<N> a;
	a.reserve((0u + ... + rest.state_size()));
	(detail::cat_once(a, std::forward<Automata>(rest)), ...);
	return a;
}

template<class ForwardIterator, unsigned int N = std::iterator_traits<ForwardIterator>::value_type::alphabet_size_v>
auto alt(ForwardIterator begin, ForwardIterator end) {
	if (begin == end)
		return empty<N>();

	Automaton<N> a;
	a.reserve(detail::total_states(begin, end) + 1);
	//initial state that transitions to the individual machines' states
	a.addState();
	std::for_each(begin, end, [&](auto& v){detail::alt_once(a, v);});
	return a;
}
template<unsigned int N>
Automaton<N> alt(std::initializer_list<Automaton<N>> list) {
	return alt(list.begin(), list.end());
}

template<unsigned int N>
Automaton<N> alt() {
	return empty<N>();
}

template<typename... Automata>
auto alt(Automata&&... rest) {
	constexpr unsigned int N = detail::deduce_size<std::decay_t<Automata>...>();
	return alt<N, Automata...>(std::forward<Automata>(rest)...);
}
template<unsigned int N, typename... Automata>
Automaton<N> alt(Automata&&... rest) {
	Automaton<N> a;
	a.reserve((0u + ... + rest.state_size()));
	(detail::alt_once(a, std::forward<Automata>(rest)), ...);
	return a;
}


namespace detail {
using state_pair = std::pair<state_type, state_type>;
//We can't templatize this together with the other maps because dense_hash_map
//needs set_empty_key.
class DenseConjMap {
public:
	DenseConjMap(std::size_t leftSize, std::size_t rightSize) : map_() {
		//silent narrowing conversion: http://stackoverflow.com/q/37928951/3614835
		map_.set_empty_key({leftSize, rightSize});
	}
	void insert(state_pair oldstates, state_type newstate) {
		map_.insert({oldstates, newstate});
	}
	template<class Callable>
	std::pair<state_type, bool> compute_if_absent(state_pair oldstates, Callable newstateProvider) {
		//dense_hashtable::find_or_insert is so close to what we want :(
		auto it = map_.find(oldstates);
		if (it != map_.end())
			return {it->second, false};
		auto r = map_.insert({oldstates, newstateProvider()});
		return {r.first->second, true};
	}
private:
	//std::hash isn't provided for pair :(
	google::dense_hash_map<state_pair, state_type, boost::hash<state_pair>> map_;
};

template<class BackingMap>
class MapConjMap {
public:
	MapConjMap(std::size_t leftSize, std::size_t rightSize) : map_() {}
	void insert(std::pair<state_type, state_type> oldstates, state_type newstate) {
		map_.insert({oldstates, newstate});
	}
	template<class Callable>
	std::pair<state_type, bool> compute_if_absent(std::pair<state_type, state_type> oldstates, Callable newstateProvider) {
		auto it = map_.find(oldstates);
		if (it != map_.end())
			return {it->second, false};
		auto r = map_.insert({oldstates, newstateProvider()});
		return {r.first->second, true};
	}
private:
	BackingMap map_;
};
using UnorderedConjMap = MapConjMap<tsl::hopscotch_map<std::pair<state_type, state_type>,
		state_type, boost::hash<std::pair<state_type, state_type>>>>;
using SparseConjMap = MapConjMap<google::sparse_hash_map<std::pair<state_type, state_type>,
		state_type, boost::hash<std::pair<state_type, state_type>>>>;
/**
 * Implements a map of pairs as a vector of maps of elements.  On one hand,
 * our maps are smaller because each only has to store half the key; on the
 * other hand, the wasted space in one map can't be used by another, so our
 * total footprint may be larger.
 *
 * So far it doesn't seem to have worked out in either space or time, though
 * the latter might be fixed with a better hash function.
 */
//class DenseConjVectorOfMaps {
//public:
//	DenseConjVectorOfMaps(std::size_t leftSize, std::size_t rightSize) : maps_(std::min(leftSize, rightSize)), useLeft_(leftSize > rightSize) {
//		for (auto& map : maps_)
//			map.set_empty_key(std::numeric_limits<state_type>::max());
//	}
//	void insert(std::pair<state_type, state_type> oldstates, state_type newstate) {
//		if (useLeft_)
//			maps_[oldstates.second].insert({oldstates.first, newstate});
//		else
//			maps_[oldstates.first].insert({oldstates.second, newstate});
//	}
//	template<class Callable>
//	std::pair<state_type, bool> compute_if_absent(std::pair<state_type, state_type> oldstates, Callable newstateProvider) {
//		auto& map = useLeft_ ? maps_[oldstates.second] : maps_[oldstates.first];
//		state_type key = useLeft_ ? oldstates.first : oldstates.second;
//		auto it = map.find(key);
//		if (it != map.end())
//			return {it->second, false};
//		auto r = map.insert({key, newstateProvider()});
//		return {r.first->second, true};
//	}
//private:
//	struct MyHash {
//		std::size_t operator()(const state_type s) const {
//			return (s * s) + s;
////			return s ^ (s >> 8) ^ (s >> 16) ^ (s >> 24);
//		}
//	};
//	//both std::hash and boost::hash just return the state as its hash,
//	//which is very bad for dense_hash_map's power-of-two tables
//	std::vector<google::dense_hash_map<state_type, state_type, MyHash>> maps_;
//	bool useLeft_;
//};

template <class Map, unsigned int N>
static Automaton<N> conj_impl(const Automaton<N>& left, const Automaton<N>& right) {
	//(left state, right state, new state)
	using state_triple = std::tuple<state_type, state_type, state_type>;
	using Transition = typename Automaton<N>::Transition;
	using symbol_mask_type = typename Automaton<N>::symbol_mask_type;

	circular_deque<state_triple, 16> worklist;
	Map newstates(left.state_size(), right.state_size());
	Automaton<N> a;
	//TODO: are we sure?
	a.deterministic_ = left.deterministic() && right.deterministic();
	a.addState();
	//TODO: assuming 0 is the initial state
	worklist.push_back({0, 0, 0});
	newstates.insert({0, 0}, 0);

	while (!worklist.empty()) {
		state_type ls, rs, ns;
		std::tie(ls, rs, ns) = worklist.pop_back();
		a.setAccept(ns, left.accept(ls) && right.accept(rs));

		for (const Transition& lt : left.transitions_[ls])
			for (const Transition& rt : right.transitions_[rs]) {
				symbol_mask_type common = lt.symbols_ & rt.symbols_;
				if (common.any()) {
					state_type leftnext = lt.next_, rightnext = rt.next_;
					auto p = newstates.compute_if_absent({leftnext, rightnext}, [&]{return a.addState();});
					if (p.second)
						worklist.push_back({leftnext, rightnext, p.first});
					a.addTrans(ns, common, p.first);
				}
			}
	}
	return a;
}
} //namespace detail


template<unsigned int N>
Automaton<N> conj(const Automaton<N>& left, const Automaton<N>& right) {
	Automaton<N> a;
	try {
		a = detail::conj_impl<detail::DenseConjMap>(left, right);
	} catch (std::bad_alloc&) {
		AUTOMATON_DEBUG(std::cout << "caught bad_alloc: conj_impl<DenseConjMap>" << std::endl);
	}
	if (!a.isEmpty())
		try {
			a = detail::conj_impl<detail::UnorderedConjMap>(left, right);
		} catch (std::bad_alloc&) {
			AUTOMATON_DEBUG(std::cout << "caught bad_alloc: conj_impl<UnorderedConjMap>" << std::endl);
		}
	if (!a.isEmpty())
		//No try-catch here because there's no further recovery
		a = detail::conj_impl<detail::SparseConjMap>(left, right);
	AUTOMATON_DEBUG(std::cout << "intersection: " << left.state_size() << ", " << right.state_size() << " -> " << a.state_size() << std::endl);
	a.removeDeadStates();
	return a;
}
//TODO: we could add vararg/iterator-range overloads of conj for convenience,
//but we know from experience actually doing a multi-way conj is worse

template<unsigned int N>
Automaton<N> star(const Automaton<N>& b) {
	Automaton<N> a;
	a.reserve(b.state_size() + 1);
	a.addState();
	a.setAccept(0);
	typename Automaton<N>::state_type base = a.append(b);
	a.addEpsilon(0, 1);
	for (typename Automaton<N>::state_type p = base; p < a.state_size(); ++p)
		if (a.accept(p))
			a.addEpsilon(p, 0);
	return a;
}

template<unsigned int N>
Automaton<N> plus(const Automaton<N>& a) {
	return nOrMore(a, 1);
}
template<unsigned int N>
Automaton<N> maybe(const Automaton<N>& a) {
	return range(a, 0, 1);
}
template<unsigned int N>
Automaton<N> nCopies(const Automaton<N>& a, unsigned int count) {
	n_copies_range r(a, count);
	return cat(r.begin(), r.end());
}
template<unsigned int N>
Automaton<N> nOrMore(const Automaton<N>& a, unsigned int min) {
	if (min == 0) return star(a);
	return cat(nCopies(a, min), star(a));
}
template<unsigned int N>
Automaton<N> range(const Automaton<N>& a, unsigned int min, unsigned int max) {
	assert(max > min);
	//TODO: inlining nCopies would save a copy
	Automaton<N> ret = nCopies(a, min);
	ret.reserve(max * a.state_size());
	//like cat, but not clearing the accept states (and so not scanning from state 0 each time)
	typename Automaton<N>::state_type lastbase = 0;
	for (typename Automaton<N>::state_type i = 0; i < max - min; ++i) {
		typename Automaton<N>::state_type base = ret.append(a);
		for (typename Automaton<N>::state_type q = lastbase; q < base; ++q)
			if (ret.accept(q))
				ret.addEpsilon(q, base);
		lastbase = base;
	}
	return ret;
}

template<unsigned int N>
Automaton<N> comp(Automaton<N> a) {
	a.determinize();
	a.totalize();
	//TODO: we could add a method to flip everything at once (or make comp a friend)
	for (typename Automaton<N>::state_type s = 0; s < a.state_size(); ++s)
		a.setAccept(s, !a.accept(s));
	return a;
}

/**
 * Returns true iff the given automata accept the same language.
 */
template<unsigned int N>
bool same_language(const Automaton<N>& left, const Automaton<N>& right) {
	return conj(left, comp(right)).isEmpty() && conj(comp(left), right).isEmpty();
}

template<unsigned int N>
struct ComparisonResult {
	//whether left accepts strings right doesn't or vice-versa
	Automaton<N> leftButNotRight, rightButNotLeft;
	bool equal() {return leftButNotRight.isEmpty() && rightButNotLeft.isEmpty();}
	bool smaller() {return leftButNotRight.empty() && !rightButNotLeft.empty();}
	bool larger() {return !leftButNotRight.empty() && rightButNotLeft.empty();}
	bool incomparable() {return !leftButNotRight.empty() && !rightButNotLeft.empty();}
	//TODO: methods to compute witnesses, for fluent use of compare_languages?
};
/**
 * Compares the languages accepted by the given automata.
 */
template<unsigned int N>
ComparisonResult<N> compare_languages(const Automaton<N>& left, const Automaton<N>& right) {
	return {conj(left, comp(right)), conj(comp(left), right)};
}

} //namespace automaton

#endif /* AUTOMATON_REGEXESQUE_HPP */

