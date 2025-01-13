// SPDX-License-Identifier: MIT
// Copyright 2019 Massachusetts Institute of Technology
// Copyright 2025 Jeffrey Bosboom
#include "precompiled.hpp"
#include "rpc.hpp"
#include <sys/stat.h>

void write_buffer(const simple_buffer& buffer, const std::string& filename) {
	FILE* file = std::fopen(filename.c_str(), "wb");
	if (!file) {
		auto savederrno = errno;
		throw std::runtime_error(fmt::format("write_buffer: failed to open {}: {} ({})\n",
				filename, strerror(savederrno), savederrno));
	}
	std::size_t bytes_written = std::fwrite(buffer.data(), sizeof(char), buffer.size(), file);
	if (bytes_written != buffer.size()) {
		auto savederrno = errno;
		throw std::runtime_error(fmt::format("write_buffer: {}, size {}, wrote {}: {} ({})\n",
				filename, buffer.size(), bytes_written, strerror(savederrno), savederrno));
	}
	if (std::fclose(file) != 0) {
		auto savederrno = errno;
		throw std::runtime_error(fmt::format("write_buffer: {}, size {}, after writing {}, while closing: {} ({})\n",
				filename, buffer.size(), bytes_written, strerror(savederrno), savederrno));
	}
}
void read_buffer(simple_buffer& buf, const std::string& filename) {
	buf.clear();
	struct stat s;
	if (stat(filename.c_str(), &s)) {
		auto savederrno = errno;
		throw std::runtime_error(fmt::format("read_buffer: stat {}: {} ({})\n",
				filename, strerror(savederrno), savederrno));
	}
	std::size_t file_size = s.st_size;
	buf.grow(file_size); //TODO: actually want a function setting exact capacity, not rounding up

	FILE* file = std::fopen(filename.c_str(), "rb");
	if (!file) {
		auto savederrno = errno;
		throw std::runtime_error(fmt::format("read_buffer: failed to open {}: {} ({})\n",
				filename, strerror(savederrno), savederrno));
	}

	std::size_t bytes_read = std::fread(buf.data(), sizeof(char), file_size, file);
	if (bytes_read != file_size) {
		if (std::feof(file))
			throw std::runtime_error(fmt::format("read_buffer: premature eof from {}, length {} but only read {}",
					filename, file_size, bytes_read));
		else if (std::ferror(file)) {
			auto savederrno = errno;
			throw std::runtime_error(fmt::format("read_buffer: error reading from {}, length {}, read {}: {} ({})\n",
					filename, file_size, bytes_read, strerror(savederrno), savederrno));
		} else
			throw std::runtime_error(fmt::format("read_buffer: unknown short read from {}, length {}, read {}",
					filename, file_size, bytes_read));
	}
	buf.size(bytes_read);

	if (std::fclose(file) != 0) {
		auto savederrno = errno;
		throw std::runtime_error(fmt::format("read_buffer: {}, size {}, after reading {}, while closing: {} ({})\n",
				filename, file_size, bytes_read, strerror(savederrno), savederrno));
	}
}
simple_buffer read_buffer(const std::string& filename) {
	simple_buffer b;
	read_buffer(b, filename);
	return b;
}



simple_buffer pack_success(uint32_t seq_no, const msgpack::object result) {
	simple_buffer buffer;
	std::tuple<uint8_t, uint32_t, msgpack::type::nil_t, msgpack::object>
			response(1, seq_no, msgpack::type::nil_t{}, result);
	msgpack::pack(buffer, response);
	return buffer;
}

simple_buffer pack_error(uint32_t seq_no, const std::string& error) {
	simple_buffer buffer;
	std::tuple<uint8_t, uint32_t, std::string, msgpack::type::nil_t>
			response(1, seq_no, error, msgpack::type::nil_t{});
	msgpack::pack(buffer, response);
	return buffer;
}

