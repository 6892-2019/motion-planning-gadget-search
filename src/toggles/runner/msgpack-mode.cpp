#include "precompiled.hpp"
#include "selsert-gadget-by-data.hpp"
#include "automaton.hpp"
#include "canonicalize.hpp"
#include "ops.hpp"
#include "provenance.hpp"
#include "../rpc.hpp"
#include "../toggles-shared.hpp"
#include "../select-by-id.hpp"
#include "../completions.hpp"
#include "../anyprov.hpp"
#include "intervals.hpp"
#include "ioutils.hpp"
#include "randutils.hpp"
#include "stringutils.hpp"
#include "varint.hpp"
#include "lmdb++.h"
#include "hopscotch/hopscotch_map.h"
#include "proj_compare.hpp"
#include <boost/container/static_vector.hpp>
#include <msgpack.hpp>
#include <cstdio>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <sys/stat.h>

using namespace automaton;
using std::uint64_t;
using std::uint32_t;
using std::size_t;
using std::pair;
using std::vector;
using std::unique_ptr;
using std::string_view;
using namespace std::literals::string_view_literals;

static std::string g_database_path;

//forward declaration:
void write_output(const void* data, size_t size);



namespace {
template<typename Iterator>
void delete_many_files(Iterator first, Iterator last) {
	for (Iterator i = first; i != last; ++i)
		if (std::remove(i->c_str()))
			//std::remove isn't documented to set errno, but maybe its implementation does anyway
			fmt::print(stderr, "warning: failed to delete {}: {} ({})\n", *i, strerror(errno), errno);
}

using mmapping = pair<const std::byte*, std::size_t>;
//TODO: based on code from invert-mode.cpp; have common mmap helper
mmapping do_mmap(const std::string& filename) {
	int fd = open(filename.c_str(), O_RDONLY);
	if (fd == -1)
		throw std::runtime_error(fmt::format("failed to open {} for mapping: {} ({})", filename, strerror(errno), errno));
	struct stat s;
	if (fstat(fd, &s) == -1)
		throw std::runtime_error(fmt::format("error: fstat {}: {} ({})\n", filename, strerror(errno), errno));
	const void* addr = mmap(nullptr, s.st_size, PROT_READ, MAP_SHARED_VALIDATE, fd, 0);
	if (addr == MAP_FAILED)
		throw std::runtime_error(fmt::format("error: failed to map {}: {} ({})\n", filename, strerror(errno), errno));
	close(fd); //map persists
	return {reinterpret_cast<const std::byte*>(addr), s.st_size};
}
void do_unmap(mmapping m) {
	munmap(const_cast<void*>(static_cast<const void*>(m.first)), m.second);
}


struct less_input2 {
	bool operator()(const CombineProvenance& a, const CombineProvenance& b) const noexcept {
		return a.input2 < b.input2;
	}
	bool operator()(const CombineProvenance* a, const CombineProvenance* b) const noexcept {
		return a->input2 < b->input2;
	}
};
}



struct identity_subscript {
	template<typename T>
	auto operator[](const T& x) const noexcept {return x;}
};

struct SkinnyPage {
	SkinnyPage(std::uint64_t a, std::vector<std::byte>&& b, std::vector<std::byte>&& c) :
			last_input(a), header(std::move(b)), page(std::move(c)) {}
	std::uint64_t last_input;
	std::vector<std::byte> header, page;
};

template<class Iterator, class IdMapper = identity_subscript>
std::vector<SkinnyPage> paginate_for_skinny_edges(Iterator first, Iterator last, IdMapper map = IdMapper()) {
	assert(std::is_sorted(first, last, InputGroupingProvCmp()));

	std::vector<SkinnyPage> ret;
	if (first == last) return ret;
	std::vector<std::byte> header, page;
	std::array<std::byte, 5> length;
	std::vector<uint64_t> block;
	std::vector<std::byte> chunk;
	uint64_t previous_input = first->input1 - 1; //so we don't commit an empty page to start

	auto commit_page = [&]() {
		std::uint16_t offset = numeric_cast<std::uint16_t>(header.size());
		std::memcpy(header.data(), &offset, 2);
		ret.emplace_back(previous_input, std::move(header), std::move(page));
		header.clear();
		header.resize(2);
		page.clear();
	};

	header.resize(2); //reserve two bytes for the first offset (rest are lengths, so delta-coded varint)
	while (first != last) {
		//Find the block sharing the same input1.
		block.clear();
		Iterator block_it = first;
		uint64_t input = block_it->input1;
		while (block_it != last && block_it->input1 == input) {
			block.push_back(map[block_it->output1]);
			++block_it;
		}
		first = block_it;

		std::sort(block.begin(), block.end());
		block.erase(std::unique(block.begin(), block.end()), block.end());

		if (chunk.size() < block.size() * 9)
			chunk.resize(block.size() * 9);
		std::byte* chunk_end = chunk.data();
		upv::write(chunk_end, block.front());
		for (std::size_t i = 1; i < block.size(); ++i) //delta coding loop
			upv::write(chunk_end, block[i] - block[i-1]);
		std::byte* length_end = length.data();
		upv::write(length_end, chunk_end - chunk.data());

		if (header.size() + (length_end - length.data()) > std::numeric_limits<std::uint16_t>::max() ||
				input != previous_input + 1)
			commit_page();

		header.insert(header.end(), length.data(), length_end);
		page.insert(page.end(), chunk.data(), chunk_end);
		previous_input = input;
	}
	if (!page.empty())
		commit_page();
	return ret;
}

void insert_skinny_edges(lmdb::txn& txn, lmdb::dbi& edges, std::vector<SkinnyPage>&& pages) {
	lmdb::cursor cur = lmdb::cursor::open(txn, edges);
	for (const SkinnyPage& p : pages) {
		//See comments elsewhere about undefined behavior.
		std::string_view value(nullptr, p.header.size() + p.page.size());
		if (!cur.put(lmdb::to_sv(p.last_input), value, MDB_RESERVE | MDB_NOOVERWRITE))
			throw std::logic_error(fmt::format("failed to insert skinny edge data for {} (length ())",
					p.last_input, value.size())); //would like to get the DB name here...
		char* dest = const_cast<char*>(value.data());
		std::memcpy(dest, p.header.data(), p.header.size());
		std::memcpy(dest + p.header.size(), p.page.data(), p.page.size());
	}
	std::vector<SkinnyPage> ensure_memory_is_freed(std::move(pages));
}



//The number of slices.  We won't write a slice if no gadgets fall in it, but if
//the hash is any good, we'll cover all slices for reasonably large gadgets.
constexpr unsigned int firsthalf_slices = 256;
//Divide a database hash (i.e., matching selsert) by this value to get the slice number.
constexpr std::size_t firsthalf_slice_divisor = static_cast<std::size_t>(1) << (64 - 8);

struct FirsthalfHeader {
	uint64_t database_id;
	uint64_t firsthalf_id;
	std::size_t gadgets; //total across all slices
	std::size_t pruned, skipped;
	std::array<std::size_t, firsthalf_slices> slice_offset;
	std::size_t provs_offset;
	std::size_t input_intervals_offset;
	//Should equal the file size.  Not strictly necessary, but allows us to use
	//mmap without carrying the length around everywhere.
	std::size_t end_offset;
	EdgeKind kind;
};

FirsthalfStatistics write_firsthalf0(uint64_t database_id, EdgeKind kind,
		vector<vector<std::byte>>&& allgadgets, std::string_view provs,
		std::size_t prov_count, std::size_t pruned, std::size_t skipped,
		vector<pair<uint64_t, uint64_t>> input_intervals = vector<pair<uint64_t, uint64_t>>()) {
	struct SliceItem {
		std::size_t hash;
		const std::byte* data;
		uint32_t length;
		uint32_t local_index;
	};

	std::string database_temp = fmt::format("/var/tmp/toggles/{:016x}", database_id);
	if (mkdir(database_temp.c_str(), 0755) < 0 && errno != EEXIST)
		throw std::runtime_error(fmt::format("failed to create database temp directory {}: {} ({})",
				database_temp, strerror(errno), errno));

	FirsthalfHeader header;
	std::memset(&header, 0, sizeof(header));
	//We could build an id from the database id, the pid, the time, etc., but
	//we can rely on the filesystem for uniqueness.
	std::string filename;
	FILE* file = nullptr;
	for (unsigned int attempts = 0; !file && attempts < 100; ++attempts) {
		header.firsthalf_id = get_random_integer<uint64_t>();
		filename = fmt::format("{}/{:016x}.bin", database_temp, header.firsthalf_id);
		file = std::fopen(filename.c_str(), "w+x");
		if (!file && errno != EEXIST)
			throw std::runtime_error(fmt::format("failed to create firsthalf {}: {} ({})",
					filename, strerror(errno), errno));
	}
	if (!file)
		throw std::runtime_error("gave up on creating firsthalf");

	header.database_id = database_id;
	header.gadgets = allgadgets.size();
	header.kind = kind;
	header.pruned = pruned;
	header.skipped = skipped;
	//Some of the header isn't filled yet, but write it to reserve space.  We'll
	//overwrite it at the end.
	std::fwrite(&header, sizeof(header), 1, file);

	vector<vector<SliceItem>> slices(firsthalf_slices);
	for (uint32_t i = 0; i < allgadgets.size(); ++i) {
		std::size_t hash = farmhash::Fingerprint64(allgadgets[i]);
		std::size_t slice = hash / firsthalf_slice_divisor;
		slices[slice].push_back({});
		slices[slice].back() = {hash, allgadgets[i].data(), numeric_cast<uint32_t>(allgadgets[i].size()), i};
	}

	FirsthalfStatistics stats = {};
	stats.firsthalf_id = header.firsthalf_id;
	stats.filenames.push_back(filename);
	stats.provs = prov_count;
	stats.provs_size = provs.size();
	stats.gadgets = allgadgets.size();
	for (unsigned int i = 0; i < slices.size(); ++i) {
		header.slice_offset[i] = static_cast<std::size_t>(std::ftell(file));
		vector<SliceItem> & s = slices[i];
		if (s.empty()) continue;
		//Sort by hash.  Previously we sorted by local index so we could
		//delta-varint it, but hash sorting is more important.  (I guess we
		//could delta-varint the hashes?)
		std::sort(s.begin(), s.end(), [](const auto& left, const auto& right){return left.hash < right.hash;});

		for (const SliceItem& p : s) {
			std::fwrite(&p.hash, sizeof(p.hash), 1, file);
			std::fwrite(&p.local_index, sizeof(p.local_index), 1, file);
			std::fwrite(&p.length, sizeof(p.length), 1, file);
			std::fwrite(p.data, p.length, 1, file);
			stats.gadgets_size += p.length;
		}
	}

	header.provs_offset = std::ftell(file);
	std::fwrite(provs.data(), provs.size(), 1, file);

	header.input_intervals_offset = std::ftell(file);
	std::fwrite(input_intervals.data(), sizeof(input_intervals.front()), input_intervals.size(), file);

	header.end_offset = std::ftell(file);
	//Rewind and write the header now that we've filled in the offsets.
	std::rewind(file);
	std::fwrite(&header, sizeof(header), 1, file);

	std::fflush(file);
	if (std::ferror(file))
		throw std::runtime_error(fmt::format("error writing {}: {} ({})", filename, strerror(errno), errno));
	std::fclose(file);

	return stats;
}

