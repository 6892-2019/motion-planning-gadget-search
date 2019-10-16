#ifndef RANDUTILS_HPP
#define RANDUTILS_HPP

#include <sys/random.h>

//despite the name, should also work for std::array
//could add an overload taking a T&, to which this one would delegate
template<typename T>
T get_random_integer() {
	T ret;
	ssize_t rc = getrandom(&ret, sizeof(ret), 0);
	if (rc != sizeof(ret)) {
		auto savederrno = errno;
		throw std::runtime_error(fmt::format("getrandom failed: asked for {} bytes ({}), got {}: {} ({})",
				sizeof(ret), typeid(ret).name(), rc, strerror(savederrno), savederrno));
	}
	return ret;
}

#endif /* RANDUTILS_HPP */

