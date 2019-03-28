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

std::string format_connect_string(std::string_view user, std::string_view pass,
		std::string_view address, std::string_view port, std::string_view database);

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

//works for any tuple, including std::pair and std::array
//user types could opt in by becoming tuples, I guess
//inv might be a prepared or parameterized invocation
//no reason to perfect-forward inv, it'll always be a &
template<class Callable, class Tuple, int = std::tuple_size<std::decay_t<Tuple>>::value>
void database_invoke_apply(Callable& inv, Tuple&& t) {
	std::apply([&inv](auto&&... args) {
		(inv(std::forward<decltype(args)>(args)), ...);
	}, std::forward<Tuple>(t));
}

template<class Record>
void batch_parameterized(pqxx::connection& conn, transaction& trans, std::string(*query_ctor)(std::size_t),
		std::size_t maximum_batch_size,	std::vector<Record>&& records) {
	//Currently this function just uses repeated parameterized statements, but
	//if we'll do multiple maximum batches we could prepare those.  We could also
	//try to find an optimal batch size: if we end up with 4 full batches and a
	//runt, and records.size() is divisible by 5, we can use smaller batches to
	//reuse the prepared query an additional time without an additional round-trip.

	//We don't need it now, but we could return a vector of the pqxx::result
	//objects (it's safe if they outlive their transaction), or take a callable
	//that processes them immediately (lengthening the transaction but reducing
	//peak memory consumption).

	std::size_t batch_base = 0;
	while (batch_base < records.size()) {
		std::size_t batch_size = std::min(maximum_batch_size, records.size() - batch_base);
		//If formatting the query string turns out to be expensive we could try
		//to cache the previous size.
		pqxx::internal::parameterized_invocation inv = trans.parameterized(query_ctor(batch_size));
		for (std::size_t i = 0; i < batch_size; ++i)
			database_invoke_apply(inv, records[batch_base+i]);
		inv.exec();
		batch_base += batch_size;
	}
}

/**
 * Execute a batch parameterized query (using multiple batches if necessary).
 * @param conn a connection
 * @param query_ctor a function constructing queries for a given batch size
 * @param maximum_batch_size the max batch size (for staying under parameter count limits)
 * @param records a vector of records to be submitted using database_invoke_apply
 * @param operation_name the name to pass to retry_db_operation
 */
template<class Record>
void batch_parameterized(pqxx::connection& conn, std::string(*query_ctor)(std::size_t),
		std::size_t maximum_batch_size,	std::vector<Record>&& records, std::string_view operation_name) {
	retry_db_operation([&]() {
		transaction trans(conn);
		batch_parameterized(conn, trans, query_ctor, maximum_batch_size, std::move(records));
		trans.commit();
		return nullptr;
	}, 10, operation_name);
}

#endif /* DATABASE_HPP */

