//If AUTOMATA_EXTERN_TEMPLATE is defined, these are extern template dclarations.
//Otherwise, they are explicit instantiations.  This way we don't have separate
//lists to keep in sync.

//Various string and/or iostream instantiations are already handled by libstdc++.

AUTOMATA_EXTERN_TEMPLATE template class std::vector<unsigned int>;
AUTOMATA_EXTERN_TEMPLATE template class std::vector<std::string>;
AUTOMATA_EXTERN_TEMPLATE template class std::vector<std::pair<std::size_t, std::size_t>>;
AUTOMATA_EXTERN_TEMPLATE template class std::vector<bool>;
AUTOMATA_EXTERN_TEMPLATE template class std::vector<std::vector<bool>>;
AUTOMATA_EXTERN_TEMPLATE template class std::vector<std::uint8_t>;
AUTOMATA_EXTERN_TEMPLATE template class std::vector<std::pair<unsigned int, unsigned int>>;

AUTOMATA_EXTERN_TEMPLATE template class std::unordered_set<unsigned int>;
AUTOMATA_EXTERN_TEMPLATE template class std::unordered_set<std::size_t>;

AUTOMATA_EXTERN_TEMPLATE template class std::unordered_map<linear_set<unsigned int>, unsigned int>;
AUTOMATA_EXTERN_TEMPLATE template class std::unordered_map<std::pair<unsigned int, unsigned int>, unsigned int, boost::hash<std::pair<unsigned int, unsigned int>>>;

AUTOMATA_EXTERN_TEMPLATE template class std::tuple<unsigned int, unsigned int, bool>;

AUTOMATA_EXTERN_TEMPLATE template class boost::dynamic_bitset<std::size_t>;

AUTOMATA_EXTERN_TEMPLATE template class google::dense_hash_map<std::pair<unsigned int, unsigned int>, unsigned int, boost::hash<std::pair<unsigned int, unsigned int>>>;
AUTOMATA_EXTERN_TEMPLATE template class google::dense_hash_map<std::tuple<unsigned int, unsigned int, bool>, unsigned int, boost::hash<std::tuple<unsigned int, unsigned int, bool>>>;

AUTOMATA_EXTERN_TEMPLATE template class google::dense_hash_set<unsigned int>;

AUTOMATA_EXTERN_TEMPLATE template class google::sparse_hash_map<std::pair<unsigned int, unsigned int>, unsigned int, boost::hash<std::pair<unsigned int, unsigned int>>>;

#include <boost/preprocessor/repetition/repeat_from_to.hpp>
#define INSTANTIATE_BITSET(z, size, unused_data_parameter) \
	AUTOMATA_EXTERN_TEMPLATE template class automaton::impl::bitset<typename boost::uint_t<size>::least, size>;
// ^ we can't use the alias template in an explicit instantiation
BOOST_PP_REPEAT_FROM_TO(2, 17, INSTANTIATE_BITSET, unused_data_parameter)
#undef INSTANTIATE_BITSET

AUTOMATA_EXTERN_TEMPLATE template class natural_map<unsigned char, unsigned char>;
AUTOMATA_EXTERN_TEMPLATE template class natural_map<unsigned short, unsigned short>;
AUTOMATA_EXTERN_TEMPLATE template class natural_map<unsigned int, unsigned int>;
AUTOMATA_EXTERN_TEMPLATE template class natural_map<unsigned long, unsigned long>;
AUTOMATA_EXTERN_TEMPLATE template class natural_map<unsigned long long, unsigned long long>;
AUTOMATA_EXTERN_TEMPLATE template class natural_map<std::pair<unsigned int, unsigned int>, unsigned int>;

AUTOMATA_EXTERN_TEMPLATE template class dynarray<int>;
AUTOMATA_EXTERN_TEMPLATE template class dynarray<unsigned int>;
AUTOMATA_EXTERN_TEMPLATE template class dynarray<std::size_t>;
AUTOMATA_EXTERN_TEMPLATE template class dynarray<std::pair<unsigned int, unsigned int>>;
//TODO: dynarray<bitset> (symbol_mask_type)

AUTOMATA_EXTERN_TEMPLATE template class linear_set<unsigned int>;

AUTOMATA_EXTERN_TEMPLATE template class bounded_queue<std::function<void()>>;

#include <boost/preprocessor/seq/for_each.hpp>
//https://stackoverflow.com/a/35999754/3614835
#define UNPACK_COMMA_TYPE( ... ) __VA_ARGS__
#define INSTANTIATE_CIRCULAR_DEQUE_SZ(r, type, size) \
	AUTOMATA_EXTERN_TEMPLATE template class circular_deque<UNPACK_COMMA_TYPE type, size>;
#define INSTANTIATE_CIRCULAR_DEQUE(type, sizes) \
	AUTOMATA_EXTERN_TEMPLATE template class circular_deque_base<UNPACK_COMMA_TYPE type>; \
	BOOST_PP_SEQ_FOR_EACH(INSTANTIATE_CIRCULAR_DEQUE_SZ, type, sizes)

INSTANTIATE_CIRCULAR_DEQUE((unsigned int), (16)(32))
INSTANTIATE_CIRCULAR_DEQUE((std::optional<unsigned int>), (16))
INSTANTIATE_CIRCULAR_DEQUE((std::pair<unsigned int, unsigned int>), (16)(32))
INSTANTIATE_CIRCULAR_DEQUE((std::pair<unsigned int*, unsigned int>), (16))
INSTANTIATE_CIRCULAR_DEQUE((std::tuple<unsigned int, unsigned int, unsigned int>), (16))
INSTANTIATE_CIRCULAR_DEQUE((std::tuple<unsigned int, unsigned int, unsigned int, bool>), (16))

#undef INSTANTIATE_CIRCULAR_DEQUE_SZ
#undef INSTANTIATE_CIRCULAR_DEQUE
#undef UNPACK_COMMA_TYPE

//TODO: <algorithm> and following