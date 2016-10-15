/*
 * File:   regex.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on October 12, 2016, 7:36 PM
 */

#ifndef REGEX_HPP
#define REGEX_HPP

#include "precompiled.hpp"
#include "expr.hpp"

namespace automaton {

/**
 * A regular expression over an abstract alphabet.  This is a value type.
 */
template<class Alphabet>
class Regex final {
public:
	Regex(const Regex&) = default;
	Regex(Regex&&) = default;
	Regex& operator=(const Regex&) = default;
	Regex& operator=(Regex&&) = default;
	~Regex() = default;

	//TODO: overloaded operators (cf. Boost.Xpressive)

	static Regex empty() {return impl::Expr::empty();}
	static Regex all() {return impl::Expr::all();}
	static Regex epsilon() {return impl::Expr::epsilon();}
	static Regex any() {return impl::Expr::any();}

	static Regex lit(typename Alphabet::symbol_type symbol) {
		return impl::Expr::lit(Alphabet::find(symbol));
	}

	template<class InputIterator>
	static Regex cat(InputIterator begin, InputIterator end) {
		return assemble(begin, end, &impl::Expr::cat);
	}
	template<class Sequence>
	static Regex cat(const Sequence& regexes) {
		//We can't factor this out like assemble because the function we'd
		//delegate to is a template, and templates can't be passed as arguments.
		//(To find the types to instantiate it with, we'd have to use decltype
		//on the result of begin() and end(), which is just as much code.)
		using std::begin; using std::end;
		return cat(begin(regexes), end(regexes));
	}

	template<class InputIterator>
	static Regex alt(InputIterator begin, InputIterator end) {
		return assemble(begin, end, &impl::Expr::alt);
	}
	template<class Sequence>
	static Regex alt(const Sequence& regexes) {
		using std::begin; using std::end;
		return alt(begin(regexes), end(regexes));
	}

	template<class InputIterator>
	static Regex conj(InputIterator begin, InputIterator end) {
		return assemble(begin, end, &impl::Expr::conj);
	}
	template<class Sequence>
	static Regex conj(const Sequence& regexes) {
		using std::begin; using std::end;
		return conj(begin(regexes), end(regexes));
	}

	constexpr static int unlimited = impl::Expr::unlimited;
	static Regex star(Regex regex) {
		return impl::Expr::repeat(regex.pimpl_, 0, unlimited);
	}
	static Regex plus(Regex regex) {
		return impl::Expr::repeat(regex.pimpl_, 1, unlimited);
	}
	static Regex maybe(Regex regex) {
		return impl::Expr::repeat(regex.pimpl_, 0, 1);
	}
	static Regex repeat(Regex regex, int copies) {
		return impl::Expr::repeat(regex.pimpl_, copies, copies);
	}
	static Regex range(Regex regex, int min, int max) {
		return impl::Expr::repeat(regex.pimpl_, min, max);
	}

	static Regex comp(Regex regex) {
		return impl::Expr::comp(regex.pimpl_);
	}
private:
	impl::Expr::ptr pimpl_;
	Regex(impl::Expr::ptr pimpl) : pimpl_(pimpl) {
		assert(pimpl_);
	}

	template<class Iterator, class VectorAcceptor>
	static Regex assemble(Iterator begin, Iterator end, VectorAcceptor acceptor) {
		//TODO: I think we only need convertibility
		static_assert(std::is_same<typename std::iterator_traits<Iterator>::value_type, Regex>::value, "type mismatch");
		std::vector<impl::Expr::ptr> v;
		v.reserve(std::distance(begin, end));
		for (; begin != end; ++begin)
			v.push_back(begin->pimpl_);
		return acceptor(std::move(v));
	}
};

//TODO: built-in alphabets may move to their own file
struct BooleanAlphabet {
	using symbol_type = bool;
	static constexpr std::array<symbol_type, 2> symbols = {false, true};
	static constexpr unsigned int find(symbol_type symbol) {
		return symbol ? 1 : 0;
	}
	static constexpr symbol_type at(unsigned int index) {
		return symbols.at(index);
	}
};

} //namespace automaton

#endif /* REGEX_HPP */

