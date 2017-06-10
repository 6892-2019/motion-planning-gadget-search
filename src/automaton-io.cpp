#include "precompiled.hpp"
#include "automatonbase.hpp"
#include "ioutils.hpp"

using std::to_string;
using boost::lexical_cast;
using state_type = automaton::AutomatonBase::state_type;
using symbol_type = automaton::AutomatonBase::symbol_type;

namespace automaton {

void serialize(const AutomatonBase& a, std::string filename) {
	std::vector<std::string> lines;
	//start with a human-readable description
	std::string humanable = lexical_cast<std::string>(a);
	boost::algorithm::split(lines, humanable, boost::algorithm::is_any_of("\n"));
	//make them comments
	for (std::string& l : lines)
		l = "# "+l;

	lines.push_back(to_string(a.alphabet_size()) + " " + to_string(a.state_size()));
	std::vector<std::string> accepts;
	a.for_each_accept([&accepts](state_type s) {
		accepts.push_back(to_string(s));
	});
	lines.push_back("accept " + boost::algorithm::join(accepts, " "));

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

	std::vector<std::string> tokens;
	boost::algorithm::split(tokens, lines[0], boost::algorithm::is_any_of(" "));
	auto alphabet_size = lexical_cast<symbol_type>(tokens[0]);
	std::unique_ptr<WorkingAutomaton> a = make_working(alphabet_size);
	auto state_size = lexical_cast<state_type>(tokens[1]);
	a->reserve(state_size);
	for (state_type s = 0; s < state_size; ++s)
		a->addState();
	for (auto it = lines.begin()+1; it != lines.end(); ++it) {
		tokens.clear();
		boost::algorithm::split(tokens, *it, boost::algorithm::is_any_of(" "));
		if (tokens[0] == "accept")
			for (auto p = tokens.begin()+1; p != tokens.end(); ++p)
				a->setAccept(lexical_cast<state_type>(*p));
		else if (tokens[0] == "trans") {
			assert(tokens.size() == 4);
			a->addTrans(lexical_cast<state_type>(tokens[1]), lexical_cast<symbol_type>(tokens[2]),
					lexical_cast<state_type>(tokens[3]));
		} else {
			std::cout << "bad: " << tokens[0] << std::endl;
			std::abort();
		}
	}
	return a;
}

} //namespace automaton