template<class Provenance>
FirsthalfStatistics write_firsthalf(uint64_t database_id, EdgeKind kind,
		vector<vector<std::byte>>&& allgadgets, vector<Provenance>&& provs,
		std::size_t pruned, std::size_t skipped,
		vector<pair<uint64_t, uint64_t>> input_intervals = vector<pair<uint64_t, uint64_t>>()) {
	std::sort(provs.begin(), provs.end(), InputGroupingProvCmp());
	return write_firsthalf0(database_id, kind, std::move(allgadgets),
			std::string_view(reinterpret_cast<char*>(provs.data()), provs.size() * sizeof(Provenance)),
			provs.size(), pruned, skipped, std::move(input_intervals));
}

DatabaseOperationStatistics do_secondhalf_db(vector<std::string> filenames) {
	lmdb::env env = lmdb::env::create(); //TODO: flags?
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(g_database_path.c_str(), MDB_NORDAHEAD); //TODO: flags?
	DatabaseMetadata meta = read_meta(env);
	lmdb::dbi gadget_hashtable, gadget_index, completions, edges_connect, edges_close, edges_mirror;
	vector<pair<uint64_t, lmdb::dbi>> edges_combine;
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		completions = lmdb::dbi::open(txn, "completions");
		edges_connect = lmdb::dbi::open(txn, "edges-skinny-connect");
		edges_close = lmdb::dbi::open(txn, "edges-close");
		edges_mirror = lmdb::dbi::open(txn, "edges-mirror");

		const std::string_view edges_combine_prefix = "edges-skinny-combine-"sv;
		lmdb::dbi main = lmdb::dbi::open(txn, nullptr);
		lmdb::cursor cur = lmdb::cursor::open(txn, main);
		std::string_view key = edges_combine_prefix;
		if (!cur.get(key, MDB_SET_RANGE))
			throw std::logic_error("no combine edge subdatabases?");
		while (key.compare(0, edges_combine_prefix.size(), edges_combine_prefix) == 0) {
			//I'm not sure the key is null-terminated.
			std::string dbname(key);
			key.remove_prefix(edges_combine_prefix.size());
			edges_combine.emplace_back(from_string<uint64_t>(key), lmdb::dbi::open(txn, dbname.c_str()));
			if (!cur.get(key, MDB_NEXT)) break;
		}

		txn.commit();
	}

	DatabaseOperationStatistics stats = {};
	std::vector<const FirsthalfHeader*> files;
	tsl::hopscotch_map<uint64_t, vector<uint64_t>, farmhash_hash> firsthalf_to_globals;
	std::optional<EdgeKind> prov_kind;
	for (const std::string& filename : filenames) {
		mmapping m = do_mmap(filename);
		auto header = reinterpret_cast<const FirsthalfHeader*>(m.first);
		files.push_back(header);

		if (header->end_offset != m.second)
			throw std::runtime_error(fmt::format("firsthalf {:016x} has header length {} but physical length {}",
					header->firsthalf_id, header->end_offset, m.second));
		if (header->database_id != meta.id)
			throw std::runtime_error(fmt::format("firsthalf {:016x} has is for database {:016x} but we're committing to database {:016x}",
					header->firsthalf_id, header->database_id, meta.id));

		if (!prov_kind) //TODO: use separate RPC entry points to pass in the expected kind
			prov_kind = header->kind;
		else if (header->kind != *prov_kind)
			//Strictly speaking, this is not an error; we'll commit the different
			//kinds separately, and everything will be fine.  But it probably
			//means something went wrong with how the driver is calling us.
			throw std::runtime_error(fmt::format("firsthalf {:016x}'s provs are {}, but others are {}",
					header->firsthalf_id, header->kind, *prov_kind));

		firsthalf_to_globals.try_emplace(header->firsthalf_id, header->gadgets, std::numeric_limits<uint64_t>::max());
		stats.pruned_locally += header->pruned;
		stats.skipped += header->skipped;
	}

	struct ProposedInsert {
		std::size_t hash;
		uint64_t* global_id;
		std::string_view data;
		bool operator==(const ProposedInsert& o) const noexcept {
			//for adjacent_find, so doesn't compare global_id
			return std::tie(hash, data) == std::tie(hash, o.data);
		}
		bool operator<(const ProposedInsert& o) const noexcept {
			return std::tie(hash, data) < std::tie(hash, o.data);
		}
	};

	struct SortStats {
		encoding::Stats stats;
		//We break ties by hash for locality of later readers, and we do so
		//explicitly rather than rely std::stable_sort's order preservation because
		//std::stable_sort allocates more memory than explicitly storing the hash
		//costs.  It turns out to be handy, anyway.
		std::size_t hash;
		uint64_t* global_id;
		bool operator<(const SortStats& b) const noexcept {
			//std::tie doesn't bind rvalues, so we have to explicitly compute some things.
			//Single out uedges == 0 and dedges == 0 because those are natural queries;
			//the rest of the edge sorts aren't super useful.  These are reversed because
			//false sorts before true.
			bool a_undirected = stats.directed_edges != 0,
					a_directed = stats.undirected_edges != 0,
					b_undirected = b.stats.directed_edges != 0,
					b_directed = b.stats.undirected_edges != 0;
			auto a_total_edges = stats.undirected_edges + stats.directed_edges,
					b_total_edges = b.stats.undirected_edges + b.stats.directed_edges;
			return std::tie(stats.components, stats.locations, stats.states, a_undirected, a_directed, stats.undirected_edges, stats.directed_edges, a_total_edges, hash) <
					std::tie(b.stats.components, b.stats.locations, b.stats.states, b_undirected, b_directed, b.stats.undirected_edges, b.stats.directed_edges, b_total_edges, b.hash);
		}
	};

	for (unsigned int slice = 0; slice < firsthalf_slices; ++slice) {
		vector<ProposedInsert> gadgets;
		for (const FirsthalfHeader* header : files) {
			vector<uint64_t>& map = firsthalf_to_globals[header->firsthalf_id];
			const std::byte* base = reinterpret_cast<const std::byte*>(header);
			const std::byte* begin = base + header->slice_offset[slice];
			const std::byte* end = base + ((slice+1) < header->slice_offset.size() ? header->slice_offset[slice+1] : header->provs_offset);
			const std::byte* data = begin;
			while (data != end) {
				ProposedInsert pi;
				uint32_t local_index, length;
				std::memcpy(&pi.hash, data, sizeof(pi.hash));
				data += sizeof(pi.hash);
				std::memcpy(&local_index, data, sizeof(local_index));
				data += sizeof(local_index);
				pi.global_id = &map[local_index];
				std::memcpy(&length, data, sizeof(length));
				data += sizeof(length);
				pi.data = std::string_view(reinterpret_cast<const char*>(data), length);
				data += length;
				gadgets.push_back(pi);
			}
		}
		std::sort(gadgets.begin(), gadgets.end());

		//Early pruning.  We don't check for adjacent equality first because it
		//is cheap to resolve against the database (the cursor won't move) and
		//we'd have to remember to fill in the ids for any removed duplicates.
		{
			auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
			lmdb::cursor cur = lmdb::cursor::open(txn, gadget_hashtable);
			vector<ProposedInsert>::iterator new_end = gadgets.begin();
			for (vector<ProposedInsert>::iterator i = gadgets.begin(); i != gadgets.end(); ++i) {
				const char* data_begin = i->data.data(), *data_end = data_begin + i->data.size();
				std::string_view key = lmdb::to_sv(i->hash), existing;
				while (cur.get(key, existing, MDB_SET))
					//skip the appended ID (remove_suffix is a mutator)
					if (std::equal(data_begin, data_end, existing.begin(), existing.end()-8)) {
						*(i->global_id) = lmdb::from_sv<std::uint64_t>(existing.substr(existing.size()-8));
						++stats.pruned_database;
						goto labeled_continue; //don't increment new_end
					} else
						++(i->hash); //linear probing

				if (i != new_end) //avoid vector's self-move-assignment
					*new_end = std::move(*i);
				++new_end;
				labeled_continue: ;
			}
			txn.commit();
			gadgets.erase(new_end, gadgets.end());
		}

		//Now we have to deduplicate locally because we want to assign hashes
		//based on a stats sort before doing the actual insertion.
		vector<pair<uint64_t*, uint64_t*>> pending_stores; //*first = *second
		{
			auto first = std::adjacent_find(gadgets.begin(), gadgets.end()), last = gadgets.end();
			//if we need to, prune like std::unique, but recording pending stores for things we remove
			if (first != last) {
				auto result = first;
				while (++first != last) {
					if (*result == *first)
						pending_stores.emplace_back(first->global_id, result->global_id);
					else if (++result != first)
						*result = *first;
				}
				last = ++result;
			}
			gadgets.erase(last, gadgets.end());
		}
		stats.novel_gadgets += gadgets.size();
		if (gadgets.empty())
			continue;

		vector<SortStats> sorted_stats;
		sorted_stats.reserve(gadgets.size());
		for (ProposedInsert& pi : gadgets)
			sorted_stats.push_back({encoding::stats(reinterpret_cast<const std::byte*>(pi.data.data())), pi.hash, pi.global_id});
		std::sort(sorted_stats.begin(), sorted_stats.end());

		{
			std::vector<std::size_t> hashes;
			hashes.reserve(sorted_stats.size());

			auto txn = lmdb::txn::begin(env);
			{ //extra scope for write cursors
				lmdb::cursor hashtable_cur = lmdb::cursor::open(txn, gadget_hashtable);
				//The only way a previous slice's commit could interfere with us is
				//if a linear probing chain extends into our slice.  We can just
				//check that the last hash in the previous slice is not used.  If
				//hashes are uniformly distributed, we have seven-nines probability
				//that these slots will be unused after 2^32 gadgets are committed.
				if (slice != 0) {
					std::size_t probe = slice * firsthalf_slice_divisor - 1;
					std::string_view probe_key = lmdb::to_sv(probe);
					if (hashtable_cur.get(probe_key, MDB_SET))
						throw std::runtime_error(fmt::format("supreme unluckiness: hash {:x} at end of slice {} is used",
								probe, slice - 1));
				}

				//This duplicates toggles-share's get_current_max_gadget_id, but
				//we're going to keep using the cursor.
				lmdb::cursor index_cur = lmdb::cursor::open(txn, gadget_index);
				std::string_view last_index_key, last_index_value;
				std::uint64_t last_id;
				if (index_cur.get(last_index_key, last_index_value, MDB_LAST))
					last_id = lmdb::from_sv<std::uint64_t>(last_index_key);
				else
					last_id = 0; //empty index; starting at 0 means first key will be 1
				const std::uint64_t first_novel_id = last_id + 1;

				for (const SortStats& ss : sorted_stats) {
					assert(*ss.global_id == std::numeric_limits<uint64_t>::max());
					*ss.global_id = ++last_id; //last_id is inclusive, so pre-increment
					hashes.push_back(ss.hash);
				}

				for (ProposedInsert& pi : gadgets) {
					//Constructing a string_view to nullptr is technically undefined
					//behavior.  We have to const_cast it later again anyway, so
					//string_view is just the wrong abstraction for MDB_RESERVE.
					//TODO: rewrite lmdbxx using std::span (hah)
					std::string_view target(nullptr, pi.data.size()+8);
					if (!hashtable_cur.put(lmdb::to_sv(pi.hash), target, MDB_RESERVE | MDB_NOOVERWRITE))
						//If there ever is a self-collision we can fix up the
						//hash in the hashes vector by taking the difference
						//between *pi.global_id and last_id's initial value.
						throw std::runtime_error(fmt::format("collision for hash {} in slice {}; possible self-collision?", pi.hash, slice));
					std::memcpy(const_cast<char*>(target.begin()), pi.data.data(), pi.data.size());
					std::memcpy(const_cast<char*>(target.begin()) + pi.data.size(), pi.global_id, sizeof(*pi.global_id));
				}

				//TODO: this code copied from the old selsert could fairly
				//easily be commoned.
				//We pack hashes into pages to save space.  See the comment in
				//select_gadget_id_to_hash.
				std::size_t hashes_index = 0;
				//LMDB overflow pages have a 16-byte header.
				constexpr std::size_t index_page_bytes = (4096-16), index_page_size = index_page_bytes / sizeof(std::size_t);
				//If the previous page wasn't full, fill it.
				if (!last_index_value.empty() && last_index_value.size() != index_page_bytes) {
					//We can't actually append to the last open page; instead we
					//append a new page and delete the old one.
					std::size_t current_size = last_index_value.size() / sizeof(std::size_t);
					std::size_t new_elements = std::min(hashes.size(), index_page_size - current_size);
					std::uint64_t current_key = lmdb::from_sv<uint64_t>(last_index_key);
					std::uint64_t new_key = current_key + new_elements;
					//See above comment about undefined behavior.
					std::string_view new_page(nullptr, (current_size + new_elements) * sizeof(std::size_t));
					if (!index_cur.put(lmdb::to_sv(new_key), new_page, MDB_APPEND | MDB_RESERVE | MDB_NOOVERWRITE))
						throw std::logic_error(fmt::format("failed to append while extending index page: {} {} {} {}",
								current_key, current_size, new_key, new_page.size()));
					std::memcpy(const_cast<char*>(new_page.data()), last_index_value.data(), last_index_value.size());
					std::memcpy(const_cast<char*>(new_page.data()) + last_index_value.size(), hashes.data(), new_elements * sizeof(std::size_t));
					if (!index_cur.get(last_index_key, MDB_SET))
						throw std::logic_error(fmt::format("failed to position for deletion? {}", current_key));
					index_cur.del(); //throws on failure
					hashes_index += new_elements;
				}

				while (hashes_index < hashes.size()) {
					std::size_t new_elements = std::min(hashes.size() - hashes_index, index_page_size);
					std::size_t new_key = first_novel_id + hashes_index + new_elements - 1;
					//See above comment about undefined behavior.
					std::string_view new_page(nullptr, new_elements * sizeof(std::size_t));
					if (!index_cur.put(lmdb::to_sv(new_key), new_page, MDB_APPEND | MDB_RESERVE | MDB_NOOVERWRITE))
						throw std::logic_error(fmt::format("failed to append new index page: {} {} {} {}",
								hashes_index, new_key, new_elements, new_page.size()));
					std::memcpy(const_cast<char*>(new_page.data()), hashes.data() + hashes_index, new_elements * sizeof(std::size_t));
					hashes_index += new_elements;
				}
			}
			txn.commit();
		}

		for (auto p : pending_stores)
			*p.first = *p.second;
	}

	while (!files.empty()) {
		const FirsthalfHeader* header = files.back();
		const std::byte* base = reinterpret_cast<const std::byte*>(header);
		auto local_to_global = firsthalf_to_globals.find(header->firsthalf_id);
		if (local_to_global == firsthalf_to_globals.end())
			throw std::logic_error(fmt::format("can't happen: didn't make firsthalf_to_globals for {:016x}?", header->firsthalf_id));

		switch (header->kind) {
			case EdgeKind::combine: {
				vector<pair<lmdb::dbi*, vector<SkinnyPage>>> pages;
				vector<pair<std::string, vector<pair<uint64_t, uint64_t>>>> pending_completions;
				auto begin = reinterpret_cast<const CombineProvenance*>(base + header->provs_offset),
						end = reinterpret_cast<const CombineProvenance*>(base + header->input_intervals_offset);
				stats.edges += end - begin;
				while (begin != end) {
					auto [first, last] = std::equal_range(begin, end, *begin, less_input2());
					auto edge_db_iter = std::find_if(edges_combine.begin(), edges_combine.end(),
							[input2=first->input2](const auto& p){return p.first == input2;});
					if (edge_db_iter == edges_combine.end())
						throw std::logic_error(fmt::format("can't happen: didn't open edge db for {}?", first->input2));
					pages.emplace_back(&edge_db_iter->second, paginate_for_skinny_edges(first, last, local_to_global->second.begin()));
					interval_accumulator<uint64_t> comp_input1(512);
					for (auto i = first; i != last; ++i)
						comp_input1(i->input1);
					pending_completions.emplace_back(fmt::format("combine-{}", first->input2), std::move(comp_input1).finish());
					begin = last;
				}

				auto txn = lmdb::txn::begin(env);
				for (std::size_t i = 0; i < pages.size(); ++i) {
					if (!record_completion(txn, completions, pending_completions[i].first, pending_completions[i].second))
						throw std::runtime_error(fmt::format(
								"combine completion collision for skinny edges; completion key {}, first input interval {}",
								pending_completions[i].first, pending_completions[i].second[0]));
					insert_skinny_edges(txn, *pages[i].first, std::move(pages[i].second));
				}
				txn.commit();
				break;
			}
			case EdgeKind::connect: {
				auto begin = reinterpret_cast<const ConnectProvenance*>(base + header->provs_offset),
						end = reinterpret_cast<const ConnectProvenance*>(base + header->input_intervals_offset);
				stats.edges += end - begin;
				vector<SkinnyPage> pages = paginate_for_skinny_edges(begin, end, local_to_global->second.begin());
				//TODO: avoid this copy
				auto interval_begin = reinterpret_cast<const pair<uint64_t, uint64_t>*>(base + header->input_intervals_offset),
						interval_end = reinterpret_cast<const pair<uint64_t, uint64_t>*>(base + header->end_offset);
				vector<pair<uint64_t, uint64_t>> input_intervals(interval_begin, interval_end);
				auto txn = lmdb::txn::begin(env);
				if (!record_completion(txn, completions, "connect", input_intervals))
					throw std::runtime_error(fmt::format(
							"connect completion collision for skinny edges; adding {}", fmt::join(input_intervals, ", ")));
				insert_skinny_edges(txn, edges_connect, std::move(pages));
				txn.commit();
				break;
			}
			case EdgeKind::close: {
				throw std::logic_error("TODO secondhalf close provs");
				break;
			}
			case EdgeKind::mirror: {
				throw std::logic_error("TODO secondhalf mirror provs");
				break;
			}
			default:
				throw std::logic_error(fmt::format("bad edge kind in prov {:x}: {}", header->firsthalf_id, header->kind));
		}

		firsthalf_to_globals.erase(local_to_global);
		do_unmap({base, header->end_offset});
		files.pop_back();
		if (std::remove(filenames.back().c_str()))
			//std::remove isn't documented to set errno, but maybe its implementation does anyway
			fmt::print(stderr, "warning: failed to delete {}: {} ({})\n", filenames.back(), strerror(errno), errno);
		filenames.pop_back();
	}

	return stats;
}



