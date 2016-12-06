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
	container folder;
	folder.reserve(regexes.size());
	for (ptr& p : regexes)
		if (auto q = boost::dynamic_pointer_cast<Epsilon>(p))
			;
		else if (auto q = boost::dynamic_pointer_cast<Concatenation>(p))
			folder.insert(folder.end(), q->children().begin(), q->children().end());
		else
			folder.push_back(std::move(p));
	if (folder.empty())
		return epsilon();
	if (folder.size() == 1)
		return folder.front();
	return new Concatenation(std::move(folder));
}
auto Expr::alt(container&& regexes) -> ptr {
	container folder;
	folder.reserve(regexes.size());
	for (ptr& p : regexes)
		if (auto q = boost::dynamic_pointer_cast<EmptyLanguage>(p))
			;
		else if (auto q = boost::dynamic_pointer_cast<Alternation>(p))
			folder.insert(folder.end(), q->children().begin(), q->children().end());
		else
			folder.push_back(std::move(p));
	if (folder.empty())
		return empty();
	if (folder.size() == 1)
		return folder.front();
	return new Alternation(std::move(folder));
}
auto Expr::conj(container&& regexes) -> ptr {
	if (regexes.empty())
		return all();
	if (regexes.size() == 1)
		return regexes.front();
	//TODO in fold: remove all-strings choices
	//If one arg is any(){n}, and the other arg has known fixed length n, we can
	//just return that arg.  (This is an important case for nonogram, though we
	//could just handle it in the frontend there.)
//	container folder;
//	folder.reserve(regexes.size());
//	for (ptr& p : regexes)
//		if (auto q = boost::dynamic_pointer_cast<Intersection>(p))
//			folder.insert(folder.end(), q->children().begin(), q->children().end());
//		else
//			folder.push_back(std::move(p));
	return new Intersection(std::move(regexes));
}
auto Expr::repeat(ptr regex, int min, int max) -> ptr {
	if (min == 0 && max == 0)
		return epsilon();
	if (min == 1 && max == 1)
		return regex;
	if (auto q = boost::dynamic_pointer_cast<AllStringsLanguage>(regex))
		return all();
	if (auto q = boost::dynamic_pointer_cast<Epsilon>(regex))
		return epsilon();
	if (min == 0 && max == unlimited) {
		//star(any()) -> all()
		if (auto q = boost::dynamic_pointer_cast<Any>(regex))
			return all();
		if (auto q = boost::dynamic_pointer_cast<Repetition>(regex)) {
			//star(star(p)) -> star(p)
			if (q->isStar())
				return q;
			//star(plus(p)) -> star(p)
			//star(maybe(p)) -> star(p)
			//star(p{0,n}) -> star(p)
			if (q->min() == 0)
				return new Repetition(q->child(), 0, unlimited);
		}
	}
	if (min == max) {
		if (auto q = boost::dynamic_pointer_cast<Repetition>(regex))
			return new Repetition(q->child(), min * q->min(), max * q->max());
	}
	//TODO: more folds
	return new Repetition(std::move(regex), min, max);
}
auto Expr::comp(ptr regex) -> ptr {
	if (auto q = boost::dynamic_pointer_cast<Complement>(regex))
		return q->child();
	//comp(all()) -> empty()
	//comp(empty()) -> all()
	//comp(epsilon()) -> plus(any())
	//comp(plus(any())) -> epsilon()
	return new Complement(std::move(regex));
}

void EmptyLanguage::print(std::ostream& o) const {
	o << "\u2205";
}
void AllStringsLanguage::print(std::ostream& o) const {
	o << '@';
}
void Epsilon::print(std::ostream& o) const {
	o << "\u03B5";
}
void Any::print(std::ostream& o) const {
	o << '.';
}
void Literal::print(std::ostream& o) const {
	//TODO: this only gives us 9 digits
	o << this->symbolIdx_;
}
void Concatenation::print(std::ostream& o) const {
	o << '(';
	for (const auto& r : regexes_)
		o << *r;
	o << ')';
}
void Alternation::print(std::ostream& o) const {
	o << '(' << *regexes_.at(0);
	for (unsigned int i = 1; i < regexes_.size(); ++i)
		o << '|' << *regexes_[i];
	o << ')';
}
void Intersection::print(std::ostream& o) const {
	o << '(' << *regexes_.at(0);
	for (unsigned int i = 1; i < regexes_.size(); ++i)
		o << '&' << *regexes_[i];
	o << ')';
}
void Repetition::print(std::ostream& o) const {
	if (isStar())
		o << *regex_ << '*';
	else if (isMaybe())
		o << *regex_ << '?';
	else if (isPlus())
		o << *regex_ << '+';
	else if (isFixed())
		o << *regex_ << '{' << min() << '}';
	else if (isBounded())
		o << *regex_ << '{' << min() << ',' << max() << '}';
	else if (isUnbounded())
		o << *regex_ << '{' << min() << ',' << '}';
	else
		throw std::logic_error("impossible case in Repetition::print");
}
void Complement::print(std::ostream& o) const {
	o << "(~" << *regex_ << ')';
}

void EmptyLanguage::repr(std::ostream& o) const {
	o << "Expr::empty()";
}
void AllStringsLanguage::repr(std::ostream& o) const {
	o << "Expr::all()";
}
void Epsilon::repr(std::ostream& o) const {
	o << "Expr::epsilon()";
}
void Any::repr(std::ostream& o) const {
	o << "Expr::any()";
}
void Literal::repr(std::ostream& o) const {
	o << "Expr::lit(" << symbolIdx_ << ")";
}
void Concatenation::repr(std::ostream& o) const {
	o << "Expr::cat({";
	for (const auto& r : regexes_)
		o << r->repr() << ", ";
	o << "})";
}
void Alternation::repr(std::ostream& o) const {
	o << "Expr::alt({";
	for (const auto& r : regexes_)
		o << r->repr() << ", ";
	o << "})";
}
void Intersection::repr(std::ostream& o) const {
	o << "Expr::conj({";
	for (const auto& r : regexes_)
		o << r->repr() << ", ";
	o << "})";
}
void Repetition::repr(std::ostream& o) const {
	o << "Expr::repeat(" << regex_->repr() << ", ";
	if (min() == Expr::unlimited)
		o << "Expr::unlimited";
	else
		o << min();
	o << ", ";
	if (max() == Expr::unlimited)
		o << "Expr::unlimited";
	else
		o << max();
	o << ")";
}
void Complement::repr(std::ostream& o) const {
	o << "Expr::comp(" << regex_->repr() << ")";
}

} //namespace impl
} //namespace automaton