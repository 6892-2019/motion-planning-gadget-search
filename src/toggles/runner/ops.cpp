#include "precompiled.hpp"
#include "ops.hpp"
#include "../toggles-shared.hpp"

using namespace automaton;
using std::uint64_t;
using std::size_t;
using std::pair;
using std::tuple;
using std::optional;
using std::nullopt;
using std::vector;
using std::unique_ptr;
using std::string_view;
using namespace std::literals::string_view_literals;

template<typename T>
void debug_scream([[maybe_unused]] T& t) {
	static_assert(std::is_trivial_v<T>, "must be trivial to scream");
#ifndef NDEBUG
	std::memset(&t, 0xAA, sizeof(T));
#endif //NDEBUG
}

bool acceptingClosure(WorkingAutomaton& connected, unsigned int locations) {
	//Transitive closure.
	//TODO: move to Automaton? (minus only being on non-accept states)
	//If we renumbered l to m, transitive-closed, then deleted m, that would be enough (?).
	using state_type = typename WorkingAutomaton::state_type;
	using symbol_type = typename WorkingAutomaton::symbol_type;
	bool progress, changed = false;
	do {
		//TODO: consider a worklist instead of fixpoint iteration
		progress = false;
		for (state_type s = 0; s < connected.state_size(); ++s) {
			if (connected.accept(s)) continue;
			for (symbol_type a = 0; a < locations; ++a) {
				for (state_type d : connected.step(s, a)) {
					assert(connected.accept(d));
					for (state_type e : connected.step(d, a))
						progress |= connected.addEpsilon(s, e);
				}
			}
		}
		changed |= progress;
	} while (progress);
	return changed;
}

template<unsigned int N>
bool enjoin(Automaton<N>& a, typename Automaton<N>::symbol_type l, typename Automaton<N>::symbol_type m) {
	using state_type = typename Automaton<N>::state_type;
	bool progress, changed = false;
	//TODO: instead of fixpoint iteration, we should put the changed state s
	//on a worklist and iterate until it's empty
	//TODO: check if we actually need to iterate in the first place -- we shouldn't be adding new edges on l/m...
	do {
		progress = false;
		for (state_type s = 0; s < a.state_size(); ++s) {
			if (a.accept(s)) continue;
			auto dests = a.step(s, l);
			for (state_type d : dests) {
				assert(a.accept(d));
				for (state_type e : a.step(d, m))
					progress |= a.addEpsilon(s, e);
			}

			dests = a.step(s, m);
			for (state_type d : dests) {
				assert(a.accept(d));
				for (state_type e : a.step(d, l))
					progress |= a.addEpsilon(s, e);
			}
		}
		changed |= progress;
	} while (progress);
	return changed;
}

template<unsigned int N>
auto connect_alphamap(unsigned int locations, unsigned int connectPoint) {
	//TODO: these alphamap manipulations could all be precomputed, though it's
	//not clear that would be any faster than using a stack variable
	std::array<unsigned int, Automaton<N>::alphabet_size_v> alphamap;
	if (connectPoint+1 == locations) {
		auto end = alphamap.begin()+locations-2;
		//other connect point is zero, so start from 1
		std::iota(alphamap.begin(), end, 1);
		std::fill(end, alphamap.end(), std::numeric_limits<typename Automaton<N>::symbol_type>::max());
	} else {
		auto middle = alphamap.begin()+connectPoint, end = alphamap.begin()+locations-2;
		std::iota(alphamap.begin(), middle, 0);
		std::iota(middle, end, connectPoint+2);
		std::fill(end, alphamap.end(), std::numeric_limits<typename Automaton<N>::symbol_type>::max());
	}
	return alphamap;
}