Finisher<ConnectProvenance> do_connect(vector<pair<std::uint64_t, vector<std::byte>>> inputs) {
	Finisher<ConnectProvenance> finisher;
	for (auto& i : inputs)
		connect(*encoding::decode(i.second), i.first, finisher);
	return finisher;
}

Finisher<SimpleProvenance> do_close(vector<pair<std::uint64_t, vector<std::byte>>> inputs) {
	Finisher<SimpleProvenance> finisher;
	for (const auto& p : inputs) {
		SimpleProvenance prov;
		prov.input1 = p.first;
		unique_ptr<WorkingAutomaton> a = encoding::decode(p.second);
		//TODO: decode could return an encoding::Stats instead of querying the active alphabet again.
		auto activealpha = a->active_alphabet_size();
		bool possibly_changed = acceptingClosure(*a, activealpha);
		if (!possibly_changed) continue;
		prov.canonicalizePermutation = numeric_cast<std::uint8_t>(canonicalize(*a, activealpha, false));
		std::vector<std::byte> r = encoding::encode(*a);
		if (r != p.second)
			finisher(std::move(r), prov);
		//TODO: should always be more edges (with undirected counting twice) after
		//successful (modified the automaton) closure, never the same or fewer
	}
	return finisher;
}

Finisher<SimpleProvenance> do_mirror(vector<pair<std::uint64_t, vector<std::byte>>> inputs) {
	Finisher<SimpleProvenance> finisher;
	for (const auto& p : inputs) {
		SimpleProvenance prov;
		prov.input1 = p.first;
		unique_ptr<WorkingAutomaton> a = encoding::decode(p.second);
		//mirror copies.  We do need to know if mirroring changed the automaton,
		//but maybe there's a way to do that while reusing *a?
		pair<unique_ptr<WorkingAutomaton>, unsigned int> m = mirror(*a);
		if (*m.first == *a) continue;
		prov.canonicalizePermutation = numeric_cast<std::uint8_t>(m.second);
		finisher(encoding::encode(*m.first), prov);
	}
	return finisher;
}



