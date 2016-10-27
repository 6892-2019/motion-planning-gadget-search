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
	if (regexes.empty())
		return epsilon();
	if (regexes.size() == 1)
		return regexes.front();
	container folder;
	folder.reserve(regexes.size());
	for (ptr& p : regexes)
		if (auto q = boost::dynamic_pointer_cast<Concatenation>(p))
			folder.insert(folder.end(), q->children().begin(), q->children().end());
		else
			folder.push_back(std::move(p));
	return new Concatenation(std::move(folder));
}
auto Expr::alt(container&& regexes) -> ptr {
	if (regexes.empty())
		return empty();
	if (regexes.size() == 1)
		return regexes.front();
	container folder;
	folder.reserve(regexes.size());
	for (ptr& p : regexes)
		if (auto q = boost::dynamic_pointer_cast<Alternation>(p))
			folder.insert(folder.end(), q->children().begin(), q->children().end());
		else
			folder.push_back(std::move(p));
	return new Alternation(std::move(folder));
}
auto Expr::conj(container&& regexes) -> ptr {
	if (regexes.empty())
		return all();
	if (regexes.size() == 1)
		return regexes.front();
	container folder;
	folder.reserve(regexes.size());
	for (ptr& p : regexes)
		if (auto q = boost::dynamic_pointer_cast<Intersection>(p))
			folder.insert(folder.end(), q->children().begin(), q->children().end());
		else
			folder.push_back(std::move(p));
	return new Intersection(std::move(folder));
}
auto Expr::repeat(ptr regex, int min, int max) -> ptr {
	if (min == 0 && max == 0)
		return epsilon();
	if (min == 1 && max == 1)
		return regex;
	//TODO: more folds
	return new Repetition(std::move(regex), min, max);
}
auto Expr::comp(ptr regex) -> ptr {
	if (auto q = boost::dynamic_pointer_cast<Complement>(regex))
		return q->child();
	return new Complement(std::move(regex));
}

} //namespace impl
} //namespace automaton