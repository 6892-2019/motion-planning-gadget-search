#ifndef COARSE_MONOTONIC_CLOCK_HPP
#define COARSE_MONOTONIC_CLOCK_HPP

#include <chrono>
#include <time.h>
#include <errno.h>
#ifndef NDEBUG
#include <fmt/core.h>
#endif //NDEBUG

#ifndef CLOCK_MONOTONIC_COARSE
#error CLOCK_MONOTONIC_COARSE not defined (ancient library?)
#endif

class coarse_monotonic_clock {
public:
    using duration = std::chrono::nanoseconds;
    using rep = duration::rep;
    using period = duration::period;
    using time_point = std::chrono::time_point<coarse_monotonic_clock>;
    static constexpr bool is_steady = true;
    static time_point now() noexcept {
		timespec t;
		[[maybe_unused]] int rc = clock_gettime(CLOCK_MONOTONIC_COARSE, &t);
		//This should only fail if we're running on an ancient kernel, and we
		//want fast time (hence using the coarse clock in the first place), so
		//we only check in debug builds.
#ifndef NDEBUG
		if (rc) {
			int savederrno = errno;
			fmt::print(stderr, "clock_gettime(CLOCK_MONOTONIC_COARSE) failure: {} ({})\n", strerror(savederrno), savederrno);
			std::terminate();
		}
#endif //NDEBUG
		return time_point{std::chrono::seconds(t.tv_sec) + std::chrono::nanoseconds(t.tv_nsec)};
	}
};

#endif /* COARSE_MONOTONIC_CLOCK_HPP */