namespace {
//I guess this could be a generalized projection function...
template<class T>
auto extract_first(const vector<T>& inputs) {
	vector<typename std::tuple_element<0, T>::type> firsts;
	firsts.reserve(inputs.size());
	for (const auto& p : inputs)
		firsts.push_back(std::get<0>(p));
	return firsts;
}
}


Finisher<CombineProvenance> operate_combine(lmdb::env& env, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index,
		vector<pair<uint64_t, uint64_t>> left_intervals, vector<std::uint64_t> right_gids,
		unsigned int precision, unsigned int max_left_states) {
	//We pass two separate vectors of gadget data to do_combine, but we want to
	//do only one select_gadget_id_to_data so as to only need one transaction.
	if (!std::is_sorted(right_gids.begin(), right_gids.end()))
		std::sort(right_gids.begin(), right_gids.end());
	vector<pair<uint64_t, uint64_t>> right_intervals = maximal_intervals(right_gids.begin(), right_gids.end());
	vector<pair<uint64_t, uint64_t>> input_intervals = interval_union(
			left_intervals.cbegin(), left_intervals.cend(), right_intervals.cbegin(), right_intervals.cend());
	vector<pair<std::uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(
			env, gadget_hashtable, gadget_index, std::move(input_intervals));

	vector<pair<uint64_t, vector<std::byte>>> right_data;
	for (pair<uint64_t, vector<std::byte>>& p : inputs) {
		auto rit = std::find(right_gids.begin(), right_gids.end(), p.first);
		if (rit != right_gids.end()) {
			uint64_t r = *rit;
			right_gids.erase(rit);
			//Move if this is exclusively a right, otherwise copy.
			if (interval_contains(left_intervals, r))
				right_data.push_back(p);
			else {
				right_data.push_back(std::move(p));
				p.second.clear(); //to be erased later
			}
		}
	}
	if (!right_gids.empty())
		throw std::logic_error(fmt::format("can't happen: some right gids not retrieved? {}", right_gids));

	//Skip any gadgets that can't possibly combine with any right.  (do_combine
	//will skip as appropriate if it sometimes fits.)  Also skip if it exceeds
	//our predefined limits.
	std::size_t skipped = 0;
	unsigned int right_min_locs = 0;
	for (pair<uint64_t, vector<std::byte>>& p : right_data)
		right_min_locs = std::min(right_min_locs, encoding::locations(p.second.data()));
	unsigned int left_max_locs = precision - right_min_locs;
	for (pair<uint64_t, vector<std::byte>>& p : inputs) {
		if (p.second.empty()) continue; //removed in the loop above
		encoding::Stats stats = encoding::stats(p.second.data());
		if (stats.locations > left_max_locs || stats.states > max_left_states) {
			p.second.clear();
			skipped += right_data.size();
		}
	}
	inputs.erase(std::partition(inputs.begin(), inputs.end(), [](const auto& p){return !p.second.empty();}), inputs.end());
	//TODO: consider sorting inputs (currently it's roughly hash-ordered).  Maybe
	//we want to sort largest-first so we get any pathological determinizes out
	//of the way early, rather than getting OOM killed at the end.  Note that
	//do_combine now processes back-first like a stack to gradually release memory.

	Finisher<CombineProvenance> outputs = do_combine(std::move(inputs), std::move(right_data), precision);
	outputs.skip(skipped);
	return outputs;
}

DatabaseOperationStatistics commit_combine_result_full(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, vector<pair<uint64_t, lmdb::dbi>>& edge_tables, lmdb::dbi& completions,
		vector<vector<std::byte>>&& gadgets, vector<CombineProvenance>&& prov, std::size_t pruned, std::size_t skipped) {
	std::size_t survivor_size = gadgets.size();
	std::size_t edge_count = prov.size();

	auto selsert_result = selsert_gadget_by_data(env, gadget_hashtable, gadget_index, std::move(gadgets));

	//The loop control below assumes provs isn't empty.  It can only be empty if
	//we skipped all the pairs.  If we produced any gadgets, we must have provs.
	if (prov.empty()) {
		if (survivor_size != 0)
			fmt::print(stderr, "warning: skipping empty combine provs but there were {} gadgets; {} skipped\n", survivor_size, skipped);
		return {skipped, pruned, survivor_size - selsert_result.novel_size(), selsert_result.novel_size(), edge_count};
	}

	//We group by input2, then by input1.  Because we combine against each right
	//operand in sequence, we're usually not already sorted, so we don't check
	//is_sorted like we do in the other commit_*_result functions.
	std::sort(prov.begin(), prov.end(), InputGroupingProvCmp());

	//We commit edges and completions one right operand at a time (so we're only
	//touching one edge table at a time).  This is more to simplify managing
	//cursor lifetime than to keep transactions short, as we'll start another
	//immediately and we can't do anything useful if we're suspended.
	for (auto block_first = prov.begin(), block_end = std::lower_bound(block_first, prov.end(), *block_first, less_input2());
			block_first != prov.end();
			block_end = std::upper_bound(block_first, prov.end(), *block_first, less_input2())) {
		uint64_t input2 = block_first->input2;
		auto edges_it = std::find_if(edge_tables.begin(), edge_tables.end(),
				[input2](const auto& p){return p.first == input2;});
		if (edges_it == edge_tables.end()) //TODO: unlikely
			throw std::logic_error(fmt::format("combine edge database for right operand {} not found; available databases: {}",
					input2, extract_first(edge_tables)));

		//Combines for a pair of operands always succeed, so completions is just
		//a summary of the edges.  (If we skipped a pair due to insufficient
		//precision, we didn't generate any edges, so we don't record any
		//completions and can come back for that pair later.)
		std::string completions_kind = fmt::format("combine-{}", input2);
		interval_accumulator<uint64_t> comp_input1(512);

		auto txn = lmdb::txn::begin(env);
		{
			lmdb::cursor cur = lmdb::cursor::open(txn, edges_it->second);
			//We could use static_vector with 512 here (16 splice * 16 rotation * 2 connect locations).
			vector<CombineEdge> buf;
			while (block_first != block_end) {
				buf.clear();
				//Find the block sharing the same input1.
				auto subblock_end = block_first;
				while (subblock_end != block_end && block_first->input1 == subblock_end->input1) {
					const CombineProvenance& p = *subblock_end;
					CombineEdge e;
					e.output = selsert_result.local_to_global[p.output1];
					e.splice = p.splice;
					e.rotation = p.rotation;
					e.connectPoint = p.connectPoint;
					e.canonicalizePermutation = p.canonicalizePermutation;
					buf.push_back(e);
					++subblock_end;
				}
				//For canonicalization purposes, sort the edges.  (We have to remap
				//through local_to_global before we can do this.)
				std::sort(buf.begin(), buf.end());
				std::string_view value(reinterpret_cast<char*>(buf.data()), buf.size()*sizeof(CombineEdge));
				if (!cur.put(lmdb::to_sv(block_first->input1), value, MDB_NOOVERWRITE)) {
					if (value.size() == 0 || value.size() % sizeof(CombineEdge) != 0)
						throw std::logic_error(fmt::format("combine edge data for key {}/{} has value length {} (not a multiple of {})",
								block_first->input1, block_first->input2, value.size(), sizeof(CombineEdge)));
					const CombineEdge* first = reinterpret_cast<const CombineEdge*>(value.data());
					const CombineEdge* last = first + value.size() / sizeof(CombineEdge);
					if (!std::equal(buf.cbegin(), buf.cend(), first, last))
						throw std::logic_error(fmt::format("differing combine edges from {}/{}: {} and {}",
								block_first->input1, block_first->input2, buf, make_range_for_pair(first, last)));
					edge_count -= buf.size(); //don't count edges already present
				}
				comp_input1(block_first->input1);
				block_first = subblock_end;
			}
		}
		txn.commit();
	}

	return {skipped, pruned, survivor_size - selsert_result.novel_size(), selsert_result.novel_size(), edge_count};
}

