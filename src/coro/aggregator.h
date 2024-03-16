#pragma once

#include "generator.h"
#include "queue.h"
#include "on_leave.h"
#include "coro.h"

#include <vector>
namespace coro {


template<typename T, coro_allocator Alloc = std_allocator>
generator<T, Alloc> aggregator(Alloc &, std::vector<generator<T> > gens) {

    //list of futures waiting for results from generators
    std::vector<deferred_future<T> > futures;
    //queue - stores index to resolved future
    queue<std::size_t> q;
    std::size_t cnt = gens.size();

    //activate function - activate generator and awaits it passing index to queue
    auto activate = [&](std::size_t idx) {
        futures[idx] = gens[idx]();
        futures[idx] >> [&q, idx]{q.push(idx);};
    };

    //prepare array of futures
    futures.resize(cnt);

    //activate all generators
    for (std::size_t i = 0; i < cnt; ++i) activate(i);

    //define function, which is called on exit (including destroy)
    on_leave lv=[&]{

        ///disarm all futures
        bool pending = false;
        for (auto &f: futures) {
            //this should return false, as the future is already resolved
            //but when returns true, we need handle this as special case
            //disarming all futures allows to detach it from the queue and detect such state
            auto r = f.set_callback([]{});
            pending = pending || r;
        }
        //if such future exists, install a coroutine, which awaits to resolution
        if (pending) {
            //coroutine
            auto dtch = [](std::vector<generator<T> > , std::vector<deferred_future<T> > futures) -> coro {
                //cycle over all futures and co_await for just has_value - we don't need the value
                for (auto &f: futures) {
                    co_await f.wait();
                }

            };
            //move futures and generators to the coroutine
            dtch(std::move(gens), std::move(futures));
        }
    };

    //any running generator?
    while (cnt) {

        //wait on queue
        std::size_t idx = co_await q.pop();
        //retrieve index, if it has value
        if (futures[idx].has_value()) {
            //yield value of future
            co_yield std::move(futures[idx]).get();
            //activate the generator
            activate(idx);
        } else {
            //if generator has no value, decrease count of running generators
            --cnt;
        }
    }
}

template<typename T>
generator<T, std_allocator> aggregator(std::vector<generator<T> > gens) {
    return aggregator(standard_allocator, std::move(gens));
}


template<typename T, typename Alloc, std::convertible_to<generator<T> > ... Args>
void aggregator_create_list(std::vector<generator<T> > &out, generator<T, Alloc> &&gen1, Args &&... gens) {
    out.push_back(generator<T>(std::move(gen1)));
    aggregator_create_list(out, std::forward<Args>(gens)...);
}

template<typename T>
void aggregator_create_list(std::vector<generator<T> > &) {}



template<typename T, typename Alloc, std::convertible_to<generator<T> > ... Args>
auto aggregator(generator<T, Alloc> &&gen1, Args &&... gens) {
    std::vector<generator<T> > out;
    out.reserve(1+sizeof...(gens));
    aggregator_create_list(out, std::move(gen1), std::forward<Args>(gens)...);
    return aggregator(std::move(out));

}

template<typename T, typename Alloc, coro_allocator GenAlloc,  std::convertible_to<generator<T> > ... Args>
auto aggregator(GenAlloc &genalloc, generator<T, Alloc> &&gen1, Args &&... gens) {
    std::vector<generator<T> > out;
    out.reserve(1+sizeof...(gens));
    aggregator_create_list(out, std::move(gen1), std::forward<Args>(gens)...);
    return aggregator(genalloc, std::move(out));

}


}



