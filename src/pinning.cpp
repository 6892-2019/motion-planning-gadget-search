#include "precompiled.hpp"
#include <sched.h>

void pin_this_thread(int core_number) {
	cpu_set_t cpus;
	CPU_ZERO(&cpus);
	CPU_SET(core_number, &cpus);
	sched_setaffinity(0, sizeof(cpus), &cpus);
}