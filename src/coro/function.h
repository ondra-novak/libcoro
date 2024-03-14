#pragma once
#include <functional>
#include <memory>

namespace coro {


///Move only function wrapper with small object optimization
/**
 * @tparam Prototype Specify prototype as RetVal<Args> or RetVal<Args> noexcept
 * @tparam reserved_space space reserved for small function. Default value is 4x voidptr.
 * So a function which fits to this space is constructed directly inside of the object. It
 * also means, that any move operation is performed directly with the closure. Larger
 * functions are allocated on the heap and only pointers are moved.
 *
 * @code
 * using MyFunction = concurrency::function<int(std::string) noexcept, 100>;
 * @endcode
 */
template<typename Prototype, unsigned int reserved_space = 4*sizeof(void *)>
class function;

template<typename T, typename RetVal, bool nx, typename ... Args>
concept IsFunctionConstructible = (nx?std::is_nothrow_invocable_r_v<RetVal,T, Args...>:std::is_invocable_r_v<RetVal, T, Args...>);



template<typename RetVal, typename ... Args, bool nx, unsigned int reserved_space>
class function<RetVal(Args...) noexcept(nx), reserved_space> {

    /*
    +------------------+
    |    vtable +------|------->  dtor()
    +------------------+       |- call()
    |                  |       |- move()
    |    payload       |       |- valid()
    |                  |
    +------------------+
    */


    ///Abstract interface, which defines methods for manupulation
    class Abstract {
    public:
        ///dtor
        virtual ~Abstract() = default;
        ///call the function
        virtual RetVal call(Args && ... args) = 0;
        ///move the function to different address
        /**
         * @param address target address, unitialized, so the function must use placement
         * new to allocate the object. The space at the address is guaranteed to be large
         * enought to accommodate the object
         */
        virtual void move(void *address) = 0;
        ///Determines whether object is valid and can be called
        /**
         * @retval true is valid and can be called
         * @retval false is not valid
         */
        virtual bool valid() const = 0;
    };

    ///Small function wrapper
    template<typename Fn>
    class WrapFnSmall: public Abstract {
    public:
        template<std::convertible_to<Fn> Fun>
        WrapFnSmall(Fun &&fn):_fn(std::forward<Fun>(fn)) {}

        virtual RetVal call(Args && ... args) override {
            return _fn(std::forward<Args>(args)...);
        }
        virtual void move(void *address) override {
            //allocate at address and move
            new(address) WrapFnSmall(std::move(*this));
        }
        virtual bool valid() const override {return true;}

    protected:
        //function is allocated directly in the object
        Fn _fn;
    };

    ///Large function wrapper
    template<typename Fn>
    class WrapFnLarge: public Abstract {
    public:
        template<std::convertible_to<Fn> Fun>
        WrapFnLarge(Fun &&fn):_fn(std::make_unique<Fn>(std::forward<Fun>(fn))) {}

        virtual RetVal call(Args && ... args) override {
            return (*_fn)(std::forward<Args>(args)...);
        }
        virtual void move(void *address) override {
            //allocate at address and move
            new(address) WrapFnLarge(std::move(*this));
        }
        virtual bool valid() const override {return true;}

    protected:
        //pointer is held to a function allocated on the heap
        std::unique_ptr<Fn> _fn;
    };

    ///Represents invalid function, which is not callable. Used as default value of the object
    class InvalidFn: public Abstract {
    public:
        virtual RetVal call(Args &&... ) override {
            throw std::bad_function_call();
        }
        virtual void move(void *address) override {
            new(address) InvalidFn(std::move(*this));
        }
        virtual bool valid() const override {return false;}
    };

    ///used to test size of WrapFnLarge in static_assert
    struct TestCallable {
        RetVal operator()(Args ...) noexcept(nx);
    };

public:
    ///Test validity of the concept
    static_assert(IsFunctionConstructible<TestCallable, RetVal, nx, Args ...>);
    ///Test size of reserved space
    static_assert(sizeof(WrapFnLarge<TestCallable>) <= reserved_space, "Reserved space is too small");

    ///Construct default state (unassigned) function - it is not callable
    function() {
        //allocate object in reserved space
        new(_reserved) InvalidFn();
    }
    ///Construct function from a callable object
    /**
     * @param fn callable object/lambda function/etc
     */
    template<IsFunctionConstructible<RetVal, nx, Args...> Fn>
    function(Fn &&fn) {
        using Small = WrapFnSmall<std::decay_t<Fn> >;
        using Large = WrapFnLarge<std::decay_t<Fn> >;
        if constexpr(sizeof(Small) <= reserved_space) {
            //allocate object in reserved space
            new(_reserved) Small(std::forward<Fn>(fn));
        } else {
            //allocate object in reserved space
            new(_reserved) Large(std::forward<Fn>(fn));
        }
    }

    ///Move constructor
    /**
     * @param other source function
     *
     * @note source function is not put into invalid state, this is for performance reasons.
     * The source function can still be callable, this depends on how the move constructor
     * of the callable is implemented
     */
    function(function &&other) {
        //move to current reserved space
        other.ref().move(_reserved);
    }
    ///dtor
    ~function() {
        destroy();
    }
    ///Assignment by move
    /**
     * @param other source function
     * @return this
     *
     * @note source function is not put into invalid state, this is for performance reasons.
     * The source function can still be callable, this depends on how the move constructor
     * of the callable is implemented
     */

    function &operator=(function &&other) {
        if (this != &other) {
            destroy();
            //move to current reserved space
            other.ref().move(_reserved);
        }
        return *this;
    }

    ///Reset the function - sets invalid state (clears the callable inside)
    /**
     * @param nullptr nullptr
     * @return this
     */
    function &operator=(std::nullptr_t) {
        if (*this) {
            destroy();
            new(_reserved) InvalidFn();
        }
        return *this;
    }

    ///Call the function
    RetVal operator()(Args && ... args) {
        return ref().call(std::forward<Args>(args)...);
    }

    ///Determines callable state
    /**
     * @retval true the function is callable
     * @retval false the function is not callable
     */
    explicit operator bool() const {
        return ref().valid();
    }


protected:

    char _reserved[reserved_space];

    ///Calculate reference to an object allocated inside of the reserved space
    Abstract &ref() {return *reinterpret_cast<Abstract *>(_reserved);}
    ///Calculate reference to an object allocated inside of the reserved space
    const Abstract &ref() const {return *reinterpret_cast<const Abstract *>(_reserved);}

    void destroy() {
        //direct call of the destructor
        ref().~Abstract();
    }

};


}
