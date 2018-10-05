//The #include part of the precompiled header.
#include <array>
#include <vector>
#include <boost/circular_buffer.hpp>
#include <hopscotch/hopscotch_set.h>
#include <hopscotch/hopscotch_map.h>
#include <string>
#include <string_view>
#include <tuple>
#include <optional>
#include <boost/container/small_vector.hpp>
#include <boost/dynamic_bitset.hpp>
#include <sparsehash/dense_hash_map>
#include <sparsehash/sparse_hash_map>
#include "dynarray.hpp"
#include "linear_set.hpp"
#include "bounded_queue.hpp"
#include "circular_deque.hpp"

#include <algorithm>
#include <random>
#include <boost/iterator/counting_iterator.hpp>
#include <boost/iterator/indirect_iterator.hpp>
#include <boost/range/iterator_range_core.hpp>
#include <boost/range/irange.hpp>
#include <regex>
#include "algoutils.hpp"

#include <atomic>
#include <thread>
#include <future>
#include <mutex>
#include <condition_variable>

#include <iostream>
#include <fstream>
#include <iomanip>

#include <memory>
#include <boost/intrusive_ptr.hpp>
#include <utility>
#include <type_traits>
#include <boost/integer.hpp>
#include "numutils.hpp"
#include <boost/functional/hash.hpp> //for std::pair/std::tuple hashing
#include <farmhash/farmhash.h>

#include <charconv>

#include <exception>
#include <cassert>

#include <chrono>
#include <sys/resource.h>

#include <fmt/format.h>
#include <fmt/ranges.h>