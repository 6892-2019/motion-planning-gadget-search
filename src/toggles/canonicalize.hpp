/*
 * File:   canonicalize.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on April 15, 2017, 9:37 PM
 */

#ifndef CANONICALIZE_HPP
#define CANONICALIZE_HPP

#include "automaton.hpp"
#include "linear_set.hpp"
#include <nausparse.h>

inline void dotfile(const sparsegraph& sg, std::ostream& destination = std::cout) {
	destination << "digraph {\n";
	for (int v = 0; v < sg.nv; ++v) {
		std::size_t begin = sg.v[v], end = begin + sg.d[v];
		if (begin != end) {
			std::sort(sg.e+begin, sg.e+end);
			destination << "v" << v << " -> {";
			for (auto i = begin; i < end; ++i) {
				destination << "v" << sg.e[i];
				if ((i+1) != end)
					destination << ", ";
			}
			destination << "};\n";
		}
	}
	destination << "}" << std::endl;
}

template<unsigned int N>
void canonicalize(automaton::Automaton<N>& a, unsigned int locations) {
	sparsegraph sg, canon;
	SG_INIT(sg);
	sg.nv = 0;
	sg.nde = 0;
	SG_INIT(canon);
	canon.nv = 0;
	canon.nde = 0;

	using state_type = automaton::AutomatonBase::state_type;
	using symbol_type = automaton::AutomatonBase::symbol_type;
	const state_type state_size = a.state_size();
	const auto edge_size = a.edge_size();
	const auto transition_size = a.transition_size();
	//TODO: these could just be functions
	std::unordered_map<std::pair<state_type, unsigned int>, int,
		boost::hash<std::pair<state_type, unsigned int>>> towers;
	std::unordered_map<unsigned int, int> edgecolors;
	std::unordered_map<unsigned int, int> towercolors;
	for (state_type s = 0; s < a.size(); ++s)
		for (unsigned int l = 0; l < locations; ++l)
			towers[{s, l}] = sg.nv++;
	for (unsigned int l = 0; l < locations; ++l)
		edgecolors[l] = sg.nv++;
	for (unsigned int t = 0; t < a.size(); ++t)
		towercolors[t] = sg.nv++;
	const int acceptvtx = sg.nv++;
	sg.nde = 2 * edgecolors.size() //undirected cycle through the colors
			+ 1 * a.size() * edgecolors.size() //edge from each color to each node in its level
			+ 1 * towercolors.size() * edgecolors.size() //edge from each color to each node in its tower
			+ a.edges()
			+ 1 //state 0 is distinguished by a self-loop
			+ a.accept_size(); //accepting states are distinguished by an edge to acceptvtx
	SG_ALLOC(sg, sg.nv, sg.nde, "asdf");

	auto incr = [&](symbol_type s){return s == locations-1 ? 0 : s+1;};
	auto decr = [&](symbol_type s){return s == 0 ? locations-1 : s-1;};

	std::size_t ei = 0;
	for (state_type s = 0; s < a.size(); ++s)
		for (unsigned int l = 0; l < locations; ++l) {
			int vi = towers.at({s, l});
			sg.v[vi] = ei;
			auto dests = a.step(s, l);
			assert(dests.size() <= 1 && "should already be deterministic");
			if (!dests.empty())
				sg.e[ei++] = towers.at({dests.front(), l});
			sg.d[vi] = static_cast<int>(ei - sg.v[vi]);
		}

	for (unsigned int l = 0; l < locations; ++l) {
		int vi = edgecolors.at(l);
		sg.v[vi] = ei;
		sg.e[ei++] = edgecolors.at(decr(l));
		sg.e[ei++] = edgecolors.at(incr(l));
		for (state_type s = 0; s < a.size(); ++s)
			sg.e[ei++] = towers.at({s, l});
		sg.d[vi] = static_cast<int>(ei - sg.v[vi]);
	}

	for (unsigned int t = 0; t < a.size(); ++t) {
		int vi = towercolors.at(t);
		sg.v[vi] = ei;
		for (symbol_type l = 0; l < locations; ++l)
			sg.e[ei++] = towers.at({t, l});
		if (t == 0) //distinguish initial state with self-loop
			sg.e[ei++] = vi;
		if (a.accept(t))
			sg.e[ei++] = acceptvtx;
		sg.d[vi] = static_cast<int>(ei - sg.v[vi]);
	}
	sg.v[acceptvtx] = ei;
	sg.d[acceptvtx] = 0;
	assert(ei == sg.nde && "wrong number of edges");

	dynarray<int> lab(sg.nv), ptn(sg.nv);
	std::iota(lab.begin(), lab.end(), 0);
	std::fill(ptn.begin(), ptn.end(), 1); //counter-intuitively, partitions end at 0
	ptn[towers.size()-1] = 0; //state-location pairs
	ptn[towers.size()+edgecolors.size()-1] = 0; //edge colors
	ptn[ptn.size()-2] = 0; //state colors
	ptn[ptn.size()-1] = 0; //the accept state distinguishing vertex

	DEFAULTOPTIONS_SPARSEDIGRAPH(options);
	options.getcanon = TRUE;
	options.defaultptn = FALSE;
	statsblk stats;
	dynarray<int> orbits(sg.nv);
	sparsenauty(&sg, lab.begin(), ptn.begin(), orbits.begin(), &options, &stats, &canon);
	SG_FREE(sg);

	a.clear();
	for (state_type s = 0; s < state_size; ++s)
		a.addState();

	//map the state-location pair vertices to their state and location
	dynarray<state_type> vertexToState(state_size * locations), vertexToLocation(state_size * locations);
	state_type stateIdx = 1;
	for (int v = towercolors.at(0); v <= towercolors.at(a.state_size()-1); ++v) {
		auto begin = canon.e + canon.v[v], end = begin + canon.d[v];
		//state 0 has a self-loop
		state_type state = std::find(begin, end, v) != end ? 0 : stateIdx++;
		//accepting if pointing to the accept vertex
		a.setAccept(state, std::find(begin, end, acceptvtx) != end);
		for (int* q = begin; q < end; ++q)
			if (*q < vertexToState.size()) //only edges to first color (skip the self-loop/accept edges)
				vertexToState[*q] = state;
	}

	//we have to walk the cycle
	int symbolVtx = edgecolors.at(0);
	linear_set<int> visitedSymbolVertices;
	for (symbol_type symbolIdx = 0; symbolIdx < locations; ++symbolIdx) {
		visitedSymbolVertices.insert(symbolVtx);
		int next = edgecolors.at(locations-1)+1; //larger than any actual value
		auto begin = canon.e + canon.v[symbolVtx], end = begin + canon.d[symbolVtx];
		for (int* q = begin; q < end; ++q)
			if (*q < vertexToLocation.size())
				vertexToLocation[*q] = symbolIdx;
			else if (!visitedSymbolVertices.count(*q) && *q < next)
				next = *q;
		symbolVtx = next;
	}

	//read off the automaton edges from the graph
	for (int v = 0; v < edgecolors.at(0); ++v) {
		auto begin = canon.e + canon.v[v], end = begin + canon.d[v];
		state_type state = vertexToState[v];
		symbol_type symbol = vertexToLocation[v];
		for (int* q = begin; q < end; ++q) {
			state_type next = vertexToState[*q];
			symbol_type on = vertexToLocation[*q];
			assert(symbol == on && "not isomorphic after all?");
			a.addTrans(state, symbol, next);
		}
	}
	assert(a.edge_size() == edge_size);
	assert(a.transition_size() == transition_size);

	a.prepareForEquals();
	SG_FREE(canon);
}

#endif /* CANONICALIZE_HPP */

