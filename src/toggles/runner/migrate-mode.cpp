#include "precompiled.hpp"
#include "../toggles-shared.hpp"
#include "lmdb++.h"
#include <sys/mman.h>

using std::uint64_t;
using std::size_t;
using std::pair;
using std::optional;
using std::variant;
using std::vector;
using std::unique_ptr;
using std::string_view;
using namespace std::literals::string_view_literals;

void migrate_gadget_index(lmdb::env& env, std::string_view temp_dir) {
	lmdb::dbi main_db, gadget_index;
	std::FILE* hash_file = nullptr;
	long file_size = 0;
	{
		DatabaseMetadata meta = read_meta(env);

		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		main_db = lmdb::dbi::open(txn, nullptr);
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		std::string_view unused_value;
		//If the gadget index doesn't have a key for the first gadget, we've already migrated.
		if (!gadget_index.get(txn, lmdb::to_sv<uint64_t>(1), unused_value)) {
			fmt::print("gadget_index already migrated\n");
			return;
		}

		std::string filename = fmt::format("{}/gadget_index-{:x}.dat", temp_dir, meta.id);
		hash_file = std::fopen(filename.c_str(), "r+b");

		if (!hash_file) {
			if (errno != ENOENT) {
				auto savederrno = errno;
				fmt::print(stderr, "error opening hash file: {} ({})\n", strerror(savederrno), savederrno);
				std::exit(1);
			}
			hash_file = std::fopen(filename.c_str(), "w+b");
			if (!hash_file) {
				auto savederrno = errno;
				fmt::print(stderr, "error creating hash file: {} ({})\n", strerror(savederrno), savederrno);
				std::exit(1);
			}

			lmdb::cursor cur = lmdb::cursor::open(txn, gadget_index);
			std::string_view key, value;
			if (!cur.get(key, value, MDB_FIRST))
				throw std::logic_error("no first key?");
			do {
				std::size_t amount = std::fwrite(value.data(), value.size(), 1, hash_file);
				if (!amount) {
					auto savederrno = errno;
					fmt::print(stderr, "error writing hash file: {} ({})\n", strerror(savederrno), savederrno);
					std::exit(1);
				}
			} while (cur.get(key, value, MDB_NEXT));
			std::fflush(hash_file);
		}

		std::fseek(hash_file, 0, SEEK_END);
		file_size = std::ftell(hash_file);
		std::fseek(hash_file, 0, SEEK_SET);
		if (numeric_cast<std::size_t>(file_size) != gadget_index.size(txn) * sizeof(std::size_t)) {
			fmt::print(stderr, "bad hash file size: {} {}\n", file_size,
					gadget_index.size(txn) * sizeof(std::size_t), gadget_index.size(txn));
			std::exit(1);
		}

		txn.commit();
	}

	void* map_ptr = mmap(nullptr, file_size, PROT_READ, MAP_SHARED_VALIDATE, fileno(hash_file), 0);
	if (map_ptr == MAP_FAILED) {
		auto savederrno = errno;
		fmt::print(stderr, "failed to map hash file: {} ({})\n", strerror(savederrno), savederrno);
		std::exit(1);
	}
	if (madvise(map_ptr, file_size, MADV_SEQUENTIAL)) {
		auto savederrno = errno;
		fmt::print(stderr, "warning: failed to madvise: {} ({})\n", strerror(savederrno), savederrno);
		//just a speed hint, so don't exit
	}
	const char* map_begin = static_cast<const char*>(map_ptr);
	const char* map_end = map_begin + file_size;

	{
		lmdb::txn txn = lmdb::txn::begin(env);
		gadget_index.drop(txn);
		//We commit the drop before inserting to allow reuse of the freed pages
		//at the cost of transactional safety.  The hash file is effectively
		//part of the database's state now, and can't be deleted until migration
		//is completed.  (If we're interrupted loading the hash file, we may
		//have to drop the partial database manually.)
		txn.commit();
	}

	const char* map = map_begin;
	constexpr std::size_t optimal_page_size = 4096 - 16;
	constexpr int pages_per_transaction = 5000;
	while (map != map_end) {
		lmdb::txn txn = lmdb::txn::begin(env);
		{
			lmdb::cursor cur = lmdb::cursor::open(txn, gadget_index);
			for (int i = 0; i < pages_per_transaction && map != map_end; ++i) {
				std::size_t length = std::min(optimal_page_size, numeric_cast<std::size_t>(map_end - map));
				std::string_view value(map, length);
				uint64_t last_index_on_page = numeric_cast<std::size_t>(((map + length) - map_begin) / sizeof(std::size_t));
				std::string_view key = lmdb::to_sv(last_index_on_page);
				if (!cur.put(lmdb::to_sv(last_index_on_page), value, MDB_APPEND | MDB_NOOVERWRITE))
					throw std::logic_error(fmt::format("failed to put page: key {}, offset {}, in-txn {}",
							key, map - map_begin, i));
				map += length;
			}
		}
		txn.commit();
	}

	munmap(map_ptr, file_size);
	std::fclose(hash_file);
	fmt::print("migrated gadget_index\n");
}

int migrate_mode(std::string_view db_path, const vector<std::string_view>& args) {
	lmdb::env env = lmdb::env::create();
	env.set_mapsize(10UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(std::string(db_path).c_str(), MDB_NORDAHEAD);

	std::string_view temp_dir;
	bool gadget_index = false;
	for (std::size_t i = 0; i < args.size(); ++i)
		if (args[i] == "--temp"sv || args[i] == "--tmp"sv || args[i] == "--temp-dir"sv)
			temp_dir = args[++i];
		else if (args[i] == "gadget_index"sv || args[i] == "gadget-index"sv)
			gadget_index = true;
		else {
			fmt::print(stderr, "ERROR: unrecognized argument {}", args[i]);
			return 1;
		}

	if (temp_dir.empty()) {
		fmt::print(stderr, "ERROR: pass --temp-dir\n");
		return 1;
	}

	if (gadget_index)
		migrate_gadget_index(env, temp_dir);

	return 0;
}