DatabaseOperationStatistics commit_combine_result_skinny(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, vector<pair<uint64_t, lmdb::dbi>>& edge_tables, lmdb::dbi& completions,
		vector<vector<std::byte>>&& gadgets, vector<CombineProvenance>&& prov, std::size_t pruned, std::size_t skipped) {
	std::size_t survivor_size = gadgets.size();
	std::size_t edge_count = prov.size();

	auto selsert_result = selsert_gadget_by_data(env, gadget_hashtable, gadget_index, std::move(gadgets));

	//We group by input2, then by input1.  Because we combine against each right
	//operand in sequence, we're usually not already sorted, so we don't check
	//is_sorted like we do in the other commit_*_result functions.
	std::sort(prov.begin(), prov.end(), InputGroupingProvCmp());
	vector<pair<lmdb::dbi*, vector<SkinnyPage>>> pages;
	vector<pair<std::string, vector<pair<uint64_t, uint64_t>>>> pending_completions;
	pages.reserve(edge_tables.size());
	while (!prov.empty()) {
		auto [first, last] = std::equal_range(prov.begin(), prov.end(), prov.back(), less_input2());
		auto edge_db_iter = std::find_if(edge_tables.begin(), edge_tables.end(),
				[input2=first->input2](const auto& p){return p.first == input2;});
		if (edge_db_iter == edge_tables.end())
			throw std::logic_error(fmt::format("can't happen: didn't open edge db for {}?", first->input2));
		pages.emplace_back(&edge_db_iter->second, paginate_for_skinny_edges(first, last, selsert_result.local_to_global.begin()));
		interval_accumulator<uint64_t> comp_input1(512);
		for (auto i = first; i != last; ++i)
			comp_input1(i->input1);
		pending_completions.emplace_back(fmt::format("combine-{}", first->input2), std::move(comp_input1).finish());
		prov.erase(first, last); //TODO: this erase isn't even helpful, it can't release memory
	}
	{vector<CombineProvenance> ensure_memory_is_freed(std::move(prov));}

	auto txn = lmdb::txn::begin(env);
	for (std::size_t i = 0; i < pages.size(); ++i) {
		if (!record_completion(txn, completions, pending_completions[i].first, pending_completions[i].second))
			throw std::runtime_error(fmt::format(
					"combine completion collision for skinny edges; completion key {}, first input interval {}",
					pending_completions[i].first, pending_completions[i].second[0]));
		insert_skinny_edges(txn, *pages[i].first, std::move(pages[i].second));
	}
	txn.commit();
	return {skipped, pruned, survivor_size - selsert_result.novel_size(), selsert_result.novel_size(), edge_count};
}

DatabaseOperationStatistics do_combine_db(vector<pair<uint64_t, uint64_t>> left_intervals,
		vector<std::uint64_t> right_gids, unsigned int precision, unsigned int max_left_states) {
	lmdb::env env = lmdb::env::create(); //TODO: flags?
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(g_database_path.c_str(), MDB_NORDAHEAD); //TODO: flags?
	lmdb::dbi gadget_hashtable, gadget_index, completions;
	vector<pair<uint64_t, lmdb::dbi>> edge_tables;
	{
		//We may have to create edge databases, though usually the driver will
		//create them for us, so try a read-only txn first.
		try {
			lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
			gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
			gadget_index = lmdb::dbi::open(txn, "gadget_index");
			completions = lmdb::dbi::open(txn, "completions");
			for (uint64_t i : right_gids)
				edge_tables.emplace_back(i, lmdb::dbi::open(txn, fmt::format("edges-skinny-combine-{}", i).c_str()));
			txn.commit();
		} catch (lmdb::not_found_error&) {
			lmdb::txn txn = lmdb::txn::begin(env);
			gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
			gadget_index = lmdb::dbi::open(txn, "gadget_index");
			completions = lmdb::dbi::open(txn, "completions");
			for (uint64_t i : right_gids)
				edge_tables.emplace_back(i, lmdb::dbi::open(txn, fmt::format("edges-skinny-combine-{}", i).c_str(),
						MDB_CREATE | MDB_INTEGERKEY));
			txn.commit();
		}
	}

	Finisher<CombineProvenance> outputs = operate_combine(env, gadget_hashtable, gadget_index,
			std::move(left_intervals), std::move(right_gids), precision, max_left_states);
	return commit_combine_result_skinny(env, gadget_hashtable, gadget_index, edge_tables, completions,
			std::move(outputs.rows_).values_container(), std::move(outputs.prov_), outputs.pruned_, outputs.skipped_);
}

DatabaseOperationStatistics do_combine_db_full(vector<pair<uint64_t, uint64_t>> left_intervals,
		vector<std::uint64_t> right_gids, unsigned int precision, unsigned int max_left_states) {
	lmdb::env env = lmdb::env::create(); //TODO: flags?
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(g_database_path.c_str(), MDB_NORDAHEAD); //TODO: flags?
	lmdb::dbi gadget_hashtable, gadget_index, completions;
	vector<pair<uint64_t, lmdb::dbi>> edge_tables;
	{
		//We may have to create edge databases, though usually the driver will
		//create them for us, so try a read-only txn first.
		try {
			lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
			gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
			gadget_index = lmdb::dbi::open(txn, "gadget_index");
			completions = lmdb::dbi::open(txn, "completions");
			for (uint64_t i : right_gids)
				edge_tables.emplace_back(i, lmdb::dbi::open(txn, fmt::format("edges-combine-{}", i).c_str()));
			txn.commit();
		} catch (lmdb::not_found_error&) {
			lmdb::txn txn = lmdb::txn::begin(env);
			gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
			gadget_index = lmdb::dbi::open(txn, "gadget_index");
			completions = lmdb::dbi::open(txn, "completions");
			for (uint64_t i : right_gids)
				edge_tables.emplace_back(i, lmdb::dbi::open(txn, fmt::format("edges-combine-{}", i).c_str(),
						MDB_CREATE | MDB_INTEGERKEY));
			txn.commit();
		}
	}

	Finisher<CombineProvenance> outputs = operate_combine(env, gadget_hashtable, gadget_index,
			std::move(left_intervals), std::move(right_gids), precision, max_left_states);
	return commit_combine_result_full(env, gadget_hashtable, gadget_index, edge_tables, completions,
			std::move(outputs.rows_).values_container(), std::move(outputs.prov_), outputs.pruned_, outputs.skipped_);
}

FirsthalfStatistics do_combine_db_firsthalf(vector<pair<uint64_t, uint64_t>> left_intervals,
		vector<std::uint64_t> right_gids, unsigned int precision, unsigned int max_left_states) {
	lmdb::env env = lmdb::env::create(); //TODO: flags?
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(g_database_path.c_str(), MDB_NORDAHEAD | MDB_RDONLY); //TODO: flags?
	lmdb::dbi gadget_hashtable, gadget_index;
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		txn.commit();
	}

	Finisher<CombineProvenance> outputs = operate_combine(env, gadget_hashtable, gadget_index,
			std::move(left_intervals), std::move(right_gids), precision, max_left_states);

	DatabaseMetadata meta = read_meta(env);
	return write_firsthalf(meta.id, EdgeKind::combine, std::move(outputs.rows_).values_container(),
			std::move(outputs.prov_), outputs.pruned_, outputs.skipped_);
}

