#include "precompiled.hpp"
#include "automatonbase.hpp"
#include "intervals.hpp"
#include "../toggles-shared.hpp"
#include "canonicalize.hpp"
#include "gadget-encoding.hpp"
#include "selsert-gadget-by-data.hpp"
#include "proj_compare.hpp"
#include "tsl/ordered_set.h"
#include "tsl/ordered_map.h"
#include "lmdb++.h"
#include <fmt/chrono.h>
#include <yaml-cpp/yaml.h>
#include <ctime>
#include <sys/random.h>
#include <variant>

using namespace automaton;
using encoding::GadgetEdge;
using encoding::GadgetBuilder;
using std::uint64_t;
using std::size_t;
using std::pair;
using std::optional;
using std::variant;
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

GadgetBuilder inflate_slls(const std::vector<GadgetEdge>& uedges,
		const std::vector<GadgetEdge>& dedges, unsigned int alphabetSize = 0) {
	unsigned int states = 0;
	if (!alphabetSize) {
		//Size-to-fit by finding the largest used symbol.
		for (auto e : uedges) {
			alphabetSize = std::max({alphabetSize, e.from, e.to});
			states = std::max({states, e.start, e.end});
		}
		for (auto e : dedges) {
			alphabetSize = std::max({alphabetSize, e.from, e.to});
			states = std::max({states, e.start, e.end});
		}
		++alphabetSize;
		++states;
	}
	GadgetBuilder b(alphabetSize, states);
	for (auto e : uedges)
		b.trans(e.start, e.from, e.to, e.end).trans(e.end, e.to, e.from, e.start);
	for (auto e : dedges)
		b.trans(e.start, e.from, e.to, e.end);
	return b;
}

struct CanonicalizeRecord {
	unsigned int gadget_state;
	variant<std::size_t, vector<std::byte>> normal;
	unsigned int normal_rotation;
	optional<variant<std::size_t, vector<std::byte>>> mirror;
	unsigned int mirror_rotation;
	bool initial_component;
};
vector<CanonicalizeRecord> canonicalize_from_slls(
		vector<GadgetEdge> uedges, vector<GadgetEdge> dedges) {
	GadgetBuilder b = inflate_slls(uedges, dedges);
	vector<unsigned int> initial_component = b.initialComponentGadgetStates();
	vector<CanonicalizeRecord> ret;
	//We build all states of the gadget.  We generally only add names for states
	//in the initial component, but that's up to the caller.
	for (unsigned int state : xrange(b.size())) {
		CanonicalizeRecord r;
		r.gadget_state = state;
		b.setGadgetState(state);
		pair<unique_ptr<WorkingAutomaton>, unsigned int> normal = b.build();
		if (!normal.first->active_alphabet_size())
			//Gadgets with components often have an empty state, which we'll
			//minimize into a 0-location gadget.  We can't represent that in the
			//encoding, so we have to pretend that state doesn't exist.  (The
			//parent gadget still has the right number of states and components.)
			continue;
		r.normal = encoding::encode(*normal.first);
		r.normal_rotation = normal.second;
		pair<unique_ptr<WorkingAutomaton>, unsigned int> mirror = ::mirror(*normal.first);
		r.mirror = encoding::encode(*mirror.first);
		r.mirror_rotation = mirror.second;
		if (r.normal == *r.mirror) {
			r.mirror.reset();
			r.mirror_rotation = std::numeric_limits<unsigned int>::max();
		}
		r.initial_component = std::find(initial_component.begin(), initial_component.end(), r.gadget_state) != initial_component.end();
		ret.push_back(std::move(r));
	}
	//We might want to deduplicate the records by r.normal, to prevent generating
	//names for the two identical states of, e.g., crossing-disemitripwire-one-way-tripwire.
	return ret;
}

