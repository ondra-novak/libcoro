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

## Use the library

Because the library is **header only** you can just an include everywhere you need to use the library

```
#include <coro.h>
```

# Getting started

## Writing asynchronous coroutines

### using future<T>

```
coro::future<T> coro_function(args...) {
...
}
```

The object `coro::future<T>` will contain a value in future. The value is set once the coroutine is finished. You can `co_await` the future variable, or you can retrieve the value synchronously by assignment

**co_await version**

```
T val = co_await coro_function(args,....)
```

**synchronous version**

```
T val = coro_function(args,....)
```

**NOTE**: Object `coro::future<T>` **cannot be copied nor moved!!**. It must not be destroyed in the pending state - so the caller must wait and pick up the value before the object can be destroyed. 

**NOTE**: The coroutine is started immediatelly once it is called 

### using lazy_future<T>


```
coro::lazy_future<T> coro_function(args...) {
...
}
```

The object `coro::lazy_future<T>` will contain a value in future. However, the coroutine is not  started immediately, it is started once the value is requested (deferred mode).

```
std::lazy_future<T> fut_val = coro_function(args,...); //ready, not yet started
int val = co_await fut_val;   //now started and resolved
```

**NOTE**: Object `coro::lazy_future<T>` **cannot be copied but can be moved**. The instance can be also destroyed before the value is retrieved, this cancels prepared asynchronous operation.

**NOTE**: The coroutine is suspended until the value is requested


### using async<T>

```
coro::async<T> coro_function(args,...) {
...
}

coro::async<T, Allocator> coro_function_with_allocator(Allocator &, args,...) {
...
}
```

The class `async<T>` represents prepared asynchronous coroutine in suspended state. The coroutine can use a custom allocator `Allocator` (see `coro_allocator` concept)

To start such coroutine, you can use one of followng options

 1. convert `async` to `future`
 2. convert `async` to `lazy_future`
 3. call `join()` to start coroutine and wait for the result synchronously
 4. call `detach()` to start coroutine detached
 5. use `co_await` to start coroutine and await for result
 
option 1

```
coro::future<T> fut = coro_function(args,...)
````

option 2

```
coro::lazy_future<T> fut = coro_function(args,...)
````

option 3

```
T result = coro_function(args,...).join();
````

option 4

```
coro_function(args,...).detach();
````
option 5

