#include "precompiled.hpp"
#include "../select-by-id.hpp"
#include "../toggles-shared.hpp"
#include "intervals.hpp"
#include "transform_reduce.hpp"
#include "stringutils.hpp"
#include <lmdb++.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h> //for fallocate

using std::uint64_t;
using std::size_t;
using std::uint32_t;
using std::pair;
using std::vector;
using std::deque;
using std::string_view;
using namespace std::literals::string_view_literals;

namespace {
struct empty_header {}; //don't want to pull in <variant> just for std::monostate

struct HashidHeader {
	std::size_t header_size;
	std::uint64_t database_id;
	std::time_t timestamp;
	unsigned int id_bytes;
};

template<unsigned int X>
struct HashId {
	uint64_t hash;
	std::array<std::byte, X> id;
	bool operator<(const HashId<X>& other) const {
		return hash < other.hash;
	}
};

//TODO: copied from invert-mode.cpp; see also necessary_bytes in gadget-encoding.cpp
unsigned int necessary_bytes(std::size_t x) {
	unsigned int i = 1;
	while (x /= 256) ++i;
	return i;
}

//copied from invert-mode.cpp
struct merge_deques {
	template<typename T>
	deque<T> operator()(deque<T>&& left, deque<T>&& right) const {
		//Edges are unique so we don't need merge_unique.
		//TODO: we could use std::merge if we had a pop_front_iterator; see GitHub #112.
		deque<T> result; //no need to reserve because deque grows incrementally
		while (!left.empty() && !right.empty()) {
			if (right.front() < left.front()) { //preserve order for equal elements (though we shouldn't have any here)
				result.push_back(std::move(right.front()));
				right.pop_front();
			} else {
				result.push_back(std::move(left.front()));
				left.pop_front();
			}
		}
		while (!left.empty()) {
			result.push_back(std::move(left.front()));
			left.pop_front();
		}
		while (!right.empty()) {
			result.push_back(std::move(right.front()));
			right.pop_front();
		}
		//Left and right are empty, and deque should release its memory, but
		//let's make sure they're left in the moved-from state.
		deque<T> release_left(std::move(left)), release_right(std::move(right));
		return result;
	}
};

//copied from invert-mode.cpp
template<typename T>
deque<T> sort_and_merge(vector<deque<T>>&& data, unsigned int threads) {
	return transform_reduce(std::move(data), threads, [](deque<T> block) {
		std::sort(block.begin(), block.end());
		return block;
	}, merge_deques());
}

template<typename T>
deque<T> sort_unique_and_merge(vector<deque<T>>&& data, unsigned int threads) {
	return transform_reduce(std::move(data), threads, [](deque<T> block) {
		std::sort(block.begin(), block.end());
		block.erase(std::unique(block.begin(), block.end()), block.end());
		return block;
	}, merge_deques());
}

//copied from invert-mode.cpp
struct concatenate_vectors {
	template<typename T>
	vector<T> operator()(vector<T>&& left_rref, vector<T>&& right_rref) {
		//I'm not sure if it's safe to return the left argument object, because
		//that might result in a self-move assignment.  Moves are cheap, so be
		//safe and move into a fresh object.
		vector<T> left(std::move(left_rref)), right(std::move(right_rref));
		left.insert(left.end(), std::move_iterator(right.begin()), std::move_iterator(right.end()));
		return left;
	}
};

template<unsigned int X>
std::vector<std::deque<HashId<X>>> invert_gadget_index_with_id(lmdb::env& env, lmdb::dbi& gadget_index,
		uint64_t id_upper_bound, unsigned int threads) {
	vector<pair<uint64_t, uint64_t>> every_gadget_ever = {{1, id_upper_bound}};
	std::size_t chunk_size = std::max<std::size_t>(id_upper_bound / (threads * 1000), 1000);
	return transform_reduce(interval_chunk(every_gadget_ever.begin(), every_gadget_ever.end(), chunk_size), threads,
			[&](vector<pair<uint64_t, uint64_t>> chunk) {
				auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
				vector<pair<uint64_t, uint64_t>> id_hash = select_gadget_id_to_hash(txn, gadget_index, std::move(chunk));
				txn.commit();

				deque<HashId<X>> ret;
				for (const pair<uint64_t, uint64_t>& p : id_hash) {
					HashId<X> h;
					h.hash = p.second;
					std::memcpy(&h.id, &p.first, h.id.size());
					ret.push_back(h);
				}

				vector<deque<HashId<X>>> real_ret;
				real_ret.push_back(std::move(ret));
				return real_ret;
			}, concatenate_vectors());
}

std::vector<std::deque<uint64_t>> invert_gadget_index(lmdb::env& env, lmdb::dbi& gadget_index,
		uint64_t id_upper_bound, unsigned int threads) {
	vector<pair<uint64_t, uint64_t>> every_gadget_ever = {{1, id_upper_bound}};
	std::size_t chunk_size = std::max<std::size_t>(id_upper_bound / (threads * 1000), 1000);
	return transform_reduce(interval_chunk(every_gadget_ever.begin(), every_gadget_ever.end(), chunk_size), threads,
			[&](vector<pair<uint64_t, uint64_t>> chunk) {
				auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
				vector<pair<uint64_t, uint64_t>> id_hash = select_gadget_id_to_hash(txn, gadget_index, std::move(chunk));
				txn.commit();

				deque<uint64_t> ret;
				for (const pair<uint64_t, uint64_t>& p : id_hash)
					ret.push_back(p.second);

				vector<deque<uint64_t>> real_ret;
				real_ret.push_back(std::move(ret));
				return real_ret;
			}, concatenate_vectors());
}

template<typename T, typename H>
void write_to_file(std::deque<T> data, const H& header, const std::string& filename) {
	FILE* file = std::fopen(filename.c_str(), "wb");
	if (!file) {
		fmt::print(stderr, "error opening {} {}\n", filename, errno);
		std::exit(1);
	}

	constexpr std::size_t header_size = std::is_empty_v<H> ? 0 : sizeof(H);
	int fd = fileno(file);
	std::size_t total_length = header_size + data.size() * sizeof(data.front());
	if (fallocate(fd, 0, 0, total_length))
		if (errno == EOPNOTSUPP)
			fmt::print(stderr, "warning: fallocate({}) not supported\n", filename);
		else {
			fmt::print(stderr, "error fallocate({}, 0, 0, {}) for {}\n", fd, total_length, filename);
			std::exit(1);
		}

	std::fwrite(&header, header_size, 1, file);
	while (!data.empty()) {
		std::fwrite(&data.front(), sizeof(data.front()), 1, file);
		data.pop_front();
	}

	auto actual_length = std::ftell(file);
	if ((uint64_t)actual_length != total_length) { //if ftell returned -1, we'll correctly see it as an error
		fmt::print(stderr, "error: planned to write {} bytes, but actually wrote {}, {}\n",
				total_length, actual_length, filename);
		std::exit(1);
	}
	std::fclose(file);
}
}//end anonymous namespace

