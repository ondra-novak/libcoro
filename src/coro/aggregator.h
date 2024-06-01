#pragma once

#include "coroutine.h"
#include "generator.h"
#include "queue.h"
#include "on_leave.h"
#include <vector>
namespace coro {

///Construct generator which aggregates results of multiple generators
/**
 * Aggregator is generator which aggregates results of multiple generators. The
 * aggregated generators can be both synchronous and asynchronous. Note that
 * it isn't a good idea to mix both types in the single aggregator.
 *
 * For synchronous generators, results are interleaved evenly. If used
 * with asynchronous generators, results are in order of finished generation
 *
 * @tparam T subject of generation
 * @tparam Alloc allocator for the generator (there is also version without allocator)
 * @param gens list of generators to aggregate
 * @return instance of aggregator
 *
 * @ingroup tools
 * @{
 */
template<typename T, coro_allocator Alloc = std_allocator>
generator<T, Alloc> aggregator(Alloc &, std::vector<generator<T> > gens) {

    LIBCORO_TRACE_SET_NAME();

    //list of futures waiting for results from generators
    std::vector<deferred_future<T> > futures;
    //queue - stores index to resolved future
    queue<std::size_t> q;
    std::size_t cnt = gens.size();

    //activate function - activate generator and awaits it passing index to queue
    auto activate = [&](std::size_t idx) {
        futures[idx] = gens[idx]();
        futures[idx] >> [&q, idx]{
            return q.push(idx).symmetric_transfer();
//            q.push(idx); return prepared_coro();
        };
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
            auto dtch = [](std::vector<generator<T> > , std::vector<deferred_future<T> > futures) -> coroutine {
                //cycle over all futures and @b co_await for just has_value - we don't need the value
                for (auto &f: futures) {
                    co_await f.wait();
                }

            };
            //move futures and generators to the coroutine
            dtch(std::move(gens), std::move(futures));
        }
    };

    std::exception_ptr e = {};
    //any running generator?
    while (cnt) {

        //wait on queue
        std::size_t idx = co_await q.pop();

        try {
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
            continue;
        } catch (...) {
            e = std::current_exception();
        }
        co_yield std::move(e);

        activate(idx);
    }
}

///Construct generator which aggregates results of multiple generators
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



///Construct generator which aggregates results of multiple generators
template<typename T, typename Alloc, std::convertible_to<generator<T> > ... Args>
auto aggregator(generator<T, Alloc> &&gen1, Args &&... gens) {
    std::vector<generator<T> > out;
    out.reserve(1+sizeof...(gens));
    aggregator_create_list(out, std::move(gen1), std::forward<Args>(gens)...);
    return aggregator(std::move(out));

}

///Construct generator which aggregates results of multiple generators
template<typename T, typename Alloc, coro_allocator GenAlloc,  std::convertible_to<generator<T> > ... Args>
auto aggregator(GenAlloc &genalloc, generator<T, Alloc> &&gen1, Args &&... gens) {
    std::vector<generator<T> > out;
    out.reserve(1+sizeof...(gens));
    aggregator_create_list(out, std::move(gen1), std::forward<Args>(gens)...);
    return aggregator(genalloc, std::move(out));

}

/*
 * @}
 */
}