template<unsigned int N, class Provenance>
void connect_at(const Automaton<N>& a, unsigned int activeAlphabetSize,
		Provenance prov, Finisher<Provenance>& finisher) {
	Automaton<N> connected = a;
	enjoin(connected, prov.connectPoint, (prov.connectPoint+1) % activeAlphabetSize);
	//We no longer close here.
	auto alphamap = connect_alphamap<N>(activeAlphabetSize, prov.connectPoint);
	connected.renumberAlphabet(alphamap.begin());

	connected.minimize();
	auto active = connected.activeAlphabet();
	if (active.size() <= 1) return; //there are no interesting 1-symbol automata
	//TODO: if this check usually doesn't fire, we can use active_alphabet_size instead of activeAlphabet
	if (active.size() != (activeAlphabetSize - 2)) {
		//compress the alphabet
		active.sort();
		std::array<typename Automaton<N>::symbol_type, Automaton<N>::alphabet_size_v> compression;
		std::copy(active.begin(), active.end(), compression.begin());
		std::fill(compression.begin()+active.size(), compression.end(), std::numeric_limits<typename Automaton<N>::symbol_type>::max());
		connected.renumberAlphabet(compression.begin());
		//Because we're deleting unused symbols, we don't need to
		//minimize again; any two equivalent states would differ only in
		//the symbols we deleted, but those symbols were inactive.
		//TODO: improve Automaton to notice this, or add a renumberAlphabet variant,
		//so that we actually skip minimizing in the finisher's canonicalize.
	}

	finisher(std::move(connected), prov);
}

template<unsigned int N>
void connect(const Automaton<N>& a, std::uint64_t input1, Finisher<ConnectProvenance>& finisher) {
	auto activeAlphabetSize = a.active_alphabet_size();
	//If there are fewer than 4 locations, there will be fewer than 2 surviving
	//after the connect (we'll always delete two locations), so no results.
	if (activeAlphabetSize < 4) return;
	ConnectProvenance prov;
	debug_scream(prov);
	prov.input1 = input1;
	for (auto connectPoint : xrange(activeAlphabetSize)) {
		prov.connectPoint = numeric_cast<std::uint8_t>(connectPoint);
		connect_at(a, activeAlphabetSize, prov, finisher);
	}
}

void connect(const AutomatonBase& a, std::uint64_t input1, Finisher<ConnectProvenance>& finisher) {
	auto alpha = a.alphabet_size();
	if (alpha < 4) {
		//If we start filtering in the driver again, we should re-enable this warning.
//		fmt::print(stderr, "ignoring connect for gadget {} with alphabet size {} (no results possible)\n",
//				input1, alpha);
		finisher.skip();
		return;
	}
	switch (alpha) {
#define TOGGLESRUNNER_CONNECT_CASE(N) case N: return connect(static_cast<const Automaton<N>&>(a), input1, finisher);
		TOGGLESRUNNER_CONNECT_CASE(4)
		TOGGLESRUNNER_CONNECT_CASE(5)
		TOGGLESRUNNER_CONNECT_CASE(6)
		TOGGLESRUNNER_CONNECT_CASE(7)
		TOGGLESRUNNER_CONNECT_CASE(8)
		TOGGLESRUNNER_CONNECT_CASE(9)
		TOGGLESRUNNER_CONNECT_CASE(10)
		TOGGLESRUNNER_CONNECT_CASE(11)
		TOGGLESRUNNER_CONNECT_CASE(12)
		TOGGLESRUNNER_CONNECT_CASE(13)
		TOGGLESRUNNER_CONNECT_CASE(14)
		TOGGLESRUNNER_CONNECT_CASE(15)
		TOGGLESRUNNER_CONNECT_CASE(16)
#undef TOGGLESRUNNER_CONNECT_CASE
		default:
			fmt::print(stderr, "unhandled toggles-runner connect for gadget {} with alphabet size {} and typeid {}\n",
					input1, alpha, typeid(a).name());
	}
}


