#pragma once

#include "queue.h"
#include "generator.h"

#include <vector>
namespace coro {

namespace _details {

template<typename T>
class aggregator_impl {
public:

    class slot: public lazy_future<T> {
    public:
        using lazy_future<T>::lazy_future;
        slot(lazy_future<T> &&other):lazy_future<T>(std::move(other)) {}
        ~slot() {
            if (this->is_pending()) {
                this->wait();
            }
        }
        slot(slot &&other) = default;
        slot &operator=(slot &&other) = default;

        //cast future to slot
        static slot *cast(future<T> *x) {
            return static_cast<slot *>(x);
        }
    };

    ///Implementation of aggregator
    template<typename Alloc>
    static generator<T, Alloc> run(Alloc &, std::vector<generator<T> > gens) {

        //no generator - nothing to aggregate
        if (gens.empty()) co_return;
        //count of active - all active at the beginning
        unsigned int active = gens.size();
        typename lazy_future<T>::target_type target;

        //list of slots - equal to list of gens
        std::vector<slot > futures;
        //queue of finished generators
        queue<slot *> events;

        //create shared target (if there is only target, it can be shared
        target_simple_activation(target, [&](future<T> *g){
            //just push the finished slot to queue
            events.push(slot::cast(g));
        });

        //prepare all slots
        futures.reserve(active);
        for (auto &g: gens) {
            //initialize from generators
            futures.push_back(g());
        }
        //start all generators - register target
        for (auto &f: futures) {
            f.register_target(target);
        }
        //cycle if there is at least one active
        while (active) {
            //await on queue
            slot *f = co_await events.pop();
            //retrieve slot has value?
            if (f->has_value()) {
                //get value and yield it (exception can be thrown)
                co_yield f->get();
                //calculate position
                auto pos = f - futures.data();
                //reinitialize slot
                *f = gens[pos]();
                //continue in generation
                f->register_target(target);
            } else {
                //no value, this generator is done
                //decrease count of active generators
                --active;
            }
        }
    }
};

}

///Construct the aggregator using vector of generators to aggregate
/**
 * @param a allocator. Note there is also overload function which doesn't contain allocator
 * @param gens list of generators
 * @return aggregator
 */
template<typename T, typename Alloc>
generator<T> aggregator(Alloc &a, std::vector<generator<T> > &&gens) {
    return _details::aggregator_impl<T>::run(a, std::move(gens));
}

///Construct the aggregator to aggregate generators passed as param list
/**
 * @param a allocator. Note there is also overload function which doesn't contain allocator
 * @param gen0 first generator
 * @param args other generators
 * @return aggregator
 */
template<typename T, typename Alloc, std::convertible_to<generator<T> > ... Args>
generator<T> aggregator(Alloc &a, generator<T> &&gen0, Args && ... args) {
    std::vector<generator<T> > gens;
    gens.reserve(sizeof...(args)+1);
    gens.push_back(std::move(gen0));
    (gens.push_back(std::move(args)),...);
    return _details::aggregator_impl<T>::run(a, std::move(gens));
}

///Construct the aggregator using vector of generators to aggregate
/**
 * @param gens list of generators
 * @return aggregator
 */
template<typename T>
generator<T> aggregator(std::vector<generator<T> > &&gens) {
    standard_allocator a;
    return _details::aggregator_impl<T>::run(a, std::move(gens));
}

///Construct the aggregator to aggregate generators passed as param list
/**
 * @param gen0 first generator
 * @param args other generators
 * @return aggregator
 */
template<typename T, std::convertible_to<generator<T> > ... Args>
generator<T> aggregator(generator<T> &&gen0, Args && ... args) {
    standard_allocator a;
    return aggregator(a, std::move(gen0), std::forward<Args>(args)...);
}

}


