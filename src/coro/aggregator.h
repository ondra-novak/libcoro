#pragma once

#include "generator.h"
#include "queue.h"
#include "on_leave.h"
#include "async.h"

#include <vector>
namespace coro {


template<typename T, CoroAllocator Alloc = StdAllocator>
generator<T, Alloc> aggregator(std::vector<generator<T> > gens) {

    //list of futures waiting for results from generators
    std::vector<deferred_future<T> > futures;
    //queue - stores index to resolved future
    queue<std::size_t> q;
    std::size_t cnt = gens.size();

    //activate function - activate generator and awaits it passing index to queue
    auto activate = [&](std::size_t idx) {
        futures[idx] = gens[idx]();
        if (!futures[idx].set_callback([&q, idx]{q.push(idx);})) {
            q.push(idx);
        }
    };

    //prepare array of futures
    futures.resize(cnt);

    //activate all generators
    for (std::size_t i = 0; i < cnt; ++i) activate(i);

    //define function, which is called on exit (including destroy)
    on_leave lv=[&]{
        //first, try to find pending future
        auto iter = std::find_if(futures.begin(), futures.end(), [&](auto &f){
            return f.is_in_progress();
        });
        //if such future exists, install a coroutine, which awaits to resolution
        if (iter != futures.end()) {
            //coroutine
            auto dtch = [](std::vector<generator<T> > , std::vector<deferred_future<T> > futures) -> async<void> {
                //cycle over all futures and co_await for just has_value - we don't need the value
                for (auto &f: futures) {
                    co_await f.has_value();
                }

            };
            //move futures and generators to the coroutine and start it detached
            dtch(std::move(gens), std::move(futures)).detach();
        }
    };

    //any running generator?
    while (cnt) {

        //wait on queue
        std::size_t idx = co_await q;
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


}



