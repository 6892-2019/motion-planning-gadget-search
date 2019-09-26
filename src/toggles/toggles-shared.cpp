#include "precompiled.hpp"
#include "toggles-shared.hpp"
#include "intervals.hpp"
#include "varint.hpp"
#include "transform_reduce.hpp"
#ifndef __SANITIZE_ADDRESS__
#include <jemalloc/jemalloc.h>
#endif //__SANITIZE_ADDRESS

using std::vector;
using std::pair;
using std::uint64_t;

void jemalloc_tuning() {
#ifndef __SANITIZE_ADDRESS__
	//Both the runner and driver often block (on lmdb or on running tasks), so
	//configure jemalloc background threads to let jemalloc yield unused memory
	//back to the operating system.
	bool yes_please = true;
	int rc = mallctl("background_thread", nullptr, 0, &yes_please, sizeof(yes_please));
	if (rc)
		fmt::print("warning: problem initializing jemalloc opts: {} {}", rc, strerror(rc));
#endif //__SANITIZE_ADDRESS
}

unsigned int check_for_stale_readers(lmdb::env& env) {
	//lmdbxx doesn't wrap this one; do it ourselves
	int dead_count = -1;
	int rc = mdb_reader_check(env, &dead_count);
	if (rc != MDB_SUCCESS)
		lmdb::error::raise("mdb_reader_check", rc);
	return numeric_cast<unsigned int>(dead_count);
}



namespace {
template<class Edge>
std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_edges0(lmdb::env& env,
		lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources) {
	interval_accumulator<uint64_t> accum(512);
	visit_edges<Edge>(env, edge_db, sources, [&](uint64_t, const Edge& e) {
		accum(e.output);
		return VisitEdgeResult::proceed;
	});
	return std::move(accum).finish();
}

auto make_follow_edges_tasks(const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources, unsigned int threads) {
	std::size_t task_size = std::clamp<std::size_t>(interval_size(sources) / (threads*4), 5000, 25000);
	return interval_chunk(sources.begin(), sources.end(), task_size);
}

//TODO: actual interval_union(Range, Range) overload; this appears in several
//places as a merge lambda
struct interval_union_vector {
	std::vector<std::pair<std::uint64_t, std::uint64_t>> operator()(
			std::vector<std::pair<std::uint64_t, std::uint64_t>> left,
			std::vector<std::pair<std::uint64_t, std::uint64_t>> right) const {
		return interval_union(left.begin(), left.end(), right.begin(), right.end());
	}
};
}//anonymous namespace

template<class Edge>
std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_edges(lmdb::env& env,
		lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources, unsigned int threads) {
	if (threads <= 1)
		return follow_edges0<Edge>(env, edge_db, sources);
	else {
		std::vector<std::vector<std::pair<std::uint64_t, std::uint64_t>>> tasks
				= make_follow_edges_tasks(sources, threads);
		return transform_reduce(std::move(tasks), threads,
				std::bind_front(follow_edges0<Edge>, std::ref(env), std::ref(edge_db)),
				interval_union_vector());
	}
}

//explicitly instantiate the three we need
template std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_edges<CombineEdge>(
		lmdb::env& env,	lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources, unsigned int threads);
template std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_edges<ConnectEdge>(
		lmdb::env& env,	lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources, unsigned int threads);
template std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_edges<SimpleEdge>(
		lmdb::env& env,	lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources, unsigned int threads);


namespace detail {
//Fills in the offsets (including a past-the-end element, so n+1 offsets for n
//lists) and returns the first id on the page.
std::uint64_t decode_skinny_edge_page(std::string_view key, std::string_view value,
		std::vector<std::uint32_t>& offsets) {
	offsets.clear();
	const std::byte* first = reinterpret_cast<const std::byte*>(value.data());
	std::uint16_t length_of_offsets;
	std::memcpy(&length_of_offsets, first, sizeof(std::uint16_t));
	const std::byte* p = first + sizeof(std::uint16_t);
	offsets.push_back(length_of_offsets);
	while (p != first + length_of_offsets)
		offsets.push_back(offsets.back() + numeric_cast<std::uint16_t>(upv::read(p))); //remaining elements are list lengths, so sum to get offset
	//Strictly speaking, the last length could be omitted because it's implied
	//by the length of the value.
	assert(offsets.back() == value.length());
	uint64_t last_id_on_page = lmdb::from_sv<uint64_t>(key);
	return last_id_on_page - (offsets.size()-1) + 1;
}
} //end namespace detail

namespace {
std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_skinny_edges0(lmdb::env& env,
		lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources) {
	interval_accumulator<uint64_t> accum(512);
	visit_skinny_edges(env, edge_db, sources, [&](uint64_t, uint64_t output) {
		accum(output);
		return VisitEdgeResult::proceed;
	});
	return std::move(accum).finish();
}
}//anonymous namespace

std::vector<std::pair<std::uint64_t, std::uint64_t>> follow_skinny_edges(lmdb::env& env,
		lmdb::dbi& edge_db, const std::vector<std::pair<std::uint64_t, std::uint64_t>>& sources, unsigned int threads) {
	if (threads <= 1)
		return follow_skinny_edges0(env, edge_db, sources);
	else {
		std::vector<std::vector<std::pair<std::uint64_t, std::uint64_t>>> tasks
				//TODO: skinny edge data is denser so we might want larger tasks,
				//in particular to avoid repeated decoding of pages
				= make_follow_edges_tasks(sources, threads);
		return transform_reduce(std::move(tasks), threads,
				std::bind_front(follow_skinny_edges0, std::ref(env), std::ref(edge_db)),
				interval_union_vector());
	}
}



DatabaseMetadata read_meta(lmdb::env& env) {
	auto txn = lmdb::txn::begin(env, nullptr, MDB_RDONLY);
	lmdb::dbi meta = lmdb::dbi::open(txn, "meta");
	DatabaseMetadata data;
	std::string_view value;
	meta.get(txn, "id_bytes", value);
	data.id = lmdb::from_sv<uint64_t>(value);
	meta.get(txn, "creator_hostname", value);
	data.creator_hostname.assign(value);
	meta.get(txn, "creation_time_bytes", value);
	data.creation_time = lmdb::from_sv<std::time_t>(value);
	meta.get(txn, "creation_timestamp", value);
	data.creation_timestamp.assign(value);
	txn.commit();
	return data;
}
