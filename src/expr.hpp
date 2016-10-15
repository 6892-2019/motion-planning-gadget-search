/*
 * File:   expr.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on October 12, 2016, 7:37 PM
 */

#ifndef EXPR_HPP
#define EXPR_HPP

namespace automaton {
namespace impl {

class Expr {
public:
	using ptr = boost::intrusive_ptr<Expr>;
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

	virtual ~Expr() = default;
private:
	std::atomic<unsigned int> refcount_;
	friend void intrusive_ptr_add_ref(Expr* p) noexcept {
		++p->refcount_;
	}
	friend void intrusive_ptr_release(Expr* p) noexcept {
		if (!(--p->refcount_))
			delete p;
	}
};

} //namespace impl
} //namespace automaton

#endif /* EXPR_HPP */

