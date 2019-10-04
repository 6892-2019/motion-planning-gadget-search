#ifndef POP_ITERATOR_HPP
#define POP_ITERATOR_HPP

#include <iterator>
#include <type_traits>
#include <cassert>

//namespace detail {
template<class Container>
class pop_iterator {
public:
	using iterator_category = std::input_iterator_tag;
	using value_type = typename Container::value_type;
	//move_iterator tolerates value-returning iterators with some logic.  We
	//don't because SequenceContainer requires front() to return a reference.
	using reference = value_type&&;
	using pointer = value_type*; //dangerous: there are no "rvalue pointers"
	using difference_type = std::ptrdiff_t;

	pop_iterator() : c(nullptr) {}
	pop_iterator(Container& container) : c(std::addressof(container)) {}
	//implicit copy and move

	void operator++(int) {
		this->operator++();
		//We deliberately do not return a copy of the iterator before the change
		//because that copy no longer points at an element.  The iterator
		//requirements expect *i++ to work, but at least libstdc++ avoids using
		//it, and the std::ranges concepts don't require it either.
	}

	//Not required for input iterators, so unlikely to be used, but why not?
	difference_type operator-(const pop_iterator& o) const {
		if (*this == o) return 0;
		if (c) return c->size();
		if (o.c) return -((difference_type)o.c->size());
		assert(false);
		__builtin_unreachable();
	}

	bool operator==(const pop_iterator& o) const {
		assert(!c || !o.c || c == o.c); //if both not nullptr, must point to same container
		return c == o.c || (c && c->empty()) || (o.c && o.c->empty());
	}
	bool operator!=(const pop_iterator& o) const {
		return !(*this == o);
	}
protected:
	Container* c;
};
//}

template<class Container>
class pop_front_iterator : public pop_iterator<Container> {
private:
	using base_type = pop_iterator<Container>;
public:
	using iterator_category = typename base_type::iterator_category;
	using value_type = typename base_type::value_type;
	using reference = typename base_type::reference;
	using pointer = typename base_type::pointer;
	using difference_type = typename base_type::difference_type;

	pop_front_iterator() : base_type() {}
	pop_front_iterator(Container& container) : base_type(container) {}
	//implicit copy and move

	reference operator*() {
		return static_cast<reference>(this->c->front());
	}
	pointer operator->() {
		return &(this->c->front());
	}
	pop_front_iterator& operator++() {
		this->c->pop_front();
		return *this;
	}
};

//https://stackoverflow.com/questions/47134311/how-to-implement-stddistance-for-custom-templated-iterator
template<class Container>
typename pop_front_iterator<Container>::difference_type distance(
		pop_front_iterator<Container> first, pop_front_iterator<Container> last) {
	return first - last;
}

//pop_front_begin is redundant with class template argument deduction, but
//pop_front_end makes it easy to construct sentinels.  pop_front_range works in
//range-for loops (use auto&&).
template<class Container>
auto pop_front_begin(Container& c) {
	return pop_front_iterator(c);
}
template<class Container>
auto pop_front_end(Container& c) {
	return pop_front_iterator<Container>();
}
template<class Container>
struct pop_front_range {
	pop_front_range(Container& container) : c(container) {}
	auto begin() const {
		return pop_front_begin(*c);
	}
	auto end() const {
		return pop_front_end(*c);
	}
	Container* c;
};

#endif /* POP_ITERATOR_HPP */

