#include "precompiled.hpp"
#include "automatonbase.hpp"
#include "hopcroft.hpp"

using namespace automaton;

namespace {
void make_empty(AutomatonBase& a) {
	a.clear();
	a.addState();
}
void make_all(AutomatonBase& a) {
	a.clear();
	a.addState();
	a.addState();
	for (AutomatonBase::symbol_type s = 0; s < a.alphabet_size(); ++s)
		a.addTrans(0, s, 1);
	a.setAccept(1);
}
} //anonymous namespace

namespace automaton {
namespace detail {

class HopcroftMinimizer final {
public:
	HopcroftMinimizer(WorkingAutomaton& a) : a_(a), state_size_(a.state_size()), alphabet_size_(a.alphabet_size()),
			partitions_(state_size_), partitionBounds_(),
			//TODO: now that the automaton isn't total, inv_ should be
			//allocated after building the inverse edge list, so that it can
			//be sized just right.
			stateToPartition_(state_size_), inv_(state_size_ * a.alphabet_size()),
			invStart_(state_size_ * (a.alphabet_size()+1)), L_(), inL_(state_size_ * a.alphabet_size()),
			move_(state_size_), moveSize_(), suspects_() {}

	HopcroftResult minimize() {
		if (!buildInverseAndInitializePartitions())
			//we've mutated a_, so must use actual size here
			return {a_.state_size(), {}, {}};
		initializeWaitingSet();
		while (!L_.empty()) {
			auto pair = remove();
			collect(pair.first, pair.second);
			refine();
			//When we get here, L_ is nearly empty and processing each pair
			//is cheap (because partitions are singletons), so it may not be
			//worth checking in this loop.  If not, we definitely want to
			//check in finish() to avoid copying a bunch for no reason.
			if (partitionBounds_.size() == state_size_)
				return {state_size_, {}, {}};
		}
		return finish();
	}
private:
	WorkingAutomaton& a_;
	state_type state_size_;
	symbol_type alphabet_size_;
	//Every partition contains at least one state, so there can only be as
	//many states as partitions.
	dynarray<state_type> partitions_;
	std::vector<std::pair<state_type, state_type>> partitionBounds_;
	//first is the partition, second is the index into partitions_
	dynarray<std::pair<state_type, state_type>> stateToPartition_;
	dynarray<state_type> inv_;
	dynarray<std::size_t> invStart_;
	circular_deque<std::pair<state_type, symbol_type>, 32> L_;
	boost::dynamic_bitset<std::size_t> inL_;
	dynarray<state_type> move_;
	std::vector<state_type> moveSize_;
	std::vector<state_type> suspects_;

