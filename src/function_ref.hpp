#ifndef AUTOMATON_FUNCTION_REF_HPP_INCLUDED
#define AUTOMATON_FUNCTION_REF_HPP_INCLUDED

#include <functional>
#if __cpp_lib_function_ref >= 202306L
using std::function_ref;
#else

#include <type_traits>

template<typename ...>
class function_ref;

template<typename R, typename ...ArgTypes>
class function_ref<R(ArgTypes...)> {
public:
	template<class F>
		requires std::is_function_v<F> && std::is_invocable_r_v<R, F, ArgTypes...>
	function_ref(F* p) noexcept : bound_entity_{.function = p},
			thunk_(+[](BoundEntity be, ArgTypes&& ...args) -> R {
		if constexpr (std::is_void_v<R>)
			std::invoke(static_cast<F*>(be.function), std::forward<ArgTypes>(args)...);
		else
			return std::invoke(static_cast<F*>(be.function), std::forward<ArgTypes>(args)...);
	}) {}
	template<class F>
		requires (!std::is_same_v<std::remove_cvref_t<F>, function_ref>)
				 && (!std::is_member_pointer_v<std::remove_reference_t<F>>)
				&& std::is_invocable_r_v<R, std::remove_reference_t<F>&, ArgTypes...>
	constexpr function_ref(F&& o) noexcept : bound_entity_{.object = std::addressof(o)},
			thunk_(+[](BoundEntity be, ArgTypes&& ...args) -> R {
		using T = std::remove_reference_t<F>;
		if constexpr (std::is_void_v<R>)
			std::invoke(static_cast<T&>(*static_cast<T*>(be.object)), std::forward<ArgTypes>(args)...);
		else
			return std::invoke(static_cast<T&>(*static_cast<T*>(be.object)), std::forward<ArgTypes>(args)...);
	}) {}
	// template<auto f>
	// constexpr function_ref(nontype_t<f>) noexcept;
	// template<auto f, class U>
	// constexpr function_ref(nontype_t<f>, U&&) noexcept;
	// template<auto f, class T>
	// constexpr function_ref(nontype_t<f>, cv T*) noexcept;

	constexpr function_ref(const function_ref&) noexcept = default;
	constexpr function_ref& operator=(const function_ref&) noexcept = default;
	template<class T>
		requires (!std::is_same_v<T, function_ref>)
				 && (!std::is_pointer_v<T>)
				 // && not a specialization of nontype_t
	function_ref& operator=(T) = delete;

	R operator()(ArgTypes ...args) const {
		return thunk_(bound_entity_, std::forward<ArgTypes>(args)...);
	}
private:
	union BoundEntity {
		void(*function)();
		void* object;
	} bound_entity_;
	using ThunkPtr = R(*)(BoundEntity, ArgTypes&&...);
	ThunkPtr thunk_;
};

#endif // __cpp_lib_function_ref >= 202306L

#endif // AUTOMATON_FUNCTION_REF_HPP_INCLUDED