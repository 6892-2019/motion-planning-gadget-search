#include "precompiled.hpp"
#include "automatonbase.hpp"
#include "intervals.hpp"
#include "../toggles-shared.hpp"
#include "canonicalize.hpp"
#include "gadget-encoding.hpp"
#include "selsert-gadget-by-data.hpp"
#include "tsl/ordered_set.h"
#include "tsl/ordered_map.h"
#include "lmdb++.h"
#include <fmt/chrono.h>
#include <yaml-cpp/yaml.h>
#include <ctime>
#include <sys/random.h>

using namespace automaton;
using std::uint64_t;
using std::size_t;
using std::pair;
using std::optional;
using std::vector;
using std::unique_ptr;
using std::string_view;
using namespace std::literals::string_view_literals;

//defined elsewhere (msgpack-mode.cpp at time of writing)
DatabaseOperationStatistics do_close_db0(vector<pair<uint64_t, uint64_t>> input_intervals,
		lmdb::env& env, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index, lmdb::dbi& close_edges,
		lmdb::dbi& completions);
DatabaseOperationStatistics do_mirror_db0(vector<pair<uint64_t, uint64_t>> input_intervals,
		lmdb::env& env, lmdb::dbi& gadget_hashtable, lmdb::dbi& gadget_index, lmdb::dbi& mirror_edges,
		lmdb::dbi& completions);

template<typename T>
T get_random_integer() {
	T ret;
	ssize_t rc = getrandom(&ret, sizeof(ret), 0);
	if (rc != sizeof(ret)) {
		auto savederrno = errno;
		throw std::runtime_error(fmt::format("getrandom failed: asked for {} bytes ({}), got {}: {} ({})",
				sizeof(ret), typeid(ret).name(), rc, strerror(savederrno), savederrno));
	}
	return ret;
}

vector<pair<std::uint64_t, std::uint64_t>> maximal_ranges(vector<std::uint64_t>&& data) {
	vector<std::uint64_t> ensure_memory_is_freed(std::move(data));
	return maximal_intervals(ensure_memory_is_freed.begin(), ensure_memory_is_freed.end());
}

////TODO: make this SCCs::find
unsigned int component_for_state(SCCs sccs, AutomatonBase::state_type state) {
	for (unsigned int c : xrange(sccs.size()))
		for (unsigned int s : make_range_for_pair(sccs.begin(c), sccs.end(c))) //TODO: add SCCs::range (name TBD)
			if (s == state)
				return c;
	//TODO: add an SCCs method giving the number of states, so we can report here
	throw std::logic_error(fmt::format("component_for_state failed: {} {}", state, sccs.size()));
}

/**
 * Canonicalizes a gadget in SLLS format, returning in database row format.
 * Intended for use when loading human-readable gadget definitions into the
 * database.
 */
vector<pair<vector<std::byte>, optional<vector<std::byte>>>> canonicalize_from_slls(
		vector<encoding::GadgetEdge> uedges, vector<encoding::GadgetEdge> dedges) {
	unique_ptr<WorkingAutomaton> a = encoding::inflate_slls(uedges, dedges);
	vector<std::byte> row = encoding::encode(*a);
	//just computed these in encoding::encode, could try to save them
	SCCs sccs = automaton::find_components(*a);
	auto activealpha = a->active_alphabet_size();

	vector<pair<unique_ptr<WorkingAutomaton>, vector<std::byte>>> normals;
	normals.emplace_back(std::move(a), std::move(row));
	//When initializing the database with named gadgets, we want to try all
	//initial states in the initial connected component.
	unsigned int initial_component = component_for_state(sccs, 0);
	for (auto state : make_range_for_pair(sccs.begin(initial_component), sccs.end(initial_component))) //TODO: SCCs::range
		if (normals.front().first->accept(state)) {
			unique_ptr<WorkingAutomaton> p = normals.front().first->clone();
			p->swapStateNumbers(0, state);
			canonicalize(*p, activealpha, false); //no mirroring
			row = encoding::encode(*p);
			normals.emplace_back(std::move(p), std::move(row));
		}
	std::sort(normals.begin(), normals.end(), [](const auto& l, const auto& r) {return l.second < r.second;});
	normals.erase(std::unique(normals.begin(), normals.end(),
			[](const auto& l, const auto& r) {return l.second == r.second;}), normals.end());

	//It's plausible that only a subset of the states are chiral.
	vector<pair<unique_ptr<WorkingAutomaton>, vector<std::byte>>> mirrors;
	for (const auto& n : normals) {
		//We don't return the rotation, but we won't add a mirror provenance edge
		//either, so the usual mirror machinery will fill it in later.  We just
		//need the gadget up front so we can give it an appropriate name.
		unique_ptr<WorkingAutomaton> p = mirror(*n.first).first;
		row = encoding::encode(*p);
		mirrors.emplace_back(std::move(p), std::move(row));
	}

	//Mirror order is the same as the normal order (mirrors aren't sorted).
	//We're also just taking the first enantiomorph as 'normal', rather than
	//the lexicographically lesser one.
	vector<pair<vector<std::byte>, optional<vector<std::byte>>>> retval;
	for (auto i : xrange(normals.size()))
		if (normals[i].second != mirrors[i].second)
			retval.emplace_back(std::move(normals[i].second), std::move(mirrors[i].second));
		else
			retval.emplace_back(std::move(normals[i].second), std::nullopt);
	return retval;
}

