#include "precompiled.hpp"
#include "gadgetdefs.hpp"
#include "../automaton.hpp"
#include "canonicalize.hpp"
#include "ops.hpp"
#include "stringutils.hpp"
#include <regex>

using namespace automaton;

namespace {
std::unique_ptr<WorkingAutomaton> make_nop(unsigned int alphabet_size, unsigned int locations) {
	if (locations > alphabet_size) return nullptr;
	//GadgetBuilder calls prepare which calls make_nop, so we have to do this
	//manually to break the cycle.
	std::unique_ptr<WorkingAutomaton> a = make_working(alphabet_size);
	a->addState();
	a->setAccept(0, true);
	for (unsigned int i = 0; i < locations; ++i) {
		auto s = a->addState();
		a->addTrans(0, i, s);
		a->addTrans(s, i, 0);
	}
	return a;
}

void setInitialStates(WorkingAutomaton& a, const StateSet& initialStates) {
	using state_type = WorkingAutomaton::state_type;
	state_type s = a.addState();
	for (state_type t : initialStates)
		a.addEpsilon(s, t);
	a.swapStateNumbers(0, s);
}

void branchToAnyAcceptState(WorkingAutomaton& a) {
	StateSet accepting;
	for (AutomatonBase::state_type s = 0; s < a.state_size(); ++s)
		if (a.accept(s))
			accepting.insert_absent(s);
	setInitialStates(a, accepting);
}

std::unique_ptr<WorkingAutomaton> prepare(const WorkingAutomaton& a) {
	std::unique_ptr<WorkingAutomaton> q = a.clone();
	q->minimize();
	q = shuffleAccept(*q, *make_nop(q->alphabet_size(), q->active_alphabet_size()), q->alphabet_size());
	q->minimize();
	acceptingClosure(*q, q->active_alphabet_size());
	branchToAnyAcceptState(*q);
	q->minimize();
	canonicalize(*q, q->active_alphabet_size());
	return q;
}

struct GadgetLine {
	WorkingAutomaton::state_type start;
	WorkingAutomaton::symbol_type from;
	WorkingAutomaton::symbol_type to;
	WorkingAutomaton::state_type end;
};

class GadgetBuilder {
public:
	GadgetBuilder(unsigned int alphabet_size, WorkingAutomaton::state_type states) : gadget(make_working(alphabet_size)) {
		for (WorkingAutomaton::state_type i = 0; i < states; ++i) {
			gadget->addState();
			gadget->setAccept(i);
		}
	}
	GadgetBuilder& trans(WorkingAutomaton::state_type start, WorkingAutomaton::symbol_type from,
			WorkingAutomaton::symbol_type to, WorkingAutomaton::state_type end) {
		assert(gadget->accept(start));
		assert(gadget->accept(end));
		WorkingAutomaton::state_type t = gadget->addState();
		gadget->addTrans(start, from, t);
		gadget->addTrans(t, to, end);
		return *this;
	}
	std::unique_ptr<WorkingAutomaton> build() {
		branchToAnyAcceptState(*gadget);
		return prepare(std::move(*gadget));
	}