//DatabaseOperationStatistics do_combine_db_secondhalf(vector<std::string> firsthalves) {
//	lmdb::env env = lmdb::env::create(); //TODO: flags?
//	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
//	env.set_max_dbs(64);
//	env.open(g_database_path.c_str(), MDB_NORDAHEAD); //TODO: flags?
//	DatabaseMetadata meta = read_meta(env);
//
//	Refinisher<CombineProvenance> refinisher(meta.id, EdgeKind::combine);
//	for (const std::string& filename : firsthalves)
//		refinisher.read(filename);
//	tsl::ordered_set<uint64_t, farmhash_hash> right_gids;
//	//We could store this in the firsthalves or try to open all the edge tables.
//	//Both avoid a pass over all the provs at the cost of some code complexity.
//	for (const CombineProvenance& p : refinisher.prov_)
//		right_gids.insert(p.input2);
//
//	lmdb::dbi gadget_hashtable, gadget_index, completions;
//	vector<pair<uint64_t, lmdb::dbi>> edge_tables;
//	{
//		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
//		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
//		gadget_index = lmdb::dbi::open(txn, "gadget_index");
//		completions = lmdb::dbi::open(txn, "completions");
//		for (uint64_t i : right_gids) //just assuming they all already exist
//			edge_tables.emplace_back(i, lmdb::dbi::open(txn, fmt::format("edges-skinny-combine-{}", i).c_str()));
//		txn.commit();
//	}
//
//	auto stats = commit_combine_result_skinny(env, gadget_hashtable, gadget_index, edge_tables, completions,
//			refinisher.gadgets(), std::move(refinisher.prov_), refinisher.pruned_, refinisher.skipped_);
//	delete_many_files(firsthalves.begin(), firsthalves.end());
//	return stats;
//}

Finisher<ConnectProvenance> operate_connect(lmdb::env& env, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index,
		vector<pair<uint64_t, uint64_t>>& input_intervals, unsigned int max_states) {
	vector<pair<uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(
			env, gadget_hashtable, gadget_index, input_intervals);
	std::size_t skipped = 0;

	//We skip any gadget with locations < 4, but still record completions.
	auto new_end = std::partition(inputs.begin(), inputs.end(),
			//partition sorts true before false, so negate filter condition
			[](const auto& p) {return !(encoding::locations(p.second.data()) < 4);});
	skipped += std::distance(new_end, inputs.end());
	inputs.erase(new_end, inputs.end());

	//We skip any gadget with states > max_states, but do not record completions
	//as we may have to come back for those later.
	new_end = std::partition(inputs.begin(), inputs.end(), [max_states](const auto& p) {
		//partition sorts true before false, so negate filter condition
		return !(encoding::stats(p.second.data()).states > max_states);
	});
	interval_accumulator<uint64_t> bad(256);
	for (auto i = new_end; i != inputs.end(); ++i)
		bad(i->first);
	vector<pair<uint64_t, uint64_t>> bad_intervals = std::move(bad).finish();
	input_intervals = interval_difference(input_intervals.begin(), input_intervals.end(),
			bad_intervals.begin(), bad_intervals.end());
	skipped += std::distance(new_end, inputs.end());
	inputs.erase(new_end, inputs.end());

	Finisher<ConnectProvenance> outputs = do_connect(std::move(inputs));
	outputs.skip(skipped);
	return outputs;
}

DatabaseOperationStatistics commit_connect_result_full(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, lmdb::dbi& edges, lmdb::dbi& completions,
		vector<pair<uint64_t, uint64_t>>&& input_intervals,	vector<vector<std::byte>>&& gadgets,
		vector<ConnectProvenance>&& prov, std::size_t pruned, std::size_t skipped) {
	std::size_t survivor_size = gadgets.size();
	std::size_t edge_count = prov.size();

	auto selsert_result = selsert_gadget_by_data(env, gadget_hashtable, gadget_index, std::move(gadgets));

	//Group provs by input1, if they aren't already.
	if (!std::is_sorted(prov.begin(), prov.end(), InputGroupingProvCmp()))
		std::sort(prov.begin(), prov.end(), InputGroupingProvCmp());

	auto txn = lmdb::txn::begin(env);
	{
		lmdb::cursor cur = lmdb::cursor::open(txn, edges);
		boost::container::static_vector<ConnectEdge, 16> buf;

		for (std::size_t i = 0; i < prov.size();) {
			buf.clear();
			//Find the block sharing the same input1.
			std::size_t j = i;
			while (j < prov.size() && prov[i].input1 == prov[j].input1) {
				const ConnectProvenance& p = prov[j];
				ConnectEdge e;
				e.output = selsert_result.local_to_global[p.output1];
				e.connectPoint = p.connectPoint;
				e.canonicalizePermutation = p.canonicalizePermutation;
				buf.push_back(e);
				++j;
			}
			//For canonicalization purposes, sort the edges.  (We have to remap
			//through local_to_global before we can do this.)
			std::sort(buf.begin(), buf.end());
			std::string_view value(reinterpret_cast<char*>(buf.data()), buf.size()*sizeof(ConnectEdge));
			if (!cur.put(lmdb::to_sv(prov[i].input1), value, MDB_NOOVERWRITE)) {
				if (value.size() == 0 || value.size() % sizeof(ConnectEdge) != 0)
					throw std::logic_error(fmt::format("connect edge data for key {} has value length {} (not a multiple of {})",
							prov[i].input1, value.size(), sizeof(ConnectEdge)));
				const ConnectEdge* first = reinterpret_cast<const ConnectEdge*>(value.data());
				const ConnectEdge* last = first + value.size() / sizeof(ConnectEdge);
				if (!std::equal(buf.cbegin(), buf.cend(), first, last))
					throw std::logic_error(fmt::format("differing connect edges from {}: {} and {}",
							prov[i].input1, buf, make_range_for_pair(first, last)));
				edge_count -= buf.size(); //don't count edges already present
			}
			i = j;
		}
	}
	//Completions now describe skinny edges, so we don't change them here.
	txn.commit();

	return {skipped, pruned, survivor_size - selsert_result.novel_size(), selsert_result.novel_size(), edge_count};
}

DatabaseOperationStatistics commit_connect_result_skinny(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, lmdb::dbi& edges, lmdb::dbi& completions,
		vector<pair<uint64_t, uint64_t>>&& input_intervals,	vector<vector<std::byte>>&& gadgets,
		vector<ConnectProvenance>&& prov, std::size_t pruned, std::size_t skipped) {
	std::size_t survivor_size = gadgets.size();
	std::size_t edge_count = prov.size();

	auto selsert_result = selsert_gadget_by_data(env, gadget_hashtable, gadget_index, std::move(gadgets));

	//Group provs by input1, if they aren't already.
	if (!std::is_sorted(prov.begin(), prov.end(), InputGroupingProvCmp()))
		std::sort(prov.begin(), prov.end(), InputGroupingProvCmp());
	vector<SkinnyPage> pages = paginate_for_skinny_edges(prov.begin(), prov.end(), selsert_result.local_to_global.begin());
	//Release memory before blocking to start a write transaction.
	{vector<ConnectProvenance> ensure_memory_is_freed(std::move(prov));}

	auto txn = lmdb::txn::begin(env);
	if (!record_completion(txn, completions, "connect", input_intervals))
		throw std::runtime_error(fmt::format(
				"connect completion collision for skinny edges; adding {}", fmt::join(input_intervals, ", ")));
	insert_skinny_edges(txn, edges, std::move(pages));
	txn.commit();

	return {skipped, pruned, survivor_size - selsert_result.novel_size(), selsert_result.novel_size(), edge_count};
}

DatabaseOperationStatistics do_connect_db(vector<pair<uint64_t, uint64_t>> input_intervals, unsigned int max_states) {
	lmdb::env env = lmdb::env::create(); //TODO: flags?
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(g_database_path.c_str(), MDB_NORDAHEAD); //TODO: flags?
	lmdb::dbi gadget_hashtable, gadget_index, completions, connect_edges;
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		completions = lmdb::dbi::open(txn, "completions");
		connect_edges = lmdb::dbi::open(txn, "edges-skinny-connect");
		txn.commit();
	}

	Finisher<ConnectProvenance> outputs = operate_connect(env, gadget_hashtable, gadget_index, input_intervals, max_states);
	return commit_connect_result_skinny(env, gadget_hashtable, gadget_index, connect_edges, completions,
			std::move(input_intervals), std::move(outputs.rows_).values_container(),
			std::move(outputs.prov_), outputs.pruned_, outputs.skipped_);
}

DatabaseOperationStatistics do_connect_db_full(vector<pair<uint64_t, uint64_t>> input_intervals, unsigned int max_states) {
	lmdb::env env = lmdb::env::create(); //TODO: flags?
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(g_database_path.c_str(), MDB_NORDAHEAD); //TODO: flags?
	lmdb::dbi gadget_hashtable, gadget_index, completions, connect_edges;
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		completions = lmdb::dbi::open(txn, "completions");
		connect_edges = lmdb::dbi::open(txn, "edges-connect");
		txn.commit();
	}

	Finisher<ConnectProvenance> outputs = operate_connect(env, gadget_hashtable, gadget_index, input_intervals, max_states);
	return commit_connect_result_full(env, gadget_hashtable, gadget_index, connect_edges, completions,
			std::move(input_intervals), std::move(outputs.rows_).values_container(),
			std::move(outputs.prov_), outputs.pruned_, outputs.skipped_);
}

FirsthalfStatistics do_connect_db_firsthalf(vector<pair<uint64_t, uint64_t>> input_intervals, unsigned int max_states) {
	lmdb::env env = lmdb::env::create(); //TODO: flags?
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(g_database_path.c_str(), MDB_NORDAHEAD | MDB_RDONLY); //TODO: flags?
	lmdb::dbi gadget_hashtable, gadget_index;
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		txn.commit();
	}

	Finisher<ConnectProvenance> outputs = operate_connect(env, gadget_hashtable, gadget_index, input_intervals, max_states);

	DatabaseMetadata meta = read_meta(env);
	return write_firsthalf(meta.id, EdgeKind::connect, std::move(outputs.rows_).values_container(),
			std::move(outputs.prov_), outputs.pruned_, outputs.skipped_, std::move(input_intervals));
}