namespace YAML {
template<>
struct convert<encoding::GadgetEdge> {
	static Node encode(const encoding::GadgetEdge& e) {
		Node node;
		node.push_back(e.start);
		node.push_back(e.from);
		node.push_back(e.to);
		node.push_back(e.end);
		return node;
	}
	static bool decode(const Node& node, encoding::GadgetEdge& e) {
		if (!node.IsSequence() || node.size() != 4)
			return false;
		e.start = node[0].as<unsigned int>();
		e.from = node[1].as<unsigned int>();
		e.to = node[2].as<unsigned int>();
		e.end = node[3].as<unsigned int>();
		return true;
	}
};
}

//also called by the predicates mode
void initialize_predicates_database(lmdb::txn& txn, lmdb::dbi& predicates) {
	predicates.drop(txn, 0);
	//We add the location predicates here, but leave it to the driver to
	//create the state predicates it actually uses.
	std::string_view empty_data = "";
	for (unsigned int locations = 2; locations <= 16; ++locations)
		if (!predicates.put(txn, fmt::format("locations<={}", locations), empty_data))
			throw std::logic_error("can't happen: failed to put predicate locations key?");
	uint64_t empty = 1; //valid_before is exclusive
	std::string_view valid_before_data = lmdb::to_sv<uint64_t>(empty);
	if (!predicates.put(txn, "valid_before", valid_before_data))
		throw std::logic_error("can't happen: failed to put predicate valid_before key?");
}

