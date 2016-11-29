/*
 * File:   expr.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on October 12, 2016, 7:37 PM
 */

#ifndef EXPR_HPP
#define EXPR_HPP

#include <iosfwd>

namespace automaton {
namespace impl {

class Expr {
public:
	using ptr = boost::intrusive_ptr<Expr>;
	using const_ptr = boost::intrusive_ptr<const Expr>;
	using container = std::vector<ptr>;

	static ptr empty();
	static ptr all();
	static ptr epsilon();
	static ptr any();

	static ptr lit(unsigned int symbolIdx);

	static ptr cat(container&& regexes);
	static ptr alt(container&& regexes);
	//'conj' for 'conjunction'.
	//'and' and 'int' (for intersection) are both keywords; 'both' is not general
	static ptr conj(container&& regexes);

	constexpr static int unlimited = -1;
	static ptr repeat(ptr regex, int min, int max);
	//'comp' for 'complement'; 'not' is a keyword and 'comp' better matches ~ anyway
	static ptr comp(ptr regex);

	//https://isocpp.org/wiki/faq/input-output#virtual-friend-fns
	friend std::ostream& operator<<(std::ostream& o, const Expr& e) {
		e.print(o);
		return o;
	}

	/**
	 * Returns an object of unspecified type that, when streamed to a
	 * std::ostream, outputs a C++ expression that constructs the Expr tree
	 * rooted at this Expr.  Use this like "std::cout << r.repr() << std::endl".
	 * @return a streamable object that outputs a repr string
	 */
	auto repr() const {
		return ReprStreamer{*this};
	}
	virtual ~Expr() = default;
protected:
	Expr() : refcount_(0) {}
	virtual void print(std::ostream& o) const = 0;
	virtual void repr(std::ostream& o) const = 0;
private:
	/** A dummy type for streaming. */
	struct ReprStreamer {
		const Expr& e;
	};
	friend std::ostream& operator<<(std::ostream& o, const ReprStreamer& rs) {
		rs.e.repr(o);
		return o;
	}

	mutable std::atomic<unsigned int> refcount_;
	friend void intrusive_ptr_add_ref(const Expr* p) noexcept {
		++p->refcount_;
	}
	friend void intrusive_ptr_release(const Expr* p) noexcept {
		if (!(--p->refcount_))
			delete p;
	}
};

class EmptyLanguage final : public Expr {
protected:
	virtual void print(std::ostream& o) const;
	virtual void repr(std::ostream& o) const;
};
class AllStringsLanguage final : public Expr {
protected:
	virtual void print(std::ostream& o) const;
	virtual void repr(std::ostream& o) const;
};
class Epsilon final : public Expr {
protected:
	virtual void print(std::ostream& o) const;
	virtual void repr(std::ostream& o) const;
};
class Any final : public Expr {
protected:
	virtual void print(std::ostream& o) const;
	virtual void repr(std::ostream& o) const;
};

class Literal final : public Expr {
public:
	Literal(unsigned int symbolIdx) : symbolIdx_(symbolIdx) {}
	unsigned int symbol() const {return symbolIdx_;}
protected:
	virtual void print(std::ostream& o) const;
	virtual void repr(std::ostream& o) const;
private:
	unsigned int symbolIdx_;
};

class Concatenation final : public Expr {
public:
	Concatenation(container&& regexes) : regexes_(std::move(regexes)) {}
	const container& children() const {return regexes_;}
protected:
	virtual void print(std::ostream& o) const;
	virtual void repr(std::ostream& o) const;
private:
	container regexes_;
};

class Alternation final : public Expr {
public:
	Alternation(container&& regexes) : regexes_(std::move(regexes)) {}
	const container& children() const {return regexes_;}
protected:
	virtual void print(std::ostream& o) const;
	virtual void repr(std::ostream& o) const;
private:
	container regexes_;
};

class Intersection final : public Expr {
public:
	Intersection(container&& regexes) : regexes_(std::move(regexes)) {}
	const container& children() const {return regexes_;}
protected:
	virtual void print(std::ostream& o) const;
	virtual void repr(std::ostream& o) const;
private:
	container regexes_;
};

class Repetition final : public Expr {
public:
	Repetition(ptr&& regex, int min, int max) : regex_(std::move(regex)), min_(min), max_(max) {
		assert(this->min_ >= 0);
		assert(this->max_ == Expr::unlimited || this->max_ >= this->min_);
	}
	ptr child() const {return regex_;}
	int min() const {return min_;}
	int max() const {return max_;}
	bool isStar() const {return min() == 0 && max() == Expr::unlimited;}
	bool isMaybe() const {return min() == 0 && max() == 1;}
	bool isPlus() const {return min() == 1 && max() == Expr::unlimited;}
	bool isFixed() const {return min() == max();}
	bool isBounded() const {return max() != Expr::unlimited;}
	bool isUnbounded() const {return max() == Expr::unlimited;}
protected:
	virtual void print(std::ostream& o) const;
	virtual void repr(std::ostream& o) const;
private:
	ptr regex_;
	int min_, max_;
};

class Complement final : public Expr {
public:
	Complement(ptr&& regex) : regex_(std::move(regex)) {}
	ptr child() const {return regex_;}
protected:
	virtual void print(std::ostream& o) const;
	virtual void repr(std::ostream& o) const;
private:
	ptr regex_;
};

} //namespace impl
} //namespace automaton

#endif /* EXPR_HPP */

