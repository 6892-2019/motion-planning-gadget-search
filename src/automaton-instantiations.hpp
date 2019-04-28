//NO INCLUDE GUARD

AUTOMATON_EXTERN_TEMPLATE template class Automaton<AUTOMATON_SIZE>;
//TODO: there are const unsigned int* instantiations of these too, try to merge them
AUTOMATON_EXTERN_TEMPLATE template void Automaton<AUTOMATON_SIZE>::renumberAlphabet<unsigned int*>(unsigned int*);
AUTOMATON_EXTERN_TEMPLATE template void Automaton<AUTOMATON_SIZE>::renumberAlphabet<unsigned int*>(
		typename Automaton<AUTOMATON_SIZE>::state_type, typename Automaton<AUTOMATON_SIZE>::state_type, unsigned int*);
AUTOMATON_EXTERN_TEMPLATE template void Automaton<AUTOMATON_SIZE>::permuteAlphabet<unsigned int*>(unsigned int*);
AUTOMATON_EXTERN_TEMPLATE template void Automaton<AUTOMATON_SIZE>::renumberStates<unsigned int*>(unsigned int*);
//AUTOMATON_EXTERN_TEMPLATE template Automaton<AUTOMATON_SIZE> conj<AUTOMATON_SIZE>(
//		const Automaton<AUTOMATON_SIZE>& left, const Automaton<AUTOMATON_SIZE>& right);

#undef AUTOMATON_SIZE
//don't undefine AUTOMATON_EXTERN_TEMPLATE