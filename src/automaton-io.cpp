// SPDX-License-Identifier: MIT
// Copyright 2017 Massachusetts Institute of Technology
#include "precompiled.hpp"
#include "automatonbase.hpp"
#include "ioutils.hpp"
#include "stringutils.hpp"

using state_type = automaton::AutomatonBase::state_type;
using symbol_type = automaton::AutomatonBase::symbol_type;

namespace automaton {
//This needs to be in the namespace so it can compete with automaton::to_string.
using std::to_string;

std::string defaultFilename(const AutomatonBase& a) {
	const char* category = a.canonical() ? "can" :
			a.minimal() ? "min" :
			a.deterministic() ? "det" :
			"non";
//	return fmt::format("{}-{}-{}-{}-{}-{}.auto",
//			a.alphabet_size(),
//			a.state_size(),
//			a.edge_size(),
//			a.transition_size(),
//			category,
//			a.hash());
	return to_string(a.alphabet_size()) + "-" +
			to_string(a.state_size()) + "-" +
			to_string(a.edge_size()) + "-" +
			to_string(a.transition_size()) + "-" +
			category + "-" +
			to_string(a.hash()) + ".auto";
}

void serialize(const AutomatonBase& a, std::string filename) {
	//start with a human-readable description
	std::string humanable = automaton::to_string(a);
	std::vector<std::string> lines = split(humanable, '\n');
	//make them comments
	for (std::string& l : lines)
		l = "# "+l;

	lines.push_back(to_string(a.alphabet_size()) + " " + to_string(a.state_size()));
	std::vector<std::string> accepts;
	a.for_each_accept([&accepts](state_type s) {
		accepts.push_back(to_string(s));
	});
	lines.push_back("accept " + join(accepts, " "));

	a.for_each_transition([&lines](state_type from, symbol_type on, state_type to) {
		lines.push_back("trans " + to_string(from) + " " + to_string(on) + " " + to_string(to));
	});

	writeAllLines(filename, lines);
}

std::unique_ptr<WorkingAutomaton> deserialize(std::string filename) {
	std::vector<std::string> lines = readAllLines(filename);
	//ignore comments
	lines.erase(std::remove_if(lines.begin(), lines.end(), [](const std::string& s){
		return s.empty() || s[0] == '#';
	}), lines.end());

	std::vector<std::string_view> tokens = split_view(lines[0], ' ');
	auto alphabet_size = from_string<symbol_type>(tokens[0]);
	std::unique_ptr<WorkingAutomaton> a = make_working(alphabet_size);
	auto state_size = from_string<state_type>(tokens[1]);
	a->reserve(state_size);
	for (state_type s = 0; s < state_size; ++s)
		a->addState();
	for (auto it = lines.begin()+1; it != lines.end(); ++it) {
		tokens.clear();
		split_view(tokens, *it, ' ');
		if (tokens[0] == "accept")
			for (auto p = tokens.begin()+1; p != tokens.end(); ++p)
				a->setAccept(from_string<state_type>(*p));
		else if (tokens[0] == "trans") {
			assert(tokens.size() == 4);
			a->addTrans(from_string<state_type>(tokens[1]), from_string<symbol_type>(tokens[2]),
					from_string<state_type>(tokens[3]));
		} else {
			std::cout << "bad: " << tokens[0] << std::endl;
			std::abort();
		}
	}
	return a;
}

} //namespace automaton