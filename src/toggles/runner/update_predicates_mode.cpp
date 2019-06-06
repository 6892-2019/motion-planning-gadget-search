#include "precompiled.hpp"
#include "../toggles-shared.hpp"

using std::vector;
using std::uint64_t;
using std::pair;
using namespace std::literals::string_view_literals;

int update_predicates_mode(std::string_view db_path, const vector<std::string_view>& more_arguments) {
	//TODO: option parsing to select thresd count, maybe limit on how much work to do?
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

	//maybe should return the number of gadgets added to the predicates for reporting?
	update_SL_predicates(env, predicates, gadget_hashtable, gadget_index);
	return 0;
}