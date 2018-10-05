#include "precompiled.hpp"
#include "stopwatch.hpp"

using std::chrono::duration_cast;

static std::chrono::microseconds from_timeval(const timeval& tv) {
	return std::chrono::seconds(tv.tv_sec) + std::chrono::microseconds(tv.tv_usec);
}

const Stopwatch::best_clock::time_point Stopwatch::beginning_of_time = best_clock::now();

Stopwatch::Stopwatch(int getrusage_who) : data_(getrusage_who), getrusage_who_(getrusage_who) {}

auto Stopwatch::Stopwatch::process() -> Stopwatch {
	return Stopwatch(RUSAGE_SELF);
}
auto Stopwatch::Stopwatch::thread() -> Stopwatch {
	return Stopwatch(RUSAGE_THREAD);
}

void Stopwatch::reset() {
	data_ = StopwatchData(getrusage_who_);
}

Stopwatch::Result Stopwatch::elapsed() const {
	//Imply to the compiler that it should make the system calls ASAP.
	auto end = StopwatchData(getrusage_who_);
	return {data_, end};
}


Stopwatch::StopwatchData::StopwatchData(int getrusage_who) : time(best_clock::now()) {
	usage = {};
	getrusage(getrusage_who, &usage);
}
Stopwatch::StopwatchData::StopwatchData(best_clock::time_point t, rusage r) : time(t), usage(r) {}


Stopwatch::Result::Result(StopwatchData start, StopwatchData end) : start_(start), end_(end) {}

template<class Duration>
Duration Stopwatch::Result::elapsed() const {
	return duration_cast<Duration>(end_.time - start_.time);
}
unsigned long Stopwatch::Result::seconds() const {
	return elapsed<std::chrono::seconds>().count();
}
unsigned long Stopwatch::Result::millis() const {
	return elapsed<std::chrono::milliseconds>().count();
}
unsigned long Stopwatch::Result::micros() const {
	return elapsed<std::chrono::microseconds>().count();
}
unsigned long Stopwatch::Result::nanos() const {
	return elapsed<std::chrono::nanoseconds>().count();
}
std::string Stopwatch::Result::hms() const {
	auto diff = end_.time - start_.time;
	auto hours = duration_cast<std::chrono::hours>(diff);
	auto minutes = duration_cast<std::chrono::minutes>(diff) - hours;
	auto seconds = duration_cast<std::chrono::seconds>(diff) - hours - minutes;
	return std::to_string(hours.count()) + "h" + std::to_string(minutes.count()) + "m" + std::to_string(seconds.count()) + "s";
}

template<class Duration>
Duration Stopwatch::Result::userTime() const {
	return std::chrono::duration_cast<Duration>(from_timeval(end_.usage.ru_utime) - from_timeval(start_.usage.ru_utime));
}
unsigned long Stopwatch::Result::userSeconds() const {
	return userTime<std::chrono::seconds>().count();
}
unsigned long Stopwatch::Result::userMillis() const {
	return userTime<std::chrono::milliseconds>().count();
}
unsigned long Stopwatch::Result::userMicros() const {
	return userTime<std::chrono::microseconds>().count();
}
unsigned long Stopwatch::Result::userNanos() const {
	return userTime<std::chrono::nanoseconds>().count();
}
template<class Duration>
Duration Stopwatch::Result::systemTime() const {
	return duration_cast<Duration>(from_timeval(end_.usage.ru_stime) - from_timeval(start_.usage.ru_stime));
}
unsigned long Stopwatch::Result::systemSeconds() const {
	return systemTime<std::chrono::seconds>().count();
}
unsigned long Stopwatch::Result::systemMillis() const {
	return systemTime<std::chrono::milliseconds>().count();
}
unsigned long Stopwatch::Result::systemMicros() const {
	return systemTime<std::chrono::microseconds>().count();
}
unsigned long Stopwatch::Result::systemNanos() const {
	return systemTime<std::chrono::nanoseconds>().count();
}
template<class Duration>
Duration Stopwatch::Result::cpuTime() const {
	return userTime<Duration>() + systemTime<Duration>();
}
unsigned long Stopwatch::Result::cpuSeconds() const {
	return cpuTime<std::chrono::seconds>().count();
}
unsigned long Stopwatch::Result::cpuMillis() const {
	return cpuTime<std::chrono::milliseconds>().count();
}
unsigned long Stopwatch::Result::cpuMicros() const {
	return cpuTime<std::chrono::microseconds>().count();
}
unsigned long Stopwatch::Result::cpuNanos() const {
	return cpuTime<std::chrono::nanoseconds>().count();
}

double Stopwatch::Result::utilization() const {
	//We can tolerate the potential loss of precision here.
	return static_cast<double>(cpuNanos()) / static_cast<double>(nanos());
}

unsigned long Stopwatch::Result::highwaterBytes() const {
	return (end_.usage.ru_maxrss - start_.usage.ru_maxrss) * 1024;
}
double Stopwatch::Result::highwaterGibibytes() const {
	return (double)(end_.usage.ru_maxrss - start_.usage.ru_maxrss) / (1024*1024);
}

unsigned long Stopwatch::Result::faults() const {
	return softFaults() + hardFaults();
}
unsigned long Stopwatch::Result::softFaults() const {
	return end_.usage.ru_minflt - start_.usage.ru_minflt;
}
unsigned long Stopwatch::Result::hardFaults() const {
	return end_.usage.ru_majflt - start_.usage.ru_majflt;
}

unsigned long Stopwatch::Result::switches() const {
	return voluntarySwitches() + involuntarySwitches();
}
unsigned long Stopwatch::Result::voluntarySwitches() const {
	return end_.usage.ru_nvcsw - start_.usage.ru_nvcsw;
}
unsigned long Stopwatch::Result::involuntarySwitches() const {
	return end_.usage.ru_nivcsw - start_.usage.ru_nivcsw;
}

Stopwatch::Result Stopwatch::Result::absolute() const {
	rusage zero_usage = {};
	return {StopwatchData(Stopwatch::beginning_of_time, zero_usage), end_};
}