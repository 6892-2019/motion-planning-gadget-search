#include "precompiled.hpp"
#include "../automaton.hpp"
#include "canonicalize.hpp"
#include "registry.hpp"
#include "ops.hpp"

using namespace automaton;

namespace {
automaton_type make_nop(unsigned int locations) {
	std::vector<automaton_type> automata;
	for (unsigned int i = 0; i < locations; ++i)
		automata.push_back(lit<automaton_type::alphabet_size_v>(i, i));
	return star(alt(automata.begin(), automata.end()));
}

void setInitialStates(automaton_type& a, const StateSet& initialStates) {
	using state_type = automaton_type::state_type;
	state_type s = a.addState();
	for (state_type t : initialStates)
		a.addEpsilon(s, t);
	a.swapStateNumbers(0, s);
}

void branchToAnyAcceptState(automaton_type& a) {
	StateSet accepting;
	for (AutomatonBase::state_type s = 0; s < a.state_size(); ++s)
		if (a.accept(s))
			accepting.insert_absent(s);
	setInitialStates(a, accepting);
}

automaton_type prepare(automaton_type a) {
	a.minimize();
	a = shuffleAccept(a, make_nop(a.active_alphabet_size()));
	a.minimize();
	acceptingClosure(a, a.active_alphabet_size());
	branchToAnyAcceptState(a);
	a.minimize();
	canonicalize(a, a.active_alphabet_size());
	return a;
}

class GadgetBuilder {
public:
	GadgetBuilder(automaton_type::state_type states) : gadget() {
		for (automaton_type::state_type i = 0; i < states; ++i) {
			gadget.addState();
			gadget.setAccept(i);
		}
	}
	GadgetBuilder& trans(automaton_type::state_type start, automaton_type::symbol_type from,
			automaton_type::symbol_type to, automaton_type::state_type end) {
		assert(gadget.accept(start));
		assert(gadget.accept(end));
		automaton_type::state_type t = gadget.addState();
		gadget.addTrans(start, from, t);
		gadget.addTrans(t, to, end);
		return *this;
	}
	automaton_type build() {
		branchToAnyAcceptState(gadget);
		return prepare(std::move(gadget));
	}
private:
	automaton_type gadget;
};

automaton_type make_twostate(automaton_type&& state0, automaton_type&& state1) {
	constexpr unsigned int N = automaton_type::alphabet_size_v;
	auto base = alt(epsilon<N>(), state0, star(cat(state0, state1)), cat(state0, star(cat(state1, state0))));
	return prepare(base);
}

std::unordered_map<std::string, automaton_type> initialize_known_gadgets() {
	std::unordered_map<std::string, automaton_type> ret;
	constexpr unsigned int N = automaton_type::alphabet_size_v;
	auto lit = [](auto... symbols){return automaton::lit<N>(symbols...);};

	ret["2-nop"] = prepare(make_nop(2));
	ret["3-nop"] = prepare(make_nop(3));
	ret["4-nop"] = prepare(make_nop(4));

	ret["split"] = prepare(star(nCopies(alt(lit(0), lit(1), lit(2)), 2)));

	ret["diode"] = prepare(star(lit(0, 1)));

	ret["1-toggle"] = make_twostate(lit(0, 1), lit(1, 0));
	ret["parallel-2-toggle"] = make_twostate(alt(lit(0, 1), lit(3, 2)), alt(lit(1, 0), lit(2, 3)));
	ret["antiparallel-2-toggle"] = make_twostate(alt(lit(0, 1), lit(2, 3)), alt(lit(1, 0), lit(3, 2)));
	ret["crossover-2-toggle"] = make_twostate(alt(lit(0, 2), lit(3, 1)), alt(lit(2, 0), lit(1, 3)));

	ret["noncrossing-tripwire-lock"] = make_twostate(
			alt(lit(0, 1), lit(1, 0), lit(3, 2), lit(2, 3)),
			alt(lit(0, 1), lit(1, 0)));
	ret["crossing-tripwire-lock"] = make_twostate(
			alt(lit(0, 2), lit(2, 0), lit(3, 1), lit(1, 3)),
			alt(lit(0, 2), lit(2, 0)));

	ret["noncrossing-toggle-lock"] = make_twostate(
			alt(lit(0, 1), lit(3, 2), lit(2, 3)),
			alt(lit(1, 0)));
	ret["crossing-toggle-lock"] = make_twostate(
			alt(lit(0, 2), lit(3, 1), lit(1, 3)),
			alt(lit(2, 0)));

	ret["noncrossing-tripwire-toggle"] = make_twostate(
			alt(lit(0, 1), lit(3, 2), lit(2, 3)),
			alt(lit(1, 0), lit(3, 2), lit(2, 3)));
	ret["crossing-tripwire-toggle"] = make_twostate(
			alt(lit(0, 2), lit(3, 1), lit(1, 3)),
			alt(lit(2, 0), lit(3, 1), lit(1, 3)));

	ret["3-spinner"] = make_twostate(alt(lit(0, 1), lit(1, 2), lit(2, 0)),
			alt(lit(1, 0), lit(2, 1), lit(0, 2)));
	ret["4-spinner"] = make_twostate(alt(lit(0, 1), lit(1, 2), lit(2, 3), lit(3, 0)),
			alt(lit(1, 0), lit(2, 1), lit(3, 2), lit(0, 3)));

	//all mismatched unless otherwise noted
	ret["parallel-seven-seven"] = GadgetBuilder(2)
			.trans(0, 2, 3, 0).trans(0, 3, 2, 0)
			.trans(0, 2, 3, 1).trans(0, 0, 1, 1)
			.trans(1, 1, 0, 0).trans(1, 3, 2, 0)
			.trans(1, 0, 1, 1).trans(1, 1, 0, 1)
			.build();
	ret["antiparallel-seven-seven"] = GadgetBuilder(2)
			.trans(0, 2, 3, 0).trans(0, 3, 2, 0)
			.trans(0, 3, 2, 1).trans(0, 0, 1, 1)
			.trans(1, 1, 0, 0).trans(1, 2, 3, 0)
			.trans(1, 0, 1, 1).trans(1, 1, 0, 1)
			.build();
	ret["crossing-seven-seven"] = GadgetBuilder(2)
			.trans(0, 1, 3, 0).trans(0, 3, 1, 0)
			.trans(0, 1, 3, 1).trans(0, 0, 2, 1)
			.trans(1, 2, 0, 0).trans(1, 3, 1, 0)
			.trans(1, 0, 2, 1).trans(1, 2, 0, 1)
			.build();

	ret["seven-lock"] = GadgetBuilder(2)
			.trans(0, 2, 3, 0).trans(0, 3, 2, 0)
			.trans(0, 0, 1, 1)
			.trans(1, 1, 0, 0)
			.trans(1, 0, 1, 1).trans(1, 1, 0, 1)
			.build();

	ret["seven-tripwire"] = GadgetBuilder(2)
			.trans(0, 0, 1, 1).trans(0, 2, 3, 1).trans(0, 3, 2, 1)
			.trans(1, 1, 0, 0).trans(1, 2, 3, 0).trans(1, 3, 2, 0)
			.trans(1, 0, 1, 1).trans(1, 1, 0, 1)
			.build();

	return ret;
}
} //anonymous namespace

automaton_type known_gadget(const std::string& name) {
	static std::unordered_map<std::string, automaton_type> map = initialize_known_gadgets();
	auto i = map.find(name);
	if (i == map.end()) {
		std::cout << "couldn't find " << name << std::endl;
		std::exit(1);
	}
	return i->second;
}