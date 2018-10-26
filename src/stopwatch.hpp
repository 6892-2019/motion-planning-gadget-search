/*
 * File:   stopwatch.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on October 4, 2018, 6:22 PM
 */

#ifndef AUTOMATON_STOPWATCH_HPP_INCLUDED
#define AUTOMATON_STOPWATCH_HPP_INCLUDED

#include <chrono>
#include <string>
#include <sys/resource.h>

class Stopwatch {
private:
	//https://stackoverflow.com/a/37440647/3614835
	using best_clock = std::conditional_t<std::chrono::high_resolution_clock::is_steady,
			std::chrono::high_resolution_clock,
			std::chrono::steady_clock>;
	/**
	 * Approximates the time of process start, to be used as the base time in
	 * Result::aboslute().  Static-initialized to best_clock::now();
	 */
	const static best_clock::time_point beginning_of_time;
	struct StopwatchData {
		StopwatchData(int getrusage_who);
		StopwatchData(best_clock::time_point t, rusage r);
		best_clock::time_point time;
		rusage usage;
	};
public:
	class Result {
	public:
		Result(StopwatchData start, StopwatchData end);

		unsigned long seconds() const;
		unsigned long millis() const;
		unsigned long micros() const;
		unsigned long nanos() const;
		std::string hms() const;

		unsigned long userSeconds() const;
		unsigned long userMillis() const;
		unsigned long userMicros() const;
		unsigned long userNanos() const;
		unsigned long systemSeconds() const;
		unsigned long systemMillis() const;
		unsigned long systemMicros() const;
		unsigned long systemNanos() const;
		unsigned long cpuSeconds() const;
		unsigned long cpuMillis() const;
		unsigned long cpuMicros() const;
		unsigned long cpuNanos() const;

		double utilization() const;

		unsigned long highwaterBytes() const;
		double highwaterGibibytes() const;

		unsigned long faults() const;
		unsigned long softFaults() const;
		unsigned long hardFaults() const;

		unsigned long switches() const;
		unsigned long voluntarySwitches() const;
		unsigned long involuntarySwitches() const;

		/**
		 * Returns a Result holding absolute values at the time elapsed() was
		 * called (instead of a delta between elapsed() and the Stopwatch's
		 * construction).
		 */
		Result absolute() const;
	private:
		StopwatchData start_, end_;

		template<class Duration>
		Duration elapsed() const;
		template<class Duration>
		Duration userTime() const;
		template<class Duration>
		Duration systemTime() const;
		template<class Duration>
		Duration cpuTime() const;
	};

	/**
	 * Returns a Stopwatch providing process-level statistics.
	 */
	static Stopwatch process();
	/**
	 * Returns a Stopwatch providing thread-level statistics.  The returned
	 * Stopwatch has thread affinity, of course.
	 */
	static Stopwatch thread();
	/**
	 * Resets the start point.
	 */
	void reset();
	/**
	 * Returns a Result describing the elapsed time and other metrics.  Doesn't
	 * modify this Stopwatch, so can be called repeatedly to measure from the
	 * same start point.
	 */
	Result elapsed() const;
private:
	Stopwatch(int getrusage_who);
	StopwatchData data_;
	int getrusage_who_;
};

#endif /* AUTOMATON_STOPWATCH_HPP_INCLUDED */