//Builds a hash index file.  It's literally just all of the hashes in sorted
//order, with no metadata.
int hash_index_mode(std::string_view db_path, std::vector<std::string_view>& args) {
	unsigned int read_threads = std::numeric_limits<unsigned int>::max(),
			cpu_threads = std::thread::hardware_concurrency();
	for (std::size_t i = 0; i < args.size(); ++i)
		if (args[i] == "--threads"sv)
			read_threads = cpu_threads = to_uint(args[++i]);
		else if (args[i] == "--read-threads"sv)
			read_threads = to_uint(args[++i]);
		else if (args[i] == "--cpu-threads"sv)
			cpu_threads = to_uint(args[++i]);
		else {
			fmt::print(stderr, "unrecognized argument {}\n", args[i]);
			return 1;
		}

	lmdb::env env = lmdb::env::create();
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(std::string(db_path).c_str(), MDB_NORDAHEAD);
	unsigned int lmdb_max_readers = 0;
	lmdb::env_get_max_readers(env.handle(), &lmdb_max_readers);
	//See comment in invert-mode.cpp.
	read_threads = std::min(read_threads, lmdb_max_readers - 1);
	DatabaseMetadata meta = read_meta(env);

	//We read gadget_index and sort it by hash.  We could instead iterate
	//gadget_hashtable and extract the id from the value.  In both cases we're
	//better off storing pairs instead of pointers to database items.  The
	//current strategy iterates the smaller table at the cost of having to sort.
	lmdb::dbi gadget_index;
	uint64_t id_upper_bound;
	{
		auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		id_upper_bound = get_current_max_gadget_id(txn, gadget_index) + 1;
		txn.commit();
	}

	std::string filename = fmt::format("{}/{}.idx", db_path, "hashes");
	//TODO: if the file exists, it might be nice to exit without doing any work
	//if it's as recent as the database or contains the same number of gadgets.
	write_to_file(
			sort_and_merge(
					invert_gadget_index(env, gadget_index, id_upper_bound, read_threads),
			cpu_threads),
			empty_header(), filename);
	return 0;
}