	static std::unique_ptr<WorkingAutomaton> interpret(unsigned int alphabet_size,
			const GadgetLine* first, const GadgetLine* last) {
		WorkingAutomaton::state_type max_state = 0;
		for (GadgetLine l : make_range_for_pair(first, last))
			max_state = std::max({max_state, l.start, l.end});
		GadgetBuilder b(alphabet_size, max_state+1);
		for (GadgetLine l : make_range_for_pair(first, last))
			b.trans(l.start, l.from, l.to, l.end);
		return b.build();
	}
private:
	std::unique_ptr<WorkingAutomaton> gadget;
};

using std::array;
using std::pair;
using std::tuple;
using std::string_view;
using namespace std::literals::string_view_literals;

const pair<string_view, string_view> gadget_aliases[] = {
	{"split"sv, "3-split"sv},
	{"1-toggle"sv, "parallel-1-toggle"sv},
	{"crossover-2-toggle"sv, "crossing-2-toggle"sv},
	{"det-split"sv, "deterministic-split"sv},
};
string_view translate_alias(string_view name) {
	for (auto [from, to] : gadget_aliases)
		if (name == from)
			return to;
	return name;
}

const GadgetLine diode[] = {
	{0, 0, 1, 0},
};
const GadgetLine crossover[] = {
	{0, 0, 2, 0}, {0, 2, 0, 0}, {0, 1, 3, 0}, {0, 3, 1, 0},
};
const GadgetLine crossing_wire_diode[] = {
	{0, 0, 2, 0}, {0, 1, 3, 0}, {0, 3, 1, 0},
};
const GadgetLine crossing_diode_diode[] = {
	{0, 0, 2, 0}, {0, 1, 3, 0},
};
const GadgetLine antiparallel_2_toggle[] = {
	{0, 0, 1, 1}, {0, 2, 3, 1},
	{1, 1, 0, 0}, {1, 3, 2, 0},
};
const GadgetLine crossing_2_toggle[] = {
	{0, 0, 2, 1}, {0, 3, 1, 1},
	{1, 2, 0, 0}, {1, 1, 3, 0},
};
const GadgetLine noncrossing_tripwire_lock[] = {
	{0, 0, 1, 1}, {0, 1, 0, 1}, {0, 2, 3, 0}, {0, 3, 2, 0},
	{1, 0, 1, 0}, {1, 1, 0, 0},
};
const GadgetLine crossing_tripwire_lock[] = {
	{0, 0, 2, 1}, {0, 2, 0, 1}, {0, 1, 3, 0}, {0, 3, 1, 0},
	{1, 0, 2, 0}, {1, 2, 0, 0},
};
const GadgetLine noncrossing_toggle_lock[] = {
	{0, 0, 1, 1}, {0, 2, 3, 0}, {0, 3, 2, 0},
	{1, 1, 0, 0},
};
const GadgetLine crossing_toggle_lock[] = {
	{0, 0, 2, 1}, {0, 1, 3, 0}, {0, 3, 1, 0},
	{1, 2, 0, 0},
};
const GadgetLine noncrossing_tripwire_toggle[] = {
	{0, 0, 1, 1}, {0, 2, 3, 1}, {0, 3, 2, 1},
	{1, 1, 0, 0}, {1, 2, 3, 0}, {1, 3, 2, 0},
};
const GadgetLine crossing_tripwire_toggle[] = {
	{0, 0, 2, 1}, {0, 1, 3, 1}, {0, 3, 1, 1},
	{1, 2, 0, 0}, {1, 1, 3, 0}, {1, 3, 1, 0},
};
const GadgetLine parallel_seven_seven[] = {
	{0, 2, 3, 0}, {0, 3, 2, 0},
	{0, 2, 3, 1}, {0, 0, 1, 1},
	{1, 1, 0, 0}, {1, 3, 2, 0},
	{1, 0, 1, 1}, {1, 1, 0, 1},
};
const GadgetLine antiparallel_seven_seven[] = {
	{0, 2, 3, 0}, {0, 3, 2, 0},
	{0, 3, 2, 1}, {0, 0, 1, 1},
	{1, 1, 0, 0}, {1, 2, 3, 0},
	{1, 0, 1, 1}, {1, 1, 0, 1},
};
const GadgetLine crossing_seven_seven[] = {
	{0, 1, 3, 0}, {0, 3, 1, 0},
	{0, 1, 3, 1}, {0, 0, 2, 1},
	{1, 2, 0, 0}, {1, 3, 1, 0},
	{1, 0, 2, 1}, {1, 2, 0, 1},
};
const GadgetLine seven_lock[] = {
	{0, 2, 3, 0}, {0, 3, 2, 0},
	{0, 0, 1, 1},
	{1, 1, 0, 0},
	{1, 0, 1, 1}, {1, 1, 0, 1},
};
const GadgetLine seven_tripwire[] = {
	{0, 0, 1, 1}, {0, 2, 3, 1}, {0, 3, 2, 1},
	{1, 1, 0, 0}, {1, 2, 3, 0}, {1, 3, 2, 0},
	{1, 0, 1, 1}, {1, 1, 0, 1},
};
const GadgetLine dichotomizer[] = {
	{0, 0, 1, 1}, {0, 3, 2, 2},
	{1, 1, 0, 0},
	{2, 2, 3, 0},
};
const GadgetLine deterministic_split[] = {
	{0, 0, 1, 1}, {0, 2, 0, 1},
	{1, 0, 2, 0}, {1, 1, 0, 0},
};

//gadgets 1 and 2 from https://coauthor.csail.mit.edu/6.890/m/gYp2jGJ75FL6ZchTS
//(3 is the deterministic split and 4 is the 3-spinner)
const GadgetLine bu2s3l_1[] = {
	{0, 0, 1, 1}, {0, 1, 0, 1}, {0, 2, 0, 1},
	{1, 0, 2, 0},
};
const GadgetLine bu2s3l_2[] = {
	{0, 0, 1, 1}, {0, 1, 0, 1}, {0, 2, 0, 1},
	{1, 0, 2, 0}, {1, 1, 2, 0}, {1, 2, 1, 0},
};

const tuple<string_view, const GadgetLine*, const GadgetLine*> simple_gadgets[] = {
	{"diode"sv, std::begin(diode), std::end(diode)},
	{"crossover"sv, std::begin(crossover), std::end(crossover)},
	{"crossing-wire-diode"sv, std::begin(crossing_wire_diode), std::end(crossing_wire_diode)},
	{"crossing-diode-diode"sv, std::begin(crossing_diode_diode), std::end(crossing_diode_diode)},
	{"antiparallel-2-toggle"sv, std::begin(antiparallel_2_toggle), std::end(antiparallel_2_toggle)},
	{"crossing-2-toggle"sv, std::begin(crossing_2_toggle), std::end(crossing_2_toggle)},
	{"noncrossing-tripwire-lock"sv, std::begin(noncrossing_tripwire_lock), std::end(noncrossing_tripwire_lock)},
	{"crossing-tripwire-lock"sv, std::begin(crossing_tripwire_lock), std::end(crossing_tripwire_lock)},
	{"noncrossing-toggle-lock"sv, std::begin(noncrossing_toggle_lock), std::end(noncrossing_toggle_lock)},
	{"crossing-toggle-lock"sv, std::begin(crossing_toggle_lock), std::end(crossing_toggle_lock)},
	{"noncrossing-tripwire-toggle"sv, std::begin(noncrossing_tripwire_toggle), std::end(noncrossing_tripwire_toggle)},
	{"crossing-tripwire-toggle"sv, std::begin(crossing_tripwire_toggle), std::end(crossing_tripwire_toggle)},
	{"parallel-seven-seven"sv, std::begin(parallel_seven_seven), std::end(parallel_seven_seven)},
	{"antiparallel-seven-seven"sv, std::begin(antiparallel_seven_seven), std::end(antiparallel_seven_seven)},
	{"crossing-seven-seven"sv, std::begin(crossing_seven_seven), std::end(crossing_seven_seven)},
	{"seven-lock"sv, std::begin(seven_lock), std::end(seven_lock)},
	{"seven-tripwire"sv, std::begin(seven_tripwire), std::end(seven_tripwire)},
	{"dichotomizer"sv, std::begin(dichotomizer), std::end(dichotomizer)},
	{"deterministic-split"sv, std::begin(deterministic_split), std::end(deterministic_split)},
	{"bu2s3l_1"sv, std::begin(bu2s3l_1), std::end(bu2s3l_1)},
	{"bu2s3l_2"sv, std::begin(bu2s3l_2), std::end(bu2s3l_2)},
};

unsigned int parse_locations(unsigned int alphabet_size, const std::cmatch& match) {
	unsigned int locations = to_uint(std::string_view(match[1].first, match[1].length()));
	if (locations == 0)
		throw std::runtime_error("0-location gadget is invalid");
	if (locations > alphabet_size)
		throw bad_alphabet_size(match.str(), alphabet_size, locations);
	return locations;
}

std::unique_ptr<WorkingAutomaton> make_nop(unsigned int alphabet_size, const std::cmatch& match) {
	auto q = make_nop(alphabet_size, parse_locations(alphabet_size, match));
	return prepare(*q);
}

std::unique_ptr<WorkingAutomaton> make_split(unsigned int alphabet_size, const std::cmatch& match) {
	unsigned int locations = parse_locations(alphabet_size, match);
	GadgetBuilder b(alphabet_size, 1);
	for (auto i : xrange(locations))
		for (auto j : xrange(locations))
			b.trans(0, i, j, 0);
	return b.build();
}

std::unique_ptr<WorkingAutomaton> make_parallel_toggle(unsigned int alphabet_size, const std::cmatch& match) {
	unsigned int lines = to_uint(std::string_view(match[1].first, match[1].length()));
	unsigned int locations = 2*lines;
	if (locations == 0)
		throw std::runtime_error("0-location gadget is invalid");
	if (locations > alphabet_size)
		throw bad_alphabet_size(match.str(), alphabet_size, locations);
	GadgetBuilder b(alphabet_size, 2);
	for (auto i : xrange(lines)) {
		b.trans(0, (locations - i) % locations, i+1, 1);
		b.trans(1, i+1, (locations - i) % locations, 0);
	}
	return b.build();
}

std::unique_ptr<WorkingAutomaton> make_spinner(unsigned int alphabet_size, const std::cmatch& match) {
	unsigned int locations = parse_locations(alphabet_size, match);
	GadgetBuilder b(alphabet_size, 2);
	for (auto i : xrange(locations)) {
		b.trans(0, i, (i+1) % locations, 1);
		b.trans(1, (i+1) % locations, i, 0);
	}
	return b.build();
}

using RegexGadgetFactory = std::unique_ptr<WorkingAutomaton>(*)(unsigned int alphabet_size, const std::cmatch&);
const pair<string_view, RegexGadgetFactory> regex_gadgets[] = {
	{"(\\d+)-nop"sv, make_nop},
	{"(\\d+)-split"sv, make_split},
	{"parallel-(\\d+)-toggle"sv, make_parallel_toggle},
	{"(\\d+)-spinner"sv, make_spinner},
};

} //anonymous namespace

