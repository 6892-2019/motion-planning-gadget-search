#include "precompiled.hpp"
#include "database.hpp"
#include "stringutils.hpp"

using std::vector;

std::string format_connect_string(std::string_view user, std::string_view pass,
		std::string_view address, std::string_view port, std::string_view database) {
	return fmt::format("postgresql://{}:{}@{}:{}/{}", user, pass, address, port, database);
}


ConnectionLease::~ConnectionLease() {
	if (conn_)
		pool_->checkin(std::move(*this));
}

ConnectionPool::ConnectionPool(std::string queryString, unsigned int maxConnections)
		: query_string_(std::move(queryString)), outstanding_(0), max_(maxConnections) {
	if (maxConnections < 1)
		throw std::logic_error("bad maxConnections");
}

ConnectionLease ConnectionPool::checkout() {
	{
		std::lock_guard lock(mutex_);
		if (conns_.empty() && outstanding_ == max_)
			throw std::runtime_error(fmt::format("too many connections, max {}", max_));
		if (!conns_.empty()) {
			ConnectionLease lease(std::move(conns_.back()), this);
			conns_.pop_back();
			++outstanding_;
			return std::move(lease);
		} else
			//We're going to make a new connection and immediately return it to
			//the caller, so we note it's now outstanding.
			++outstanding_;
	}

	//Make a new connection.  We've released the lock so other connections
	//can be checked back in (or more checkouts can occur in parallel).
	//Having incremented outstanding_, we know the setup funcs can't change
	//out from underneath us.
	std::unique_ptr<pqxx::connection> c = std::make_unique<pqxx::connection>(query_string_);
	for (const std::pair<std::string, std::function<void(pqxx::connection&)>>& f : conn_setup_funcs_)
		f.second(*c);
	return {std::move(c), this};
}

void ConnectionPool::checkin(ConnectionLease&& lease) {
	std::lock_guard lock(mutex_);
	assert(outstanding_);
	conns_.push_back(std::move(lease.conn_));
	--outstanding_;
}

unsigned int ConnectionPool::quiesce() {
	std::lock_guard lock(mutex_);
	while (!conns_.empty()) {
		conns_.back().reset();
		conns_.pop_back();
	}
	return numeric_cast<unsigned int>(outstanding_);
}

void ConnectionPool::register_setup(const std::string& key, std::function<void(pqxx::connection&)> func) {
	if (!func)
		throw std::logic_error(fmt::format("registering an empty function under key {}", key));

	std::lock_guard lock(mutex_);
	if (outstanding_)
		throw std::logic_error(fmt::format("registering a setup function under key {} but {} connections outstanding",
				key, outstanding_));
	for (const std::pair<std::string, std::function<void(pqxx::connection&)>>& f : conn_setup_funcs_)
		if (f.first == key)
			return;

	//We're calling while holding the mutex, which isn't great.  We could
	//check all the connections out to ourselves, release the lock, call,
	//then check them back in.
	for (std::unique_ptr<pqxx::connection>& p : conns_)
		func(*p);
	conn_setup_funcs_.emplace_back(key, std::move(func));
}

unsigned int ConnectionPool::size() const {
	std::lock_guard lock(mutex_);
	return numeric_cast<unsigned int>(max_ - outstanding_);
}
unsigned int ConnectionPool::capacity() const {
	return numeric_cast<unsigned int>(max_);
}



void retry_db_operation0(void(*delegate)(void*), void* context, unsigned int attempts, std::string_view identifier) {
	assert(attempts);
	vector<std::exception_ptr> suppressed;
	//Ideally we'd wait to format these messages, but std::exception_ptr erases
	//the exception type (in fact, we can't even dereference it).
	vector<std::string> messages;

	auto record_message = [&](unsigned int i, const char* type, const char* what) {
		messages.push_back(fmt::format("  attempt {} got {}: {}", i, type, what));
	};
	auto record_stderr = [&](unsigned int i, const char* type, const char* what) {
		fmt::print(stderr, "attempt {} of {} at {} got {}: {}\n",
				i, attempts, identifier, type, what);
	};

	for (unsigned int i = 0; i < attempts; ++i) {
		try {
			return (*delegate)(context);
		} catch (const pqxx::serialization_failure& e) {
			record_message(i, "serialization_failure", e.what());
			record_stderr(i, "serialization_failure", e.what());
			suppressed.push_back(std::current_exception());
		} catch (const pqxx::deadlock_detected& e) {
			record_message(i, "deadlock_detected", e.what());
			record_stderr(i, "deadlock_detected", e.what());
			suppressed.push_back(std::current_exception());
		} catch (const pqxx::broken_connection& e) {
			//It seems a bit odd to retry this, but pqxx::perform does, and if
			//the transaction rolled back, it's safe to try again.
			record_message(i, "broken_connection", e.what());
			record_stderr(i, "broken_connection", e.what());
			suppressed.push_back(std::current_exception());
		} catch (const std::exception& e) {
			//All non-whitelisted errors lead to termination, but we also tack
			//on information from any suppressed exceptions from previous attempts.
			record_message(i, typeid(e).name(), e.what());
			record_stderr(i, "broken_connection", e.what());
			suppressed.push_back(std::current_exception());
			break; //common code path with attempts-exhausted
		} catch (...) {
			record_message(i, "[unknown exception]", "(what not available)");
			record_stderr(i, "[unknown exception]", "(what not available)");
			suppressed.push_back(std::current_exception());
			break; //common code path with attempts-exhausted
		}
	}

	if (suppressed.size() == 1) //if we failed immediately
		std::rethrow_exception(suppressed.front());
	std::string message = fmt::format("{} failed after {} of {} attempts:\n{}",
			identifier, suppressed.size(), attempts,
			join(messages, "\n"));
	//I think std::nested_exception could be used to preserve the fatal
	//exception's type if that was important.
	throw retry_failed_exception(std::move(message), std::move(suppressed));
}