//Builds a hash -> id index file.  We can use these to speed up looking for
//equivalent hashes across databases, because gadgets not in the same hash chunk
//cannot be equal.  Most of the speedup comes from these indices being
//sequential, as opposed to the fragmented B-tree; they might not be worth using
//if the database is on SSD.  (But they make the code a bit easier to write.)
int hashid_index_mode(std::string_view db_path, std::vector<std::string_view>& args) {
	unsigned int read_threads = std::numeric_limits<unsigned int>::max(),
			cpu_threads = std::thread::hardware_concurrency();
	for (std::size_t i = 0; i < args.size(); ++i)
		if (args[i] == "--threads"sv)
			read_threads = cpu_threads = to_uint(args[++i]);
		else if (args[i] == "--read-threads"sv)
			read_threads = to_uint(args[++i]);
		else if (args[i] == "--cpu-threads"sv)
			cpu_threads = to_uint(args[++i]);
		else {
			fmt::print(stderr, "unrecognized argument {}\n", args[i]);
			return 1;
		}

	lmdb::env env = lmdb::env::create();
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(std::string(db_path).c_str(), MDB_NORDAHEAD);
	unsigned int lmdb_max_readers = 0;
	lmdb::env_get_max_readers(env.handle(), &lmdb_max_readers);
	//See comment in invert-mode.cpp.
	read_threads = std::min(read_threads, lmdb_max_readers - 1);
	DatabaseMetadata meta = read_meta(env);

	//We read gadget_index and sort it by hash.  We could instead iterate
	//gadget_hashtable and extract the id from the value.  In both cases we're
	//better off storing pairs instead of pointers to database items.  The
	//current strategy iterates the smaller table at the cost of having to sort.
	lmdb::dbi gadget_index;
	uint64_t id_upper_bound;
	{
		auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		id_upper_bound = get_current_max_gadget_id(txn, gadget_index) + 1;
		txn.commit();
	}

	HashidHeader header;
	header.header_size = sizeof(HashidHeader);
	header.database_id = meta.id;
	header.timestamp = std::time(nullptr);
	header.id_bytes = necessary_bytes(id_upper_bound);

	std::string filename = fmt::format("{}/{}.idx", db_path, "hashid");
	//TODO: if the file exists, it might be nice to exit without doing any work
	//if it's as recent as the database or contains the same number of gadgets.
	switch (header.id_bytes) {
		case 1: write_to_file(sort_and_merge(invert_gadget_index_with_id<1>(env, gadget_index, id_upper_bound, read_threads), cpu_threads), header, filename); break;
		case 2: write_to_file(sort_and_merge(invert_gadget_index_with_id<1>(env, gadget_index, id_upper_bound, read_threads), cpu_threads), header, filename); break;
		case 3: write_to_file(sort_and_merge(invert_gadget_index_with_id<1>(env, gadget_index, id_upper_bound, read_threads), cpu_threads), header, filename); break;
		case 4: write_to_file(sort_and_merge(invert_gadget_index_with_id<1>(env, gadget_index, id_upper_bound, read_threads), cpu_threads), header, filename); break;
		case 5: write_to_file(sort_and_merge(invert_gadget_index_with_id<1>(env, gadget_index, id_upper_bound, read_threads), cpu_threads), header, filename); break;
		default:
			fmt::print(stderr, "error: need {} id bytes, unhandled case\n", header.id_bytes);
			std::exit(1);
	}
	return 0;
}

namespace {
struct DataThing {
	lmdb::env env;
	lmdb::dbi gadget_hashtable;
	const uint64_t* hashes_begin = nullptr, *hashes_end = nullptr;
};

struct Batcher {
	const uint64_t* begin, *end, *cur;
	bool operator()(vector<uint64_t>& v) {
		v.clear();
		if (cur == end) return false;
		v.push_back(*cur++);
		while (cur != end && *cur == (v.back()+1))
			v.push_back(*cur++);
		return true;
	}
};
}//end anonymous namespace

