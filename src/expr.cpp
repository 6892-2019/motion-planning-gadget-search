#include "precompiled.hpp"
#include "expr.hpp"

namespace automaton {
namespace impl {

class EmptyLanguage final : public Expr {};
class AllStringsLanguage final : public Expr {};
class Epsilon final : public Expr {};
class Any final : public Expr {};

namespace {
//We can share these across all expressions.
//We could skip updating their refcounts if we add AddRef/Release to Expr and
//customize it for these classes.  That seems unlikely to be worth it, though.
//We definitely need to take care not to destroy them!
EmptyLanguage emptyLanguage;
AllStringsLanguage allStringsLanguage;
Epsilon epsilonString;
Any anyCharacter;
}

auto Expr::empty() -> ptr {
	return &emptyLanguage;
}
auto Expr::all() -> ptr {
	return &allStringsLanguage;
}
auto Expr::epsilon() -> ptr {
	return &epsilonString;
}
auto Expr::any() -> ptr {
	return &anyCharacter;
}

class Literal final : public Expr {
public:
	Literal(unsigned int symbolIdx) : symbolIdx_(symbolIdx) {}
private:
	unsigned int symbolIdx_;
};

auto Expr::lit(unsigned int symbolIdx) -> ptr {
	return new Literal(symbolIdx);
}

class Concatenation final : public Expr {
public:
	Concatenation(container&& regexes) : regexes_(std::move(regexes)) {}
private:
	container regexes_;
};

auto Expr::cat(container&& regexes) -> ptr {
	return new Concatenation(std::move(regexes));
}

class Alternation final : public Expr {
public:
	Alternation(container&& regexes) : regexes_(std::move(regexes)) {}
private:
	container regexes_;
};

auto Expr::alt(container&& regexes) -> ptr {
	return new Alternation(std::move(regexes));
}

class Intersection final : public Expr {
public:
	Intersection(container&& regexes) : regexes_(std::move(regexes)) {}
private:
	container regexes_;
};

auto Expr::conj(container&& regexes) -> ptr {
	return new Intersection(std::move(regexes));
}

class Repetition final : public Expr {
public:
	Repetition(ptr&& regex, int min, int max) : regex_(std::move(regex)), min_(min), max_(max) {
		assert(this->min_ >= 0);
		assert(this->max_ == Expr::unlimited || this->max_ >= this->min_);
	}
private:
	ptr regex_;
	int min_, max_;
};

auto Expr::repeat(ptr regex, int min, int max) -> ptr {
	return new Repetition(std::move(regex), min, max);
}

class Complement final : public Expr {
public:
	Complement(ptr&& regex) : regex_(std::move(regex)) {}
private:
	ptr regex_;
};

auto Expr::comp(ptr regex) -> ptr {
	return new Complement(std::move(regex));
}

} //namespace impl
} //namespace automaton