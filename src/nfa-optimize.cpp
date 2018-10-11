#include "precompiled.hpp"
#include "nfa-optimize.hpp"
#include "automatonbase.hpp"

namespace automaton {
namespace detail {

class NodeList {
public:
	using offset_type = state_type;
	constexpr static offset_type INV = std::numeric_limits<state_type>::max();
	NodeList(state_type capacity) : nodes_(capacity) {}

	state_type operator[](state_type p) const {
		return nodes_[p].part;
	}
	bool has_next(offset_type p) const {
		return nodes_[p].next != INV;
	}
	bool next(offset_type p) const {
		assert(has_next(p));
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
public:
	NFAOptimizer(AutomatonBase& a) :
			a_(a), state_size_(a.state_size()), alphabet_size_(a.alphabet_size()),
			partitions_(state_size_), partitionBounds_(),
			stateToPartition_(state_size_), inv_(a.transition_size()),
			invStart_(state_size_ * (alphabet_size_ + 1)), splitters_(state_size_),
			waiting_(), partitionToSplitter_() {}

	int minimize() {
		if (!buildInverseAndInitializePartitions())
			//all states in same partition; accept or reject
			return a_.accept(0) ? 42 : 43; //TODO: all or empty automata
		coreLoop();
		if (partitionBounds_.size() == state_size_) //each state in own partition; no changes
			return 9001; //TODO: do we even need to handle this separately? caller will know not to do anything...
		return finish();
	}
private:
	const AutomatonBase& a_;
	state_type state_size_;
	symbol_type alphabet_size_;
	//Every partition contains at least one state, so there can only be as
	//many partitions as states.
	dynarray<state_type> partitions_;
	std::vector<std::pair<state_type, state_type>> partitionBounds_;
	//first is the partition, second is the index into partitions_
	dynarray<std::pair<state_type, state_type>> stateToPartition_;
	dynarray<state_type> inv_;
	dynarray<std::size_t> invStart_; //TODO: only needs to be large enough for a transition index, so usually 32 bits
	//We represent X as a collection of singly-linked nodes holding a partition
	//number.  We track the index of each partition's node (if any) so we can
	//link in any split partitions.  The waiting queue holds the heads of each
	//list with more than two elements.
	NodeList splitters_;
	circular_deque<NodeList::offset_type, 32> waiting_;
	std::vector<NodeList::offset_type> partitionToSplitter_;

	void coreLoop() {

	}

	int finish() {
		//maybe we want to compute a suggested survivor for each partition, to
		//ensure we have contiguous state numbers? but if we're going to determinize()
		//(not optimize()) it's computing a new numbering anyway, so we shouldn't bother compressing state numbers
		return 32;
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

		std::vector<Edge> edgelist;
		edgelist.reserve(state_size_ * alphabet_size_ + 1);
		a_.for_each_transition([&edgelist](state_type from, symbol_type on, state_type to) {
			edgelist.push_back({from, on, to});
		});
		bool crashed = edgelist.size() != state_size_ * alphabet_size_;
		//We need to know if we crashed (equivalently, if we're not total), and
		//testing whether we're total requires iterating all the states anyway,
		//so we'll wait until after building the edgelist on the assumption that
		//trivial automata are uncommon.
		if ((finalIdx+1) == 0U && !crashed)
			return false;

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
//			moveSize_.resize(partitionBounds_.size(), 0);
		} else {
			partitionBounds_.push_back({0, nonfinalIdx});
			partitionBounds_.push_back({nonfinalIdx, static_cast<state_type>(partitions_.size())});
//			moveSize_.push_back(0);
//			moveSize_.push_back(0);
		}

		initializeStateToPartition();
		initializeSplitters();
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
		partitionToSplitter_[0] = head;
		NodeList::offset_type list = head;
		for (state_type i = 1; i < partitionBounds_.size(); ++i) {
			list = splitters_.insert_after(list, i);
			partitionToSplitter_[i] = list;
		}
		waiting_.push_back(head);
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

} //namespace detail

} //namespace automaton