	/**
	 * @return true if we should continue; flase if the automaton is the
	 * trivial empty-language or all-strings automaton, in which case we're
	 * already done
	 */
	bool buildInverseAndInitializePartitions() {
		struct InverseEntry {
			state_type source;
			symbol_type symbol;
			state_type target;
			bool operator==(const InverseEntry& other) const {
				return source == other.source &&
						symbol == other.symbol &&
						target == other.target;
			}
			bool operator!=(const InverseEntry& other) const {
				return !(*this == other);
			}
			bool operator<(const InverseEntry& other) const {
				return std::tie(target, symbol, source) < std::tie(other.target, other.symbol, other.source);
			}
		};

		unsigned int nonfinalIdx = 0, finalIdx = static_cast<unsigned int>(partitions_.size() - 1);
		//TODO: for_each_accept?
		for (state_type s = 0; s < state_size_; ++s) {
			if (a_.accept(s))
				partitions_[finalIdx--] = s;
			else
				partitions_[nonfinalIdx++] = s;
		}
		assert(nonfinalIdx == finalIdx+1 && "didn't partition all the states somehow");
		if (nonfinalIdx == partitions_.size()) {
			make_empty(a_);
			return false;
		}

		std::vector<InverseEntry> edgelist;
		edgelist.reserve(state_size_ * alphabet_size_ + 1);
		a_.for_each_transition([&edgelist](state_type from, symbol_type on, state_type to) {
			edgelist.push_back(InverseEntry{from, on, to});
		});
		bool crashed = edgelist.size() != state_size_ * alphabet_size_;
		//We need to know if we crashed (equivalently, if we're not total), and
		//testing whether we're total requires iterating all the states anyway,
		//so we'll wait until after building the edgelist on the assumption that
		//trivial automata are uncommon.
		if ((finalIdx+1) == 0U && !crashed) {
			make_all(a_);
			return false;
		}
		edgelist.push_back(InverseEntry{std::numeric_limits<state_type>::max(), std::numeric_limits<symbol_type>::max(), std::numeric_limits<state_type>::max()});

		if (crashed) {
			//Because we didn't totalize, we need to manually partition
			//crashing vs. non-crashing on each symbol.
			std::vector<typename decltype(partitions_)::iterator> bounds = {
				partitions_.begin(), partitions_.begin()+nonfinalIdx, partitions_.end()
			}, newbounds;
			for (symbol_type s = 0; s < alphabet_size_; ++s) {
				newbounds.clear();
				for (typename decltype(bounds)::size_type i = 0; i < bounds.size() - 1; ++i) {
					newbounds.push_back(bounds[i]);
					newbounds.push_back(std::partition(bounds[i], bounds[i+1], [this, s](state_type state) {
						//if we crash
						return !a_.stepDeterministic(state, s).has_value();
					}));
					newbounds.push_back(bounds[i+1]);
				}
				newbounds.erase(std::unique(newbounds.begin(), newbounds.end()), newbounds.end());
				bounds.swap(newbounds);
			}
			partitionBounds_.reserve(bounds.size()-1);
			for (state_type p = 0; p < bounds.size()-1; ++p)
				partitionBounds_.push_back({bounds[p] - partitions_.begin(), bounds[p+1] - partitions_.begin()});
			moveSize_.resize(partitionBounds_.size(), 0);
		} else {
			partitionBounds_.push_back({0, nonfinalIdx});
			partitionBounds_.push_back({nonfinalIdx, static_cast<state_type>(partitions_.size())});
			moveSize_.push_back(0);
			moveSize_.push_back(0);
		}

		for (state_type p = 0; p < partitionBounds_.size(); ++p) {
			auto bounds = partitionBounds_[p];
			//Sort for locality when accessing stateToPartition_.
			std::sort(partitions_.begin() + bounds.first, partitions_.begin() + bounds.second);
			for (state_type i = bounds.first; i != bounds.second; ++i)
				stateToPartition_[partitions_[i]] = {p, i};
		}

		//TODO: use a parallel sort (beyond a size threshold)
		std::sort(edgelist.begin(), edgelist.end());
		std::size_t invEltsIdx = 0;
		for (state_type stateIdx = 0; stateIdx < state_size_; ++stateIdx) {
			for (symbol_type symbolIdx = 0; symbolIdx < alphabet_size_; ++symbolIdx) {
				invStart_[stateIdx * (alphabet_size_+1) + symbolIdx] = invEltsIdx;
				while (edgelist[invEltsIdx].target == stateIdx &&
						edgelist[invEltsIdx].symbol == symbolIdx) {
					inv_[invEltsIdx] = edgelist[invEltsIdx].source;
					++invEltsIdx;
				}
			}
			invStart_[stateIdx * (alphabet_size_+1) + alphabet_size_] = invEltsIdx;
		}
		//TODO: we can reassert this when inv_ is lazily sized
//			assert(invEltsIdx == inv_.size());
		return true;
	}

	void initializeWaitingSet() {
		//For each symbol, add all but the largest partition to the waiting set.
		//TODO: factor this into max_element_transform algorithm
		state_type maxPartition = 0, maxPartitionSize = partitionSize(0);
		for (state_type p = 1; p < partitionBounds_.size(); ++p)
			if (partitionSize(p) > maxPartitionSize) {
				maxPartitionSize = partitionSize(p);
				maxPartition = p;
			}

		for (symbol_type i = 0; i < alphabet_size_; ++i)
			for (state_type p = 0; p < partitionBounds_.size(); ++p)
				if (p != maxPartition)
					add(p, i);
	}