//DatabaseOperationStatistics do_connect_db_secondhalf(vector<std::string> firsthalves) {
//	lmdb::env env = lmdb::env::create(); //TODO: flags?
//	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
//	env.set_max_dbs(64);
//	env.open(g_database_path.c_str(), MDB_NORDAHEAD); //TODO: flags?
//	DatabaseMetadata meta = read_meta(env);
//
//	Refinisher<ConnectProvenance> refinisher(meta.id, EdgeKind::connect);
//	for (const std::string& filename : firsthalves)
//		refinisher.read(filename);
//
//	lmdb::dbi gadget_hashtable, gadget_index, completions, connect_edges;
//	{
//		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
//		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
//		gadget_index = lmdb::dbi::open(txn, "gadget_index");
//		completions = lmdb::dbi::open(txn, "completions");
//		connect_edges = lmdb::dbi::open(txn, "edges-skinny-connect");
//		txn.commit();
//	}
//
//	auto stats = commit_connect_result_skinny(env, gadget_hashtable, gadget_index, connect_edges, completions,
//			std::move(refinisher.input_intervals_), refinisher.gadgets(), std::move(refinisher.prov_),
//			refinisher.pruned_, refinisher.skipped_);
//	delete_many_files(firsthalves.begin(), firsthalves.end());
//	return stats;
//}

DatabaseOperationStatistics commit_simple_result(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, lmdb::dbi& edges, lmdb::dbi& completions,
		std::string_view completion_kind, bool idempotent,
		vector<pair<uint64_t, uint64_t>>&& input_intervals,	vector<vector<std::byte>>&& gadgets,
		vector<SimpleProvenance>&& prov, std::size_t pruned, std::size_t skipped) {
	std::size_t survivor_size = gadgets.size();
	std::size_t edge_count = prov.size();

	auto selsert_result = selsert_gadget_by_data(env, gadget_hashtable, gadget_index, std::move(gadgets));

	//We don't need to group provs by input1 (as there's only one close edge
	//from a given gadget), but sorting improves insert performance.
	if (!std::is_sorted(prov.begin(), prov.end(), InputGroupingProvCmp()))
		std::sort(prov.begin(), prov.end(), InputGroupingProvCmp());

	auto txn = lmdb::txn::begin(env);
	{
		lmdb::cursor cur = lmdb::cursor::open(txn, edges);
		SimpleEdge e;
		for (SimpleProvenance& p : prov) {
			e.output = selsert_result.local_to_global[p.output1];
			e.canonicalizePermutation = p.canonicalizePermutation;
			std::string_view value = lmdb::to_sv(e);
			if (!cur.put(lmdb::to_sv(p.input1), value, MDB_NOOVERWRITE)) {
				//Having done some duplicate work is fine, so long as we got the
				//same result.  If not, either there's a bug in the code or the
				//database is corrupt.
				const SimpleEdge exist = lmdb::from_sv<SimpleEdge>(value);
				if (e != exist)
					throw std::runtime_error(fmt::format("differing {} edges from {}: {}/{} and {}/{}",
							completion_kind, p.input1, std::as_const(e).output, e.canonicalizePermutation,
							exist.output, exist.canonicalizePermutation));
				--edge_count; //we didn't actually add this edge, don't count it
			}
		}
	}

	if (idempotent) {
		//Any target of a close edge cannot also be the source of a close edge,
		//so we can add completion for them.  (We can't sort local_to_global
		//until we're done with the above loop, though we could copy if we
		//really want to get this out of the transaction.)
		std::sort(selsert_result.local_to_global.begin(), selsert_result.local_to_global.end());
		auto stuff = maximal_intervals(selsert_result.local_to_global.cbegin(), selsert_result.local_to_global.cend());
		input_intervals = interval_union(input_intervals.cbegin(), input_intervals.cend(), stuff.cbegin(), stuff.cend());
	}

	record_completion(txn, completions, completion_kind, input_intervals, true);
	txn.commit();

	return {skipped, pruned, survivor_size - selsert_result.novel_size(), selsert_result.novel_size(), edge_count};
}

DatabaseOperationStatistics commit_close_result(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, lmdb::dbi& close_edges, lmdb::dbi& completions,
		vector<pair<uint64_t, uint64_t>>&& input_intervals,	vector<vector<std::byte>>&& gadgets,
		vector<SimpleProvenance>&& prov, std::size_t pruned, std::size_t skipped) {
	return commit_simple_result(env, gadget_hashtable, gadget_index, close_edges, completions, "close", true,
			std::move(input_intervals), std::move(gadgets), std::move(prov), pruned, skipped);
}

//declared in sync.cpp, extracted for the benefit of sync_mode
DatabaseOperationStatistics do_close_db0(vector<pair<uint64_t, uint64_t>> input_intervals,
		lmdb::env& env, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index, lmdb::dbi& close_edges,
		lmdb::dbi& completions) {
	vector<pair<uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(
			env, gadget_hashtable, gadget_index, input_intervals);
	Finisher<SimpleProvenance> outputs = do_close(std::move(inputs));
	return commit_close_result(env, gadget_hashtable, gadget_index, close_edges, completions,
			std::move(input_intervals), std::move(outputs.rows_).values_container(),
			std::move(outputs.prov_), outputs.pruned_, outputs.skipped_);
}

DatabaseOperationStatistics do_close_db(vector<pair<uint64_t, uint64_t>> input_intervals) {
	lmdb::env env = lmdb::env::create(); //TODO: flags?
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(g_database_path.c_str(), MDB_NORDAHEAD); //TODO: flags?
	lmdb::dbi gadget_hashtable, gadget_index, completions, close_edges;
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		completions = lmdb::dbi::open(txn, "completions");
		close_edges = lmdb::dbi::open(txn, "edges-close");
		txn.commit();
	}
	return do_close_db0(std::move(input_intervals), env, gadget_hashtable, gadget_index, close_edges, completions);
}

FirsthalfStatistics do_close_db_firsthalf(vector<pair<uint64_t, uint64_t>> input_intervals) {
	lmdb::env env = lmdb::env::create(); //TODO: flags?
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(g_database_path.c_str(), MDB_NORDAHEAD | MDB_RDONLY); //TODO: flags?

	DatabaseMetadata meta = read_meta(env);
	vector<pair<uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(env, input_intervals);
	Finisher<SimpleProvenance> outputs = do_close(std::move(inputs));
	return write_firsthalf(meta.id, EdgeKind::close, std::move(outputs.rows_).values_container(),
			std::move(outputs.prov_), outputs.pruned_, outputs.skipped_, std::move(input_intervals));
}

//DatabaseOperationStatistics do_close_db_secondhalf(vector<std::string> firsthalves) {
//	lmdb::env env = lmdb::env::create(); //TODO: flags?
//	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
//	env.set_max_dbs(64);
//	env.open(g_database_path.c_str(), MDB_NORDAHEAD); //TODO: flags?
//	DatabaseMetadata meta = read_meta(env);
//
//	Refinisher<SimpleProvenance> refinisher(meta.id, EdgeKind::close);
//	for (const std::string& filename : firsthalves)
//		refinisher.read(filename);
//
//	lmdb::dbi gadget_hashtable, gadget_index, completions, close_edges;
//	{
//		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
//		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
//		gadget_index = lmdb::dbi::open(txn, "gadget_index");
//		completions = lmdb::dbi::open(txn, "completions");
//		close_edges = lmdb::dbi::open(txn, "edges-close");
//		txn.commit();
//	}
//
//	auto stats = commit_close_result(env, gadget_hashtable, gadget_index, close_edges, completions,
//			std::move(refinisher.input_intervals_), refinisher.gadgets(),
//			std::move(refinisher.prov_), refinisher.pruned_, refinisher.skipped_);
//	delete_many_files(firsthalves.begin(), firsthalves.end());
//	return stats;
//}

DatabaseOperationStatistics commit_mirror_result(lmdb::env& env, lmdb::dbi& gadget_hashtable,
		lmdb::dbi& gadget_index, lmdb::dbi& mirror_edges, lmdb::dbi& completions,
		vector<pair<uint64_t, uint64_t>>&& input_intervals,	vector<vector<std::byte>>&& gadgets,
		vector<SimpleProvenance>&& prov, std::size_t pruned, std::size_t skipped) {
	return commit_simple_result(env, gadget_hashtable, gadget_index, mirror_edges, completions, "mirror", false,
			std::move(input_intervals), std::move(gadgets), std::move(prov), pruned, skipped);
}

//TODO: There's a lot of duplication between close and mirror (and maybe also
//connect in the future).  Can we parameterize/templatize them together?

//declared in sync.cpp, extracted for the benefit of sync_mode
DatabaseOperationStatistics do_mirror_db0(vector<pair<uint64_t, uint64_t>> input_intervals,
		lmdb::env& env, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index, lmdb::dbi& mirror_edges,
		lmdb::dbi& completions) {
	vector<pair<uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(
			env, gadget_hashtable, gadget_index, input_intervals);
	Finisher<SimpleProvenance> outputs = do_mirror(std::move(inputs));
	return commit_mirror_result(env, gadget_hashtable, gadget_index, mirror_edges, completions,
			std::move(input_intervals), std::move(outputs.rows_).values_container(),
			std::move(outputs.prov_), outputs.pruned_, outputs.skipped_);
}

