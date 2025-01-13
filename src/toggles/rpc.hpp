// SPDX-License-Identifier: MIT
// Copyright 2019 Massachusetts Institute of Technology
#ifndef RPC_HPP
#define RPC_HPP

#include <msgpack.hpp>

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



//TODO: use jemalloc functions to get some extra capacity
/**
 * A simple owning buffer class with a size/valid-data-mark and capacity.
 */
class simple_buffer {
public:
	simple_buffer() : data_(nullptr), size_(0), capacity_(0) {}
	simple_buffer(std::size_t initial_size) : data_(std::malloc(initial_size)), size_(0), capacity_(initial_size) {}
	std::size_t size() const {return size_;}
	//this method provided for after asio reads into this buffer
	void size(std::size_t new_size) {
		if (new_size > capacity_)
			throw std::logic_error(fmt::format("setting overlarge size; old size {}, capacity {}, new size {}",
					size_, capacity_, new_size));
		size_ = new_size;
	}
	std::size_t capacity() const {return capacity_;}
	void* data() {
		return data_.get();
	}
	const void* data() const {
		return data_.get();
	}
	void grow(std::size_t min_capacity) {
		std::size_t a = std::max(capacity_, 128ul);
		while (a < min_capacity)
			//TODO: this growth policy is too aggressive for large sizes, should back down to 1.5
			a *= 2;

		void* n = std::realloc(data_.get(), a);
		if (!n)
			//data_ still owns the pointer and will free it
			throw std::bad_alloc();
		data_.release(); //realloc already freed this pointer (if moved), don't double-free it
		data_.reset(n);
		capacity_ = a;
	}
	void clear() {
		size_ = 0;
	}
	void free() {
		size_ = capacity_ = 0;
	}

	//for msgpack
	void write(const char* src, std::size_t len) {
		if (capacity_ - size_ < len)
			grow(size_ + len);
		std::copy(src, src + len, static_cast<unsigned char*>(data()) + size_);
		size_ += len;
	}
private:
	std::unique_ptr<void, free_deleter> data_;
	std::size_t size_, capacity_;
};
/**
 * Writes the contents of the buffer to a new file at the given filename, failing
 * if that file already exists.
 */
void write_buffer(const simple_buffer& buf, const std::string& filename);
/**
 * Reads the contents of the specified file into the given buffer, overwriting
 * any existing contents.
 */
void read_buffer(simple_buffer& buf, const std::string& filename);
/**
 * Load the contents of the specified file into a new buffer.
 */
simple_buffer read_buffer(const std::string& filename);



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

simple_buffer dispatch(msgpack::object_handle hcmd,
		const std::pair<std::string_view, handler_ptr>* handlers_begin,
		const std::pair<std::string_view, handler_ptr>* handlers_end);
simple_buffer dispatch(const std::byte* data_begin, const std::byte* data_end,
		const std::pair<std::string_view, handler_ptr>* handlers_begin,
		const std::pair<std::string_view, handler_ptr>* handlers_end);
simple_buffer dispatch(const std::byte* data_begin, const std::size_t data_length,
		const std::pair<std::string_view, handler_ptr>* handlers_begin,
		const std::pair<std::string_view, handler_ptr>* handlers_end);



template<typename ...Args>
[[nodiscard]] simple_buffer pack_call(uint32_t seq_no, std::string_view command, Args&& ...args) {
	simple_buffer buffer;
	pack_call(buffer, seq_no, command, std::forward<Args&&>(args)...);
	return buffer;
}
template<typename ...Args>
void pack_call(simple_buffer& buffer, uint32_t seq_no, std::string_view command, Args&& ...args) {
	using args_type = std::tuple<Args...>;
	std::tuple<uint8_t, uint32_t, std::string, args_type>
			call(0, seq_no, command, args_type(std::forward<Args&&>(args)...));
	msgpack::pack(buffer, call);
}

class Response;

Response unpack_response(const simple_buffer& buf);
Response unpack_response(const std::byte* data, std::size_t len);

class Response {
public:
	explicit operator bool() const {
		return error_.type == msgpack::type::NIL;
	}
	uint32_t seq() const {
		return seq_no_;
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

