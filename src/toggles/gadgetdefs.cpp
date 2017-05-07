#include "precompiled.hpp"
#include "../automaton.hpp"
#include "canonicalize.hpp"
#include "registry.hpp"
#include "ops.hpp"

using namespace automaton;

namespace {
std::unordered_map<std::string, Gadget> initialize_known_gadgets() {
	std::unordered_map<std::string, Gadget> ret;
	constexpr unsigned int N = automaton_type::alphabet_size_v;
	auto lit = [](auto... symbols){return automaton::lit<N>(symbols...);};

	automaton_type nop3 = star(alt(lit(0, 0), lit(1, 1), lit(2, 2)));
	nop3.minimize();
	canonicalize(nop3, 3);
	ret["3-nop"] = Gadget(nop3, 3);

	automaton_type nop4 = star(alt(lit(0, 0), lit(1, 1), lit(2, 2), lit(3, 3)));
	nop4.minimize();
	canonicalize(nop4, 4);
	ret["4-nop"] = Gadget(nop4, 4);

	automaton_type split = star(nCopies(alt(lit(0), lit(1), lit(2)), 2));
	split.minimize();
	canonicalize(split, 3);
	ret["split"] = Gadget(split, 3);

	auto make_twostate = [&](auto&& state0, auto&& state1, const auto& nop) {
		auto base = alt(epsilon<N>(), state0, star(cat(state0, state1)), cat(state0, star(cat(state1, state0))));
		base.minimize();
		unsigned int locations = static_cast<unsigned int>(base.activeAlphabet().size());
		auto toggle = shuffleAccept(base, nop);
		toggle.minimize();
		acceptingClosure(toggle, locations);
		toggle.minimize();
		canonicalize(toggle, locations);
		return Gadget(std::move(toggle), locations);
	};
	ret["parallel-2-toggle"] = make_twostate(alt(lit(0, 1), lit(3, 2)), alt(lit(1, 0), lit(2, 3)), nop4);
	ret["antiparallel-2-toggle"] = make_twostate(alt(lit(0, 1), lit(2, 3)), alt(lit(1, 0), lit(3, 2)), nop4);
	ret["crossover-2-toggle"] = make_twostate(alt(lit(0, 2), lit(3, 1)), alt(lit(2, 0), lit(1, 3)), nop4);

	ret["3-spinner"] = make_twostate(alt(lit(0, 1), lit(1, 2), lit(2, 0)),
			alt(lit(1, 0), lit(2, 1), lit(0, 2)), nop3);
	ret["4-spinner"] = make_twostate(alt(lit(0, 1), lit(1, 2), lit(2, 3), lit(3, 0)),
			alt(lit(1, 0), lit(2, 1), lit(3, 2), lit(0, 3)), nop4);

	return ret;
}
} //anonymous namespace

Gadget known_gadget(const std::string& name) {
	static std::unordered_map<std::string, Gadget> map = initialize_known_gadgets();
	auto i = map.find(name);
	if (i == map.end()) {
		std::cout << "couldn't find " << name << std::endl;
		std::exit(1);
	}
	return i->second;
}