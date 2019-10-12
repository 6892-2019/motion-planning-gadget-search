#include "precompiled.hpp"
#include "ops.hpp"

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
AutomatonBase::SymbolSet connect_at(const Automaton<N>& a, unsigned int activeAlphabetSize,
		Provenance prov, Finisher<Provenance>& finisher) {
	Automaton<N> connected = a;
	enjoin(connected, prov.connectPoint, (prov.connectPoint+1) % activeAlphabetSize);
	//We no longer close here.
	auto alphamap = connect_alphamap<N>(activeAlphabetSize, prov.connectPoint);
	connected.renumberAlphabet(alphamap.begin());

	connected.minimize();
	auto active = connected.activeAlphabet();
	if (active.size() <= 1) return {}; //there are no interesting 1-symbol automata

	using state_type = typename WorkingAutomaton::state_type;
	using symbol_type = typename WorkingAutomaton::symbol_type;
	bitset<N> known_not_nop;
	boost::container::small_vector<pair<state_type, symbol_type>, 16> check_again;
	//Strictly speaking, we only need two bits (0, 1, >1) but a saturating 2-bit
	//counter is nontrivial to implement.
	std::vector<unsigned int> indegree(connected.state_size(), 0);
	for (state_type s = 0, end = connected.state_size(); s < end && known_not_nop.size() != active.size(); ++s) {
		if (!connected.accept(s)) continue;
		for (auto&& [symbols, next] : connected.edges(s)) {
			indegree[next] += symbols.size();
			if (symbols.size() > 1) {
				for (symbol_type a : symbols)
					known_not_nop.set(a);
				continue;
			}

			auto dests = connected.destinations(next);
			if (dests.size() > 1) {
				known_not_nop.set(symbols.front());
				continue;
			}
			auto labels = connected.labels(next, dests.front());
			if (labels.size() > 1 || labels.front() != symbols.front()) {
				known_not_nop.set(symbols.front());
				for (symbol_type a : labels)
					known_not_nop.set(a);
				continue;
			}

			//remember next as needing revalidation
			check_again.emplace_back(next, symbols.front());
		}
	}
	for (auto i = check_again.begin(); i != check_again.end(); ++i)
		//If i->second was later found to be not-nop, this won't change anything.
		if (indegree[i->first] != 1)
			known_not_nop.set(i->second);

	if (known_not_nop.size() != active.size()) {
		for (unsigned int i = active.size(); i-- > 0;)
			if (!known_not_nop[active[i]])
				active.erase(active.begin()+i);
		if (active.size() <= 1) return {}; //there are no interesting 1-symbol automata
	}

	if (active.size() != (activeAlphabetSize - 2)) {
		//compress the alphabet
		active.sort();
		std::array<typename Automaton<N>::symbol_type, Automaton<N>::alphabet_size_v> compression;
		std::copy(active.begin(), active.end(), compression.begin());
		std::fill(compression.begin()+active.size(), compression.end(), std::numeric_limits<typename Automaton<N>::symbol_type>::max());
		connected.renumberAlphabet(compression.begin());
		//If we only deleted unused symbols, we don't need to minimize again;
		//any two equivalent states would differ only in the symbols we deleted,
		//but those symbols were inactive.
		//TODO: improve Automaton to notice this, or add a renumberAlphabet variant,
		//so that we actually skip minimizing in the finisher's canonicalize.
	}

	finisher(std::move(connected), prov);
	return active;
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

AutomatonBase::SymbolSet connect_deleted_symbols(const AutomatonBase& a, unsigned int connectPoint) {
	//This is to avoid another instantiation of connect_at.
	ConnectProvenance prov;
	prov.connectPoint = numeric_cast<std::uint8_t>(connectPoint);
	Finisher<ConnectProvenance> finisher;
	auto active_alphabet_size = a.active_alphabet_size();

	AutomatonBase::SymbolSet active;
	switch (a.alphabet_size()) {
#define TOGGLESRUNNER_CONNECT_CASE(N) case N: active = connect_at(static_cast<const Automaton<N>&>(a), active_alphabet_size, prov, finisher); break;
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
			fmt::print(stderr, "unhandled toggles-runner connect_deleted_symbols with alphabet size {} and typeid {}\n",
					a.alphabet_size(), typeid(a).name());
			std::terminate();
	}

	AutomatonBase::SymbolSet deleted;
	if (active_alphabet_size < 2) return deleted; //can't happen?
	//We should now be dense for 0..alpha-2.
	for (unsigned int i = 0; i < active_alphabet_size-2; ++i)
		if (!active.count(i))
			deleted.insert(i);
	return deleted;
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
Finisher<CombineProvenance> do_combine0(vector<pair<uint64_t, vector<std::byte>>> left_data,
		vector<pair<uint64_t, vector<std::byte>>> right_data, unsigned int precision) {
	vector<unique_ptr<Automaton<Precision>>> right_autos;
	vector<unsigned int> right_locations;
	vector<RotationVec> right_rotations;
	for (pair<uint64_t, vector<std::byte>>& r : right_data) {
		right_locations.push_back(encoding::locations(r.second.data()));
		right_autos.push_back(encoding::decode<Precision>(r.second));
		assert(right_locations.back() == right_autos.back()->active_alphabet_size());
		right_rotations.push_back(find_useful_rotations(*right_autos.back()));
	}

	CombineProvenance prov;
	Finisher<CombineProvenance> finisher;
	while (!left_data.empty()) {
		pair<uint64_t, vector<std::byte>>& l = left_data.back();
		prov.input1 = l.first;
		unsigned int leftLocations = encoding::locations(l.second.data());
		unique_ptr<Automaton<Precision>> pla = encoding::decode<Precision>(l.second);
		for (auto ri : xrange(right_data.size())) {
			if (leftLocations + right_locations[ri] > Precision) {
				finisher.skip();
				continue;
			}
			prov.input2 = right_data[ri].first;
			combine(*pla, leftLocations, *right_autos[ri], right_locations[ri], right_rotations[ri], prov, finisher);
		}
		left_data.pop_back();
	}
	return finisher;
}

Finisher<CombineProvenance> do_combine(vector<pair<uint64_t, vector<std::byte>>>&& left_data,
		vector<pair<uint64_t, vector<std::byte>>>&& right_data, unsigned int precision) {
	switch (precision) {
#define TOGGLESRUNNER_DO_COMBINE_CASE(N) case N: return do_combine0<N>(std::move(left_data), std::move(right_data), precision);
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

//based on copying combine
AutomatonBase::SymbolSet combine_deleted_symbols(const Automaton<16>& la, const Automaton<16>& ra,
		unsigned int splice, unsigned int rotation, unsigned int connectPoint) {
	AutomatonBase::state_type leftLocations = la.active_alphabet_size();
	AutomatonBase::state_type rightLocations = ra.active_alphabet_size();
	using symbol_type = WorkingAutomaton::symbol_type;
	std::array<symbol_type, Automaton<16>::alphabet_size_v> slide;
	Automaton<16> shiftedRight = ra;
	std::iota(slide.begin(), slide.end(), 0);
	std::rotate(slide.rbegin(), slide.rbegin()+leftLocations, slide.rend());
	shiftedRight.renumberAlphabet(slide.data());
	Automaton<16> shuffled = automaton::shuffleAccept(la, shiftedRight);
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

	for (unsigned int ll = 0; ll < splice; ++ll)
		std::swap(slide[ll], slide[ll+rightLocations]);
	std::iota(slide.begin()+splice, slide.begin()+splice+rightLocations, leftLocations);
	std::rotate(slide.begin()+splice, slide.begin()+splice+rotation, slide.begin()+splice+rightLocations);
	Automaton<16> permuted = shuffled;
	permuted.permuteAlphabet(slide.data());
	return connect_deleted_symbols(permuted, connectPoint);
}