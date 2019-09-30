#include "precompiled.hpp"
#include "../toggles-shared.hpp"
#include "../select-by-id.hpp"
#include "../gadget-set.hpp"
#include "../anyprov.hpp" //for EdgeKind
#include "intervals.hpp"
#include "varint.hpp"
#include "transform_reduce.hpp"
#include "stringutils.hpp"
#include "proj_compare.hpp"
#include <deque>
#include <ctime>
#include <fcntl.h> //for fallocate
#include <sys/mman.h> //for mmap
#include <sys/stat.h>

using std::vector;
using std::deque;
using std::pair;
using std::uint64_t;
using std::uint32_t;
using namespace std::literals::string_view_literals;

namespace {
std::string_view env_basename(std::string_view path) {
	if (path.substr(path.size()-1) == "/"sv)
		path.remove_suffix(1);
	if (path.substr(path.size()-4) == ".mdb"sv)
		path.remove_suffix(4);
	auto start = path.rfind('/');
	if (start != std::string_view::npos)
		path.remove_prefix(start+1);
	return path;
}

struct Header {
	std::size_t header_size;
	uint64_t database_id;
	std::time_t timestamp;
	EdgeKind kind; //cannot be EdgeKind::source
	uint64_t combine_right; //or 0 if not a combine database
	unsigned int id_bytes, offset_bytes;
	std::size_t offsets; //the number of following Offset structures
};
static_assert(std::is_standard_layout_v<Header>, "Header's layout not robust");

void fill_header_from_name(Header& header, std::string_view name) {
	header.combine_right = 0;
	if (auto i = name.find("combine"sv); i != std::string_view::npos) {
		header.kind = EdgeKind::combine;
		i += "combine-"sv.size();
		header.combine_right = to_uint64(name.substr(i, std::string_view::npos));
	} else if (name.find("connect"sv) != std::string_view::npos)
		header.kind = EdgeKind::connect;
	else if (name.find("close"sv) != std::string_view::npos)
		header.kind = EdgeKind::close;
	else if (name.find("mirror"sv) != std::string_view::npos)
		header.kind = EdgeKind::mirror;
	else
		throw std::logic_error("couldn't parse kind from "+std::string(name));
}

struct OffsetBase {};

template<unsigned int X, unsigned int Y>
struct Offset : public OffsetBase {
	//little-endian
	std::array<std::byte, X> id_;
	std::array<std::byte, Y> offset_; //offset from start of edge lists, after all Offset structures
	uint64_t id() const {
		uint64_t id = 0;
		std::memcpy(&id, id_.data(), id_.size());
		return id;
	}
	std::size_t offset() const {
		std::size_t offset = 0;
		std::memcpy(&offset, offset_.data(), offset_.size());
		return offset;
	}
};

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

//TODO: use deque with larger page sizes; we'll have millions of objects so the default 512 is bad
vector<deque<pair<uint32_t, uint32_t>>> invert_skinny(lmdb::env& env, lmdb::dbi& database, unsigned int threads) {
	uint64_t max_id = get_current_max_gadget_id(env);
	std::size_t chunk_size = std::min<std::size_t>(max_id / (threads * 10), 1000);
	vector<pair<uint64_t, uint64_t>> every_gadget_ever = {{1, max_id+1}};
	return transform_reduce(interval_chunk(every_gadget_ever.begin(), every_gadget_ever.end(), chunk_size), threads,
			[&](vector<pair<uint64_t, uint64_t>> chunk) {
				vector<deque<pair<uint32_t, uint32_t>>> ret(1);
				visit_skinny_edges(env, database, std::move(chunk), [&inverted=ret[0]](uint64_t input, uint64_t output) {
					inverted.emplace_back(numeric_cast<uint32_t>(output), numeric_cast<uint32_t>(input));
					return VisitEdgeResult::proceed;
				});
				if (ret.front().empty())
					ret.pop_back();
				return ret;
			}, concatenate_vectors());
}

template<typename Edge>
vector<deque<pair<uint32_t, uint32_t>>> invert_full(lmdb::env& env, lmdb::dbi& database, unsigned int threads) {
	uint64_t max_id = get_current_max_gadget_id(env);
	std::size_t chunk_size = std::min<std::size_t>(max_id / (threads * 10), 1000);
	vector<pair<uint64_t, uint64_t>> every_gadget_ever = {{1, max_id+1}};
	return transform_reduce(interval_chunk(every_gadget_ever.begin(), every_gadget_ever.end(), chunk_size), threads,
			[&](vector<pair<uint64_t, uint64_t>> chunk) {
				vector<deque<pair<uint32_t, uint32_t>>> ret(1);
				visit_edges<Edge>(env, database, std::move(chunk), [&inverted=ret[0]](uint64_t input, const Edge& e) {
					inverted.emplace_back(numeric_cast<uint32_t>(e.output), numeric_cast<uint32_t>(input));
					return VisitEdgeResult::proceed;
				});
				if (ret.front().empty())
					ret.pop_back();
				else
					//Skinny edge tables are deduplicated, but full edge tables
					//are not, and merge_deques expects uniqueness.
					ret.front().erase(std::unique(ret.front().begin(), ret.front().end()), ret.front().end());
				return ret;
			}, concatenate_vectors());
}

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

deque<pair<uint32_t, uint32_t>> sort_and_merge(vector<deque<pair<uint32_t, uint32_t>>>&& data, unsigned int threads) {
	return transform_reduce(std::move(data), threads, [](deque<pair<uint32_t, uint32_t>> block) {
		std::sort(block.begin(), block.end());
		return block;
	}, merge_deques());
}

//If we want to parallelize encoding, this will split into appropriate groups.
//But then we can't gradually release memory with pop_front().
//vector<deque<pair<uint32_t, uint32_t>>::const_iterator> chunk_respecting_groups(
//		deque<pair<uint32_t, uint32_t>>::const_iterator first,
//		deque<pair<uint32_t, uint32_t>>::const_iterator last, unsigned int min_chunk_size) {
//	vector<deque<pair<uint32_t, uint32_t>>::const_iterator> ret;
//	ret.push_back(first);
//	while (first != last) {
//		first += std::min<std::ptrdiff_t>(min_chunk_size, std::distance(first, last));
//		first = std::adjacent_find(first, last, proj_not_equal<0>());
//		//adjacent_find returns a pointer to the first element, but we want past-the-end
//		if (first != last) ++first;
//		ret.push_back(first);
//	}
//	return ret;
//}

//We can't store the data in a deque because we can only write contiguous data
//to files, but we want to grow memory gradually (as we draw down the edges), so
//we do our own chunking.
struct CodedChunk {
	vector<pair<uint32_t, uint32_t>> id_length;
	vector<std::byte> data;
};
constexpr std::size_t chunk_datalen = 1024 * 1024; //1MB
vector<CodedChunk> encode_chunked(deque<pair<uint32_t, uint32_t>> edges) {
	vector<CodedChunk> chunks;
	CodedChunk cur;
	vector<std::byte> buf;
	while (!edges.empty()) {
		auto id = edges.front().first;
		auto first = edges.begin(), last = std::find_if(first, edges.end(), [id](const auto& p){return p.first != id;});

		uint32_t prev = 0;
		buf.resize(std::distance(first, last) * (sizeof(prev)+1)); //ensure enough space, but p determines actual length
		std::byte* p = buf.data();
		for (auto i = first; i != last; ++i) {
			varint64::write(p, i->second - prev); //delta-encode
			prev = i->second;
		}

		std::ptrdiff_t length = std::distance(buf.data(), p);
		if (cur.data.size() + length > chunk_datalen) {
			chunks.push_back(std::move(cur));
			cur.id_length.clear();
			cur.data.clear();
		}
		cur.id_length.emplace_back(id, numeric_cast<uint32_t>(length));
		cur.data.insert(cur.data.end(), buf.data(), p);
		edges.erase(first, last);
	}
	if (!cur.id_length.empty())
		chunks.push_back(std::move(cur));
	return chunks;
}

unsigned int necessary_bytes(std::size_t x) {
	unsigned int i = 1;
	while (x /= 256) ++i;
	return i;
}

bool write_fully(int fd, const void* data_any, std::size_t length) {
	const char* data = static_cast<const char*>(data_any); //avoid 'arithmetic on void*' warning
	ssize_t written;
	while (length) {
		do {
			written = write(fd, data, length);
		} while ((written < 0) && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK));
		if (written < 0) return false;
		length -= written;
		data += written;
	}
	return true;
}

