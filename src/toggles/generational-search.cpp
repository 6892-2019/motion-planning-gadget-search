#include "precompiled.hpp"
#include "automaton.hpp"
#include "provenance.hpp"
#include "ops.hpp"
#include "gadgetdefs.hpp"
#include "packedautomaton.hpp"
#include "hopscotch/hopscotch_set.h"
#include "stringutils.hpp"
#include "maybe_owning_ptr.hpp"

using namespace automaton;
using std::vector;
using std::pair;
using std::string;
using std::unique_ptr;

struct Runnable {
	virtual void operator()() = 0;
	virtual ~Runnable() {};
};

template<class Callable>
struct RunnableImpl : public Runnable {
	RunnableImpl(const RunnableImpl&) = default;
	RunnableImpl(RunnableImpl&&) = default;
	RunnableImpl(const Callable& callable) : callable_(callable) {}
	RunnableImpl(Callable&& callable) : callable_(std::move(callable)) {}
	void operator()() override {callable_();}
private:
	Callable callable_;
};

class ThreadPool {
public:
	ThreadPool(int threads = std::thread::hardware_concurrency(), unsigned int queueSize = 1024) : tasks_(queueSize) {
		for (int i = 0; i < threads; ++i)
			workers_.emplace_back(threadProc, std::ref(tasks_));
	}
	template<class Callable, class... Args>
	auto submit(Callable&& task, Args&&... args) {
		std::promise<std::invoke_result_t<Callable, Args...>> promise;
		auto future = promise.get_future();
//		unique_ptr<Runnable> f = std::make_unique<RunnableImpl>(
//			//http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0780r2.html
////			[promise=std::move(promise), task=std::move(task), ...args=std::move(args)] {
//			[promise=std::move(promise), task=std::move(task), args=std::make_tuple(std::move(args)...)] () mutable {
//			try {
//				promise.set_value(std::apply(task, args));
//			} catch(...) {
//				try {
//					promise.set_exception(std::current_exception());
//				} catch (...) {} //nothing to be done
//			}
//		});
		unique_ptr<Runnable> f(new RunnableImpl(
				[promise=std::move(promise), task=std::move(task), args=std::make_tuple(std::move(args)...)] () mutable {
					try {
						//TODO: should we be moving from args here?
						promise.set_value(std::apply(task, args));
					} catch(...) {
						try {
							promise.set_exception(std::current_exception());
						} catch (...) {} //nothing to be done
					}
				}
				));
		tasks_.put(std::move(f));
		return future;
	}
private:
	bounded_queue<unique_ptr<Runnable>> tasks_;
	std::vector<std::thread> workers_;
	static void threadProc(decltype(tasks_)& queue) {
		//TODO: consider pinning threads to cores
		while (true) {
			auto p = queue.take();
			if (!p) return;
			p->operator()();
		}
	}
};

typedef Automaton<8u> automaton_type;
typedef pair<automaton_type, Provenance> AutoProv;
typedef pair<unique_ptr<const PackedAutomaton>, Provenance> PackProv;
template<class PackPointer>
using ClosedSet = tsl::hopscotch_set<PackPointer,
		indirect_hash, indirect_equal, std::allocator<PackPointer>,
		30, true /* store the hash */>;
using OwningClosedSet = ClosedSet<std::unique_ptr<const PackedAutomaton>>;
using NonowningClosedSet = ClosedSet<const PackedAutomaton*>;
typedef unsigned int index_type;

struct Input {
	index_type index;
	automaton_type normal, mirror; //mirror is empty if the input is not chiral
	AutomatonBase::symbol_type active_alphabet_size;
};

struct Target {
	std::size_t packed_hash, mirror_packed_hash;
	std::unique_ptr<const PackedAutomaton> normal, mirror;
};

