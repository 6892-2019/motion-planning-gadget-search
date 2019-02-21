#include "precompiled.hpp"
#include "database.hpp"
#include "stringutils.hpp"

using std::vector;

std::string format_connect_string(std::string_view user, std::string_view pass,
		std::string_view address, std::string_view port, std::string_view database) {
	return fmt::format("postgresql://{}:{}@{}:{}/{}", user, pass, address, port, database);
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