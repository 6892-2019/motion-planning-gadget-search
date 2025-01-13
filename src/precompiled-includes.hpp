// SPDX-License-Identifier: MIT
// Copyright 2017 Massachusetts Institute of Technology
// Copyright 2025 Jeffrey Bosboom
//The #include part of the precompiled header.
#include <array>
#include <vector>
#include <boost/circular_buffer.hpp>
#include <tsl/hopscotch_set.h>
#include <tsl/hopscotch_map.h>
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
#include "circular_deque.hpp"

#include <algorithm>
#include <random>
#include <boost/range/iterator_range_core.hpp>
#include <boost/range/irange.hpp>
#include "algoutils.hpp"

#include <iostream>
#include <iomanip>

#include <memory>
#include <utility>
#include <type_traits>
#include <boost/integer.hpp>
#include "numutils.hpp"
#include <boost/functional/hash.hpp> //for std::pair/std::tuple hashing
#include "hashutils.hpp"

#include <charconv>

#include <exception>
#include <cassert>

#include <chrono>
#include <sys/resource.h>

#include <fmt/format.h>
#include <fmt/ranges.h>
