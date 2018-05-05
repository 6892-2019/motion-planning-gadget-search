#ifndef OPS_HPP
#define OPS_HPP

#include "automaton.hpp"

template<class AutomatonType, class FinishAction>
void combine(const AutomatonType& la, uint32_t l, bool leftMirror, typename AutomatonType::state_type leftLocations,
		const AutomatonType& ra, uint32_t r, bool rightMirror, typename AutomatonType::state_type rightLocations,
		FinishAction& finish) {
	using symbol_type = typename AutomatonType::symbol_type;
	std::vector<symbol_type> slide(AutomatonType::alphabet_size_v);
	std::vector<symbol_type> sliderotate(AutomatonType::alphabet_size_v);
	std::fill(slide.begin(), slide.begin()+rightLocations, std::numeric_limits<symbol_type>::max());
	std::iota(slide.begin()+rightLocations, slide.begin()+rightLocations+leftLocations, 0);
	std::fill(slide.begin()+rightLocations+leftLocations, slide.end(), std::numeric_limits<symbol_type>::max());
	for (decltype(leftLocations) ll = 0; ll < leftLocations; ++ll) {
		std::fill(sliderotate.begin(), sliderotate.end(), std::numeric_limits<symbol_type>::max());
		std::iota(sliderotate.begin()+ll, sliderotate.begin()+ll+rightLocations, 0);
		AutomatonType lm = la;
		lm.renumberAlphabet(slide);
		for (decltype(rightLocations) rl = 0; rl < rightLocations; ++rl) {
			AutomatonType rm = ra;
			rm.renumberAlphabet(sliderotate);
			AutomatonType combined = automaton::shuffleAccept(lm, rm);
			finish(std::move(combined), Provenance(l, ll, leftMirror, r, rl, rightMirror, 0));
			std::rotate(sliderotate.begin()+ll, sliderotate.begin()+ll+rightLocations-1, sliderotate.begin()+ll+rightLocations);
		}
		std::swap(slide[ll], slide[ll+rightLocations]);
	}
}




#endif /* OPS_HPP */