int sync_mode(std::string_view db_path, const vector<std::string_view>& files) {
	//vector_ordered_set
	tsl::ordered_set<vector<std::byte>, farmhash_hash, std::equal_to<vector<std::byte>>,
			std::allocator<vector<std::byte>>, std::vector<vector<std::byte>>> canonicals;
	auto register_gadget = [&](vector<std::byte>&& gadget) {
		auto it = canonicals.insert(std::move(gadget)).first;
		return numeric_cast<std::size_t>(std::distance(canonicals.begin(), it));
	};
	tsl::ordered_map<std::string, vector<std::size_t>> naming;
	vector<pair<std::string, std::string>> deferred_aliases;
	for (std::string_view filename : files) {
		YAML::Node toplevel = YAML::LoadFile(std::string(filename));
		YAML::Node gadgets = toplevel["gadgets"];
		for (auto it = gadgets.begin(); it != gadgets.end(); ++it) {
			std::string gadget_name = it->first.as<std::string>();
			vector<encoding::GadgetEdge> uedges, dedges;
			if (it->second["uedges"])
				uedges = it->second["uedges"].as<vector<encoding::GadgetEdge>>();
			if (it->second["dedges"])
				dedges = it->second["dedges"].as<vector<encoding::GadgetEdge>>();
			if (uedges.empty() && dedges.empty()) {
				fmt::print(stderr, "no edges for gadget {} in {}\n", gadget_name, filename);
				return 1;
			}

			vector<pair<vector<std::byte>, optional<vector<std::byte>>>> morphs =
					canonicalize_from_slls(std::move(uedges), std::move(dedges));
			//We are chiral if any state has enantiomorphs.
			bool chiral = std::any_of(morphs.begin(), morphs.end(), [](const auto& q){return q.second.has_value();});
			vector<std::size_t> whole_group_indices;
			if (morphs.size() == 1 && !chiral) {
				whole_group_indices.push_back(register_gadget(std::move(morphs[0].first)));
				//singleton group -- we'll install the usual group name later
			} else if (morphs.size() == 1 && chiral) {
				auto& p = morphs[0];
				naming["r-"+gadget_name] = {register_gadget(std::move(p.first))};
				naming["s-"+gadget_name] = {register_gadget(std::move(*p.second))};
				whole_group_indices = {naming["r-"+gadget_name].front(), naming["s-"+gadget_name].front()};
			} else if (morphs.size() > 1 && !chiral)
				for (std::size_t i = 0; i < morphs.size(); ++i) {
					std::size_t number = register_gadget(std::move(morphs[i].first));
					naming[fmt::format("{}-{}", gadget_name, i)] = {number};
					whole_group_indices.push_back(number);
				}
			else if (morphs.size() > 1 && chiral)
				for (std::size_t i = 0; i < morphs.size(); ++i) {
					std::size_t number = register_gadget(std::move(morphs[i].first));
					naming[fmt::format("r-{}-{}", gadget_name, i)] = {number};
					whole_group_indices.push_back(number);
					if (morphs[i].second) {
						number = register_gadget(std::move(*morphs[i].second));
						naming[fmt::format("s-{}-{}", gadget_name, i)] = {number};
						whole_group_indices.push_back(number);
					} else
						naming[fmt::format("s-{}-{}", gadget_name, i)] = naming.at(fmt::format("r-{}-{}", gadget_name, i));
				}
			else
				throw std::logic_error("empty morphs somehow?");
			naming[gadget_name] = std::move(whole_group_indices);
		}

		YAML::Node aliases = toplevel["aliases"];
		for (auto it = aliases.begin(); it != aliases.end(); ++it)
			deferred_aliases.emplace_back(it->first.as<std::string>(), it->second.as<std::string>());
	}

	for (const auto& p : deferred_aliases) {
		const auto& source = p.first, target = p.second;
		if (naming.count(source)) {
			fmt::print(stderr, "alias {} (intended for {}) already names a gadget\n", source, target);
			return 1;
		}
		if (!naming.count(target)) {
			//An alias can reference another alias, but only if the referent
			//is defined first, to prevent alias cycles.
			fmt::print(stderr, "alias target {} (from {}) doesn't name a gadget\n", target, source);
			return 1;
		}
		naming[source] = naming[target];
	}

	lmdb::env env = lmdb::env::create(); //TODO: flags?
	env.set_mapsize(1UL * 1024 * 1024 * 1024 * 1024);
	env.set_max_dbs(64);
	env.open(std::string(db_path).c_str()); //TODO: flags?
	lmdb::dbi gadget_hashtable, gadget_index, names_db, completions, close_edges, mirror_edges;
	{
		lmdb::txn txn = lmdb::txn::begin(env);
		gadget_hashtable = lmdb::dbi::open(txn, "gadget_hashtable", MDB_CREATE | MDB_INTEGERKEY);
		//TODO: can we pass MDB_INTEGERDUP without MDB_DUPSORT/FIXED?  We won't
		//have duplicates, but all our keys are binary integers.
		gadget_index = lmdb::dbi::open(txn, "gadget_index", MDB_CREATE | MDB_INTEGERKEY);
		//We could use duplicate integer keys here, but because we aren't adding
		//or removing any keys, it's more convenient for our code to just store
		//byte arrays.  There are few enough names that compression isn't useful.
		names_db = lmdb::dbi::open(txn, "names", MDB_CREATE);

		//We also open some databases we don't use here, just to ensure they
		//exist when the database starts.  Combine-related databases are created
		//on demand (because they are specific to the right operand).

		//The completions database holds keys named "close", "mirror", "connect"
		//and "combine-{}" whose values are an interval list.
		completions = lmdb::dbi::open(txn, "completions", MDB_CREATE);

		mirror_edges = lmdb::dbi::open(txn, "edges-mirror", MDB_CREATE | MDB_INTEGERKEY);
		close_edges = lmdb::dbi::open(txn, "edges-close", MDB_CREATE | MDB_INTEGERKEY);
		lmdb::dbi::open(txn, "edges-connect", MDB_CREATE | MDB_INTEGERKEY);
		//edges-combine-{} are generated on demand by the driver

		lmdb::dbi meta = lmdb::dbi::open(txn, "meta", MDB_CREATE);
		if (meta.size(txn) == 0) {
			//for ensuring checkpoints match the DB they were created against
			uint64_t uid = get_random_integer<uint64_t>();
			meta.put(txn, "id_bytes", lmdb::to_sv(uid));
			meta.put(txn, "id", fmt::to_string(uid));

			std::array<char, 64> hostname;
			std::memset(hostname.data(), 0, hostname.size());
			if (gethostname(hostname.data(), hostname.size()))
				throw std::logic_error("problem getting hostname");
			//gethostname is awkward -- let's be safe
			hostname.back() = 0;
			meta.put(txn, "creator_hostname", std::string_view(hostname.data()));

			std::time_t now = std::time(nullptr);
			meta.put(txn, "creation_time_bytes", lmdb::to_sv(now));
			meta.put(txn, "creation_time", fmt::to_string(now));
			meta.put(txn, "creation_timestamp", fmt::format("{:%F %T %Z}", *std::localtime(&now)));
		}

		lmdb::dbi predicates = lmdb::dbi::open(txn, "predicates", MDB_CREATE);
		MAYBE_UNUSED std::string_view unused_dont_care;
		if (!predicates.get(txn, "valid_before", unused_dont_care))
			initialize_predicates_database(txn, predicates);

		txn.commit();
	}

	std::size_t canonicals_size = canonicals.size();
	auto selsert_result = selsert_gadget_by_data(env, gadget_hashtable, gadget_index, std::move(canonicals).values_container());
	//Punning a bit on this vector: in the map, it's indices into canonicals,
	//but we're about to remap it to gadget ids.
	std::deque<pair<std::string, std::vector<uint64_t>>> sorted_names = std::move(naming).values_container();
	for (auto& p : sorted_names) {
		for (std::size_t i = 0; i < p.second.size(); ++i)
			p.second[i] = selsert_result.local_to_global[p.second[i]];
		std::sort(p.second.begin(), p.second.end());
		p.second.erase(std::unique(p.second.begin(), p.second.end()), p.second.end());
	}
	//TODO: this compare-tupleish-by-nth-element also appears in the driver,
	//and is probably worth elevating to a named utility function/lambda.
	std::sort(sorted_names.begin(), sorted_names.end(), [](const auto& a, const auto& b) {
		return std::get<0>(a) < std::get<0>(b);
	});

	{
		lmdb::txn txn = lmdb::txn::begin(env);
		names_db.drop(txn);
		for (const pair<std::string, vector<std::uint64_t>>& p : sorted_names)
			if (!names_db.put(txn, p.first,
					std::string_view(reinterpret_cast<const char*>(p.second.data()), p.second.size()*sizeof(std::uint64_t)),
					MDB_APPEND | MDB_NOOVERWRITE))
				throw std::runtime_error(fmt::format("failed to insert names {} -> {}", p.first, p.second));
		txn.commit();
	}
	fmt::print("loaded {} gadgets ({} novel) and {} names\n",
			canonicals_size, selsert_result.novel_size(), sorted_names.size());

	//Now close and mirror all gadgets (even non-novel ones) that need it, for
	//the benefit of the reporter.
	std::sort(selsert_result.local_to_global.begin(), selsert_result.local_to_global.end());
	vector<pair<uint64_t, uint64_t>> named_gadgets = maximal_ranges(std::move(selsert_result.local_to_global));
	auto needs_close = filter_completion(env, completions, "close", named_gadgets);
	DatabaseOperationStatistics close_stats = do_close_db0(std::move(needs_close),
			env, gadget_hashtable, gadget_index, close_edges, completions);
	fmt::print("close: {} locally pruned, {} globally pruned, {} discovered, {} edges\n",
			close_stats.pruned_locally, close_stats.pruned_database, close_stats.novel_gadgets, close_stats.edges);

	auto followed_close = follow_edges<SimpleEdge>(env, close_edges, named_gadgets);
	fmt::print("followed close edges to {} gadgets\n", interval_size(followed_close.cbegin(), followed_close.cend()));

	auto desire_mirror = interval_union(named_gadgets.cbegin(), named_gadgets.cend(),
			followed_close.cbegin(), followed_close.cend());
	auto needs_mirror = filter_completion(env, completions, "mirror", desire_mirror);
	DatabaseOperationStatistics mirror_stats = do_mirror_db0(std::move(needs_mirror),
			env, gadget_hashtable, gadget_index, mirror_edges, completions);
	fmt::print("mirror: {} locally pruned, {} globally pruned, {} discovered, {} edges\n",
			mirror_stats.pruned_locally, mirror_stats.pruned_database, mirror_stats.novel_gadgets, mirror_stats.edges);

	return 0;
}