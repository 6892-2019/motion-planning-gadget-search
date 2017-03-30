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
#include "automaton.hpp"

namespace automaton {

namespace impl {

template<class Target>
constexpr auto cast = boost::dynamic_pointer_cast<Target, const Expr>;

template<unsigned int AlphabetSize>
typename Automaton<AlphabetSize>::ptr interpret(Expr::const_ptr expr) {
	using A = Automaton<AlphabetSize>;
	auto recurse = interpret<AlphabetSize>;
	if (auto e = cast<const EmptyLanguage>(expr))
		return A::empty();
	else if (auto a = cast<const AllStringsLanguage>(expr))
		return A::all();
	else if (auto e = cast<const Epsilon>(expr))
		return A::epsilon();
	else if (auto a = cast<const Any>(expr))
		return A::any();
	else if (auto l = cast<const Literal>(expr))
		return A::lit(l->symbol());
	else if (auto c = cast<const Complement>(expr))
		return A::comp(recurse(c->child()));
	else if (auto r = cast<const Repetition>(expr)) {
		typename A::const_ptr child = recurse(r->child());
		if (r->isStar())
			return A::star(child);
		else if (r->isMaybe())
			return A::maybe(child);
		else if (r->isPlus())
			return A::plus(child);
		else if (r->isFixed())
			return A::nCopies(child, r->min());
		else if (r->isUnbounded())
			return A::nOrMore(child, r->min());
		else if (r->isBounded())
			return A::range(child, r->min(), r->max());
	} else if (auto e = cast<const Intersection>(expr)) {
		assert(e->children().size() > 0);
		//We minimize intersection inputs, but not outputs.
		typename A::ptr c = recurse(e->children()[0]);
		c->minimize();
		for (std::size_t i = 1; i < e->children().size(); ++i) {
			typename A::ptr child = recurse(e->children()[i]);
			child->minimize();
			c = A::conj(c, child);
		}
		return c;
	} else if (auto e = cast<const Concatenation>(expr)) {
		std::vector<typename A::const_ptr> children;
		children.reserve(e->children().size());
		for (typename Expr::ptr p : e->children())
			children.push_back(recurse(p));
		return A::cat(children.begin(), children.end());
	} else if (auto e = cast<const Alternation>(expr)) {
		std::vector<typename A::const_ptr> children;
		children.reserve(e->children().size());
		for (typename Expr::ptr p : e->children())
			children.push_back(recurse(p));
		return A::alt(children.begin(), children.end());
	}
	assert(false && "reached end of interpret");
}
} //namespace impl

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
	static Regex cat(std::initializer_list<Regex> regexes) {
		return cat(regexes.begin(), regexes.end());
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
	static Regex alt(std::initializer_list<Regex> regexes) {
		return alt(regexes.begin(), regexes.end());
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
	static Regex conj(std::initializer_list<Regex> regexes) {
		return conj(regexes.begin(), regexes.end());
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

	typename Automaton<Alphabet::size>::ptr compile() const {
		return impl::interpret<Alphabet::size>(pimpl_);
	}

	/**
	 * Returns true iff this regex represents the empty language.
	 * @return true iff this regex represents the empty language
	 */
	bool isEmpty() const {
		auto automaton = impl::interpret<Alphabet::size>(pimpl_);
		return automaton->isEmpty();
	}

	/**
	 * Returns true iff this regex represents an infinite language.
	 * @return true iff this regex represents an infinite language
	 */
	bool infinite() const {
		auto automaton = impl::interpret<Alphabet::size>(pimpl_);
		return automaton->infinite();
	}

	template<class Callable>
	void enumerate(Callable callback) const {
		auto automaton = impl::interpret<Alphabet::size>(pimpl_);
		automaton->enumerate<Alphabet>(callback);
	}

	friend std::ostream& operator<<(std::ostream& o, const Regex& r) {
		return o << *r.pimpl_;
	}

	/**
	 * Returns an object of unspecified type that, when streamed to a
	 * std::ostream, outputs a C++ expression that constructs the Expr tree
	 * contained within this Regex.  Use this like {@code std::cout << r.repr() <<
	 * std::endl}.
	 *
	 * This function is for debugging purposes only; its output is not generally
	 * useful to clients.
	 * @return a streamable object that outputs a repr string
	 */
	auto repr() const {
		return ReprStreamer{*this};
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

	struct ReprStreamer {
		const Regex& r;
	};
	friend std::ostream& operator<<(std::ostream& o, const ReprStreamer& rs) {
		return o << rs.r.pimpl_->repr();
	}
};

} //namespace automaton

#endif /* REGEX_HPP */

