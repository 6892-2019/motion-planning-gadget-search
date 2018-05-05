/*
 * File:   provenance.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on May 5, 2018, 2:49 AM
 */

#ifndef PROVENANCE_HPP
#define PROVENANCE_HPP

#include "automaton.hpp"

struct Provenance {
	std::uint32_t first, second, i, j, generation;
	Provenance() = default;
	Provenance(std::uint32_t initialIndex) : first(ALLONES), second(initialIndex), i(ALLONES), j(ALLONES), generation(0) {}
	Provenance(std::uint32_t parent, std::uint32_t connection, automaton::AutomatonBase::state_type newInitialState, bool mirrored, std::uint32_t generatio)
		: first(parent), second(ALLONES), i(mirrored ? connection | TOPBIT : connection), j(newInitialState), generation(generatio) {}
	Provenance(std::uint32_t firstParent, std::uint32_t firstSplice, bool firstMirrored,
			std::uint32_t secondParent, std::uint32_t secondSplice, bool secondMirrored, std::uint32_t generatio)
		: first(firstParent), second(secondParent),
		  i(firstMirrored ? firstSplice | TOPBIT : firstSplice),
		  j(secondMirrored ? secondSplice | TOPBIT : secondSplice), generation(generatio) {}

	bool isInput() const {return first == ALLONES;}
	bool isConnect() const {return second == ALLONES;}
	bool isCombine() const {return !isInput() && !isConnect();}
	boost::container::small_vector<std::uint32_t, 2> parents() {
		boost::container::small_vector<std::uint32_t, 2> p;
		if (first != ALLONES)
			p.push_back(first);
		if (second != ALLONES)
			p.push_back(second);
		return p;
	};
	friend std::ostream& operator<<(std::ostream&, Provenance&);
private:
	static constexpr std::uint32_t ALLONES = std::numeric_limits<std::uint32_t>::max();
	static constexpr std::uint32_t TOPBIT = 1 << 31;
};

#endif /* PROVENANCE_HPP */