void write_to_file(const std::string& filename, const Header& header, vector<CodedChunk> chunks) {
	std::size_t total_length = sizeof(Header);
	//We could merge this loop into the caller's loop over the chunks.
	for (const CodedChunk& c : chunks) {
		total_length += c.id_length.size() * (header.id_bytes + header.offset_bytes);
		total_length += c.data.size();
	}

	FILE* file = std::fopen(filename.c_str(), "wb");
	if (!file) {
		fmt::print(stderr, "error opening {} {}\n", filename, errno);
		std::exit(1);
	}

	int fd = fileno(file);
	if (fallocate(fd, 0, 0, total_length))
		if (errno == EOPNOTSUPP)
			fmt::print(stderr, "warning: fallocate({}) not supported\n", filename);
		else {
			fmt::print(stderr, "error fallocate({}, 0, 0, {}) for {}\n", fd, total_length, filename);
			std::exit(1);
		}


	std::fwrite(&header, sizeof(Header), 1, file);

	std::size_t offset;
	for (const CodedChunk& c : chunks) {
		for (pair<uint32_t, uint32_t> p : c.id_length) {
			std::fwrite(&p.first, 1, header.id_bytes, file);
			std::fwrite(&offset, 1, header.offset_bytes, file);
			offset += p.second;
		}
	}

	std::fflush(file); //switching to syscall API; flush libc buffers
	for (const CodedChunk& c : chunks)
		if (!write_fully(fd, c.data.data(), c.data.size())) {
			fmt::print(stderr, "failed while writing data to {}\n", filename);
			std::exit(1);
		}

	if (std::ferror(file)) {
		fmt::print(stderr, "some kind of error writing {}\n", filename);
		std::exit(1);
	}
	std::fclose(file);
}
} //end anonymous namespace

