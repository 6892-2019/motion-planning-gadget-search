#include "precompiled.hpp"
#include "nfa-optimize.hpp"

using std::vector;
using std::pair;

namespace automaton {
namespace detail {

class NodeList {
public:
	using offset_type = state_type;
	constexpr static offset_type INV = std::numeric_limits<state_type>::max();
	NodeList(state_type capacity) : nodes_(capacity) {}

	state_type& operator[](state_type p) {
		return nodes_[p].part;
	}
	state_type operator[](state_type p) const {
		return nodes_[p].part;
	}
	bool has_next(offset_type p) const {
		return nodes_[p].next != INV;
	}
	offset_type next(offset_type p) const {
		return nodes_[p].next;
	}
	/**
	 * Links in a new node with the given data after the given node.
	 * @return the newly-inserted node's offset
	 */
	offset_type insert_after(offset_type p, state_type data) {
		offset_type foo = allocate();
		nodes_[foo].part = data;
		nodes_[foo].next = nodes_[p].next;
		nodes_[p].next = foo;
		return foo;
	}
	/**
	 * Erases the node after the given offset.
	 * @return the offset to the node after the erased node, or INV if there
	 * isn't one
	 */
	offset_type erase_after(offset_type p) {
		assert(has_next(p));
		offset_type victim = nodes_[p].next;
		offset_type rv = nodes_[victim].next;
		nodes_[p].next = rv;
		free(victim);
		return rv;
	}
	/**
	 * Unlinks and frees the given node from the front of its list.
	 * @return the new front of that list, or INV if !has_next(p)
	 */
	offset_type pop_front(offset_type p) {
		auto rv = nodes_[p].next;
		free(p);
		return rv;
	}
	/**
	 * Creates a new list, storing data in the first node in that list.
	 */
	offset_type create(state_type data) {
		offset_type p = allocate();
		nodes_[p].part = data;
		nodes_[p].next = INV;
		return p;
	}

private:
	struct Node {
		state_type part;
		offset_type next;
	};
	dynarray<Node> nodes_;
	offset_type free_head_ = INV;
	offset_type highwater_ = 0;
	offset_type allocate() {
		if (free_head_ != INV) {
			offset_type a = free_head_;
			free_head_ = nodes_[a].next; //may be INV if a is the last node, so don't call next()
			assert(nodes_[a].part == INV);
			return a;
		}
		if (highwater_ < nodes_.size())
			return highwater_++;
		std::abort();
	}
	/**
	 * Frees the given node, which we assume to be the front of a list.
	 * (We can't check that because this is a singly-linked list.)
	 */
	void free(offset_type p) {
		nodes_[p].next = free_head_;
		free_head_ = p;
#ifndef NDEBUG
		nodes_[p].part = INV;
#endif
	}
};



class NFAOptimizer {
private:
	using offset_type = NodeList::offset_type;
public:
	NFAOptimizer(const AutomatonBase& a) :
			a_(a), state_size_(a.state_size()), alphabet_size_(a.alphabet_size()),
			active_alphabet_(a.activeAlphabet()), partitions_(state_size_), partitionBounds_(),
			stateToPartition_(state_size_), inv_(a.transition_size()),
			invStart_(state_size_ * (alphabet_size_ + 1)), splitters_(state_size_),
			waiting_(), partitionToSplitter_(), move_(state_size_), moveMarks_(state_size_),
			moveSize_(), suspects_() {
		active_alphabet_.sort();
	}

	OptimizeResult optimize() {
		if (!buildInverseAndInitializePartitions())
			return {a_.accept(0) ? OptimizeResult::ALL : OptimizeResult::EMPTY, {}, {}};
		coreLoop();
		if (partitionBounds_.size() == state_size_) //already optimal
			return {state_size_, {}, {}};
		return finish();
	}
private:
	const AutomatonBase& a_;
	state_type state_size_;
	symbol_type alphabet_size_;
	SymbolSet active_alphabet_;
	//Every partition contains at least one state, so there can only be as
	//many partitions as states.
	dynarray<state_type> partitions_;
	vector<pair<state_type, state_type>> partitionBounds_;
	//first is the partition, second is the index into partitions_
	dynarray<pair<state_type, state_type>> stateToPartition_;
	dynarray<state_type> inv_;
	dynarray<std::size_t> invStart_; //TODO: only needs to be large enough for a transition index, so usually 32 bits
	//We represent X as a collection of singly-linked nodes holding a partition
	//number.  For each partition, we track the head of the list it is in, so we
	//can add split partitions (and detect if the list is a singleton).  The
	//waiting queue holds the heads of each list with more than two elements.
	NodeList splitters_;
	circular_deque<offset_type, 32> waiting_;
	vector<offset_type> partitionToSplitter_;
	dynarray<state_type> move_;
	//In hopcroft we knew we'd only consider moving a state once, because a DFA
	//state can't be the predecessor of more than one state on the same symbol.
	//In an NFA that's clearly possible, so we have to track.
	//(TODO: We could save some space by stealing a bit from one of the other
	//state_size_-sized structures.)
	boost::dynamic_bitset<std::size_t> moveMarks_;
	std::vector<state_type> moveSize_;
	std::vector<state_type> suspects_;
	//The counts map is necessary for O(m log n) runtime.  For each splitter and
	//symbol, we store the number of occurences of each state in the inverse of
	//all states in the splitter under that symbol.  A count not in the map is
	//zero (and we erase when a count reaches zero).  The sum of the counts
	//across all symbols is transition_size_.  We continue to store counts for
	//singleton splitters because they might get refined later.
	using count_key = std::tuple<offset_type, symbol_type, state_type>;
	tsl::hopscotch_map<count_key, state_type, boost::hash<count_key>> counts_;

