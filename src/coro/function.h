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
 *
 * @ingroup tools
 */
template<typename Prototype, unsigned int reserved_space = 4*sizeof(void *)>
class function;
template<typename Prototype, bool nx, unsigned int reserved_space = 4*sizeof(void *)>
class function_impl;


template<typename T, typename RetVal, bool nx, typename ... Args>
concept IsFunctionConstructible = (nx?std::is_nothrow_invocable_r_v<RetVal,T, Args...>:std::is_invocable_r_v<RetVal, T, Args...>);


template<typename T, typename ToObj>
concept hasnt_cast_operator = !requires(T t) {
    {t.operator ToObj()};
};




template<typename RetVal, bool nx, typename ... Args, unsigned int reserved_space>
class function_impl<RetVal(Args...), nx, reserved_space> {

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

    template<typename Fn>
    class WrapFnPlacement: public Abstract {
    public:
        struct Deleter {
            void operator()(Fn *fn){std::destroy_at(fn);};
        };

        template<std::convertible_to<Fn> Fun>
        WrapFnPlacement(Fun &&fn, void *ptr):_fn(new(ptr) Fn(std::forward<Fun>(fn))) {}

        virtual RetVal call(Args && ... args) override {
            return (*_fn)(std::forward<Args>(args)...);
        }
        virtual void move(void *address) override {
            //allocate at address and move
            new(address) WrapFnPlacement(std::move(*this));
        }
        virtual bool valid() const override {return true;}

    protected:
        //pointer is held to a function allocated on the heap
        std::unique_ptr<Fn, Deleter> _fn;
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
    function_impl() {
        //allocate object in reserved space
        new(_reserved) InvalidFn();
    }
    ///Construct function from a callable object
    /**
     * @param fn callable object/lambda function/etc
     */
    template<IsFunctionConstructible<RetVal, nx, Args...> Fn>
    function_impl(Fn &&fn) {
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

    ///Construct function while placing the closure at given memory place (placement creation)
    /**
     * @param ptr pointer to a memory, where function will be constructed. The space for
     * the function must be enough to hold Fn. use sizeof(Fn) to calculate required space
     *
     * @param fn function + closure
     *
     */
    template<IsFunctionConstructible<RetVal, nx, Args...> Fn>
    function_impl(void *ptr, Fn &&fn) {
        new(_reserved) WrapFnPlacement<std::decay_t<Fn> >(std::forward<Fn>(fn), ptr);
    }

    ///Move constructor
    /**
     * @param other source function
     *
     * @note source function is not put into invalid state, this is for performance reasons.
     * The source function can still be callable, this depends on how the move constructor
     * of the callable is implemented
     */
    function_impl(function_impl &&other) {
        //move to current reserved space
        other.ref().move(_reserved);
    }
    ///dtor
    ~function_impl() {
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

    function_impl &operator=(function_impl &&other) {
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
    function_impl &operator=(std::nullptr_t) {
        if (*this) {
            destroy();
            new(_reserved) InvalidFn();
        }
        return *this;
    }

    ///Call the function
    template<typename ... XArgs>
    RetVal operator()(XArgs && ... args) noexcept(nx) {
        return ref().call(Args(std::forward<XArgs>(args))...);
    }

    ///Determines callable state
    /**
     * @retval true the function is callable
     * @retval false the function is not callable
     */
    explicit operator bool() const {
        return ref().valid();
    }

    static constexpr bool is_noexcept = nx;
    using value_type = RetVal;


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


template<typename RetVal, typename ... Args, unsigned int reserved_space>
class function<RetVal(Args...) noexcept(false), reserved_space>: public function_impl<RetVal(Args...), false, reserved_space> {
public:
    using function_impl<RetVal(Args...), false, reserved_space>::function_impl;
};

template<typename RetVal, typename ... Args, unsigned int reserved_space>
class function<RetVal(Args...) noexcept(true), reserved_space>: public function_impl<RetVal(Args...), true, reserved_space> {
public:
    using function_impl<RetVal(Args...), true, reserved_space>::function_impl;
};



///Movable any replacement with small object optimization - uses coro::function
/**
 * @ingroup tools
 *
 * @tparam reserved_space specifies reserved space for small objects, default value is
 * 4 pointers. Note that there is always 1 pointer reserved for type information
 */
template<unsigned int reserved_space = 4*sizeof(void *)>
class any {
public:

    ///Contains information about stored content
    struct content {
        ///reference to type information
        const std::type_info &type;
        ///pointer to memory, where  content is located.
        void *ptr;
        ///size in bytes
        std::size_t size;
    };

    ///Construct any instance
    /**
     * @param arg value moved into storage
     * The Arg must be move_constructible
     */
    template<hasnt_cast_operator<any> Arg>
    any(Arg &&arg):_storage([v = Arg(std::forward<Arg>(arg))]()mutable -> content{
        return {
            typeid(v),
            &v,
            sizeof(v)
        };
    }) {}


    ///Construct empty
    any():_storage([]()mutable->content{
        return {typeid(nullptr),nullptr,0};
    }) {}

    ///Retrieve information about the content
    /**
     * @return information as content
     * @exception std::bad_function_call - object is empty
     */
    const content get_info() const {
        return _storage();
    }
    ///Retrieve information about the content
    /**
     * @return information as content
     * @exception std::bad_function_call - object is empty
     */
    content get_info() {
        return _storage();
    }

    ///Get as pointer
    /**
     *
     * @tparam T expected type
     * @return returns pointer to held variable, or nullptr if T mismatch
     *
     * doesn't throw exception
     */

    template<typename T>
    const T *get_ptr() const noexcept {
        auto ctx = get_info();
        if (ctx.type != typeid(T)) return nullptr;
        return reinterpret_cast<const T *>(ctx.ptr);
    }
    ///Get as pointer
    /**
     *
     * @tparam T expected type
     * @return returns pointer to held variable, or nullptr if T mismatch
     *
     * doesn't throw exception
     */
    template<typename T>
    T *get_ptr() noexcept  {
        auto ctx = get_info();
        if (ctx.type != typeid(T)) return nullptr;
        return reinterpret_cast<T *>(ctx.ptr);
    }

    ///Get as reference
    /**
     *
     * @tparam T expected type
     * @return returns reference to held variable
     * @exception bad_cast T type mismatch
     */
    template<typename T>
    const T &get() const {
        auto ptr = get_ptr<T>();
        if (ptr == nullptr) throw std::bad_cast();
        return *ptr;
    }
    ///Get as reference
    /**
     *
     * @tparam T expected type
     * @return returns reference to held variable
     * @exception bad_cast T type mismatch
     */
    template<typename T>
    T &get()  {
        auto ptr = get_ptr<T>();
        if (ptr == nullptr) throw std::bad_cast();
        return *ptr;
    }

    ///Determines whether object is empty
    /**
     * @retval true empty
     * @retval false not empty
     */

    bool empty() const noexcept {
        return get_info().ptr == nullptr;
    }

    ///Determines validity
    /**
     * @retval true valid (not empty)
     * @retval false invalid (empty)
     */
    operator bool() const noexcept {
        return get_info().ptr != nullptr;
    }

    ///Tests whether object contains given type
    /**
     * @tparam T expected type
     * @retval true contains
     * @retval false doesn't contains
     */
    template<typename T>
    bool contains() const noexcept {
        return  get_info().type ==  typeid(T);
    }

protected:
    mutable function<content()> _storage;
};


}