int invert_index_mode(std::string_view db_path, std::vector<std::string_view>& args) {
	std::string_view output_dir;
	vector<std::string_view> tables_to_invert;
	//max I/O parallelism (reduced by env's max readers); sort parallelism equal to threads
	unsigned int read_threads = std::numeric_limits<unsigned int>::max(),
			cpu_threads = std::thread::hardware_concurrency();

	for (std::size_t i = 0; i < args.size(); ++i)
		if (args[i] == "--output-dir"sv || args[i] == "--output"sv)
			output_dir = args[++i];
		else if (args[i] == "--threads"sv)
			read_threads = cpu_threads = to_uint(args[++i]);
		else if (args[i] == "--read-threads"sv)
			read_threads = to_uint(args[++i]);
		else if (args[i] == "--cpu-threads"sv)
			cpu_threads = to_uint(args[++i]);
		else
			tables_to_invert.push_back(args[i]);

	if (output_dir.empty()) {
		fmt::print(stderr, "no output directory specified\n");
		return 1;
	}

	lmdb::env env = lmdb::env::create();
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(std::string(db_path).c_str(), MDB_NORDAHEAD);
	unsigned int lmdb_max_readers = 0;
	lmdb::env_get_max_readers(env.handle(), &lmdb_max_readers);
	//We've read from this thread, so one reader slot is already taken.  Without
	//the -1, we silently read no data some of the time (or LMDB reports an
	//error that lmdbxx swallows).
	read_threads = std::min(read_threads, lmdb_max_readers - 1);
	DatabaseMetadata meta = read_meta(env);
	vector<lmdb::dbi> databases;
	{
		auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		for (std::string_view t : tables_to_invert)
			databases.push_back(lmdb::dbi::open(txn, t.data()));
		txn.commit();
	}

	for (std::size_t overall_index = 0; overall_index < tables_to_invert.size(); ++overall_index) {
		Header header;
		std::memset(&header, 0, sizeof(Header));
		header.header_size = sizeof(Header);
		header.database_id = meta.id;
		header.timestamp = std::time(nullptr);
		std::string_view database_name = tables_to_invert[overall_index];
		fill_header_from_name(header, database_name);

		lmdb::dbi& database = databases[overall_index];
		vector<deque<pair<uint32_t, uint32_t>>> unsorted_edges;
		if (database_name.find("skinny"sv) != std::string_view::npos)
			unsorted_edges = invert_skinny(env, database, read_threads);
		else if (header.kind == EdgeKind::combine)
			unsorted_edges = invert_full<CombineEdge>(env, database, read_threads);
		else if (header.kind == EdgeKind::connect)
			unsorted_edges = invert_full<ConnectEdge>(env, database, read_threads);
		else if (header.kind == EdgeKind::close || header.kind == EdgeKind::mirror)
			unsorted_edges = invert_full<SimpleEdge>(env, database, read_threads);

		deque<pair<uint32_t, uint32_t>> sorted_edges = sort_and_merge(std::move(unsorted_edges), cpu_threads);

		vector<CodedChunk> coded_chunks = encode_chunked(std::move(sorted_edges));
		std::size_t total_edgelist_length = 0;
		header.offsets = 0;
		for (const CodedChunk& c : coded_chunks) {
			total_edgelist_length += c.data.size();
			header.offsets += c.id_length.size();
		}
		uint32_t max_id = coded_chunks.back().id_length.back().first;
		header.id_bytes = necessary_bytes(max_id);
		//This is a slight overestimation, as the last offset is implied by the
		//end of the file, not stored.
		header.offset_bytes = necessary_bytes(total_edgelist_length);

		if (database_name.compare(0, "edges-skinny-"sv.size(), "edges-skinny-"sv) == 0)
			database_name.remove_prefix("edges-skinny-"sv.size());
		else if (database_name.compare(0, "edges-"sv.size(), "edges-"sv) == 0)
			database_name.remove_prefix("edges-"sv.size());
		std::string filename = fmt::format("{}/invert-{}-{:x}-{}.dat",
				output_dir, env_basename(db_path), header.database_id, database_name);
		write_to_file(filename, header, std::move(coded_chunks));
		fmt::print("wrote {}\n", filename);
	}
	return 0;
}