int db_equiv_mode(std::vector<std::string_view>& args) {
	if (isatty(1)) {
		fmt::print(stderr, "error: I won't write binary output to a terminal.\n");
		return 1;
	}

	std::array<std::string_view, 2> db_paths = {};
	unsigned int db_paths_index = 0;
	unsigned int read_threads = std::numeric_limits<unsigned int>::max(),
			cpu_threads = std::thread::hardware_concurrency();
	for (std::size_t i = 0; i < args.size(); ++i)
		if (args[i] == "--threads"sv)
			read_threads = cpu_threads = to_uint(args[++i]);
		else if (args[i] == "--read-threads"sv)
			read_threads = to_uint(args[++i]);
		else if (args[i] == "--cpu-threads"sv)
			cpu_threads = to_uint(args[++i]);
		else if (db_paths_index < db_paths.size())
			db_paths[db_paths_index++] = args[i];
		else {
			fmt::print(stderr, "unrecognized argument or too many positionals: {}\n", args[i]);
			return 1;
		}

	//TODO: there has to be a cleaner way to work with DataThing...
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
	std::array<DataThing, 2> dbs = {{{lmdb::env::create()}, {lmdb::env::create()}}};
#pragma GCC diagnostic pop
	for (unsigned int i = 0; i < db_paths.size(); ++i) {
		dbs[i].env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
		dbs[i].env.set_max_dbs(64);
		dbs[i].env.open(std::string(db_paths[i]).c_str(), MDB_NORDAHEAD | MDB_RDONLY);

		{
			auto txn = lmdb::txn::begin(dbs[i].env, nullptr, MDB_RDONLY);
			dbs[i].gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
			txn.commit();
		}

		std::string hashes_index_filename = fmt::format("{}/{}.idx", db_paths[i], "hashes");
		int fd = open(hashes_index_filename.c_str(), O_RDONLY);
		if (fd) {
			struct stat s = {};
			fstat(fd, &s);
			void* m = mmap(nullptr, s.st_size, PROT_READ, MAP_SHARED_VALIDATE, fd, 0);
			close(fd); //map persists
			dbs[i].hashes_begin = reinterpret_cast<const uint64_t*>(m);
			dbs[i].hashes_end = reinterpret_cast<const uint64_t*>(m) + (s.st_size / sizeof(uint64_t));
			//TODO: not sure if MADV_SEQUENTIAL would help here.  We should
			//probably have prefetched this before running anyway.
		} else
			dbs[i].hashes_begin = dbs[i].hashes_end = nullptr;
	}

	if (!dbs[0].hashes_begin || !dbs[1].hashes_begin) {
		fmt::print(stderr, "TODO: implement cursor-based hash batching\n");
		return 1;
	}

	unsigned int lmdb_max_readers = 0;
	lmdb::env_get_max_readers(dbs[0].env.handle(), &lmdb_max_readers);
	//See comment in invert-mode.cpp.
	read_threads = std::min(read_threads, lmdb_max_readers - 1);

	vector<pair<uint64_t, uint64_t>> all_hashes = {{0, std::numeric_limits<uint64_t>::max()-10}};
	std::size_t chunk_size = std::max<std::size_t>(std::numeric_limits<uint64_t>::max() / (read_threads * 100), 250000);
	auto unsorted_results = transform_reduce(interval_chunk(all_hashes.begin(), all_hashes.end(), chunk_size), read_threads,
			[&](vector<pair<uint64_t, uint64_t>> chunk) {
				deque<pair<uint64_t, uint64_t>> ret;
				vector<uint64_t> left, right;
				for (pair<std::size_t, std::size_t> interval : chunk) {
					const uint64_t* left_begin = std::lower_bound(dbs[0].hashes_begin, dbs[0].hashes_end, interval.first);
					const uint64_t* left_end = std::lower_bound(dbs[0].hashes_begin, dbs[0].hashes_end, interval.second);
					const uint64_t* right_begin = std::lower_bound(dbs[1].hashes_begin, dbs[1].hashes_end, interval.first);
					const uint64_t* right_end = std::lower_bound(dbs[1].hashes_begin, dbs[1].hashes_end, interval.second);
					Batcher left_batcher = {left_begin, left_end, left_begin},
							right_batcher = {right_begin, right_end, right_begin};
					left_batcher(left);
					right_batcher(right);
					auto ltxn = lmdb::txn::begin(dbs[0].env, nullptr, MDB_RDONLY);
					auto rtxn = lmdb::txn::begin(dbs[1].env, nullptr, MDB_RDONLY);
					lmdb::cursor lcur = lmdb::cursor::open(ltxn, dbs[0].gadget_hashtable),
							rcur = lmdb::cursor::open(rtxn, dbs[1].gadget_hashtable);
					while (!left.empty() && !right.empty()) {
						if ((left.front() <= right.front() && right.front() <= left.back()) || //if overlapping
								(right.front() <= left.front() && left.front() <= right.back())) {
							//This does more comparisons than necessary if both sides have
							//multiple hashes, but that seems to be rare enough.
							for (uint64_t lhash : left) {
								std::string_view lkey = lmdb::to_sv(lhash);
								std::string_view lvalue;
								lcur.get(lkey, lvalue, MDB_SET);
								for (uint64_t rhash : right) {
									std::string_view rkey = lmdb::to_sv(rhash);
									std::string_view rvalue;
									rcur.get(rkey, rvalue, MDB_SET);

									if (std::equal(lvalue.begin(), lvalue.end()-8, rvalue.begin(), rvalue.end()-8)) {
										lvalue.remove_prefix(lvalue.size()-8);
										rvalue.remove_prefix(rvalue.size()-8);
										ret.emplace_back(lmdb::from_sv<uint64_t>(lvalue), lmdb::from_sv<uint64_t>(rvalue));
									}
								}
							}
						}

						if (std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end()))
							left_batcher(left);
						else if (std::lexicographical_compare(right.begin(), right.end(), left.begin(), left.end()))
							right_batcher(right);
						else {
							left_batcher(left);
							right_batcher(right);
						}
					}
				}

				vector<deque<pair<uint64_t, uint64_t>>> real_ret;
				real_ret.push_back(std::move(ret));
				return real_ret;
			}, concatenate_vectors());

	deque<pair<uint64_t, uint64_t>> sorted_results = sort_unique_and_merge(std::move(unsorted_results), cpu_threads);
	uint64_t prev_key = 0;
	std::array<std::byte, 4096> buf;
	std::byte* p = buf.data();
	while (!sorted_results.empty()) {
		//The key is delta-coded.  The value cannot be because the delta might
		//be negative, but we can varint-encode it.
		upv::write(p, sorted_results.front().first - prev_key);
		upv::write(p, sorted_results.front().second);
		prev_key = sorted_results.front().first;
		sorted_results.pop_front();
		if (std::distance(p, buf.end()) < 9*2) {
			std::fwrite(buf.data(), 1, std::distance(buf.data(), p), stdout);
			p = buf.data();
		}
	}
	std::fwrite(buf.data(), 1, std::distance(buf.data(), p), stdout);

	return 0;
}

