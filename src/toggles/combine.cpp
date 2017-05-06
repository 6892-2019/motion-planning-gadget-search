#include "precompiled.hpp"
#include "ops.hpp"

void combine(Registry::index_type l, bool leftMirror, Registry::index_type r, bool rightMirror, const Registry& registry, Result& finishArg) {
	const Gadget& left = registry.at(l), &right = registry.at(r);
	const automaton_type& la = leftMirror ? *left.mirror_ : *left.a_;
	const automaton_type& ra = rightMirror ? *right.mirror_ : *right.a_;

	using symbol_type = automaton_type::symbol_type;
	std::vector<symbol_type> slide(automaton_type::alphabet_size_v);
	std::vector<symbol_type> sliderotate(automaton_type::alphabet_size_v);
	std::fill(slide.begin(), slide.begin()+right.locations_, std::numeric_limits<symbol_type>::max());
	std::iota(slide.begin()+right.locations_, slide.begin()+right.locations_+left.locations_, 0);
	std::fill(slide.begin()+right.locations_+left.locations_, slide.end(), std::numeric_limits<symbol_type>::max());
	for (location_type ll = 0; ll < left.locations_; ++ll) {
		std::fill(sliderotate.begin(), sliderotate.end(), std::numeric_limits<symbol_type>::max());
		std::iota(sliderotate.begin()+ll, sliderotate.begin()+ll+right.locations_, 0);
		automaton_type lm = la;
		lm.renumberAlphabet(slide);
		for (location_type rl = 0; rl < right.locations_; ++rl) {
			automaton_type rm = ra;
			rm.renumberAlphabet(sliderotate);
			automaton_type combined = automaton::shuffleAccept(lm, rm);
			combined.minimize();
			canonicalize(combined, left.locations_ + right.locations_);
			finish(Gadget(std::make_shared<const automaton_type>(std::move(combined)),
					left.locations_ + right.locations_),
					Provenance(l, ll, leftMirror, r, rl, rightMirror,
					//TODO: make a reasoned choice for this function
					(registry.provenance(l).generation + registry.provenance(r).generation)+1),
					finishArg);
			std::rotate(sliderotate.begin()+ll, sliderotate.begin()+ll+right.locations_-1, sliderotate.begin()+ll+right.locations_);
		}
		std::swap(slide[ll], slide[ll+right.locations_]);
	}
}

void combine(Registry::index_type l, Registry::index_type r, const Registry& registry, Result& finishArg) {
	const Gadget& left = registry.at(l), &right = registry.at(r);
	if (left.locations_ + right.locations_ > automaton_type::alphabet_size_v) {
//		std::cout << "Skipping combine due to size\n";
		return;
	}

	combine(l, false, r, false, registry, finishArg);
	if (left.mirror_)
		combine(l, true, r, false, registry, finishArg);
	if (right.mirror_)
		combine(l, false, r, true, registry, finishArg);
	if (left.mirror_ && right.mirror_) //TODO: do we need this?
		combine(l, true, r, true, registry, finishArg);
}