simple_buffer dispatch(msgpack::object_handle hcmd,
		const std::pair<std::string_view, handler_ptr>* handlers_begin,
		const std::pair<std::string_view, handler_ptr>* handlers_end) {
	auto [msg_type, seq_no, command, arg_array] = hcmd.get().as<std::tuple<uint8_t, uint32_t, std::string, msgpack::object>>();
	if (msg_type != 0) {
		fmt::print(stderr, "bad msg_type {}\n", msg_type);
		//TODO: send error response to stdout?  but this is a clearly-broken case
		std::exit(1);
	}
	if (arg_array.type != msgpack::type::ARRAY)
		return pack_error(seq_no,
				fmt::format("argument for command '{}' not an array, actually a {:x}",
				command, static_cast<unsigned int>(arg_array.type)));

	handler_ptr handler = nullptr;
	for (auto [name, h] : make_range_for_pair(handlers_begin, handlers_end))
		if (command == name)
			handler = h;
	if (!handler)
		return pack_error(seq_no, fmt::format("no handler for command '{}'", command));

	try {
		msgpack::object_handle result = handler(arg_array);
		return pack_success(seq_no, result.get());
	} catch (bad_arity& e) {
		return pack_error(seq_no, fmt::format("bad arity for command '{}': expected {}, got {}",
				command, e.expected, e.actual));
	} catch (std::exception& e) {
		return pack_error(seq_no, fmt::format("command '{}' threw a {} exception: {}", command, typeid(e).name(), e.what()));
	} catch (...) {
		return pack_error(seq_no, fmt::format("command '{}' threw an unusual exception", command));
	}
	//We only get here if we unwind out of one of the above catch blocks.  We'll
	//probably just throw again for the same reason, but we might as well try:
	return pack_error(seq_no, "somehow threw exception while reporting error?");
}

simple_buffer dispatch(const std::byte* data_begin, const std::byte* data_end,
		const std::pair<std::string_view, handler_ptr>* handlers_begin,
		const std::pair<std::string_view, handler_ptr>* handlers_end) {
	return dispatch(data_begin, data_end - data_begin, handlers_begin, handlers_end);
}
simple_buffer dispatch(const std::byte* data_begin, const std::size_t data_length,
		const std::pair<std::string_view, handler_ptr>* handlers_begin,
		const std::pair<std::string_view, handler_ptr>* handlers_end) {
	return dispatch(msgpack::unpack(reinterpret_cast<const char*>(data_begin), data_length), handlers_begin, handlers_end);
}

Response unpack_response(const simple_buffer& buf) {
	return unpack_response(reinterpret_cast<const std::byte*>(buf.data()), buf.size());
}
Response unpack_response(const std::byte* data, std::size_t len) {
	msgpack::object_handle obj = msgpack::unpack(reinterpret_cast<const char*>(data), len);
	if (obj->type != msgpack::type::ARRAY)
		throw std::runtime_error(fmt::format("when unpacking response, expected array but got {:x}", static_cast<unsigned int>(obj->type)));
	if (obj->via.array.size != 4)
		throw std::runtime_error(fmt::format("when unpacking response, array had unexpected size {}", obj->via.array.size));
	auto resp = obj->as<std::tuple<std::uint8_t, std::uint32_t, msgpack::object, msgpack::object>>();
	if (std::get<0>(resp) != 1)
		throw std::runtime_error(fmt::format("when unpacking response with seqno {}, expected message type 1 but was {}", std::get<1>(resp), std::get<0>(resp)));
	if (std::get<2>(resp).is_nil() && std::get<3>(resp).is_nil())
		throw std::runtime_error(fmt::format("when unpacking response with seqno {}, both error and return value objects were nil", std::get<1>(resp)));
	if (!std::get<2>(resp).is_nil() && !std::get<3>(resp).is_nil())
		//The error message object should be a string, so treat that case specially.
		if (std::get<2>(resp).type == msgpack::type::STR) {
			std::string_view error_str(std::get<2>(resp).via.str.ptr, std::get<2>(resp).via.str.size);
			throw std::runtime_error(fmt::format(R"(when unpacking response with seqno {}, both error and return value were present; error "{}", return value type {:x})",
					std::get<1>(resp), error_str,
					static_cast<unsigned int>(std::get<3>(resp).type)));
		} else
			throw std::runtime_error(fmt::format(R"(when unpacking response with seqno {}, both error and return value were present; error type {:x}, return value type {:x})",
					std::get<1>(resp),
					static_cast<unsigned int>(std::get<2>(resp).type),
					static_cast<unsigned int>(std::get<3>(resp).type)));

	return {std::get<1>(resp), std::get<2>(resp), std::get<3>(resp), std::move(obj.zone())};
}

std::logic_error Response::response_was_error(const char* target_typename) const {
	if (error_.type == msgpack::type::STR)
		return std::logic_error(fmt::format("tried to convert result to {}, but response {} was an error: {}",
				target_typename, seq_no_, std::string_view(error_.via.str.ptr, error_.via.str.size)));
	return std::logic_error(fmt::format("tried to convert result to {}, but response {} was an error of type {:x}",
				target_typename, seq_no_, static_cast<unsigned int>(error_.type)));
}