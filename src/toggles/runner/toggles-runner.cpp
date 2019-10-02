#include "precompiled.hpp"

using std::vector;
using namespace std::literals::string_view_literals;

//defined in toggles-shared.cpp
void jemalloc_tuning();
//defined in sync.cpp
int sync_mode(std::string_view db_path, const vector<std::string_view>& files);
//defined in dump-gadget-mode.cpp
int dump_gadget_mode(std::string_view db_path, vector<std::string_view>& gadget_spec);
//defined in incoming-edges-mode.cpp
int incoming_edges_mode(std::string_view db_path, const vector<std::string_view>& gadget_spec);
//defined in invert-mode.cpp
int invert_index_mode(std::string_view db_path, std::vector<std::string_view>& args);
int invert_search_mode(std::string_view db_path, std::vector<std::string_view>& args);
//defined in db-equivalence-mode.cpp
int hash_index_mode(std::string_view db_path, std::vector<std::string_view>& args);
int hashid_index_mode(std::string_view db_path, std::vector<std::string_view>& args);
//defned in update-predicates-mode.cpp
int update_predicates_mode(std::string_view db_path, const vector<std::string_view>& more_arguments);
//defined in repair-completions-mode.cpp
int repair_completions_mode(std::string_view db_path, const vector<std::string_view>& args);
//defined in migrate-mode.cpp
int migrate_mode(std::string_view db_path, const vector<std::string_view>& args);
//defined in msgpack-mode.cpp
int msgpack_mode(std::string_view db_path, std::string_view input_file, std::string_view output_file);

int main(int argc, char* argv[]) { //genbuild {'entrypoint': True, 'ldflags': '-llmdb -lyaml-cpp'}
	setvbuf(stdout, nullptr, _IOLBF, 0); //line buffering
	jemalloc_tuning();

	std::string_view mode = "unknown-mode";
	std::string_view db_path = "/bad-db-path-arg", input_file = "-", output_file = "-";
	vector<std::string_view> positionals;
	for (int i = 1; i < argc; ++i) {
		if (i == 1)
			mode = argv[i];
		else if (argv[i] == "--db-path"sv)
			db_path = argv[++i];
		else if (argv[i] == "--input-file"sv || argv[i] == "--input"sv || argv[i] == "-i"sv)
			input_file = argv[++i];
		else if (argv[i] == "--output-file"sv || argv[i] == "--output"sv || argv[i] == "-o"sv)
			output_file = argv[++i];
		else
			//These may be not be positionals, strictly speaking, but instead
			//options parsed by the mode.
			positionals.push_back(argv[i]);
	}

	if (mode == "sync"sv)
		return sync_mode(db_path, positionals);
	else if (mode == "dump-gadget"sv || mode == "dump-gadgets"sv)
		return dump_gadget_mode(db_path, positionals);
	else if (mode == "incoming-edges"sv)
		return incoming_edges_mode(db_path, positionals);
	else if (mode == "invert-index"sv)
		return invert_index_mode(db_path, positionals);
	else if (mode == "invert-search"sv)
		return invert_search_mode(db_path, positionals);
	else if (mode == "hash-index"sv)
		return hash_index_mode(db_path, positionals);
	else if (mode == "hashid-index"sv)
		return hashid_index_mode(db_path, positionals);
	else if (mode == "predicates"sv)
		return update_predicates_mode(db_path, positionals);
	else if (mode == "completions"sv)
		return repair_completions_mode(db_path, positionals);
	else if (mode == "migrate"sv)
		return migrate_mode(db_path, positionals);
	else if (mode == "msgpack"sv) {
		if (!positionals.empty()) {
			fmt::print(stderr, "ERROR: msgpack mode takes no positional arguments, but some passed: {}\n", positionals);
			std::exit(2);
		}
		setvbuf(stdout, nullptr, _IOFBF, 0); //output not human readable -- back to full buffering
		return msgpack_mode(db_path, input_file, output_file);
	} else {
		fmt::print(stderr, "ERROR: unknown mode {}\n", mode);
		std::exit(2);
	}
	return 0;
}