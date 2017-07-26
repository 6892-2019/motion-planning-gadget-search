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

automaton_type make_twostate(automaton_type&& trans00, automaton_type&& trans01,
		automaton_type&& trans10, automaton_type&& trans11) {
	automaton_type a;
	a.addState(); a.addState();
	a.setAccept(0); a.setAccept(1);

	auto do_trans = [&](automaton_type::state_type from, automaton_type::state_type to, auto&& trans) {
		auto base = a.append(std::move(trans));
		a.addEpsilon(from, base);
		for (auto i = base; i < a.state_size(); ++i)
			if (a.accept(i)) {
				a.setAccept(i, false);
				a.addEpsilon(i, to);
			}
	};
	do_trans(0, 0, std::move(trans00));
	do_trans(0, 1, std::move(trans01));
	do_trans(1, 0, std::move(trans10));
	do_trans(1, 1, std::move(trans11));

	return prepare(a);
}

automaton_type make_twostate(automaton_type&& trans01, automaton_type&& trans10) {
	auto empty = [](){return automaton::empty<automaton_type::alphabet_size_v>();};
	return make_twostate(empty(), std::move(trans01), std::move(trans10), empty());
}

std::unordered_map<std::string, automaton_type> initialize_known_gadgets() {
	std::unordered_map<std::string, automaton_type> ret;
	constexpr unsigned int N = automaton_type::alphabet_size_v;
	auto lit = [](auto... symbols){return automaton::lit<N>(symbols...);};
	auto empty = [](){return automaton::empty<automaton_type::alphabet_size_v>();};

	ret["2-nop"] = prepare(make_nop(2));
	ret["3-nop"] = prepare(make_nop(3));
	ret["4-nop"] = prepare(make_nop(4));

	ret["split"] = prepare(star(nCopies(alt(lit(0), lit(1), lit(2)), 2)));

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

	ret["seven-tripwire"] = make_twostate(
			empty(),
			alt(lit(0, 1), lit(2, 3), lit(3, 2)),
			alt(lit(1, 0), lit(2, 3), lit(3, 2)),
			lit(1, 2));

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