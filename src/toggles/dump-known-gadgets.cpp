#include "precompiled.hpp"
#include "gadgetdefs.hpp"
#include "stringutils.hpp"
#include "automaton-io.hpp"

using namespace automaton;
using std::vector;
using std::string;
using std::pair;
using std::unique_ptr;

namespace {
vector<pair<string, unique_ptr<WorkingAutomaton>>> getAll(unsigned int alphabetSize) {
	vector<pair<string, unique_ptr<WorkingAutomaton>>> result;
	for (auto name : known_gadget_keys()) {
		auto start = name.find("(\\d+)");
		if (start == string::npos)
			result.emplace_back(name, known_gadget(name, alphabetSize));
		else {
			for (unsigned int i = 1; i <= alphabetSize; ++i) {
				string instance(name);
				instance.replace(start, std::strlen("(\\d+)"), std::to_string(i));
				try {
					result.emplace_back(instance, known_gadget(instance, alphabetSize));
				} catch (const bad_alphabet_size&) {
					break;
				}
			}
		}
	}
	return result;
}
}

int main(int argc, char* argv[]) { //genbuild entrypoint
	int alphabetSize = to_int(argv[1]);
	string destFolder = argv[2];
	for (auto& [name, pa] : getAll(alphabetSize)) {
		string dest = destFolder + "/" + name + "-" + defaultFilename(*pa);
		serialize(*pa, dest);
	}
	return 0;
}