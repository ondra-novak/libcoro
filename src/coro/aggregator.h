#pragma once

#include "queue.h"
#include "generator.h"

#include <vector>
namespace coro {

///aggregates multiple generators into single generator
/**
 * All generators must return the same type
 */
template<typename T>
class aggregator {
public:

    using value_type = T;

    template<int n>
    aggregator(generator<T> (&&arr)[n]) {
        init(std::begin(arr), std::end(arr));
        init_targets();
    }

    aggregator(aggregator &&other)
        :_slots(std::move(other._slots))
        ,_queue(std::move(other._queue))
        ,_output_promise(std::move(other._output_promise))
        ,_last_slot(other._last_slot)
        ,_dead(other._dead) {
        if (_dead >= 0) throw already_pending_exception();
        init_targets();
        other._dead = 0;
    }


    lazy_future<T> operator()() {
        return _lazy_promise;
    }

    explicit operator bool() const {
        return is_done();
    }

    auto begin() {
        return generator_iterator<aggregator &>::begin(*this);
    }
    auto end() {
        return generator_iterator<aggregator &>::end(*this);
    }

    ~aggregator() {
        //mark stop - ignores any generator's output
        _stop = true;
        //wait until everything is done
        (*this)().wait();
    }
protected:

    class slot: public lazy_future<T> {
        generator<T> _gen;
        aggregator &_owner;
    public:
        slot(aggregator &owner, generator<T> &&gen)
            :_gen(std::move(gen))
            ,_owner(owner) {
        }
        bool charge(typename lazy_future<T>::target_type &t) {
            if (_gen) {
                lazy_future<T>::operator =(_gen());
                lazy_future<T>::register_target(t);
                return true;
            }
            return false;
        }
        static slot *cast(future<T> *x) {
            return static_cast<slot *>(x);
        }
    };


    std::vector<slot> _slots;
    queue<slot *> _queue;
    future<slot *> _queue_fut;

    typename lazy_future<T>::promise _output_promise;
    slot *_last_slot = nullptr;
    int _dead = -1;
    bool _stop = false;


    typename lazy_future<T>::target_type _gen_target;
    typename future<slot *>::target_type _queue_target;


    typename lazy_future<T>::promise_target_type _lazy_promise;


    template<typename Iter>
    void init(Iter beg, Iter end) {
        auto sz = std::distance(beg,end);
        _slots.reserve(sz);
        while (beg != end) {
            _slots.push_back(slot(*this, std::move(*beg)));
            ++beg;
        }
    }

    //this is called, when generator generates a value
    /* @param fut pointer to future, which is our slot */
    /* we just push its pointer to queue */
    void on_generate(future<T> *fut) noexcept {
        auto s = slot::cast(fut);
        //push to queue
        _queue.push(s);
    }

    //this is called, when there is at least one item in the queue
    /* we use _queue_fut to access poped item */
    void on_queue(future<slot *> *) noexcept {
        //whene queue has value
        //retrieve slot
        slot *s = _queue_fut;
        //if slot has value (and no stop)
        if (!_stop && s->has_value()) {
            //mark last slot
            _last_slot = s;
            //forward result
            s->template forward<T>(std::move(_output_promise));
        } else {
            _last_slot = nullptr;
            //has no value, increase dead
            ++_dead;
            //if dead reaches slots size,
            if (is_done()) {
                //drop promise
                _output_promise.drop();
            } else {
                //pop from queue
               _queue.pop(_queue_fut.get_promise());
               //register target for queue, but if ready, cycle
               if (!_queue_fut.register_target_async(_queue_target)) {

                   return on_queue(nullptr);
               }

            }
        }
        //this is done, we will be called again on generate
    }

    //this is called when caller requests a result
    /* @param prom contains output promise
     */
    void on_first_call(typename lazy_future<T>::promise &&prom) noexcept {
        //lazy future destroyed prematurely
        if (!prom) return;
        //store output promise
        _output_promise = std::move(prom);

        if (_stop) return;

        ++_dead; //ok all items are up

        for (auto &x: _slots) {
            x.charge(_gen_target);
        }

        target_member_fn_activation<&aggregator::on_call>(_lazy_promise, this);
        on_call_trailer();
    }

    //this is called when caller requests a result
    /* @param prom contains output promise
     */
    void on_call(typename lazy_future<T>::promise &&prom) noexcept {
        //lazy future destroyed prematurely
        if (!prom) return;
        //store output promise
        _output_promise = std::move(prom);
        
        //we need recharge some slot
        if (!_stop && _last_slot) {
            //charge - if not done
            if (!_last_slot->charge(_gen_target)) {
                //if done, increase counter
                ++_dead;
            }
        }
        on_call_trailer();
    }

    void on_call_trailer() {
        //if no slots are available
        if (is_done()) {
            //last dead, drop promise
            _output_promise.drop();
        } else {
            //pop from queue
           _queue.pop(_queue_fut.get_promise());
           //register target for queue, but if ready, cycle
           _queue_fut.register_target(_queue_target);
        }
    }

    bool is_done() const {
        return _dead >= static_cast<int>(_slots.size());
    }

    void init_targets() {
        //initialize targets - now object can no longer move
        if (_dead < 0) {
            target_member_fn_activation<&aggregator::on_first_call>(_lazy_promise, this);
        } else {
            target_member_fn_activation<&aggregator::on_call>(_lazy_promise, this);
        }
        target_member_fn_activation<&aggregator::on_queue>(_queue_target, this);
        target_member_fn_activation<&aggregator::on_generate>(_gen_target, this);

    }
};

}

