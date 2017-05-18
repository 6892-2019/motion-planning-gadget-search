#ifndef NUMUTILS_HPP
#define NUMUTILS_HPP

#include <boost/numeric/conversion/converter.hpp>
#include <cassert>

namespace detail {
	struct assert_on_overflow {
		void operator() (boost::numeric::range_check_result r) {
			if (r != boost::numeric::range_check_result::cInRange)
				assert(false && "numeric cast problem");
		}
	};
}

template<typename Target, typename Source>
Target numeric_cast(Source src) noexcept {
	using converter = boost::numeric::converter<Target, Source,
			boost::numeric::conversion_traits<Target, Source>, //the default
			detail::assert_on_overflow
			>;
	return converter::convert(std::forward<Source>(src));
}

#endif /* NUMUTILS_HPP */