	void coreLoop() {
		while (!waiting_.empty()) {
			auto [refiner, remainder] = select(); //Tarjan's B and S-B; for count purposes, remainder is S
			for (symbol_type a : active_alphabet_) {
				collect(refiner, a);
				refine();
				collect(refiner, remainder, a);
				refine();
			}
		}
	}

	/**
	 * Removes a partition from the next splitter (removing the splitter from
	 * the waiting set if it's also a singleton).
	 * @return the refining splitter (a singleton) and the splitter holding the
	 * remaining partitions from that splitter block (possibly also a singleton)
	 */
	pair<offset_type, offset_type> select() {
		offset_type remainder = waiting_.front(), refiner = splitters_.next(remainder);
		//Of the first two, pop the smaller one.  We actually remove the second
		//list element so that the head pointer doesn't change; if we removed
		//the first instead, we'd have to update partitionToSplitter_ for all
		//the remaining list elements.
		if (partitionSize(splitters_[remainder]) < partitionSize(splitters_[refiner]))
			std::swap(splitters_[remainder], splitters_[refiner]);
		state_type refinerPart = splitters_[refiner];
		//Just a sanity check that we think it's in the list we're about to remove it from.
		assert(partitionToSplitter_[refinerPart] == remainder);
		splitters_.erase_after(remainder);
		refiner = splitters_.create(refinerPart);
		partitionToSplitter_[refinerPart] = refiner;
		if (!splitters_.has_next(remainder)) //if remainder has also become a singleton
			waiting_.pop_front();
		return {refiner, remainder};
	}

