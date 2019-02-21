#include "precompiled.hpp"
#include "database.hpp"
#include "stringutils.hpp"
#include "ioutils.hpp"
#include <regex>

using std::vector;
using std::pair;
using std::uint64_t;
using namespace std::literals::string_view_literals;

struct GadgetSet {
	vector<uint64_t> ids;
	vector<pair<uint64_t, uint64_t>> ranges; //inclusive, exclusive
	vector<std::string> names;
};

GadgetSet parse_gid_specs(const std::vector<std::string_view>& specs) {
	std::regex is_integer(R"((\d+))"), is_range(R"((\(|\[)(\d+), ?(\d+)(\)|\]))");
	std::cmatch match;
	GadgetSet g;
	for (std::string_view v : specs) {
		if (std::regex_match(v.begin(), v.end(), is_integer))
			g.ids.push_back(from_string<uint64_t>(v));
		else if (std::regex_match(v.begin(), v.end(), match, is_range)) {
			uint64_t lower = from_string<uint64_t>(std::string_view(match[2].first, match[2].length())),
					upper = from_string<uint64_t>(std::string_view(match[3].first, match[3].length()));
			if (match[1] == "(")
				++lower;
			if (match[4] == "]")
				++upper;
			if (!(lower < upper))
				throw std::runtime_error(fmt::format("bad gid range: {}", v));
			g.ranges.emplace_back(lower, upper);
		} else
			g.names.emplace_back(v);
	}
	return g;
}

int main(int argc, char* argv[]) { //genbuild entrypoint
	std::string_view db_user = "jbosboom", db_pass = "", db_host = "127.0.0.1",
			db_port = "5432", db_name = "togglesearch";
	std::vector<std::string> worker_addrs; //or @foo for response files
	std::string_view checkpoint_file = ""; //TODO: split into resume file and path to save new checkpoints
	bool multiplayer = false;
	std::vector<std::string_view> gid_specs;
	for (int i = 1; i < argc; ++i) {
		if (argv[i] == "--db-user"sv)
			db_user = argv[++i];
		else if (argv[i] == "--db-pass"sv)
			db_pass = argv[++i];
		else if (argv[i] == "--db-host"sv)
			db_host = argv[++i];
		else if (argv[i] == "--db-port"sv)
			db_port = argv[++i];
		else if (argv[i] == "--db-name"sv)
			db_name = argv[++i];
		else if (argv[i] == "--worker"sv)
			worker_addrs.emplace_back(argv[++i]);
		else if (argv[i] == "--checkpoint"sv)
			checkpoint_file = argv[++i];
		else if (argv[i] == "--multiplayer"sv)
			multiplayer = true;
		else
			gid_specs.emplace_back(argv[i]);
	}

	if (worker_addrs.empty()) {
		fmt::print("ERROR: no worker address arguments given, exiting\n");
		return 1;
	}
	worker_addrs = processFilenameArgs(std::move(worker_addrs));
	if (worker_addrs.empty()) {
		fmt::print("ERROR: no worker addresses after processing response files, exiting\n");
		return 1;
	}

	GadgetSet spec = parse_gid_specs(gid_specs);

	std::string connect_str = format_connect_string(db_user, db_pass, db_host, db_port, db_name);

	return 0;
}