#include "precompiled.hpp"
#include "automaton.hpp"
#include "provenance.hpp"
#include "ops.hpp"
#include "gadgetdefs.hpp"
#include "packedautomaton.hpp"
#include "hopscotch/hopscotch_set.h"

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
	template<class PackPointer>
	using ClosedSet = tsl::hopscotch_set<PackPointer,
			indirect_hash, indirect_equal, std::allocator<PackPointer>,
			30, true /* store the hash */>;
	using OwningClosedSet = ClosedSet<std::unique_ptr<const PackedAutomaton>>;
	using NonowningClosedSet = ClosedSet<const PackedAutomaton*>;
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

	struct Finisher {
		Finisher(const OwningClosedSet* closed) : globalClosed(closed), globalClosedPruned(0), localClosedPruned(0) {}
		vector<PackProv> nextgen;
		NonowningClosedSet localClosed;
		const OwningClosedSet* globalClosed;
		unsigned int globalClosedPruned, localClosedPruned;
		void operator()(automaton_type&& a, Provenance p) {
			canonicalize(a, a.active_alphabet_size());
			auto packed = pack(a);
			auto hash = packed->packed_hash();
			//Check the closed set to deduplicate early.
			if (globalClosed->find(packed, hash) != globalClosed->end()) {
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
	};

	void combine_once(const PackedAutomaton* source, index_type sourceIndex, Finisher& finishAction) {
		//from toggles.cpp's Combine::operator(); TODO: may want to reunify
		automaton_type unpacked(*source);
		automaton_type::symbol_type leftLocations = unpacked.active_alphabet_size();
		automaton_type mirrored = mirror(unpacked);
		bool shouldmirror = unpacked == mirrored;
		for (const Input& i : inputs_) {
			if (leftLocations + i.active_alphabet_size > automaton_type::alphabet_size_v) continue;
			combine(unpacked, sourceIndex, false, leftLocations, i.normal, i.index, false, i.active_alphabet_size, finishAction);
			if (i.mirror.state_size())
				combine(unpacked, sourceIndex, false, leftLocations, i.mirror, i.index, true, i.active_alphabet_size, finishAction);
			if (shouldmirror) {
				combine(mirrored, sourceIndex, true, leftLocations, i.normal, i.index, false, i.active_alphabet_size, finishAction);
				if (i.mirror.state_size()) //TODO: the both-mirrored combine may be redundant
					combine(mirrored, sourceIndex, true, leftLocations, i.mirror, i.index, true, i.active_alphabet_size, finishAction);
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
		automaton_type mirrored = mirror(inflated);
		automaton_type::symbol_type locations = inflated.active_alphabet_size();
		connect(inflated, sourceIndex, false, locations, finishAction);
		if (inflated != mirrored)
			connect(mirrored, sourceIndex, true, locations, finishAction);
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

	std::vector<std::string> tokens;
	boost::algorithm::split(tokens, argv[1], boost::algorithm::is_any_of(","));
	for (unsigned int i = 0; i < tokens.size(); ++i) {
		std::cout << "input " << i << ": " << tokens[i] << "\n";
		inputs.push_back(*known_gadget(tokens[i], automaton_type::alphabet_size_v));
	}

	tokens.clear();
	boost::algorithm::split(tokens, argv[2], boost::algorithm::is_any_of(","));
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