```
T result = co_await coro_function(args,...);
````


### Advantages of using `future<T>` over `async<T>`

The `async<T>` object represents a ready-made coroutine that can be directly executed or converted to `future<T>`. In contrast, `future<T>` represents any future value that may not automatically imply the use of a coroutine. This is particularly useful when designing interfaces where we try to use `future<T>` to hide the implementation detail of async operations

### Advantages (and disadvantages) of using `lazy_future<T>` over `future<T>`

The advantage of the `lazy_future` is that it can be moved, while the base `future` cannot be moved. An instance of `lazy_future` can be destroyed without attempting to retrieve the value

For the coroutines, you can always choose `lazy_future` if you want to relax tight rules of standard `future`. However, this can make diffulties when the operation is not implemented as a coroutine (for example because performance reason).

The disadvantage of `lazy_future` is that the callee does not control the moment of starting the asynchronous operation, it starts only when the caller tries to get the value from `lazy_future`. Therefore, the callee cannot start the asynchronous operation immediately. For later execution, he must save the parameters of the operation that he received when he called it. The callee must also assume that the caller can destroy the `lazy_future` instance without attempting to retrieve the value.

## Scheduling execution

```
coro::scheduler sch(<thread_count>)
```

The `coro::scheduler` allows to control execution of coroutines (and other tasks). When you initialize the scheduler, you can specify the number of threads.

```
coro::scheduler sch1; //initialze 1 thread
coro::scheduler sch2(10); //initialze 10 threads
coro::scheduler schX(0); //start without threads (use main thread, manually)
```

The coroutine can use `sleep_since` and `sleep_until` to perform asynchronous sleep

```
coro::future<void> sleeping_coro(coro::scheduler &sch) {
    co_await sch.sleep_for(std::chrono::seconds(2));
}
```

### switching threads

The coroutine can switch thread into scheduler's thread by simply co_await on scheduler's instance

```
co_await sch
```

### manual scheduling (with 0 threads)

When scheduler is constructed with 0 thread, you need to use main thread. There are
functions `await` and `await_until_ready`

```
scheduler sch(0);
auto res = sch.await(coro_function(sch));
```

The operation `.await` on the scheduler uses the main thread for scheduling and returns
once the result of coroutine is known. The coroutine can use the scheduler for
all scheduling purposes that it needs. 

The function `await_until_ready` works similar, just does't return the value of the future (nor exception).


## Mutex

```
coro::mutex mx
```

The `coro::mutex` is a simple lock of the mutex type that can be safely used in a coroutine through the `co_await` operation.

To get ownership of the mutex, just use `co_await` inside the coroutine, or `lock_sync()` outside the coroutine


```
coro::mutex mx;
auto ownership = co_await mx
```

The result of locking a mutex is always an `ownership` object whose lifetime defines the period of time during which the mutex is owned by the caller. Once an object of type `ownership` is dropped, the caller loses the ownership


## Queue

The object represents an asynchronous queue

```
coro::queue<T> q;
```

Operations
* **push(T)** or **emplace(...)***
* **pop()** -> `coro::future<T>`

Because the queue in the pop() operation returns `future<T>`, the result of this operation is treated the same as the regular `future<T>`

## Generators

Generators are special coroutines that support the `co_yield` operation. They can be used to generate values, and on the interface they behave like functions that on repeated calls return the generated values

```
coro::generator<int> gen_test() {
    for (int i = 0; i < 10; i++) {
        co_yield i;
    }
}
```

The instance of generator can be called as a function. It always returns `coro::lazy_future`. In case that generator is finite, the moment it finishes generating, it starts returning the result without a value. 

Usage in coroutine:

```
coro::generator<int> my_gen = gen_test();
coro::lazy_future<int> result = my_gen(); 
while (co_await !!result) { 
    int v = result;
    std::cout << v << std::endl;
    result = gen2();
}
```
Usage outside of coroutine:

```
coro::generator<int> my_gen = gen_test();
coro::lazy_future<int> result = my_gen(); 
while (!!result) { 
    int v = result;
    std::cout << v << std::endl;
    result = gen2();
}
```

**NOTE**: the **!** operation returns **true** if the generator has finished generating. The **!!** operation is the inverse of **!**, so it returns **true** if the value is available. Both operations can be co_awaited

**NOTE**: The generator is movable only. 

**NOTE**: The generator may be destroyed prematurely in a state where it is waiting for the next call. In this case, the destructors of all objects created during the generation should be called. For this reason, when writing generator code, one should assume that code execution may be terminated at co_yield as if there were an uncatchable exception. It is recommended to use the RAII programming style

# Advanced topic

## Promises

Promise is a companion object to the `future<T>` object. This object allows you to create instances of `future<T>` without using coroutines. 

```
coro::future<T>::promise prom;
```

The instance of `promise` can be default constructed. This represents unbound promise. 

To construct bound promise, you can use two ways

 1. construct `coro::future<T>` and call `.get_promise()`
 2. construct `coro::future<T>` with function, which receives promise as an argument

Option 1:

```
coro::future<int> fut;
auto promise = fut.get_promise();
```

Option 2:

```
coro::future<int> fut ([&](auto promise) {

});
```

Second option has best usage in a function returning the future type

```
coro::future<int> asyn_calculation() {
    return [&](auto prom){
        prom(42); //fake calculation
    };
}
```

The `promise` object can be moved, assigned, but cannot be copied. There can be at most one bound `promise` for each `future`. Once the asynchronous operation is finished, you can set value of the associated future by **calling** the bound promise with the result. The `promise` behaves like a function that accepts the same arguments as the constructor of the type that holds the `future` object (as if `promise` were a substitute constructor)

The `promise` object can be used to capture and forward an exception

```
prom.reject(std::runtime_exceptin("error"));
```

or to capture inside a catch block

```
try {
    prom(calculate_something());
} catch (...) {
    prom.reject();
}
```
the above code forwards caught exception to the associated future.

### Breaking promise

If the `promise` object is dropped without being called, future is set to "no value". If you read a value in such a state it leads to a `broken_promise_exception` exception. Promise can be explicitly broken using the `drop()` function.

```
prom.drop();  //break promise
```


### Detecting broken state

The `broken promise` state is a special state on the future that can be detected. The `has_value` function on promise is used for this purpose

synchronous:

```
future<T> res = calculate_async(...);
if (res.has_value()) {
        T val = res;
        //process the value
} else {
        //no value
}
```

in coroutine

```
future<T> res = calculate_async(...);
if (co_await res.has_value()) {
        T val = res;
        //process the value
} else {
        //no value
}
```

As a shortcut, the operator **!** can be used to get the opposite state. This can be co_awaited as well

synchronous:

```
future<T> res = calculate_async(...);
if (!res) {
        //no value
} else {
        T val = res;
        //process the value
}
```

in coroutine

```
future<T> res = calculate_async(...);
if (co_await !res) {
        //no value
} else {
        T val = res;
        //process the value
}
```

Operation **!!** works as expected 


## Aggregator of generators

Aggregates multiple generators into one generator. All generators must return same type of value

```
coro::aggregator<T> gen({gen1, gen2, gen3,....});
```

It acts as standard generator. 

If the aggregated generators are asynchronous, then the order of values obtained depends on how long each of the aggregate generators has been generating its values.

Example: A generator that generates new connections based on how requests arrive on a network interface. At the same time, each generator monitors its network port. An aggregated generator can aggregate these requests, i.e. return a connection from any sub-generator in one place