namespace {
struct Mapping {
	int fd;
	std::size_t length;
	const Header* header; //also the beginning of the mapping
	const OffsetBase* offset_begin, *offset_end;
	const std::byte* edges_begin, *edges_end;
};

pair<const std::byte*, std::size_t> do_mmap(int fd) {
	struct stat s;
	if (fstat(fd, &s) == -1) {
		fmt::print(stderr, "error: fstat problem: {} ({})\n", strerror(errno), errno);
		std::exit(1);
	}
	const void* addr = mmap(nullptr, s.st_size, PROT_READ, MAP_SHARED_VALIDATE, fd, 0);
	if (addr == MAP_FAILED) {
		fmt::print(stderr, "error: failed to map file (already opened): {} ({})\n", strerror(errno), errno);
		std::exit(1);
	}
	return {reinterpret_cast<const std::byte*>(addr), s.st_size};
}

struct offset_id_compare {
	template<unsigned int X, unsigned int Y>
	bool operator()(const Offset<X, Y>& o, uint64_t needle) const {
		return o.id() < needle;
	}
};

template<unsigned int X, unsigned int Y>
pair<std::size_t, std::size_t> process_offsets(const Mapping& map, uint64_t needle) {
	const Offset<X, Y>* begin = static_cast<const Offset<X, Y>*>(map.offset_begin);
	const Offset<X, Y>* end = static_cast<const Offset<X, Y>*>(map.offset_end);
	const Offset<X, Y>* p = std::lower_bound(begin, end, needle, offset_id_compare());
	if (p == end || p->id() != needle)
		return {0, 0};
	if ((p+1) == end)
		return {p->offset(), 0};
	return {p->offset(), (p+1)->offset()};
}

pair<const std::byte*, const std::byte*> lookup_edge_range(const Mapping& map, uint64_t needle) {
	pair<std::size_t, std::size_t> offsets;
	//We'll always use the same version for a given mapping, so we could store
	//a function pointer in the Mapping struct to skip this switch table.
	switch (map.header->id_bytes << 3 | map.header->offset_bytes) {
#define LOOKUP_EDGE_RANGE_CASE(X, Y) case (X << 3 | Y): offsets = process_offsets<X, Y>(map, needle); break;
		LOOKUP_EDGE_RANGE_CASE(1, 1)
		LOOKUP_EDGE_RANGE_CASE(1, 2)
		LOOKUP_EDGE_RANGE_CASE(1, 3)
		LOOKUP_EDGE_RANGE_CASE(1, 4)
		LOOKUP_EDGE_RANGE_CASE(1, 5)
		LOOKUP_EDGE_RANGE_CASE(2, 1)
		LOOKUP_EDGE_RANGE_CASE(2, 2)
		LOOKUP_EDGE_RANGE_CASE(2, 3)
		LOOKUP_EDGE_RANGE_CASE(2, 4)
		LOOKUP_EDGE_RANGE_CASE(2, 5)
		LOOKUP_EDGE_RANGE_CASE(3, 1)
		LOOKUP_EDGE_RANGE_CASE(3, 2)
		LOOKUP_EDGE_RANGE_CASE(3, 3)
		LOOKUP_EDGE_RANGE_CASE(3, 4)
		LOOKUP_EDGE_RANGE_CASE(3, 5)
		LOOKUP_EDGE_RANGE_CASE(4, 1)
		LOOKUP_EDGE_RANGE_CASE(4, 2)
		LOOKUP_EDGE_RANGE_CASE(4, 3)
		LOOKUP_EDGE_RANGE_CASE(4, 4)
		LOOKUP_EDGE_RANGE_CASE(4, 5)
		LOOKUP_EDGE_RANGE_CASE(5, 1)
		LOOKUP_EDGE_RANGE_CASE(5, 2)
		LOOKUP_EDGE_RANGE_CASE(5, 3)
		LOOKUP_EDGE_RANGE_CASE(5, 4)
		LOOKUP_EDGE_RANGE_CASE(5, 5)
#undef LOOKUP_EDGE_RANGE_CASE
		default:
			throw std::logic_error(fmt::format("unhandled case in lookup_edge_range: {} {}",
					map.header->id_bytes, map.header->offset_bytes));
	}

	if (!offsets.first)
		return {nullptr, nullptr};
	if (!offsets.second)
		return {map.edges_begin+offsets.first, map.edges_end};
	return {map.edges_begin+offsets.first, map.edges_begin+offsets.second};
}
}//end anonymous namespace

