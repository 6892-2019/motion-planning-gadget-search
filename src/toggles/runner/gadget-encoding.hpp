/*
 * File:   gadget-encoding.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on May 4, 2019, 5:37 PM
 */

#ifndef GADGET_ENCODING_HPP
#define GADGET_ENCODING_HPP

#include "automaton.hpp"
#include "farmhash/farmhash.h"
#include <vector>
#include <cstddef>
#include <memory>
#include <tuple>
#include <array>


//Reading the header is available to all toggles targets for predicate indices.
//Operations involving automata are defined here and limited to the runner.
#include "../gadget-encoding-stats.hpp"

namespace encoding {

struct GadgetEdge {
	automaton::WorkingAutomaton::state_type start;
	automaton::WorkingAutomaton::symbol_type from;
	automaton::WorkingAutomaton::symbol_type to;
	automaton::WorkingAutomaton::state_type end;
	GadgetEdge reverse() const {return {end, to, from, start};}
};
inline bool operator==(const GadgetEdge& l, const GadgetEdge& r) {
	return std::tie(l.start, l.from, l.to, l.end) == std::tie(r.start, r.from, r.to, r.end);
}
inline bool operator!=(const GadgetEdge& l, const GadgetEdge& r) {
	return !(l == r);
}
inline bool operator<(const GadgetEdge& l, const GadgetEdge& r) {
	return std::tie(l.start, l.from, l.to, l.end) < std::tie(r.start, r.from, r.to, r.end);
}

std::unique_ptr<automaton::WorkingAutomaton> inflate_slls(const std::vector<GadgetEdge>& uedges,
		const std::vector<GadgetEdge>& dedges, unsigned int alphabetSize = 0);



std::unique_ptr<automaton::WorkingAutomaton> decode(const std::byte* encoded_gadget, std::size_t length,
		unsigned int alphabet_size = 0);
template<unsigned int N>
std::unique_ptr<automaton::Automaton<N>> decode(const std::byte* encoded_gadget, std::size_t length) {
	return unique_cast<automaton::Automaton<N>>(decode(encoded_gadget, length, N));
}

//convenience functions
inline std::unique_ptr<automaton::WorkingAutomaton> decode(const std::vector<std::byte>& encoded_gadget,
		unsigned int alphabet_size = 0) {
	return decode(encoded_gadget.data(), encoded_gadget.size(), alphabet_size);
}
template<unsigned int N>
std::unique_ptr<automaton::Automaton<N>> decode(const std::vector<std::byte>& encoded_gadget) {
	return unique_cast<automaton::Automaton<N>>(decode(encoded_gadget, N));
}

std::pair<std::vector<GadgetEdge>, std::vector<GadgetEdge>> decode_to_slls(const std::byte* encoded_gadget, std::size_t length);

std::vector<std::byte> encode(const automaton::WorkingAutomaton& a);

//If we bring back PageHolder etc., we'd use this to encode into a page.
//bool encode(const automaton::WorkingAutomaton& a, std::byte* first, std::byte* last);

//TODO: see if avoiding the virtual call overhead is worth the extra code duplication
//template<unsigned int N>
//std::vector<std::byte> encode(const automaton::Automaton<N>& a);
//template<unsigned int N>
//bool encode(const automaton::Automaton<N>& a, std::byte* first, std::byte* last);

} //namespace encoding

namespace std {
template<>
struct hash<encoding::GadgetEdge> {
	size_t operator()(const encoding::GadgetEdge& e) const {
		std::array<char, 4 * sizeof(unsigned int)> a;
		std::memcpy(&a[0], &e.start, sizeof(e.start));
		std::memcpy(&a[4], &e.from, sizeof(e.from));
		std::memcpy(&a[8], &e.to, sizeof(e.to));
		std::memcpy(&a[12], &e.end, sizeof(e.end));
		return farmhash::Hash(a.cbegin(), a.size());
	}
};
}

namespace fmt {
template<>
struct formatter<encoding::GadgetEdge> {
	template<typename ParseContext>
	constexpr auto parse(ParseContext& ctx) {return ctx.begin();}
	template<typename FormatContext>
	auto format(const encoding::GadgetEdge& e, FormatContext& ctx) {
		return format_to(ctx.begin(), "[{}, {}, {}, {}]", e.start, e.from, e.to, e.end);
	}
};

template<>
struct formatter<encoding::Stats> {
	template<typename ParseContext>
	constexpr auto parse(ParseContext& ctx) {return ctx.begin();}
	template<typename FormatContext>
	auto format(const encoding::Stats& s, FormatContext& ctx) {
		if (s.components == 1)
			return format_to(ctx.begin(), "{}l{}s{}u{}d",
					s.locations, s.states, s.undirected_edges, s.directed_edges);
		return format_to(ctx.begin(), "{}l{}s{}u{}d{}c",
				s.locations, s.states, s.undirected_edges, s.directed_edges, s.components);
	}
};
}

#endif /* GADGET_ENCODING_HPP */

