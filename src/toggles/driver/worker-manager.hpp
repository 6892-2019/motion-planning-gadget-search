#ifndef WORKER_MANAGER_HPP
#define WORKER_MANAGER_HPP

#include "../rpc.hpp"
#include <experimental/propagate_const>

struct WorkGenerator {
	virtual bool next(simple_buffer& buffer) = 0;
	//caller should unpack into rpc Response object and use sequence number to
	//match with produced work
	virtual void process(simple_buffer& response) = 0;
};

struct DelegateGenerator : WorkGenerator {
	DelegateGenerator(std::function<bool(simple_buffer&)> next, std::function<void(simple_buffer&)> process)
			: next_(std::move(next)), process_(std::move(process)) {}
	bool next(simple_buffer& buffer) override {return next_(buffer);}
	void process(simple_buffer& response) override {return process_(response);}
private:
	std::function<bool(simple_buffer&)> next_;
	std::function<void(simple_buffer&)> process_;
};

class WorkerManager {
	class Impl;
	std::experimental::propagate_const<std::unique_ptr<Impl>> impl_;
public:
	WorkerManager(const std::vector<std::string>& worker_addrs);
	//could be movable if we need it to, but not copyable
	WorkerManager(const WorkerManager&) = delete;
	WorkerManager& operator=(const WorkerManager&) = delete;
	~WorkerManager();
	std::size_t size() const;
	void run(WorkGenerator* generator);
	void run(std::function<bool(simple_buffer&)> next, std::function<void(simple_buffer&)> process);
};

#endif /* WORKER_MANAGER_HPP */