	void collect(offset_type refiner, symbol_type symbol) {
		moveMarks_.reset();
		suspects_.clear();
		//This was copied in from hopcroft, except that we iterate over every
		//partition in the splitter.
		for (offset_type offset = refiner; offset != NodeList::INV; offset = splitters_.next(offset)) {
			state_type part = splitters_[offset];
			for (state_type target : partition(part))
				for (state_type source : inverseStep(target, symbol)) {
					++counts_[{refiner, symbol, source}];
					if (moveMarks_.test_set(source))
						continue; //some other transition already caused this state to move
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
		}
	}

	void collect(offset_type refiner, offset_type remainder, symbol_type symbol) {
		moveMarks_.reset();
		suspects_.clear();
		//This was copied in from hopcroft, except that we iterate over every
		//partition in the splitter.
		for (offset_type offset = refiner; offset != NodeList::INV; offset = splitters_.next(offset)) {
			state_type part = splitters_[offset];
			for (state_type target : partition(part))
				for (state_type source : inverseStep(target, symbol)) {
					if (moveMarks_.test_set(source))
						continue; //some other transition already caused this state to move
					auto rem_count = counts_.find({remainder, symbol, source});
					assert(rem_count != counts_.end()); //can't be 0/absent
					rem_count.value() -= counts_[{refiner, symbol, source}];
					if (rem_count->second > 0)
						continue; //not in E^-1(B) - E^-1(S - B)
					else
						counts_.erase(rem_count); //and proceed to move

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
		}
	}

	void refine() {
		for (state_type part : suspects_) {
			assert(moveSize_[part] > 0); //shouldn't be in suspects otherwise
			if (moveSize_[part] < partitionSize(part))
				split(part);
				//hopcroft would add to the waiting set here, but we did that in split().
				//For the first collect (wrt B), only D_1 can be split in the
				//next collect (wrt S-B), per Paige/Tarjan (Lemma 3 (3)).  We
				//could remember the D_1 here and filter collect's suspects.
			moveSize_[part] = 0;
		}
	}

	state_type split(state_type part) {
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

		offset_type head = partitionToSplitter_[part];
		bool splitSingleton = !splitters_.has_next(head);
		splitters_.insert_after(head, newPart);
		partitionToSplitter_.push_back(head);
		if (splitSingleton)
			waiting_.push_back(head);

		return newPart;
	}

	//This method copied from WorkingAutomatonHopcroft.  We should factor it out
	//along with maintaining stateToPartition/etc.  Maybe even move_/suspects_
	//via record_for_refine/refine() method pair.
	OptimizeResult finish() {
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

	/**
	 * @return true if we should continue; false if the automaton is the
	 * trivial empty-language or all-strings automaton, in which case we're
	 * already done.  (in both cases all states are in the same partition; the
	 * difference is whether they all accept or all reject)
	 */
	bool buildInverseAndInitializePartitions() {
		unsigned int nonfinalIdx = 0, finalIdx = static_cast<unsigned int>(partitions_.size() - 1);
		//TODO: for_each_accept?
		for (state_type s = 0; s < state_size_; ++s) {
			if (a_.accept(s))
				partitions_[finalIdx--] = s;
			else
				partitions_[nonfinalIdx++] = s;
		}
		assert(nonfinalIdx == finalIdx+1 && "didn't partition all the states somehow");
		if (nonfinalIdx == partitions_.size())
			return false;

		//hopcroft builds the edge list first because we can cheaply check if a
		//DFA is total.  NFA totality checking requires iterating the states, so
		//doing it now lets us skip the edge list for all-strings automata.
		auto staterange = xrange(state_size_);
		bool crashed = active_alphabet_.size() < alphabet_size_ ||
				std::any_of(staterange.begin(), staterange.end(), [&](state_type s){
					return a_.outgoing(s).size() != alphabet_size_;
				});
		if ((finalIdx+1) == 0U && !crashed)
			return false;

		std::vector<Edge> edgelist;
		edgelist.reserve(inv_.size() + 1); //we add a sentinel edge in initializeInv
		a_.for_each_transition([&edgelist](state_type from, symbol_type on, state_type to) {
			edgelist.push_back({from, on, to});
		});

		if (crashed) {
			//Because we didn't totalize, we need to manually partition
			//crashing vs. non-crashing on each symbol.
			std::vector<typename decltype(partitions_)::iterator> bounds = {
				partitions_.begin(), partitions_.begin()+nonfinalIdx, partitions_.end()
			}, newbounds;
			for (symbol_type s : active_alphabet_) {
				newbounds.clear();
				for (typename decltype(bounds)::size_type i = 0; i < bounds.size() - 1; ++i) {
					newbounds.push_back(bounds[i]);
					newbounds.push_back(std::partition(bounds[i], bounds[i+1], [this, s](state_type state) {
						//if we crash
						return a_.step(state, s).empty();
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

		initializeSplitters();
		if (waiting_.empty())
			//We're all-strings on some but not all symbols.  It isn't worth
			//signalling this separately, but as we won't actually run the loop
			//we can skip some work.
			return true;
		initializeStateToPartition();
		initializeCounts();
		initializeInv(edgelist);
		//TODO: we can reassert this when inv_ is lazily sized
//			assert(invEltsIdx == inv_.size());
		return true;
	}

	void initializeStateToPartition() {
		for (state_type p = 0; p < partitionBounds_.size(); ++p) {
			auto bounds = partitionBounds_[p];
			//Sort for locality when accessing stateToPartition_.
			std::sort(partitions_.begin() + bounds.first, partitions_.begin() + bounds.second);
			for (state_type i = bounds.first; i != bounds.second; ++i)
				stateToPartition_[partitions_[i]] = {p, i};
		}
	}

	void initializeSplitters() {
		//Initially X is {U} (in Paige/Tarjan's terms), so all partitions are in
		//a single list.  Because we partition on final/nonfinal, there will
		//always be at least two partitions, so the list goes into waiting_.
		NodeList::offset_type head = splitters_.create(0);
		NodeList::offset_type list = head;
		for (state_type i = 1; i < partitionBounds_.size(); ++i)
			list = splitters_.insert_after(list, i);
		//initially all in the same splitter
		partitionToSplitter_.assign(partitionBounds_.size(), head);
		//All states might be in the same partition.
		if (splitters_.has_next(head))
			waiting_.push_back(head);
	}

	void initializeCounts() {
		//Initially everything's in a single splitter.
		assert(waiting_.size() == 1);
		offset_type splitter = waiting_.front();
		for (state_type s : xrange(state_size_))
			for (symbol_type a : active_alphabet_)
				counts_.insert_or_assign({splitter, a, s}, a_.step(s, a).size());
	}

	void initializeInv(std::vector<Edge>& edgelist) {
		//TODO: use a parallel sort (beyond a size threshold)
		std::sort(edgelist.begin(), edgelist.end(), Edge::Backwards());
		edgelist.push_back({std::numeric_limits<state_type>::max(), std::numeric_limits<symbol_type>::max(), std::numeric_limits<state_type>::max()});
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
		edgelist.pop_back();
	}
};

OptimizeResult optimize_for_renumber(const AutomatonBase& a) {
	return NFAOptimizer(a).optimize();
}

//dynarray<state_type> optimize_for_compress(AutomatonBase& a) {
//
//}

} //namespace detail
} //namespace automaton