using RotationVec = boost::container::small_vector<unsigned int, 16>;
template<unsigned int N>
RotationVec find_useful_rotations(const Automaton<N>& a) {
	using symbol_type = AutomatonBase::symbol_type;
	RotationVec useful;
	auto locations = a.active_alphabet_size();
	if (!locations) return useful;

	vector<pair<std::uint64_t, Automaton<N>>> distinct;
	std::array<symbol_type, Automaton<N>::alphabet_size_v> rotation;
	std::iota(rotation.begin(), rotation.end(), 0);
	for (unsigned int rl = 0; rl < locations; ++rl) {
		//It's arbitrary which way we rotate so long as we match what combine does.
		std::iota(rotation.begin(), rotation.begin()+locations, 0);
		std::rotate(rotation.begin(), rotation.begin()+rl, rotation.begin()+locations);
		Automaton<N> rm = a;
		rm.permuteAlphabet(rotation.data());
		rm.canonicalize(); //The normal, non-alphabet-adjusting canonicalize.
		auto our_hash = rm.working_hash();
		bool labeled_continue = false;
		for (const auto& p : distinct)
			if (our_hash == p.first && rm == p.second)
				labeled_continue = true;
		if (labeled_continue) continue;

		distinct.emplace_back(our_hash, std::move(rm));
		useful.push_back(rl);
	}
	return useful;
}

RotationVec find_useful_rotations(const WorkingAutomaton& a) {
	switch (a.alphabet_size()) {
#define TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(N) case N: return find_useful_rotations(static_cast<const Automaton<N>&>(a));
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(1)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(2)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(3)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(4)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(5)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(6)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(7)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(8)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(9)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(10)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(11)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(12)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(13)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(14)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(15)
		TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE(16)
#undef TOGGLESRUNNER_FIND_USEFUL_ROTATIONS_CASE
		default:
			fmt::print(stderr, "unhandled find_useful_rotations for alphabet size {}, typeid {}\n",
					a.alphabet_size(), typeid(a).name());
			std::terminate();
	}
}

template<unsigned int Precision>
void combine(const Automaton<Precision>& la, AutomatonBase::state_type leftLocations,
		const Automaton<Precision>& ra, AutomatonBase::state_type rightLocations,
		const RotationVec& rightRotations, CombineProvenance prov, Finisher<CombineProvenance>& finish) {
	using symbol_type = WorkingAutomaton::symbol_type;
	std::array<symbol_type, Automaton<Precision>::alphabet_size_v> slide;
	Automaton<Precision> shiftedRight = ra;
	std::iota(slide.begin(), slide.end(), 0);
	std::rotate(slide.rbegin(), slide.rbegin()+leftLocations, slide.rend());
	shiftedRight.renumberAlphabet(slide.data());
	Automaton<Precision> shuffled = automaton::shuffleAccept(la, shiftedRight);
	shuffled.minimize();
	//Now [0,leftLocations) are from the left and [leftLocations,leftLocations+rightLocations)
	//are from the right.  We want to start inserting at left location 0, so we
	//write all the right locations, then all the left locations.  We'll rotate
	//the right locations as appropriate.  Then we'll move a left location to
	//the other end of the array.  Locations beyond leftLocations+rightLocations
	//are left alone, as they are always inactive.
	std::iota(slide.begin(), slide.begin()+rightLocations, leftLocations);
	std::iota(slide.begin()+rightLocations, slide.begin()+rightLocations+leftLocations, 0);
	std::iota(slide.begin()+rightLocations+leftLocations, slide.end(), rightLocations+leftLocations);
	for (decltype(leftLocations) ll = 0; ll < leftLocations; ++ll) {
		for (auto rotation : rightRotations) {
			//Because we're reading a list of rotations (not rotation deltas),
			//we have to re-initialize the right locations each time.
			std::iota(slide.begin()+ll, slide.begin()+ll+rightLocations, leftLocations);
			std::rotate(slide.begin()+ll, slide.begin()+ll+rotation, slide.begin()+ll+rightLocations);
			Automaton<Precision> permuted = shuffled;
			permuted.permuteAlphabet(slide.data());
			prov.splice = numeric_cast<std::uint8_t>(ll);
			prov.rotation = numeric_cast<std::uint8_t>(rotation);
			prov.connectPoint = numeric_cast<std::uint8_t>(
					(leftLocations+rightLocations+ll-1) % (leftLocations + rightLocations));
			connect_at(permuted, leftLocations + rightLocations, prov, finish);
			prov.connectPoint = numeric_cast<std::uint8_t>(
					(leftLocations+rightLocations+ll+rightLocations-1) % (leftLocations + rightLocations));
			connect_at(permuted, leftLocations + rightLocations, prov, finish);
		}
		std::swap(slide[ll], slide[ll+rightLocations]);
	}
}

