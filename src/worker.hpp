#ifndef WORKER_HPP
#define WORKER_HPP

#include <functional>
#include "bounded_queue.hpp"

void worker_thread(int core_number, bounded_queue<std::function<void()>>& queue);

#endif /* WORKER_HPP */

