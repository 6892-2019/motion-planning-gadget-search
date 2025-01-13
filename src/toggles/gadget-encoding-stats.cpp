// SPDX-License-Identifier: MIT
// Copyright 2019 Massachusetts Institute of Technology
#include "precompiled.hpp"
#include "gadget-encoding-stats.hpp"

namespace encoding {
namespace detail {
std::pair<Stats, const std::byte*> stats(const std::byte* encoded_gadget) {
	Stats s = {};
	s.components = 1;
	s.locations = locations(encoded_gadget++);

	std::byte second_byte = *encoded_gadget++;
	std::size_t state_length = 1 + static_cast<bool>(second_byte & detail::state_mask);
	std::size_t uedge_length = std::to_integer<unsigned int>(second_byte & detail::uedge_mask) >> 3;
	std::size_t dedge_length = std::to_integer<unsigned int>(second_byte & detail::dedge_mask) >> 1;
	std::size_t comp_length = std::to_integer<unsigned int>(second_byte & detail::component_mask);

	std::memcpy(&s.states, encoded_gadget, state_length);
	std::memcpy(&s.undirected_edges, encoded_gadget + state_length, uedge_length);
	std::memcpy(&s.directed_edges, encoded_gadget + state_length + uedge_length, dedge_length);
	std::memcpy(&s.components, encoded_gadget + state_length + uedge_length + dedge_length, comp_length);
	encoded_gadget += state_length + uedge_length + dedge_length + comp_length;

	return {s, encoded_gadget};
}
}//namespace detail

unsigned int locations(const std::byte* encoded_gadget) {
	return std::to_integer<unsigned int>(*encoded_gadget & detail::location_mask) + 1;
}

Stats stats(const std::byte* encoded_gadget) {
	return detail::stats(encoded_gadget).first;
}

}//namespace encoding