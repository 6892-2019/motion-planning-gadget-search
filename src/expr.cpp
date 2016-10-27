#include "precompiled.hpp"
#include "expr.hpp"

namespace automaton {
namespace impl {

//We can share these across all expressions.
//We could skip updating their refcounts if we add AddRef/Release to Expr and
//customize it for these classes.  That seems unlikely to be worth it, though.
//We definitely need to take care not to destroy them!
namespace {
EmptyLanguage emptyLanguage;
AllStringsLanguage allStringsLanguage;
Epsilon epsilonString;
Any anyCharacter;

//Add a fake reference to the refcount of those globals so they won't be detroyed.
struct FakeRefAdder {
	FakeRefAdder() {
		intrusive_ptr_add_ref(&emptyLanguage);
		intrusive_ptr_add_ref(&allStringsLanguage);
		intrusive_ptr_add_ref(&epsilonString);
		intrusive_ptr_add_ref(&anyCharacter);
	}
};
//Because it's in the same source file, this objects is guaranteed to be
//constructed after the globals.
FakeRefAdder fakeRefAdder;
} //end anonymous namespace

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
auto Expr::lit(unsigned int symbolIdx) -> ptr {
	return new Literal(symbolIdx);
}
auto Expr::cat(container&& regexes) -> ptr {
	return new Concatenation(std::move(regexes));
}
auto Expr::alt(container&& regexes) -> ptr {
	return new Alternation(std::move(regexes));
}
auto Expr::conj(container&& regexes) -> ptr {
	return new Intersection(std::move(regexes));
}
auto Expr::repeat(ptr regex, int min, int max) -> ptr {
	return new Repetition(std::move(regex), min, max);
}
auto Expr::comp(ptr regex) -> ptr {
	return new Complement(std::move(regex));
}

} //namespace impl
} //namespace automaton