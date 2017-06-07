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

//can't use the alias template in an explicit instantiation
AUTOMATA_EXTERN_TEMPLATE template class automaton::impl::bitset<typename boost::uint_t<2>::least, 2>;
AUTOMATA_EXTERN_TEMPLATE template class automaton::impl::bitset<typename boost::uint_t<3>::least, 3>;
AUTOMATA_EXTERN_TEMPLATE template class automaton::impl::bitset<typename boost::uint_t<4>::least, 4>;
AUTOMATA_EXTERN_TEMPLATE template class automaton::impl::bitset<typename boost::uint_t<5>::least, 5>;
AUTOMATA_EXTERN_TEMPLATE template class automaton::impl::bitset<typename boost::uint_t<6>::least, 6>;
AUTOMATA_EXTERN_TEMPLATE template class automaton::impl::bitset<typename boost::uint_t<7>::least, 7>;
AUTOMATA_EXTERN_TEMPLATE template class automaton::impl::bitset<typename boost::uint_t<8>::least, 8>;
AUTOMATA_EXTERN_TEMPLATE template class automaton::impl::bitset<typename boost::uint_t<9>::least, 9>;
AUTOMATA_EXTERN_TEMPLATE template class automaton::impl::bitset<typename boost::uint_t<10>::least, 10>;
AUTOMATA_EXTERN_TEMPLATE template class automaton::impl::bitset<typename boost::uint_t<11>::least, 11>;
AUTOMATA_EXTERN_TEMPLATE template class automaton::impl::bitset<typename boost::uint_t<12>::least, 12>;
AUTOMATA_EXTERN_TEMPLATE template class automaton::impl::bitset<typename boost::uint_t<13>::least, 13>;
AUTOMATA_EXTERN_TEMPLATE template class automaton::impl::bitset<typename boost::uint_t<14>::least, 14>;
AUTOMATA_EXTERN_TEMPLATE template class automaton::impl::bitset<typename boost::uint_t<15>::least, 15>;
AUTOMATA_EXTERN_TEMPLATE template class automaton::impl::bitset<typename boost::uint_t<16>::least, 16>;

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

AUTOMATA_EXTERN_TEMPLATE template class circular_deque<unsigned int, 16>;
AUTOMATA_EXTERN_TEMPLATE template class circular_deque<std::pair<unsigned int, unsigned int>, 32>;
AUTOMATA_EXTERN_TEMPLATE template class circular_deque<std::optional<unsigned int>, 16>;
AUTOMATA_EXTERN_TEMPLATE template class circular_deque<std::tuple<unsigned int, unsigned int, unsigned int>, 16>;
AUTOMATA_EXTERN_TEMPLATE template class circular_deque<std::tuple<unsigned int, unsigned int, unsigned int, bool>, 16>;

//TODO: <algorithm> and following