template<unsigned int Precision>
Finisher<CombineProvenance> do_combine0(const tsl::hopscotch_map<std::uint64_t, vector<std::byte>, farmhash_hash>& map,
		const vector<pair<uint64_t, uint64_t>>& left_intervals, const vector<std::uint64_t>& right_gids) {
	vector<unique_ptr<Automaton<Precision>>> right_autos;
	vector<unsigned int> right_locations;
	vector<RotationVec> right_rotations;
	for (std::uint64_t r : right_gids) {
		auto it = map.find(r);
		if (it == map.end())
			throw std::logic_error(fmt::format("right gid {} not in map", r));
		right_autos.push_back(encoding::decode<Precision>(it->second));
		right_locations.push_back(right_autos.back()->active_alphabet_size());
		right_rotations.push_back(find_useful_rotations(*right_autos.back()));
	}

	CombineProvenance prov;
	Finisher<CombineProvenance> finisher;
	for (const pair<uint64_t, uint64_t>& p : left_intervals) {
		for (uint64_t l = p.first; l < p.second; ++l) {
			prov.input1 = l;
			auto it = map.find(l);
			if (it == map.end())
				throw std::logic_error(fmt::format("left gid {} not in map", l));
			unique_ptr<Automaton<Precision>> pla = encoding::decode<Precision>(it->second);
			//TODO: this and probably other places could use the encoded locations instead of iterating again
			auto leftLocations = pla->active_alphabet_size();
			for (auto ri : xrange(right_gids.size())) {
				if (leftLocations + right_locations[ri] > Precision) {
					//If we start filtering in the driver again, we should re-enable this warning.
//					fmt::print(stderr, "WARNING: skipping combine between {} ({} locations) and {} ({} locations) which exceeds precision {}\n",
//							l, leftLocations, right_gids[ri], right_locations[ri], Precision);
					finisher.skip();
					continue;
				}
				prov.input2 = right_gids[ri];
				combine(*pla, leftLocations, *right_autos[ri], right_locations[ri], right_rotations[ri], prov, finisher);
			}
		}
	}
	return finisher;
}

Finisher<CombineProvenance> do_combine(tsl::hopscotch_map<std::uint64_t, vector<std::byte>, farmhash_hash> map,
		const vector<pair<uint64_t, uint64_t>>& left_intervals, const vector<std::uint64_t>& right_gids, unsigned int precision) {
	switch (precision) {
#define TOGGLESRUNNER_DO_COMBINE_CASE(N) case N: return do_combine0<N>(map, left_intervals, right_gids);
		TOGGLESRUNNER_DO_COMBINE_CASE(4)
		TOGGLESRUNNER_DO_COMBINE_CASE(5)
		TOGGLESRUNNER_DO_COMBINE_CASE(6)
		TOGGLESRUNNER_DO_COMBINE_CASE(7)
		TOGGLESRUNNER_DO_COMBINE_CASE(8)
		TOGGLESRUNNER_DO_COMBINE_CASE(9)
		TOGGLESRUNNER_DO_COMBINE_CASE(10)
		TOGGLESRUNNER_DO_COMBINE_CASE(11)
		TOGGLESRUNNER_DO_COMBINE_CASE(12)
		TOGGLESRUNNER_DO_COMBINE_CASE(13)
		TOGGLESRUNNER_DO_COMBINE_CASE(14)
		TOGGLESRUNNER_DO_COMBINE_CASE(15)
		TOGGLESRUNNER_DO_COMBINE_CASE(16)
#undef TOGGLESRUNNER_DO_COMBINE_CASE
		case 1:
		case 2:
		case 3:
			fmt::print(stderr, "impossibly small precision for do_combine: {}\n", precision);
			std::terminate();
		default:
			fmt::print(stderr, "unhandled do_combine for precision {}\n", precision);
			std::terminate();
	}
}