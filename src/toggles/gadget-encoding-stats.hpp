// SPDX-License-Identifier: MIT
// Copyright 2019 Massachusetts Institute of Technology
#ifndef GADGET_ENCODING_STATS_HPP
#define GADGET_ENCODING_STATS_HPP

/**
 * The packed gadget encoding has a two-byte header, a few words of statistics
 * information, and variable-length edge-list data.
 *
 * The low four bits of the first byte store the number of locations - 1 (we
 * don't care to store 0, and we do care to store 16).  The remaining four bits
 * are reserved.
 *
 * The second byte stores the lengths of further statistics information in its
 * six low bits.  The top two bits are reserved.
 *   bit 6-7: reserved
 *   bit 5: 1 or 2 bytes holding the number of states
 *   bit 3-4: 0, 1, 2 or 3 bytes holding the number of undirected edges
 *   bit 1-2: 0, 1, 2 or 3 bytes holding the number of directed edges
 *   bit 0: 0 or 1 bytes holding the number of strongly-connected components
 * When 0 bytes are allocated for edges, the corresponding count is 0; when 0
 * bytes are allocated for components, the count is 1.
 */

namespace encoding {

struct Stats {
	unsigned int locations, states, undirected_edges, directed_edges, components;
};
unsigned int locations(const std::byte* encoded_gadget);
Stats stats(const std::byte* encoded_gadget);



namespace detail {
constexpr static std::byte location_mask{0b1111};

constexpr static std::byte state_mask    {0b00100000};
constexpr static std::byte uedge_mask    {0b00011000};
constexpr static std::byte dedge_mask    {0b00000110};
constexpr static std::byte component_mask{0b00000001};

std::pair<Stats, const std::byte*> stats(const std::byte* encoded_gadget);
}//namespace detail
}//namespace encoding

#endif /* GADGET_ENCODING_STATS_HPP */