struct Finisher {
	Finisher(const OwningClosedSet* closed) : globalClosed(closed) {}
	Finisher(const Finisher& f, tbb::split) : globalClosed(f.globalClosed) {}
	vector<PackProv> nextgen;
	NonowningClosedSet localClosed;
	const OwningClosedSet* globalClosed;
	unsigned int globalClosedPruned = 0, localClosedPruned = 0;
	void operator()(automaton_type&& a, Provenance p) {
		canonicalize(a, a.active_alphabet_size());
		auto packed = pack(a);
		auto hash = packed->packed_hash();
		//Check the closed set to deduplicate early.
		if (globalClosed && globalClosed->find(packed, hash) != globalClosed->end()) {
			++globalClosedPruned;
			return;
		}
		if (localClosed.insert(packed.get()).second) { //TODO: insert overload taking the hash
			//We could check targets here, but we can't easily report a finding
			//and, once we go parallel, we want to ensure we get a deterministic
			//finding, so we'd have to check that an earlier thread hadn't yet.
			nextgen.emplace_back(std::move(packed), p);
		} else
			++localClosedPruned;
	}
	void join(Finisher& rhs) {
		//If we're globally pruning, we did it already.
		assert(((bool)globalClosed) && ((bool)rhs.globalClosed));
		for (PackProv& p : rhs.nextgen) {
			if (localClosed.insert(p.first.get()).second) //TODO: if we save the hash, use it here
				nextgen.push_back(std::move(p));
			else
				++localClosedPruned;
		}
	}
};

template<class Iter>
void setInitialStatesToAcceptingStatesInRange(automaton_type& a, Iter first, Iter last) {
	using state_type = automaton_type::state_type;
	state_type s = a.addState();
	for (state_type t : make_range_for_pair(first, last))
		if (a.accept(t))
			a.addEpsilon(s, t);
	a.swapStateNumbers(0, s);
}

