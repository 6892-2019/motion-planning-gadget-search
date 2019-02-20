/*
 * File:   database.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on February 20, 2019, 12:36 AM
 */

#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <pqxx/pqxx>

using transaction = pqxx::transaction<pqxx::serializable>;
using ro_transaction = pqxx::transaction<pqxx::serializable, pqxx::read_only>;

struct retry_failed_exception : public std::exception {
	retry_failed_exception(std::string&& msg, std::vector<std::exception_ptr>&& v) :
			std::exception(), message(std::move(msg)), causes(std::move(v)) {}
	//Technically we shouldn't use string or vector because they might throw
	//when copied, and std::exceptions shouldn't.
	std::string message;
	std::vector<std::exception_ptr> causes;
	const char* what() const noexcept override {
		return message.c_str();
	}
};

void retry_db_operation0(void(*delegate)(void*), void* context, unsigned int attempts = 10, std::string_view identifier = "<unnamed>");

/**
 * Retries a database operation if transient errors occur (such as serialization
 * failures).
 *
 * pqxx::perform will retry things like a prepared statement getting the wrong
 * number of parameters, which is clearly a logic error, so we need our own
 * retry loop.
 */
template<class Callback>
auto retry_db_operation(Callback&& callback, unsigned int attempts = 10, std::string_view identifier = "<unnamed>") {
	using context_pair = std::pair<Callback*, std::optional<decltype(callback())>>;
	context_pair context;
	context.first = &callback;
	auto delegate = [](void* context) -> void {
		context_pair* ctx = reinterpret_cast<context_pair*>(context);
		ctx->second = (*ctx->first)();
	};
	retry_db_operation0(+delegate, &context, attempts, identifier);
	return std::move(*context.second);
}

#endif /* DATABASE_HPP */