std::unique_ptr<WorkingAutomaton> known_gadget(std::string_view name, unsigned int alphabet_size) {
	name = translate_alias(name);

	for (auto [n, first, last] : simple_gadgets)
		if (name == n)
			return GadgetBuilder::interpret(alphabet_size, first, last);

	std::cmatch match;
	for (auto [r, factory] : regex_gadgets) {
		std::regex expr(r.data(), r.length());
		if (std::regex_match(name.begin(), name.end(), match, expr))
			return factory(alphabet_size, match);
	}

	throw unknown_gadget(name, alphabet_size);
}

std::vector<std::string_view> known_gadget_keys() {
	std::vector<std::string_view> ret;
	for (auto& q : simple_gadgets)
		ret.push_back(std::get<std::string_view>(q));
	for (auto& q : regex_gadgets)
		ret.push_back(std::get<std::string_view>(q));
	return ret;
}

std::string unknown_gadget::format(const std::string& thing, unsigned int requested) {
	//TODO: maybe replace with fmt formatting
	return "unknown gadget "+thing+" (with requested size "+std::to_string(requested)+")";
}

std::string bad_alphabet_size::format(const std::string& gadget, unsigned int requested, unsigned int required) {
	return "bad alphabet size for "+gadget+": "+std::to_string(requested)+" requested, but "+std::to_string(required)+" required";
}
