#include "polyvariant.hpp"
#include <doctest.h>

class B {
public:
	virtual int foo() const = 0;
	virtual ~B() = default;
};
class D1 : public B {
	int foo() const override {
		return 1;
	}
};
class D2 : public B {
	int foo() const override {
		return 2;
	}
};
class D3 : public B {
	int foo() const override {
		return 3;
	}
};

TEST_CASE("polyvariant_Singleton") {
	polyvariant<B, D1> just_one{std::in_place_type<D1>};
	CHECK_EQ((*just_one).foo(), 1);
	CHECK_EQ(just_one->foo(), 1);

	polyvariant<B, D3> just_three{std::in_place_type<D3>};
	CHECK_EQ((*just_three).foo(), 3);
	CHECK_EQ(just_three->foo(), 3);
}

TEST_CASE("polyvariant_ConstSingleton") {
	const polyvariant<B, D1> just_one{std::in_place_type<D1>};
	CHECK_EQ((*just_one).foo(), 1);
	CHECK_EQ(just_one->foo(), 1);

	const polyvariant<B, D3> just_three{std::in_place_type<D3>};
	CHECK_EQ((*just_three).foo(), 3);
	CHECK_EQ(just_three->foo(), 3);
}

TEST_CASE("polyvariant_BasicTwoOptions") {
	polyvariant<B, D1, D2> one{std::in_place_type<D1>};
	CHECK_EQ((*one).foo(), 1);
	CHECK_EQ(one->foo(), 1);
	polyvariant<B, D1, D2> two{std::in_place_type<D2>};
	CHECK_EQ((*two).foo(), 2);
	CHECK_EQ(two->foo(), 2);
}

TEST_CASE("polyvariant_ConstBasicTwoOptions") {
	const polyvariant<B, D1, D2> one{std::in_place_type<D1>};
	CHECK_EQ((*one).foo(), 1);
	CHECK_EQ(one->foo(), 1);
	const polyvariant<B, D1, D2> two{std::in_place_type<D2>};
	CHECK_EQ((*two).foo(), 2);
	CHECK_EQ(two->foo(), 2);
}

//	polyvariant<B, D1, D2> one_and_two;
//	polyvariant<B, D1, D3> one_and_three;
//	polyvariant<B, D2, D3> two_and_three;
//	polyvariant<B, D1, D2, D3> all;