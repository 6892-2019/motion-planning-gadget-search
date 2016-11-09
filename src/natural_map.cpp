#include "natural_map.hpp"

template class natural_map<unsigned char, unsigned char>;
template class natural_map<unsigned short, unsigned short>;
template class natural_map<unsigned int, unsigned int>;
template class natural_map<unsigned long, unsigned long>;
template class natural_map<unsigned long long, unsigned long long>;

template class natural_map<std::pair<unsigned int, unsigned int>, unsigned int>;