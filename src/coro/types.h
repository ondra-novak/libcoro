#pragma once

#include <concepts>
#include <type_traits>
#include <coroutine>

namespace coro {

namespace _details {

    template<typename X>
    struct extract_object_type;

    template<typename Obj, typename Ret, typename ... Args>
    struct extract_object_type<Ret (Obj::*)(Args...) noexcept> {
        using type = Obj;
    };

    template<typename X>
    using extract_object_type_t = typename extract_object_type<X>::type;


}

template<typename T, typename R, typename ... Args>
concept invocable_with_result = std::is_same_v<std::invoke_result_t<T, Args...>, R>;


template<typename A, typename B, typename ... Args>
concept member_function = requires(A a, B b, Args ... args) {
    (a->*b)(args...);
};

template<typename Alloc>
concept coro_allocator_base = requires(Alloc alloc, void *ptr, std::size_t sz) {
    {alloc.allocate(sz)} -> std::same_as<void *>;
    {std::decay_t<Alloc>::deallocate(ptr, sz)}->std::same_as<void>;
};

template<typename Alloc>
concept coro_allocator = coro_allocator_base<std::remove_pointer_t<Alloc> >;
template<typename Alloc>
concept coro_optional_allocator = coro_allocator<Alloc> || std::is_void_v<Alloc>;

template<typename T>
concept target_type = requires(T val) {
    typename T::user_space;
    typename T::subject_type;
    {val.activate(std::declval<typename T::subject_type>())} -> std::convertible_to<std::coroutine_handle<> >;
};

template<typename T, typename Target, typename ... Args>
concept target_activation_function = target_type<Target>
                            && std::invocable<T,Args...>
                            && sizeof(T) <= sizeof(typename Target::user_space)
                            && std::is_trivially_copy_constructible_v<T>
                            && std::is_trivially_destructible_v<T>;

template<typename T>
concept is_linked_list = requires(T val) {
    requires std::convertible_to<T *, decltype(val.next)>;
};


struct nolock {
    constexpr void lock() {};
    constexpr void unlock() {}
    constexpr bool try_lock() {return true;}
};

}



