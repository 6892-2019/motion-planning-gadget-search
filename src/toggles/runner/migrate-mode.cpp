#include "precompiled.hpp"
#include "lmdb++.h"

using std::uint64_t;
using std::size_t;
using std::pair;
using std::optional;
using std::variant;
using std::vector;
using std::unique_ptr;
using std::string_view;
using namespace std::literals::string_view_literals;

void migrate_gadget_index(lmdb::env& env) {
	lmdb::dbi main_db, gadget_index, migrate_index;
	{
		lmdb::txn txn = lmdb::txn::begin(env);
		main_db = lmdb::dbi::open(txn, nullptr);
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		std::string_view unused_value;
		//If the gadget index doesn't have a key for the first gadget, and there's
		//no migration in progress, we've already migrated.
		if (!gadget_index.get(txn, lmdb::to_sv<uint64_t>(1), unused_value) &&
				!main_db.get(txn, "gadget_index_migration"sv, unused_value)) {
			fmt::print("gadget_index already migrated\n");
			return;
		}
		migrate_index = lmdb::dbi::open(txn, "gadget_index_migration", MDB_CREATE | MDB_INTEGERKEY);
	}

	while (true) { //while gadget_index not empty
		//TODO: if we're interrupted while copying back from gadget_index_migration,
		//when we restart, we'll think we didn't finish copying to it.  I guess
		//we could copy only single-element values?  We still need a side table
		//to use MDB_APPEND.
		lmdb::txn txn = lmdb::txn::begin(env);
		std::size_t remaining = gadget_index.size(txn);
		if (!remaining) break;

		{ //for write cursors
			lmdb::cursor old_cur = lmdb::cursor::open(txn, gadget_index);
			lmdb::cursor new_cur = lmdb::cursor::open(txn, migrate_index);
			std::string_view key, value;
			uint64_t current = 1;
			if (new_cur.get(key, MDB_LAST)) {
				current = lmdb::from_sv<uint64_t>(key) + 1;
				fmt::print("gadget_index migration at {}\n", current);
			}

			std::size_t pages_this_transaction = 0;
			constexpr std::size_t index_page_size = (4096-16) / sizeof(std::size_t);
			std::vector<std::size_t> buffer;
			buffer.reserve(index_page_size);
			key = lmdb::to_sv(current);
			if (!old_cur.get(key, value, MDB_SET))
				throw std::logic_error("problem");
			do {
				buffer.push_back(lmdb::from_sv<std::uint64_t>(value));
				if (buffer.size() == index_page_size) {
					std::string_view page(reinterpret_cast<char*>(buffer.data()), buffer.size()*sizeof(buffer.front()));
					//key is already the last id read, and we haven't deleted yet, so it's pinned.
					if (!new_cur.put(key, page, MDB_APPEND | MDB_NOOVERWRITE))
						throw std::logic_error(fmt::format("failed to put page"));
					buffer.clear();
					++pages_this_transaction;
				}
				old_cur.del();
			} while (pages_this_transaction < 500 && old_cur.get(key, value, MDB_NEXT));
		}
		txn.commit();
	}

	while (true) { //while gadget_index_migration not empty
		lmdb::txn txn = lmdb::txn::begin(env);
		std::size_t remaining = migrate_index.size(txn);
		if (!remaining) break;

		{ //for write cursors
			lmdb::cursor src_cur = lmdb::cursor::open(txn, migrate_index);
			lmdb::cursor dest_cur = lmdb::cursor::open(txn, gadget_index);
			std::string_view key, value;
			src_cur.get(key, value, MDB_FIRST);

			//TODO: copy stuff
		}
		txn.commit();
	}

	//TODO: delete gadget_index_migration
}

int migrate_mode(std::string_view db_path, const vector<std::string_view>& args) {
	lmdb::env env = lmdb::env::create(MDB_NORDAHEAD);
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(std::string(db_path).c_str());

	for (std::string_view arg : args) {
		if (arg == "gadget_index"sv || arg == "gadget-index"sv)
			migrate_gadget_index(env);
		else {
			fmt::print(stderr, "ERROR: unrecognized {}", arg);
			return 1;
		}
	}

	return 0;
}