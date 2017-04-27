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
void canonicalize(automaton::Automaton<N>& a, const unsigned int locations, bool allowMirroring = true) {
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
	MAYBE_UNUSED const auto edge_size = a.edge_size();
	const auto transition_size = a.transition_size();
	MAYBE_UNUSED const auto accept_size = a.accept_size();

	const std::pair<int, int> towerVertices = {0, state_size * locations},
			edgeVertices = {towerVertices.second, towerVertices.second + locations},
			stateVertices = {edgeVertices.second, edgeVertices.second + state_size};
	const int acceptVertex = stateVertices.second;
	auto size = [](std::pair<int, int> p){return p.second - p.first;};
	auto towerVertex = [&](state_type s, symbol_type a) {
		assert(s < state_size);
		assert(a < locations);
		return s * locations + a;
	};
	auto edgeColor = [&](symbol_type a) {
		assert(a < locations);
		return edgeVertices.first + a;
	};
	auto stateColor = [&](state_type s) {
		assert(s < state_size);
		return stateVertices.first + s;
	};

	sg.nv = size(towerVertices) + size(edgeVertices) + size(stateVertices) + 1 /* acceptVertex */;
	sg.nde = (allowMirroring ? 2 : 1) * size(edgeVertices) //(un)directed cycle through the colors
			+ 1 * state_size * size(edgeVertices) //edge from each color to each node in its level
			+ 1 * locations * size(stateVertices) //edge from each color to each node in its tower
			+ transition_size //the actual edges in the automaton
			+ 1 //state 0 is distinguished by a self-loop on the state vertex
			+ accept_size; //accepting states are distinguished by an edge to acceptVertex
	SG_ALLOC(sg, sg.nv, sg.nde, "asdf");

	auto incr = [&](symbol_type a){return a == locations-1 ? 0 : a+1;};
	auto decr = [&](symbol_type a){return a == 0 ? locations-1 : a-1;};

	std::size_t ei = 0;
	for (state_type s = 0; s < state_size; ++s)
		for (symbol_type l = 0; l < locations; ++l) {
			int vi = towerVertex(s, l);
			sg.v[vi] = ei;
			auto dests = a.step(s, l);
			assert(dests.size() <= 1 && "should already be deterministic");
			if (!dests.empty())
				sg.e[ei++] = towerVertex(dests.front(), l);
			sg.d[vi] = static_cast<int>(ei - sg.v[vi]);
		}

	for (symbol_type l = 0; l < locations; ++l) {
		int vi = edgeColor(l);
		sg.v[vi] = ei;
		sg.e[ei++] = edgeColor(decr(l));
		if (allowMirroring)
			sg.e[ei++] = edgeColor(incr(l));
		for (state_type s = 0; s < state_size; ++s)
			sg.e[ei++] = towerVertex(s, l);
		sg.d[vi] = static_cast<int>(ei - sg.v[vi]);
	}

	for (state_type s = 0; s < state_size; ++s) {
		int vi = stateColor(s);
		sg.v[vi] = ei;
		for (symbol_type l = 0; l < locations; ++l)
			sg.e[ei++] = towerVertex(s, l);
		if (s == 0) //distinguish initial state with self-loop
			sg.e[ei++] = vi;
		if (a.accept(s))
			sg.e[ei++] = acceptVertex;
		sg.d[vi] = static_cast<int>(ei - sg.v[vi]);
	}
	sg.d[acceptVertex] = 0;
	sg.v[acceptVertex] = std::numeric_limits<std::size_t>::max(); //nauty shouldn't look at this
	assert(ei == sg.nde && "wrong number of edges");

	dynarray<int> lab(sg.nv), ptn(sg.nv);
	std::iota(lab.begin(), lab.end(), 0);
	std::fill(ptn.begin(), ptn.end(), 1); //counter-intuitively, partitions end at 0 and are inclusive
	ptn[towerVertices.second-1] = 0; //state-location pairs
	ptn[edgeVertices.second-1] = 0; //edge colors
	ptn[stateVertices.second-1] = 0; //state colors
	ptn[acceptVertex] = 0; //the accept state distinguishing vertex

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
	dynarray<state_type> vertexToState(size(towerVertices)), vertexToLocation(size(towerVertices));
	state_type stateIdx = 1;
	for (int v = stateVertices.first; v < stateVertices.second; ++v) {
		auto begin = canon.e + canon.v[v], end = begin + canon.d[v];
		//state 0 has a self-loop
		state_type state = std::find(begin, end, v) != end ? 0 : stateIdx++;
		//accepting if pointing to the accept vertex
		a.setAccept(state, std::find(begin, end, acceptVertex) != end);
		for (int* q = begin; q < end; ++q)
			if (*q < static_cast<int>(vertexToState.size())) //only edges to first color (skip the self-loop/accept edges)
				vertexToState[*q] = state;
	}

	//we have to walk the cycle
	int symbolVtx = edgeVertices.first;
	linear_set<int> visitedSymbolVertices;
	for (symbol_type symbolIdx = 0; symbolIdx < locations; ++symbolIdx) {
		visitedSymbolVertices.insert_absent(symbolVtx);
		int next = edgeVertices.second; //larger than any actual value
		auto begin = canon.e + canon.v[symbolVtx], end = begin + canon.d[symbolVtx];
		for (int* q = begin; q < end; ++q)
			if (*q < static_cast<int>(vertexToLocation.size()))
				vertexToLocation[*q] = symbolIdx;
			//We pick the smaller of the two choices on the first edge-chase.
			else if (!visitedSymbolVertices.count(*q) && *q < next)
				next = *q;
		symbolVtx = next;
	}

	//read off the automaton edges from the graph
	for (int v = towerVertices.first; v < towerVertices.second; ++v) {
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
	assert(a.accept_size() == accept_size);

	a.prepareForEquals();
	SG_FREE(canon);
}

#endif /* CANONICALIZE_HPP */