	void collect(state_type part, symbol_type symbol) {
		checkRep();
		suspects_.clear();
		for (state_type target : partition(part))
			for (state_type source : inverseStep(target, symbol)) {
				state_type invPart = stateToPartition_[source].first;
				//TODO: if partitionSize(invPart) == 1, we shouldn't bother
				//adding it to suspects (and checking it later).  This might
				//be important for no-op re-minimization of large automata,
				//to avoid unnecessarily growing suspects_.  (Test first.)
				if (moveSize_[invPart] == 0) //only add to suspects if not already present
					suspects_.push_back(invPart);
				move_[partitionBounds_[invPart].first + (moveSize_[invPart]++)] = source;
				assert(moveSize_[invPart] <= partitionSize(invPart));
			}
		checkRep();
	}

	void refine() {
		checkRep();
		for (state_type part : suspects_) {
			if (moveSize_[part] < partitionSize(part)) {
				state_type newPart = split(part);
				//This is done unconditionally outside this if.
//					moveSize_[part] = 0;
				for (symbol_type symbol = 0; symbol < alphabet_size_; ++symbol)
					if (contains(part, symbol))
						add(newPart, symbol);
					else
						addBetter(part, newPart, symbol);
			}
			//In Knuutila's paper, cls_head[B].counter is only cleared when
			//refinement actually happens.  I think that's wrong, because
			//just because we couldn't refine with respect to one state-symbol
			//pair doesn't mean we won't with another, but not clearing
			//counter prevents this partition from entering suspects again.
			moveSize_[part] = 0;
		}
		checkRep();
	}

	state_type split(state_type part) {
		checkRep();
		state_type newPart = static_cast<state_type>(partitionBounds_.size());
		//swap states in move[part] into the front of partitions[part]
		for (state_type i = 0; i < moveSize_[part]; ++i) {
			state_type victimIdx = partitionBounds_[part].first + i;
			state_type beneficiaryState = move_[victimIdx];
			state_type beneficiaryIdx = stateToPartition_[beneficiaryState].second;
			state_type victimState = partitions_[victimIdx];
			assert(stateToPartition_[victimState].second == victimIdx);
			std::swap(partitions_[victimIdx], partitions_[beneficiaryIdx]);
			std::swap(stateToPartition_[victimState].second, stateToPartition_[beneficiaryState].second);
			stateToPartition_[beneficiaryState].first = newPart;
		}
		state_type oldPartOldStart = partitionBounds_[part].first;
		partitionBounds_[part].first += moveSize_[part];
		partitionBounds_.push_back({oldPartOldStart, partitionBounds_[part].first});
		//partitionBounds_[part].second stays where it is
		moveSize_.push_back(0);
		checkRep();
		return newPart;
	}

	HopcroftResult finish() {
		state_type initialStatePart = stateToPartition_[0].first;
		//Free memory.
//		invStart_.clear();
//		decltype(L_)().swap(L_);
//		inL_.clear();

		state_type new_state_size = numeric_cast<state_type>(partitionBounds_.size());
		//reuse some memory
		auto& remap = move_;
		auto& comes_from = inv_;
		auto& partitions_not_mapped = moveSize_;
		auto& survivors_not_assigned = suspects_;
#ifndef NDEBUG
		std::fill(remap.begin(), remap.end(), std::numeric_limits<state_type>::max());
		std::fill(comes_from.begin(), comes_from.end(), std::numeric_limits<state_type>::max());
		if (new_state_size + 1 < comes_from.size())
			comes_from[new_state_size+1] = std::numeric_limits<state_type>::max() - 1;
#endif
		partitions_not_mapped.clear();
		survivors_not_assigned.clear();
		for (state_type i = 0; i < partitionBounds_.size(); ++i) {
			auto part = partition(i);
			//Choose a survivor if possible.  0 must map to 0 to avoid changing
			//the language.  Otherwise, pick an arbitrary victim.
			state_type maybe_survivor;
			if (i == initialStatePart)
				maybe_survivor = 0;
			else {
				auto it = std::find_if(part.begin(), part.end(), [new_state_size](auto s){return s < new_state_size;});
				maybe_survivor = it != part.end() ? *it : *part.begin();
			}
			if (maybe_survivor < new_state_size) {
				comes_from[maybe_survivor] = maybe_survivor;
				for (state_type s : part) {
					remap[s] = maybe_survivor;
					if (s < new_state_size && s != maybe_survivor)
						survivors_not_assigned.push_back(s);
				}
			} else {
				if (!survivors_not_assigned.empty()) {
					state_type survivor = survivors_not_assigned.back();
					survivors_not_assigned.pop_back();
					//maybe_survivor may as well be an arbitrary element
					comes_from[survivor] = maybe_survivor;
					for (state_type s : part)
						remap[s] = survivor;
						//We know there are no survivors in this partition.
				} else
					partitions_not_mapped.push_back(i);
			}
		}

		//Any partitions with multiple survivors have been processed, so we have
		//all the survivors we ever will.
		assert(partitions_not_mapped.size() == survivors_not_assigned.size());
		for (auto part_number : partitions_not_mapped) {
			state_type survivor = survivors_not_assigned.back();
			survivors_not_assigned.pop_back();
			auto part = partition(part_number);
			//pick an arbitrary element rather than running min_element again
			comes_from[survivor] = *part.begin();
			for (state_type s : part)
				remap[s] = survivor;
		}
		return {new_state_size, std::move(comes_from), std::move(remap)};
	}

