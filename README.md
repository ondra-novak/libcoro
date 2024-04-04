# LIBCORO 

C++20 support for coroutines and asynchronous operations

## Install - system wide

```
$ mkdir build
$ cd build
$ cmake ..
$ make all
$ sudo make install
```

When library is installed, you can use FIND_PACKAGE(libcoro) to add the library to the project


## Install - as submodule of a project

You can add libcoro as submodule to your project. Then you only need to include following lines into your CMakeLists.txt

```
set(LIBCORO_SUBMODULE_PATH "src/libcoro")
include(${LIBCORO_SUBMODULE_PATH}/library.cmake)
```

## Using the library

Because the library is **header only** you can just an include everywhere you need to use the library

```
#include <coro.h>
```

## Documentation

Doxygen: [https://ondra-novak.github.io/libcoro/](https://ondra-novak.github.io/libcoro/)


## Supported compilers

* gcc-12
* clang-16
* msvc 19.40 (Visual Studio 2022 17.10 preview) - partial support 
    - custom allocators are not supported in Release builds (Internal Compiler Error)
    - workaround for symmetric transfer for coro::async (Compiler bug)
    

## MT Safety

Most classes are considered MT-Unsafe, except when the class provides communication between (two) threads. In this case, MT safety is guaranteed for this type of communication, but accessing one side of the interface via multiple threads at the same time is still MT-Unsafe

MT Safe classes:
 * **scheduler_t / scheduler** - Scheduling and running tasks
 * **thread_pool_t / thread_pool_t** - Scheduling and running tasks, join()
 * **mutex**
 * **semaphore**
 * **queue**
 * **distributor** - you need to specify LOCK
 * **atomic_promise** - uses atomic variables

Parial MT Safety (two threads)
 * **generator** - coroutine can be asynchronous
 * **future/promise** - even though these objects are linked, access to each object can be from different threads. 
 * **shared_future** - accessing shared state is MT-Safe, accessing single instance from multiple threads at the same time is not MT-Safe
 *


## Short guide



### Writing coroutine

#### coroutine, basic_coroutine

```
coro::coroutine my_coroutine(int arg) {
    co_await ...;
    co_return ...;
}

int main() {
    my_coroutine(42);
}
```

This is simple coroutine, which doesn't return value and which cannot be synchronized. 
It is *detached*. Inside of coroutine, you can use all features of this library

**basic_coroutine** is template, which allows to set allocator for the coroutine

```
coro::basic_coroutine<MyAllocator> my_coroutine_with_allocator(MyAllocator &alloc, args...) {
    ...
}
```

#### async<T>

The coroutine **async<T>** is advanced coroutine, which can return a value and which
can be synchronized. 

The instance itself is created by calling the coroutine, which is always created in
suspended state. This acts as a constructor. Now, you can start the execution of the
coroutine by various ways. Once the coroutine is started, the associated instance is
empty and cannot be used to manage the coroutine

```
coro::async<int> my_async(int arg) {
    co_await ...;
    co_return ...;
}

int main() {
    coro::async c = my_async(42); // c holds reference to coroutine in suspended state
    int result = c;                 //start coroutine and retrieve result (can block)
}
```

or

```
int main() {
    coro::async c = my_async(42); // c holds reference to coroutine in suspended state
    coro::future<int> future_result = c;  //retrieve result as future
    future_result.then([&]{             //install a callback
        int val = future_result;        //retrieve result in callback
    })
}
```

or

```
coro::coroutine coro_example() {
    int result = co_await my_async(42);     //co_await on result (suspend during waiting)
}
```

or 

```
int main() {
    my_async(42).detach();      //start detached
}
```

#### future, deferred_future

It is allowed to return future or deferred_future as result of coroutine 


```
coro::future<int> my_coroutine(int arg) {
    co_await ...;
    co_return ...;
}
```

In this case, coroutine is started immediatelly and return value is returned as future


```
coro::deferred_future<int> my_coroutine(int arg) {
    co_await ...;
    co_return ...;
}
```

In this case, coroutine is started suspended and it is resumed once the value
is requested

### Synchronization

Promise-Future pattern is used. It is recommended to use this design pattern in API design. 
On the other hand, avoid returning `async` on public interfaces.

```
    virtual coro::async<int> incorrect_public_method() = 0;
    virtual coro::future<int> correct_public_method() = 0;
```

#### coro::future<T>

Represents the future result of the calculation. An instance of the future class can be returned from a function to represent the result of a calculation in the future

```
coro::future<double> run_async_calculation();
```

In this case, the function call is made asynchronously. The result of the call may not contain the result at the time of return. The result will be available later. An instance of the `future` class provides different ways to await the result

**Note that instances of the `future` class cannot be moved or copied**

in coroutine

```
double result = co_await run_async_calculation(); //Never block the thread
```

outside coroutine

```
double result = run_async_calculation(); //NOTE, can block the thread
```

#### coro::deferred_future<T>

```
coro::deferred_future<double> run_async_calculation();
```

Represents the result of a deferred calculation. The calculation may not have started yet, meaning that it will start when the value is requested

The advantage of this class is that if the value is not required, the calculation can be omitted. Thus, an instance of this class can be destructed without getting the value. The instance can also be moved until the value is requested

#### using coro::promise<T>


The object `promise` can be obtained together with `future`. These objects are linked. The `promise` object is used to pass the result to the corresponding `future`. The `promise` object acts as a function that can be called to pass the result to the corresponding `future`. This object can be moved freely, but always exists in only one copy.

```
coro::future<int> ping_async(int val) {
    return [&](auto promise) {
        std::thread thr([val, promise = std::move(promise)]() mutable {
            promise(val);
        });
        thr.detach();
    };
}
```
In the example above, the promise setting is done in another thread. Thus, after returning from a function, `future` does not need to contain a value right away, because starting the thread and calling `promise` is done in the background.

You can reject promise in an exception handler to pass an exception instead.

```
try {
    /// a calculation
} catch (...) {
    promise.reject();
}
```

If you drop instance of `promise`, the corresponding `future` is resolved without the value. It leads to special exception `coro::await_canceled_exception`. However, you can detect
this and prevent throwing this exception

```
future<int> fut = some_calculation_which_can_drop_promise();
if (co_await !fut) {
    //canceled
} else {
    //ok
    int result = fut;
}
```

don't forget to use `co_await !fut` in the coroutine. Outside of the coroutine you can
use `!fut`, without `co_await`, but this is blocking operation

#### combining promises

You can combine two promises into 

```
coro::promise<int> a = ...
coro::promise<int> b = ...
coro::promise<int> combined = a + b;
combined(42);   //resolve both at once
```

Note that both promises must be the same type and the type must be `copy constructible`


#### using callback with future/deferred_future

You can install a callback which is called once the value is ready

```
future<int> fut = ...;
fut.then([&]{

});
```
Note, that you must not destroy the future before the resolution, so you need to store
the future somewhere

```
auto fut = new auto(run_async_calculation());
fut->then([fut]{
    double result = *fut;
    delete fut;
};
```

### Generator

```
coro::generator<int> my_generator() {
    co_yield;
    co_await;    
}

coro::coroutine async_generator_reader(coro::generator<int> &gen) {
    coro::deferred_future<int> v = gen();
    while (co_await !!v) {
        int val = v;
        std::cout << val << std::endl;
        v = gen();
    }
}

void sync_generator_reader(coro::generator<int> &gen) {
    coro::deferred_future<int> v = gen();
    while (!!v) {
        int val = v;
        std::cout << val << std::endl;
        v = gen();
    }
}

void sync_gen_iterator(coro::generator<int> &gen) {
    for (int val: gen) {
        std::cout << val << std::endl;
    }
}

```

* Both synchronous and asynchronous generators are supported.
* Generator can be called as function
* Generator always returns `deferred_future` as result. Actual call of
the generator is performed once the value is requested
* Generator can be destroyed while there is nobody waiting for next value


