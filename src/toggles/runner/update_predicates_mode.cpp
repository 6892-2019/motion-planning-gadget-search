#include "precompiled.hpp"
#include "../toggles-shared.hpp"
#include "stringutils.hpp"
#include "stopwatch.hpp"

using std::vector;
using std::uint64_t;
using std::pair;
using namespace std::literals::string_view_literals;

int update_predicates_mode(std::string_view db_path, const vector<std::string_view>& args) {
	bool update = false;
	unsigned int num_threads = 1;
	vector<unsigned int> create_state;
	for (std::size_t i = 0; i < args.size(); ++i) {
		if (args[i] == "--update"sv)
			update = true;
		else if (args[i] == "--create-state"sv)
			create_state.push_back(from_string<unsigned int>(args[++i]));
		else if (args[i] == "--threads"sv)
			num_threads = from_string<unsigned int>(args[++i]);
		else {
			fmt::print(stderr, "ERROR: unknown option {}\n", args[i]);
			std::exit(2);
		}
	}

	//TODO: option parsing to select thresd count, maybe limit on how much work to do?
	//TODO: delete state predicate indices
	//TODO: option to scrap all predicates and recompute (in case they're corrupt)
	//TODO: should probably also do completion scanning in this mode, it's about the same.

	lmdb::env env = lmdb::env::create();
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(std::string(db_path).c_str(), MDB_NORDAHEAD);

	lmdb::dbi gadget_hashtable, gadget_index, predicates;
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		predicates = lmdb::dbi::open(txn, "predicates");
		txn.commit();
	}

	for (unsigned int state : create_state) {
		Stopwatch stopwatch = Stopwatch::process();
		if (create_state_predicate(env, predicates, gadget_hashtable, gadget_index, state, num_threads))
			fmt::print("created states<={} in {}\n", state, stopwatch.elapsed().hms());
		else
			fmt::print("states<={} already exists\n", state);
	}

	if (update) {
		Stopwatch stopwatch = Stopwatch::process();
		//maybe should return the number of gadgets added to the predicates for reporting?
		if (update_SL_predicates(env, predicates, gadget_hashtable, gadget_index))
			fmt::print("updated predicates in {}\n", stopwatch.elapsed().hms());
		else
			fmt::print("predicates already up-to-date\n");
	}
	return 0;
}