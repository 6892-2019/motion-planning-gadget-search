/*
 * File:   rpc.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on February 26, 2019, 12:25 AM
 */

#ifndef RPC_HPP
#define RPC_HPP

#include "msgpack.hpp"

template<typename T>
struct callable_traits : callable_traits<decltype(&T::operator())> {};

template<typename C, typename R, typename... Args>
struct callable_traits<R(C::*)(Args...)> : callable_traits<R(*)(Args...)> {};

template<typename C, typename R, typename... Args>
struct callable_traits<R(C::*)(Args...) const> : callable_traits<R(*)(Args...)> {};

template<typename R, typename... Args>
struct callable_traits<R(*)(Args...)> {
    using result_type = R;
    using arity = std::integral_constant<std::size_t, sizeof...(Args)>;
    using args_type = std::tuple<typename std::decay<Args>::type...>;
	template<std::size_t idx>
	using arg = typename std::tuple_element<idx, args_type>::type;
};



struct bad_arity {size_t expected, actual;};
template<auto f>
msgpack::object_handle handler_adapter(const msgpack::object& arg_array) {
	using args_type = typename callable_traits<decltype(f)>::args_type;
	if (arg_array.via.array.size != std::tuple_size<args_type>::value)
		throw bad_arity{std::tuple_size<args_type>::value, arg_array.via.array.size};
	auto zone = std::make_unique<msgpack::zone>();
	msgpack::object result(std::apply(f, arg_array.as<args_type>()), *zone);
	return {std::move(result), std::move(zone)};
}

using handler_ptr = msgpack::object_handle(*)(const msgpack::object&);



//these seem to be internal-only
//msgpack::sbuffer pack_success(uint32_t seq_no, const msgpack::object result);
//msgpack::sbuffer pack_error(uint32_t seq_no, const std::string& error);

msgpack::sbuffer dispatch(msgpack::object_handle hcmd,
		const std::pair<std::string_view, handler_ptr>* handlers_begin,
		const std::pair<std::string_view, handler_ptr>* handlers_end);
msgpack::sbuffer dispatch(const std::byte* data_begin, const std::byte* data_end,
		const std::pair<std::string_view, handler_ptr>* handlers_begin,
		const std::pair<std::string_view, handler_ptr>* handlers_end);
msgpack::sbuffer dispatch(const std::byte* data_begin, const std::size_t data_length,
		const std::pair<std::string_view, handler_ptr>* handlers_begin,
		const std::pair<std::string_view, handler_ptr>* handlers_end);



template<typename ...Args>
msgpack::sbuffer pack_call(uint32_t seq_no, std::string_view command, Args&& ...args) {
	msgpack::sbuffer buffer;
	using args_type = std::tuple<Args...>;
	std::tuple<uint8_t, uint32_t, std::string, args_type>
			call(0, seq_no, command, args_type(std::forward<Args&&>(args)...));
	msgpack::pack(buffer, call);
	return buffer;
}

class Response;

Response unpack_response(const std::byte* data, std::size_t len);

class Response {
public:
	explicit operator bool() const {
		return error_.type == msgpack::type::NIL;
	}
	template<class T = std::string>
	T error_as() const {
		if (*this) throw std::logic_error(fmt::format("response {} wasn't an error", seq_no_));
		return error_.as<T>();
	}
	template<class T>
	T result_as() const {
		if (!*this) throw response_was_error(typeid(T).name());
		return result_.as<T>();
	}
private:
	Response(uint32_t seq, msgpack::object error, msgpack::object result, std::unique_ptr<msgpack::zone>&& zone) :
			seq_no_(seq), error_(error), result_(result), zone_(std::move(zone)) {}
	uint32_t seq_no_;
	msgpack::object error_, result_; //one is nil, the other holds a value
	std::unique_ptr<msgpack::zone> zone_; //object_handle uniquely owns a zone, so can't use it here
	friend Response unpack_response(const std::byte* data, std::size_t len);

	std::logic_error response_was_error(const char* target_typename) const;
};

#endif /* RPC_HPP */