namespace YAML {
template<>
struct convert<GadgetEdge> {
	static Node encode(const GadgetEdge& e) {
		Node node;
		node.push_back(e.start);
		node.push_back(e.from);
		node.push_back(e.to);
		node.push_back(e.end);
		return node;
	}
	static bool decode(const Node& node, GadgetEdge& e) {
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

struct GadgetPragma {
	//By default, we throw if a named state was pruned (identical to some other
	//state).  This is what we want for human-written definitions, because it
	//indicates an error in the gadget or in the names.  But for machine-
	//generated definitions, we may not know if any states will be pruned, so we
	//can't just omit names.
	bool allow_pruning_named_states = false;
};
GadgetPragma parse_gadget_pragma(YAML::Node pragma_node, std::string_view gadget_name, std::string_view filename) {
	if (!pragma_node) return {}; //if not present, defaults

	vector<std::string> pragmas;
	if (pragma_node.IsSequence())
		pragmas = pragma_node.as<vector<std::string>>();
	else if (pragma_node.IsScalar())
		pragmas = {pragma_node.as<std::string>()};
	else
		throw std::runtime_error(fmt::format("unexpected pragma type for {} in {}", gadget_name, filename));

	GadgetPragma ret;
	for (const std::string& p : pragmas)
		if (p == "allow-pruning-named-states"sv)
			ret.allow_pruning_named_states = true;
		else
			throw std::runtime_error(fmt::format("unrecognized pragma \"{}\" for {} in {}", p, gadget_name, filename));
	return ret;
}

struct SynclogRecord {
	//work around emplace_back being broken with aggregates
	SynclogRecord(const std::string& a, const std::string& b, unsigned int c, unsigned int d, std::optional<unsigned int> e)
	: name(a), base_name(b), gadget_state(c), normal_rotation(d), mirror_rotation(e) {}
	std::string name, base_name;
	unsigned int gadget_state;
	unsigned int normal_rotation;
	std::optional<unsigned int> mirror_rotation; //after normal_rotation and mirroring applied, else absent
};

int sync_mode(std::string_view db_path, const vector<std::string_view>& positionals) {
	vector<std::string_view> files;
	std::string_view synclog_path;
	for (std::size_t i = 0; i < positionals.size(); ++i)
		if (positionals[i] == "--log"sv)
			synclog_path = positionals[++i];
		else
			files.push_back(positionals[i]);

	//vector_ordered_set
	tsl::ordered_set<vector<std::byte>, farmhash_hash, std::equal_to<vector<std::byte>>,
			std::allocator<vector<std::byte>>, std::vector<vector<std::byte>>> canonicals;
	auto register_gadget = [&](vector<std::byte>&& gadget) {
		auto it = canonicals.insert(std::move(gadget)).first;
		return numeric_cast<std::size_t>(std::distance(canonicals.begin(), it));
	};
	tsl::ordered_map<std::string, vector<std::size_t>> naming;
	vector<pair<std::string, std::string>> deferred_aliases;
	vector<SynclogRecord> synclog; //data we need for automatic graph drawing
	for (std::string_view filename : files) {
		YAML::Node toplevel = YAML::LoadFile(std::string(filename));
		YAML::Node gadgets = toplevel["gadgets"];
		for (auto it = gadgets.begin(); it != gadgets.end(); ++it) {
			std::string gadget_name = it->first.as<std::string>();
			YAML::Node data = it->second;
			vector<GadgetEdge> uedges, dedges;
			if (data["uedges"])
				uedges = data["uedges"].as<vector<GadgetEdge>>();
			if (data["dedges"])
				dedges = data["dedges"].as<vector<GadgetEdge>>();
			if (uedges.empty() && dedges.empty()) {
				fmt::print(stderr, "no edges for gadget {} in {}\n", gadget_name, filename);
				return 1;
			}

			GadgetPragma pragma = parse_gadget_pragma(data["pragma"], gadget_name, filename);

			vector<CanonicalizeRecord> morphs = canonicalize_from_slls(std::move(uedges), std::move(dedges));
			if (morphs.empty()) {
				//e.g., all states have no edges?
				fmt::print(stderr, "warning: no morphs for {} from {}\n", gadget_name, filename);
				continue;
			}

			vector<std::size_t> all_normals, all_mirrors;
			for (CanonicalizeRecord& r : morphs) {
				r.normal = register_gadget(std::move(std::get<1>(r.normal)));
				all_normals.push_back(std::get<0>(r.normal));
				if (r.mirror) {
					r.mirror = register_gadget(std::move(std::get<1>(*r.mirror)));
					all_mirrors.push_back(std::get<0>(*r.mirror));
				}
			}
			//If all mirrors are also normals, reflection just changes the state,
			//so we aren't really chiral.  foo-r and foo-s would name the same
			//set of gadgets.
			std::sort(all_normals.begin(), all_normals.end());
			std::sort(all_mirrors.begin(), all_mirrors.end());
			if (std::includes(all_normals.begin(), all_normals.end(), all_mirrors.begin(), all_mirrors.end()))
				for (CanonicalizeRecord& r : morphs)
					r.mirror.reset();

			//If state change is equivalent to rotation, drop the extra names.
			for (std::size_t i = morphs.size(); i-- > 0;)
				for (std::size_t j = i; j-- > 0;)
					if (morphs[i].normal == morphs[j].normal) {
						morphs.erase(morphs.begin()+i);
						break; //continue the outer loop
					}

			tsl::ordered_map<unsigned int, std::string> state_names;
			//By default, we generate names for all states in the initial
			//connected component and their mirrors, if any.  But the YAML file
			//can explicitly ask for all states to be generated, or specify
			//custom names.
			bool custom_names = false;
			if (data["state-names"] && data["state-names"].IsMap()) {
				custom_names = true;
				for (auto nit = data["state-names"].begin(); nit != data["state-names"].end(); ++nit) {
					unsigned int number = nit->first.as<unsigned int>();
					state_names[number] = nit->second.as<std::string>();
					if (!pragma.allow_pruning_named_states && std::find_if(morphs.begin(), morphs.end(),
							[number](const CanonicalizeRecord& r){return r.gadget_state == number;}) == morphs.end())
						throw std::runtime_error(fmt::format("named state was pruned {} {} {} {}",
									gadget_name, number, state_names[number], morphs.size()));
				}
			}
			//could allow a sequence of integers specifying states to give the default integer names to
			else if (data["state-names"] && data["state-names"].IsScalar()) {
				std::string maybe = data.as<std::string>();
				if (maybe == "all")
					for (unsigned int i = 0; i < morphs.size(); ++i)
						state_names[i] = std::to_string(morphs[i].gadget_state);
				else
					throw std::runtime_error(fmt::format("unrecognized state-names scalar {} {}", gadget_name, maybe));
			} else
				for (const CanonicalizeRecord& r : morphs)
					if (r.initial_component)
						state_names[r.gadget_state] = std::to_string(r.gadget_state);

			//We want chirality markers in names if one of the named states has enantiomorphs.
			bool chiral = std::any_of(morphs.begin(), morphs.end(), [&](const CanonicalizeRecord& r) {
				return state_names.count(r.gadget_state) && r.mirror.has_value();
			});

			if (!custom_names && state_names.size() == 1) {
				auto record_it = std::find_if(morphs.begin(), morphs.end(),
						[number=state_names.front().first](const CanonicalizeRecord& r){return r.gadget_state == number;});
				if (record_it->mirror) {
					std::string normal_name = fmt::format("{}-r", gadget_name), mirror_name = fmt::format("{}-s", gadget_name);
					naming[normal_name] = {std::get<0>(record_it->normal)};
					naming[mirror_name] = {std::get<0>(*record_it->mirror)};
					naming[gadget_name] = {std::get<0>(record_it->normal), std::get<0>(*record_it->mirror)};
					synclog.emplace_back(normal_name, gadget_name, record_it->gadget_state, record_it->normal_rotation, std::nullopt);
					synclog.emplace_back(mirror_name, gadget_name, record_it->gadget_state, record_it->normal_rotation, record_it->mirror_rotation);
				} else {
					naming[gadget_name] = {std::get<0>(record_it->normal)};
					synclog.emplace_back(gadget_name, gadget_name, record_it->gadget_state, record_it->normal_rotation, std::nullopt);
				}
			} else {
				vector<std::size_t> whole_group_indices;
				vector<std::size_t> chiral_r, chiral_s;
				for (const CanonicalizeRecord& r : morphs) {
					auto name_it = state_names.find(r.gadget_state);
					//We register these gadgets, but don't generate names for them.
					if (name_it == state_names.end()) continue;
					const std::string& state_name = name_it->second;

					std::size_t normal = std::get<0>(r.normal);
					optional<std::size_t> mirror;
					if (r.mirror)
						mirror = std::get<0>(*r.mirror);

					whole_group_indices.push_back(normal);
					if (mirror)
						whole_group_indices.push_back(*mirror);

					if (chiral) {
						//If a state is achiral, we don't generate -r and -s
						//names for it, but that gadget still goes in the -r and
						//-s groups (so they represent all states of the gadget).
						if (mirror) {
							std::string normal_name = fmt::format("{}-{}-r", gadget_name, state_name),
									mirror_name = fmt::format("{}-{}-s", gadget_name, state_name);
							naming[normal_name] = {normal};
							naming[mirror_name] = {*mirror};
							naming[fmt::format("{}-{}", gadget_name, state_name)]  = {normal, *mirror};
							synclog.emplace_back(normal_name, gadget_name, r.gadget_state, r.normal_rotation, std::nullopt);
							synclog.emplace_back(mirror_name, gadget_name, r.gadget_state, r.normal_rotation, r.mirror_rotation);
						} else {
							std::string normal_name = fmt::format("{}-{}", gadget_name, state_name);
							naming[normal_name] = {normal};
							synclog.emplace_back(normal_name, gadget_name, r.gadget_state, r.normal_rotation, std::nullopt);
						}
						chiral_r.push_back(normal);
						chiral_s.push_back(mirror ? *mirror : normal);
					} else {
						std::string normal_name = fmt::format("{}-{}", gadget_name, state_name);
						naming[normal_name] = {normal};
						synclog.emplace_back(normal_name, gadget_name, r.gadget_state, r.normal_rotation, std::nullopt);
					}
				}
				if (chiral) {
					naming[fmt::format("{}-r", gadget_name)] = std::move(chiral_r);
					naming[fmt::format("{}-s", gadget_name)] = std::move(chiral_s);
				}
				naming[gadget_name] = std::move(whole_group_indices);
			}
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
	std::sort(sorted_names.begin(), sorted_names.end(), proj_less<0>());

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

	if (!synclog_path.empty()) {
		FILE* synclog_file = std::fopen(std::string(synclog_path).c_str(), "w");
		if (!synclog_file) {
			std::perror("error opening synclog");
			return 1;
		}
		for (const SynclogRecord& r : synclog)
			fmt::print(synclog_file, "{} {} {} {} {}\n", r.name, r.base_name, r.gadget_state, r.normal_rotation,
					r.mirror_rotation ? static_cast<int>(*r.mirror_rotation) : -1);
		std::fclose(synclog_file);
		fmt::print("wrote {} entries to synclog\n", synclog.size());
	}

	return 0;
}