DatabaseOperationStatistics do_mirror_db(vector<pair<uint64_t, uint64_t>> input_intervals) {
	lmdb::env env = lmdb::env::create(); //TODO: flags?
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(g_database_path.c_str(), MDB_NORDAHEAD); //TODO: flags?
	lmdb::dbi gadget_hashtable, gadget_index, completions, mirror_edges;
	{
		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
		gadget_index = lmdb::dbi::open(txn, "gadget_index");
		completions = lmdb::dbi::open(txn, "completions");
		mirror_edges = lmdb::dbi::open(txn, "edges-mirror");
		txn.commit();
	}
	return do_mirror_db0(std::move(input_intervals), env, gadget_hashtable, gadget_index, mirror_edges, completions);
}

FirsthalfStatistics do_mirror_db_firsthalf(vector<pair<uint64_t, uint64_t>> input_intervals) {
	lmdb::env env = lmdb::env::create(); //TODO: flags?
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(g_database_path.c_str(), MDB_NORDAHEAD | MDB_RDONLY); //TODO: flags?

	DatabaseMetadata meta = read_meta(env);
	vector<pair<uint64_t, vector<std::byte>>> inputs = select_gadget_id_to_data(env, input_intervals);
	Finisher<SimpleProvenance> outputs = do_mirror(std::move(inputs));
	return write_firsthalf(meta.id, EdgeKind::mirror, std::move(outputs.rows_).values_container(),
			std::move(outputs.prov_), outputs.pruned_, outputs.skipped_, std::move(input_intervals));
}

//DatabaseOperationStatistics do_mirror_db_secondhalf(vector<std::string> firsthalves) {
//	lmdb::env env = lmdb::env::create(); //TODO: flags?
//	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
//	env.set_max_dbs(64);
//	env.open(g_database_path.c_str(), MDB_NORDAHEAD); //TODO: flags?
//	DatabaseMetadata meta = read_meta(env);
//
//	Refinisher<SimpleProvenance> refinisher(meta.id, EdgeKind::mirror);
//	for (const std::string& filename : firsthalves)
//		refinisher.read(filename);
//
//	lmdb::dbi gadget_hashtable, gadget_index, completions, mirror_edges;
//	{
//		lmdb::txn txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
//		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable");
//		gadget_index = lmdb::dbi::open(txn, "gadget_index");
//		completions = lmdb::dbi::open(txn, "completions");
//		mirror_edges = lmdb::dbi::open(txn, "edges-mirror");
//		txn.commit();
//	}
//
//	auto stats = commit_mirror_result(env, gadget_hashtable, gadget_index, mirror_edges, completions,
//			std::move(refinisher.input_intervals_), refinisher.gadgets(),
//			std::move(refinisher.prov_), refinisher.pruned_, refinisher.skipped_);
//	delete_many_files(firsthalves.begin(), firsthalves.end());
//	return stats;
//}



vector<pair<uint64_t, vector<unsigned int>>> do_deleted_locations(vector<AnyProv> provs) {
	vector<uint64_t> gids;
	for (const AnyProv& p : provs) {
		gids.push_back(p.output());
		gids.push_back(p.input1());
		if (p.kind() == EdgeKind::combine)
			gids.push_back(p.input2());
	}
	std::sort(gids.begin(), gids.end());
	gids.erase(std::unique(gids.begin(), gids.end()), gids.end());

	tsl::hopscotch_map<uint64_t, vector<std::byte>, farmhash_hash> gadget_data;
	{
		lmdb::env env = lmdb::env::create();
		env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
		env.set_max_dbs(64);
		env.open(g_database_path.c_str(), MDB_RDONLY | MDB_NORDAHEAD);
		for (pair<uint64_t, vector<std::byte>>& p : select_gadget_id_to_data(env, gids))
			gadget_data.try_emplace(p.first, std::move(p.second));
	}

	vector<pair<uint64_t, vector<unsigned int>>> responses;
	for (const AnyProv& p : provs) {
		if (p.kind() == EdgeKind::combine) {
			const auto& output_data = gadget_data.at(p.output());
			const auto& input1_data = gadget_data.at(p.input1());
			const auto& input2_data = gadget_data.at(p.input2());
			if (encoding::locations(output_data.data()) == encoding::locations(input1_data.data()) +
					encoding::locations(input2_data.data()) - 2)
				continue; //no deleted symbols
			SymbolSet dels = combine_deleted_symbols(
					*encoding::decode<16>(input1_data), *encoding::decode<16>(input2_data),
					p.splice(), p.rotation(), p.connectPoint());
			if (!dels.empty())
				responses.emplace_back(p.output(), vector<unsigned int>{dels.begin(), dels.end()});
		} else if (p.kind() == EdgeKind::connect) {
			const auto& output_data = gadget_data.at(p.output());
			const auto& input_data = gadget_data.at(p.input1());
			if (encoding::locations(output_data.data()) == encoding::locations(input_data.data()) - 2)
				continue; //no deleted symbols
			SymbolSet dels = connect_deleted_symbols(*encoding::decode(input_data.data(), input_data.size()), p.connectPoint());
			if (!dels.empty())
				responses.emplace_back(p.output(), vector<unsigned int>{dels.begin(), dels.end()});
		} else
			throw std::runtime_error(fmt::format("bad deleted-locations request prov: {}", p));
	}
	return responses;
}



/**
 * @return a string containing various information about this worker
 */
std::string do_ping() {
	//maybe information about the parent process (might be socat)
	//hostname
	//try to get information about stdin/stdout if they're sockets
	//information about slurm environment variables (if any)
	return "pong";
}



const std::pair<string_view, handler_ptr> handlers[] = {
	{"ping"sv, &handler_adapter<do_ping>},

//	{"canonicalize"sv, &handler_adapter<canonicalize_from_slls>},

	{"connect-db"sv, &handler_adapter<do_connect_db>},
	{"connect-db-full"sv, &handler_adapter<do_connect_db_full>},
	{"connect-db-firsthalf"sv, &handler_adapter<do_connect_db_firsthalf>},
	{"connect-db-secondhalf"sv, &handler_adapter<do_secondhalf_db>},
	{"combine-db"sv, &handler_adapter<do_combine_db>},
	{"combine-db-firsthalf"sv, &handler_adapter<do_combine_db_firsthalf>},
	{"combine-db-secondhalf"sv, &handler_adapter<do_secondhalf_db>},
	{"combine-db-full"sv, &handler_adapter<do_combine_db_full>},
	{"close-db"sv, &handler_adapter<do_close_db>},
	{"close-db-firsthalf"sv, &handler_adapter<do_close_db_firsthalf>},
	{"close-db-secondhalf"sv, &handler_adapter<do_secondhalf_db>},
	{"mirror-db"sv, &handler_adapter<do_mirror_db>},
	{"mirror-db-firsthalf"sv, &handler_adapter<do_mirror_db_firsthalf>},
	{"mirror-db-secondhalf"sv, &handler_adapter<do_secondhalf_db>},

	{"deleted-locations"sv, &handler_adapter<do_deleted_locations>},
};



vector<char> exhaust_stdin() {
	vector<char> data;
	data.resize(4096, 0);
	size_t index = 0;
	while (true) {
		size_t count = data.size() - index;
		size_t bytes_read = std::fread(&data[index], sizeof(unsigned char), count, stdin);
		if (bytes_read != count) {
			if (std::feof(stdin)) {
				data.resize(index + bytes_read);
				return data;
			}
			if (std::ferror(stdin)) {
				auto savederrno = errno;
				fmt::print(stderr, "error reading from stdin: {} ({}), after reading {} before and {} last\n",
						strerror(savederrno), savederrno, index, bytes_read);
				std::exit(1);
			}
		} else {
			index += bytes_read;
			data.resize(std::min(data.size() * 2, data.size() + 1024*1024*1024), 0);
		}
	}
	//We always return out of the loop or exit(1).
}

msgpack::object_handle read_input() {
	vector<char> input = exhaust_stdin();
	//TODO: optionally decompress the message (based on a command-line option)
	return msgpack::unpack(input.data(), input.size());
}

void write_output(const void* data, size_t size) {
	//TODO: optional compression based on command-line option
	size_t index = 0;
	while (index < size) {
		size_t count = size - index;
		size_t bytes_written = std::fwrite(reinterpret_cast<const char*>(data) + index, sizeof(char), count, stdout);
		if (bytes_written != count) {
			if (std::ferror(stdout)) {
				auto savederrno = errno;
				fmt::print(stderr, "error writing to stdout: {} ({}), after writing {} before and {} last\n",
						strerror(savederrno), savederrno, index, bytes_written);
				std::exit(1);
			} else
				//Unusual enough to be worth remarking about.
				fmt::print(stderr, "Short fwrite? index {}, count {}, wrote {} (short by {})\n",
						index, count, bytes_written, (count - bytes_written));
		}
		index += bytes_written;
	}
	std::fflush(stdout);
}

int msgpack_mode(std::string_view db_path, std::string_view input_file, std::string_view output_file) {
	g_database_path = std::string(db_path);
	if (input_file != "-"sv) {
		if (!std::freopen(std::string(input_file).c_str(), "rb", stdin)) {
			auto savederrno = errno;
			fmt::print(stderr, "unable to reopen stdin from {}: {} ({})\n", input_file, strerror(savederrno), errno);
			std::exit(1);
		}
	}
	if (output_file != "-"sv) {
		if (!std::freopen(std::string(output_file).c_str(), "wbx", stdout)) {
			auto savederrno = errno;
			fmt::print(stderr, "unable to reopen stdout from {}: {} ({})\n", output_file, strerror(savederrno), errno);
			std::exit(1);
		}
	}

	simple_buffer response = dispatch(read_input(), std::begin(handlers), std::end(handlers));
	write_output(response.data(), response.size());
	return 0;
}