bool enjoin(automaton_type& a, automaton_type::symbol_type l, automaton_type::symbol_type m) {
	using state_type = automaton_type::state_type;
	bool progress, changed = false;
	//TODO: instead of fixpoint iteration, we should put the changed state s
	//on a worklist and iterate until it's empty
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

template<class State, typename SplitFunc, typename EvalFunc, typename JoinFunc>
struct MutableReduce {
	const SplitFunc* split_;
	const EvalFunc* eval_;
	const JoinFunc* join_;
	State state_;
	template<class S>
	MutableReduce(S&& initialState, SplitFunc& split, EvalFunc& eval, JoinFunc& join) :
			split_(&split), eval_(&eval), join_(&join), state_(std::forward<S>(initialState)) {}
	MutableReduce(MutableReduce& lhs, tbb::split) : split_(lhs.split_), eval_(lhs.eval_),
			join_(lhs.join_), state_((*split_)(lhs.state_)) {}
	template<class Range>
	void operator()(const Range& r) {
		(*eval_)(r, state_);
	}
	void join(MutableReduce& rhs) {
		(*join_)(state_, rhs.state_);
	}
};

template<class Range, class State, typename SplitFunc, typename EvalFunc, typename JoinFunc>
auto parallel_reduce(Range range, State&& initialState, SplitFunc splitter, EvalFunc eval, JoinFunc joiner) {
	MutableReduce<State, SplitFunc, EvalFunc, JoinFunc> body(std::forward<State>(initialState), splitter, eval, joiner);
	tbb::parallel_reduce(range, body);
	return std::move(body.state_);
}

void connect(const automaton_type& a, std::uint32_t gadgetIndex, bool mirrored,
		unsigned int locations, Finisher& finish) {
	using state_type = typename automaton_type::state_type;
	using symbol_type = typename automaton_type::symbol_type;

	struct ConnectReduceBody {
		const automaton_type* a_;
		std::uint32_t gadgetIndex_;
		bool mirrored_;
		unsigned int locations_;
		maybe_owning_ptr<Finisher> finisher_;
		ConnectReduceBody(const automaton_type& a, std::uint32_t gadgetIndex, bool mirrored, unsigned int locations, Finisher& finisher) :
				a_(&a), gadgetIndex_(gadgetIndex), mirrored_(mirrored), locations_(locations), finisher_(&finisher, false) {}
		ConnectReduceBody(ConnectReduceBody& lhs, tbb::split) : a_(lhs.a_), gadgetIndex_(lhs.gadgetIndex_),
				mirrored_(lhs.mirrored_), locations_(lhs.locations_), finisher_(new Finisher(*lhs.finisher_, tbb::split{}), true) {}
		void operator()(const tbb::blocked_range<unsigned int> locationRange) {
			const automaton_type& a = *a_;
			std::uint32_t gadgetIndex = gadgetIndex_;
			bool mirrored = mirrored_;
			unsigned int locations = locations_;
			Finisher& finish = *finisher_;

			//TODO: these alphamap manipulations could all be precomputed
			std::vector<symbol_type> alphamap(automaton_type::alphabet_size_v);
			for (unsigned int l = locationRange.begin(); l < locationRange.end(); ++l) {
				unsigned int m = (l+1) % locations;
				automaton_type connected = a;
				enjoin(connected, l, m);
				acceptingClosure(connected, locations);

				std::iota(alphamap.begin(), alphamap.begin() + locations, 0);
				std::fill(alphamap.begin() + locations, alphamap.end(), std::numeric_limits<symbol_type>::max());
				//remove larger first to avoid off-by-one
				alphamap.erase(alphamap.begin()+std::max(l, m));
				alphamap.erase(alphamap.begin()+std::min(l, m));
				//pad with 0
				alphamap.push_back(std::numeric_limits<symbol_type>::max());
				alphamap.push_back(std::numeric_limits<symbol_type>::max());
				connected.renumberAlphabet(0, connected.state_size(), alphamap.begin());

				//We may have disconnected the automaton (disconnecting the
				//configuration graph of the gadget it represents).
				automaton::SCCs sccs = automaton::find_components(connected);
				//TODO: don't reduce if just one; don't reduce over singleton components (?)

				maybe_owning_ptr<Finisher> f = parallel_reduce(tbb::blocked_range<unsigned int>(0, sccs.size()),
						maybe_owning_ptr<Finisher>(finisher_.get(), false),
						[](const maybe_owning_ptr<Finisher>& f){return maybe_owning_ptr<Finisher>(new Finisher(*f, tbb::split{}), true);},
						[&](const tbb::blocked_range<unsigned int>& r, maybe_owning_ptr<Finisher>& finish) {
							std::array<automaton_type::symbol_type, automaton_type::alphabet_size_v> compression;
							for (unsigned int c = r.begin(); c != r.end(); ++c) {
								automaton_type op = connected;
								setInitialStatesToAcceptingStatesInRange(op, sccs.begin(c), sccs.end(c));
								op.minimize();
								automaton::AutomatonBase::SymbolSet active = op.activeAlphabet();
								if (active.size() <= 1) continue; //there are no interesting 1-symbol automata
								if (active.size() != (locations - 2)) {
									//compress the alphabet
									active.sort();
									std::copy(active.begin(), active.end(), compression.begin());
									std::fill(compression.begin()+active.size(), compression.end(), std::numeric_limits<symbol_type>::max());
									op.renumberAlphabet(compression.begin());
									//Because we're deleting unused symbols, we don't need to
									//minimize again; any two equivalent states would differ only in
									//the symbols we deleted, but those symbols were inactive.
								}
								(*finish)(std::move(op), Provenance(gadgetIndex, l, c, mirrored));
							}
						},
						[](const maybe_owning_ptr<Finisher>& lhs, const maybe_owning_ptr<Finisher>& rhs) {
							lhs->join(*rhs);
						});
			}
		}
		void join(ConnectReduceBody& rhs) {
			finisher_->join(*rhs.finisher_);
		}
	};

	ConnectReduceBody body{a, gadgetIndex, mirrored, locations, finish};
	tbb::parallel_reduce(tbb::blocked_range<unsigned int>(0, locations), body);
}

constexpr index_type combine_batch_size = 500, connect_batch_size = 500;
class GenerationalSearch {
public:
	GenerationalSearch(vector<automaton_type>& inputs, vector<automaton_type>& targets) {
		inputs_.reserve(inputs.size());
		for (auto i : xrange(inputs.size())) {
			automaton_type m = mirror(inputs[i]);
			if (m == inputs[i])
				m.clear();
			auto asz = inputs[i].active_alphabet_size();
			inputs_.push_back({numeric_cast<index_type>(i), std::move(inputs[i]), std::move(m), asz});
		}
		targets_.reserve(targets.size());
		for (auto& t : targets) {
			auto p = pack(t);
			auto m = pack(mirror(t));
			auto ph = p->packed_hash(), mh = m->packed_hash();
			targets_.push_back({ph, mh, std::move(p), std::move(m)});
		}
	}
	void advance() {
		vector<PackProv> nextgen;
		std::vector<std::future<Finisher>> futures;
		auto finishAction = [&](automaton_type&& a, Provenance p) {
			a.minimize();
			canonicalize(a, a.active_alphabet_size());
			auto packed = pack(a);
			auto hash = packed->packed_hash();
			//Check the closed set to deduplicate early.
			if (closed_.find(packed, hash) != closed_.end()) return;
			//We could check targets here, but we can't easily report a finding
			//and, once we go parallel, we want to ensure we get a deterministic
			//finding, so we'd have to check that an earlier thread hadn't yet.
			//TODO: local deduplication in our Expansion struct
			nextgen.emplace_back(std::move(packed), p);
		};
		if (curgen_.empty()) {
			assert(closed_.empty());
			//"combine against nothing" to get started
			for (auto& i : inputs_)
				finishAction(automaton_type{i.normal}, Provenance(i.index));
		} else {
			futures.clear();
			for (index_type i = 0, sourceIndex = numeric_cast<index_type>(provenance_.size()-curgen_.size());
					i < curgen_.size();
					i += combine_batch_size, sourceIndex += combine_batch_size) {
				auto first = curgen_.data()+i, last = curgen_.data() + std::min<std::size_t>(i+combine_batch_size, curgen_.size());
				futures.push_back(pool_.submit(&GenerationalSearch::combine_range, this, first, last, sourceIndex));
			}
			unsigned int localClosedPruned = 0, globalClosedPruned = 0;
			for (auto& future : futures) {
				Finisher f = future.get();
				nextgen.insert(nextgen.end(), std::move_iterator(f.nextgen.begin()), std::move_iterator(f.nextgen.end()));
				localClosedPruned += f.localClosedPruned;
				globalClosedPruned += f.globalClosedPruned;
			}
			std::cout << "combine: " << localClosedPruned << " locally pruned, " << globalClosedPruned << " globally pruned\n";
//			index_type firstSourceIndex = numeric_cast<index_type>(provenance_.size()-curgen_.size());
//			Finisher result = pool_.submit(&GenerationalSearch::combine_range, this,
//					curgen_.data(), curgen_.data()+curgen_.size(), firstSourceIndex).get();
//			nextgen = std::move(result.nextgen);
//			for (index_type i = 0, sourceIndex = numeric_cast<index_type>(provenance_.size()-curgen_.size());
//					i < curgen_.size();
//					i++, sourceIndex++) {
//				const PackedAutomaton* source = curgen_[i];
//				automaton_type unpacked(*source);
//				automaton_type::symbol_type leftLocations = unpacked.active_alphabet_size();
//				automaton_type mirrored = mirror(unpacked);
//				bool shouldmirror = unpacked == mirrored;
//				for (const Input& i : inputs_) {
//					if (leftLocations + i.active_alphabet_size > automaton_type::alphabet_size_v) continue;
//					combine(unpacked, sourceIndex, false, leftLocations, i.normal, i.index, false, i.active_alphabet_size, finishAction);
//					if (i.mirror.state_size())
//						combine(unpacked, sourceIndex, false, leftLocations, i.mirror, i.index, true, i.active_alphabet_size, finishAction);
//					if (shouldmirror) {
//						combine(mirrored, sourceIndex, true, leftLocations, i.normal, i.index, false, i.active_alphabet_size, finishAction);
//						if (i.mirror.state_size()) //TODO: the both-mirrored combine may be redundant
//							combine(mirrored, sourceIndex, true, leftLocations, i.mirror, i.index, true, i.active_alphabet_size, finishAction);
//					}
//				}
//			}
		}
		curgen_.clear();
		auto newStart = append(nextgen);
		while (curgen_.size() != newStart) {
			nextgen.clear();
			futures.clear();
			for (index_type i = numeric_cast<index_type>(newStart), sourceIndex = numeric_cast<index_type>(provenance_.size()-(curgen_.size()-newStart));
					i < curgen_.size();
					i += connect_batch_size, sourceIndex += connect_batch_size) {
				auto first = curgen_.data()+i, last = curgen_.data() + std::min<std::size_t>(i+connect_batch_size, curgen_.size());
				futures.push_back(pool_.submit(&GenerationalSearch::connect_range, this, first, last, sourceIndex));
			}
			//TODO: this finish-merging is copied from above
			unsigned int localClosedPruned = 0, globalClosedPruned = 0;
			for (auto& future : futures) {
				Finisher f = future.get();
				nextgen.insert(nextgen.end(), std::move_iterator(f.nextgen.begin()), std::move_iterator(f.nextgen.end()));
				localClosedPruned += f.localClosedPruned;
				globalClosedPruned += f.globalClosedPruned;
			}
			std::cout << "connect: " << localClosedPruned << " locally pruned, " << globalClosedPruned << " globally pruned\n";
//			for (index_type i = numeric_cast<index_type>(newStart), sourceIndex = numeric_cast<index_type>(provenance_.size()-(curgen_.size()-newStart));
//					i < curgen_.size();
//					i++, sourceIndex++) {
//				automaton_type inflated(*curgen_[i]);
//				automaton_type mirrored = mirror(inflated);
//				automaton_type::symbol_type locations = inflated.active_alphabet_size();
//				connect(inflated, sourceIndex, false, locations, finishAction);
//				if (inflated != mirrored)
//					connect(mirrored, sourceIndex, true, locations, finishAction);
//			}
			newStart = append(nextgen);
			std::cout << newStart << " " << curgen_.size() << " " << nextgen.size() << std::endl;
		}
	}
private:
	vector<const PackedAutomaton*> curgen_; //non-owning, owned by closed_'s elements
	vector<Provenance> provenance_;
	OwningClosedSet closed_;
	vector<Input> inputs_;
	vector<Target> targets_;
	ThreadPool pool_;

	std::size_t append(std::vector<PackProv>& next) {
		auto newStart = curgen_.size();
		for (PackProv& p : next) {
			const PackedAutomaton* observer = p.first.get();
			if (closed_.insert(std::move(p.first)).second) {
				auto hash = observer->packed_hash();
				for (const Target& t : targets_)
					if (t.packed_hash == hash || t.mirror_packed_hash == hash) {
						print_provenance_backtrace(p.second);
						//TODO: maybe put it in some member variable to be checked when convenient?
					}
				//TODO: add insert overload taking the hash so we only compute it once
				curgen_.push_back(observer);
				provenance_.push_back(p.second);
			}
			//otherwise unique_ptr cleans it up somewhere, possibly in the guts
			//of closed_.insert.  TODO: we might prefer to release memory in a
			//large batch at the end of the loop rather than during each
			//iteration, for better locality (both data and code).
		}
		return newStart;
	}



	void combine_once(const PackedAutomaton* source, index_type sourceIndex, Finisher& finishAction) {
		//from toggles.cpp's Combine::operator(); TODO: may want to reunify
		automaton_type unpacked(*source);
		automaton_type::symbol_type leftLocations = unpacked.active_alphabet_size();
		//We only mirror once we have to.  Once we've mirrored, we check if we're
		//chiral so we can skip the == after the first time.  mirrored continues
		//to live until the end of the function even if we're achiral, but that's good enough.
		std::optional<automaton_type> mirrored;
		bool chiral;
		for (const Input& i : inputs_) {
			if (leftLocations + i.active_alphabet_size > automaton_type::alphabet_size_v) continue;
			combine(unpacked, sourceIndex, false, leftLocations, i.normal, i.index, false, i.active_alphabet_size, finishAction);
			if (i.mirror.state_size())
				combine(unpacked, sourceIndex, false, leftLocations, i.mirror, i.index, true, i.active_alphabet_size, finishAction);
			else {
				if (!mirrored) {
					mirrored.emplace(mirror(unpacked));
					chiral = *mirrored != unpacked;
				}
				if (chiral)
					combine(*mirrored, sourceIndex, true, leftLocations, i.normal, i.index, false, i.active_alphabet_size, finishAction);
			}
		}
	}

	Finisher combine_range(const PackedAutomaton** first, const PackedAutomaton** last, index_type firstSourceIndex) {
		Finisher finisher(&closed_);
		//https://stackoverflow.com/a/18514815/3614835
		for (auto [source, sourceIndex] = std::make_pair(first, firstSourceIndex); source != last; ++source, ++sourceIndex)
			combine_once(*source, sourceIndex, finisher);
		return finisher;
	}

	void connect_once(const PackedAutomaton* source, index_type sourceIndex, Finisher& finishAction) {
		automaton_type inflated(*source);
		automaton_type::symbol_type locations = inflated.active_alphabet_size();
		connect(inflated, sourceIndex, false, locations, finishAction);
	}

	Finisher connect_range(const PackedAutomaton** first, const PackedAutomaton** last, index_type firstSourceIndex) {
		//TODO: this is basically the same as combine_range, but as we need to
		//form a pointer to it, it may be awkward to template-merge them.
		Finisher finisher(&closed_);
		//https://stackoverflow.com/a/18514815/3614835
		for (auto [source, sourceIndex] = std::make_pair(first, firstSourceIndex); source != last; ++source, ++sourceIndex)
			connect_once(*source, sourceIndex, finisher);
		return finisher;
	}

	[[gnu::cold]]
	void print_provenance_backtrace(Provenance& provenance) {
		circular_deque<std::uint32_t, 32> queue;
		linear_set<std::uint32_t> printed;

		std::cout << "<found> = " << provenance << '\n';
		for (auto p : provenance.parents())
			if (printed.insert(p).second)
				queue.push_back(p);

		while (!queue.empty()) {
			auto idx = queue.pop_front();
			auto prov = provenance_.at(idx);
			std::cout << idx << " = " << prov << '\n';
			//Ideally we'd print the automaton here, but we're no longer
			//maintaining an id->automaton map.  We'll have to replay the
			//log this code is printing out.
			for (auto p : prov.parents())
				if (printed.insert(p).second)
					queue.push_back(p);
		}

		std::cout << std::flush;
	}
};

//struct Expansion;
//
//Expansion map(const automaton_type& a, index_type index, const vector<AutoProv>& combinables /* inputs + more? */) {
//	throw std::logic_error("");
//}
//
//Expansion map(const PackedAutomaton& a, index_type index, const vector<AutoProv>& combinables) {
//
//}
//
//Expansion reduce(const Expansion& left, const Expansion& right) {
//
//}

int main(int argc, char* argv[]) { //genbuild entrypoint
	vector<automaton_type> inputs, outputs;

	std::vector<std::string_view> tokens = split_view(argv[1], ',');
	for (unsigned int i = 0; i < tokens.size(); ++i) {
		std::cout << "input " << i << ": " << tokens[i] << "\n";
		inputs.push_back(*known_gadget(tokens[i], automaton_type::alphabet_size_v));
	}

	tokens = split_view(argv[2], ',');
	for (unsigned int i = 0; i < tokens.size(); ++i) {
		std::cout << "output " << i << ": " << tokens[i] << "\n";
		outputs.push_back(*known_gadget(tokens[i], automaton_type::alphabet_size_v));
	}

	GenerationalSearch gs(inputs, outputs);
	for (int generation = 1; ; ++generation) {
		gs.advance();
		std::cout << "finished " << generation << std::endl;
	}

	return 0;
}