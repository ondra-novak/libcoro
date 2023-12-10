#pragma once

#include "queue.h"
#include "generator.h"

#include <vector>
namespace coro {

namespace _details {

template<typename T>
class aggregator_impl {
public:

    ///contains generator, and its lazy future
    /** We use advantage of inheriting lazy_future - we can upcast */
    class slot: public lazy_future<T> {
    public:
        ///contains generator
        coro::generator<T> gen_inst;

        ///inicialize with generator moved inside
        slot(coro::generator<T> &&gen_inst):gen_inst(std::move(gen_inst)) {}
        ///default move
        slot(slot &&other) = default;
        ///default move copy
        slot &operator=(slot &&other) = default;

        ///charge the generator with target
        /**
         * generator starts generating and call target when done
         * @param t target
         */
        void charge(typename lazy_future<T>::target_type &t) {
            lazy_future<T>::operator=(gen_inst());
            this->register_target(t);
        }

        //cast future to slot
        static slot *cast(future<T> *x) {
            return static_cast<slot *>(x);
        }
        ///replaces target of future
        /**this is HACK. We can replace target atomicaly, so different
         * target is activated when done. This is needed, because current target
         * is being destroyed
         *
         * @param t new target pointer
         * @retval true success, different target will be activated
         * @retval false failure, future is no longer pending (generator made it in time)
         */
        bool replace_target(typename lazy_future<T>::target_type *t) {
            const typename lazy_future<T>::target_type *out = this->_targets.exchange(t);
            if (out == &disabled_target<typename lazy_future<T>::target_type>) {
                this->_targets.store(out);
                return false;
            }
            return true;
        }
    };

    ///contains shared target for out of sync finalise
    /**
     * It is refcounted object which is destroyed once all pending futures
     * are resolved. This object adopts list of generators to prevent
     * destroying them by original coroutine
     *
     * They are destroyed once all are done
     */
    struct pending_target: target<future<T> *> {
        std::atomic<int> ref_count = {1};
        std::vector<slot> slots;
        void on_done(future<T> *) noexcept {
            if (--ref_count == 0) delete this;
        }
    };

    static_assert(is_linked_list<pending_target>);

    ///contains all aggregated generators
    /**
     * once the destructor is called, it checks, whether all generators
     * are done. If there is at least one pending, destruction is paused
     * and handle_still_pending() is called. Then empty vector is destroyed.
     *
     * Otherwise - when none is pending, generators are destroyed here
     */
    class slot_vector: public std::vector<slot> {
    public:

        using std::vector<slot>::vector;
        ~slot_vector() {
            for (auto &x: *this) {
                if (x.is_pending()) {
                    handle_still_pending();
                    return ;
                }
            }
        }

    private:
        //handles content, if there is at least one pending future
        //we just create temporary target and move vector there
        //ref counter counts pending futures
        //if counter reaches zero, temporary target is release
        //new feature - replace target, prevents to store result in the queue
        void handle_still_pending() {
            //allocate shared target
            auto pt = new pending_target;
            //initialize target
            target_member_fn_activation<&pending_target::on_done>(*pt, pt);
            //swap slots to the target
            std::swap<std::vector<slot> >(pt->slots, *this);
            //replace all targets and count pending
            for (auto &f: pt->slots) {
                if (f.is_pending()) {
                    ++pt->ref_count;
                    if (f.replace_target(pt) == false) --pt->ref_count;
                }
            }
            //there is always +1 pending to prevent deallocation during initialization
            //release this reference now - it can also release target nows
            pt->on_done(nullptr);
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

        //queue of finished generators
        queue<slot *> events;
        //list of slots - equal to list of gens
        slot_vector futures;

        //create shared target (if there is only target, it can be shared
        target_simple_activation(target, [&](future<T> *g){
            //just push the finished slot to queue
            events.push(slot::cast(g));
        });

        //prepare all slots
        futures.reserve(active);
        for (auto &g: gens) {
            futures.push_back(slot(std::move(g)));
        }
        gens.clear();
        //start all generators - register target
        for (auto &f: futures) {
            f.charge(target);
        }
        //cycle if there is at least one active
        while (active) {
            //await on queue
            slot *f = co_await events.pop();
            //retrieve slot has value?
            if (f->has_value()) {
                //get value and yield it (exception can be thrown)
                co_yield f->get();
                //recharge generator
                f->charge(target);
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


