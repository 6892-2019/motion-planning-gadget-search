#include "precompiled.hpp"
#include "../select-by-id.hpp"
#include "../toggles-shared.hpp"
#include "intervals.hpp"
#include "transform_reduce.hpp"
#include "stringutils.hpp"
#include <lmdb++.h>
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
