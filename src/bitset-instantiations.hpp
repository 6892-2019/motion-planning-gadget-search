//NO INCLUDE GUARD

#include <boost/preprocessor/repetition/repeat_from_to.hpp>
#define INSTANTIATE_BITSET(z, size, unused_data_parameter) \
	BITSET_EXTERN_TEMPLATE template class automaton::impl::bitset<typename boost::uint_t<size>::least, size>;
//// ^ we can't use the alias template in an explicit instantiation
BOOST_PP_REPEAT_FROM_TO(1, 17, INSTANTIATE_BITSET, unused_data_parameter)
INSTANTIATE_BITSET(z, 64, unused_data_parameter)
#undef INSTANTIATE_BITSET
#undef BITSET_EXTERN_TEMPLATE