int invert_search_mode(std::string_view db_path, std::vector<std::string_view>& args) {
	auto separator = std::find(args.begin(), args.end(), "--"sv);
	if (separator == args.end()) {
		fmt::print(stderr, "error: separator argument -- not found\n");
		return 1;
	} else if (separator == args.begin()) {
		fmt::print(stderr, "error: no index files given\n");
		return 1;
	} else if (std::next(separator) == args.end()) {
		fmt::print(stderr, "error: no search sources given\n");
		return 1;
	}

	lmdb::env env = lmdb::env::create();
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(std::string(db_path).c_str(), MDB_NORDAHEAD);
	DatabaseMetadata meta = read_meta(env);

	vector<Mapping> mappings;
	for (auto file_it = args.begin(); file_it != separator; ++file_it) {
		std::string filename(*file_it); //ensure null terminated
		Mapping mapping;
		mapping.fd = open(filename.c_str(), O_RDONLY);
		if (mapping.fd == -1) {
			fmt::print(stderr, "error opening {}: {} ({})", filename, strerror(errno), errno);
			return 1;
		}
		pair<const std::byte*, std::size_t> raw_map = do_mmap(mapping.fd);
		mapping.length = raw_map.second;
		mapping.header = reinterpret_cast<const Header*>(raw_map.first);
		mapping.offset_begin = reinterpret_cast<const OffsetBase*>(raw_map.first + mapping.header->header_size);
		mapping.edges_begin = reinterpret_cast<const std::byte*>(mapping.offset_begin) +
				(mapping.header->offsets * (mapping.header->id_bytes + mapping.header->offset_bytes));
		mapping.offset_end = reinterpret_cast<const OffsetBase*>(mapping.edges_begin);
		mapping.edges_end = raw_map.first + raw_map.second;
		mappings.push_back(mapping);

		if (madvise(const_cast<std::byte*>(raw_map.first), mapping.length, MADV_RANDOM))
			fmt::print(stderr, "warning: failed to madvise: {} ({})\n", strerror(errno), errno);

		if (mapping.header->database_id != meta.id) {
			fmt::print(stderr, "error: using database id {:x} but {} is from {:x}\n",
					meta.id, filename, mapping.header->database_id);
			return 1;
		}
	}

	vector<std::string_view> gadget_spec(std::next(separator), args.end());
	GadgetSet gadget_set = parse_gid_specs(gadget_spec);
	vector<uint64_t> sources = collect_initial_gadget_set(env, gadget_set);

	//We could use threads here, but I think the inverted indices will be small
	//enough to be fully prefetched.  If not, or we want to lazily load them for
	//some other reason, we should definitely use threads to get more in-flight
	//page faults.
	deque<uint64_t> worklist(sources.begin(), sources.end());
	tsl::hopscotch_set<uint64_t, farmhash_hash> closed(worklist.begin(), worklist.end());
	while (!worklist.empty()) {
		uint64_t cur = worklist.front();
		worklist.pop_front();
		for (const Mapping& m : mappings) {
			pair<const std::byte*, const std::byte*> range = lookup_edge_range(m, cur);
			uint64_t prev = 0;
			while (range.first != range.second) { //handles nullptr pairs
				uint64_t next = prev + varint64::read(range.first); //delta-decode
				if (closed.insert(next).second) {
					worklist.push_back(next);
					fmt::print("{}\n", next);
				}
				prev = next;
			}
		}
	}

	return 0;
}