int equiv_map_mode(std::vector<std::string_view>& args) {
	if (args.size() > 2) {
		fmt::print(stderr, "error: too many arguments\n");
		return 1;
	}

	auto do_map = [](std::string_view filename) -> pair<const std::byte*, const std::byte*> {
		int fd = open(std::string(filename).c_str(), O_RDONLY);
		if (fd) {
			struct stat s = {};
			fstat(fd, &s);
			void* m = mmap(nullptr, s.st_size, PROT_READ, MAP_SHARED_VALIDATE, fd, 0);
			close(fd); //map persists
			madvise(m, s.st_size, MADV_SEQUENTIAL);
			const std::byte* base = reinterpret_cast<const std::byte*>(m);
			return {base, base + s.st_size};
		} else {
			fmt::print(stderr, "error: failed to open {}\n", filename);
			return {nullptr, nullptr};
		}
	};

	pair<const std::byte*, const std::byte*> table = do_map(args[0]), scalar = do_map(args[1]);
	if (!table.first || !scalar.first)
		return 1;

	uint64_t key = upv::read(table.first), value = upv::read(table.first), query = upv::read(scalar.first);
	while (table.first != table.second && scalar.first != scalar.second) {
		if (key < query) {
			key = upv::read(table.first);
			value = upv::read(table.first);
		} else if (key > query)
			query = upv::read(scalar.first);
		else { //equal
			fmt::print("{}\n", value);
			key = upv::read(table.first);
			value = upv::read(table.first);
			query = upv::read(scalar.first);
		}
	}

	return 0;
}