	boost::iterator_range<const state_type*> partition(state_type partitionIdx) const {
		assert(partitionIdx < partitionBounds_.size());
		auto p = partitionBounds_[partitionIdx];
		return boost::make_iterator_range(&partitions_[0] + p.first, &partitions_[0] + p.second);
	}
	boost::iterator_range<const state_type*> inverseStep(state_type target, symbol_type symbol) const {
		assert(target <= state_size_);
		assert(symbol <= alphabet_size_);
		std::size_t start = target * (alphabet_size_+1) + symbol;
		assert((start + 1) < invStart_.size());
		return boost::make_iterator_range(&inv_[0] + invStart_[start], &inv_[0] + invStart_[start+1]);
	}

	state_type partitionSize(state_type partitionIdx) const {
		assert(partitionIdx < partitionBounds_.size());
		return partitionBounds_[partitionIdx].second - partitionBounds_[partitionIdx].first;
	}

	void addBetter(state_type partA, state_type partB, symbol_type symbol) {
		//This is the |B| criterion.
		int part = (partitionSize(partA) <= partitionSize(partB)) ? partA : partB;
		add(part, symbol);
	}
	void add(state_type part, symbol_type symbol) {
		checkRep();
		assert(!contains(part, symbol));
		L_.push_back({part, symbol});
		inL_.set(part * alphabet_size_ + symbol);
		checkRep();
	}
	bool contains(int part, int symbol) const {
		checkRep();
		return inL_.test(part * alphabet_size_ + symbol);
	}
	std::pair<state_type, symbol_type> remove() {
		checkRep();
		auto pair = L_.pop_front();
		inL_.reset(pair.first * alphabet_size_ + pair.second);
		checkRep();
		return pair;
	}

	void checkRep() const {
#ifndef NDEBUG
		//We exit early in the trivial empty/all cases, so we always have at
		//least accept/reject partitions.
		assert(partitionBounds_.size() >= 2);
		for (const auto& p : partitionBounds_)
			assert(p.second - p.first > 0);
		assert(moveSize_.size() == partitionBounds_.size());
		assert(suspects_.size() <= partitionBounds_.size());

		//All states in each partition should have the same accept status.
		//(Established in the very first split.)
		for (state_type i = 0; i < partitionBounds_.size(); ++i)
			for (state_type j = partitionBounds_[i].first; j < partitionBounds_[i].second - 1; ++j)
				assert(a_.accept(partitions_[j]) == a_.accept(partitions_[j+1]));

		//TODO: every element in partitions_ is within a parititionBounds_ element
		//TODO: partitions_ is a permutation of [0..n) (is there a cheap way to check?)
		//TODO: inL is true iff L contains the state-symbol pair (if is cheap, only-if is costly)

		//It's a fixed invariant so maybe not worth checking constantly, but
		//invStart_ should be nondecreasing and contain only valid indices
		//into inv_.
#endif //NDEBUG
	}
};

HopcroftResult hopcroft(WorkingAutomaton& a) {
	return HopcroftMinimizer(a).minimize();
}

} //namespace detail
} //namespace automaton