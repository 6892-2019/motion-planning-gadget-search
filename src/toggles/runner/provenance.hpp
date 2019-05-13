/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/*
 * File:   provenance.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on May 12, 2019, 9:19 PM
 */

#ifndef PROVENANCE_HPP
#define PROVENANCE_HPP

#include <msgpack.hpp>
#include <tuple>

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


/**
 * A comparator that sorts by input1 (only), grouping them together for database
 * insertion.  We have to remap output1 anyway so there's no point in a full
 * sort until afterward, and that's on the "half" edges.
 */
struct InputGroupingProvCmp {
	template<class P>
	bool operator()(const P& a, const P& b) const noexcept {
		return a.input1 < b.input1;
	}
};

#endif /* PROVENANCE_HPP */

