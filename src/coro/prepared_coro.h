#pragma once

#include <coroutine>
#include "trace.h"

namespace coro {


///contains prepared coroutine (prepared to run)
/**
 * This object allows to schedule coroutine resumption. If this object is destroyed
 * without scheduling, the coroutine is resumed in the destructor. Object is movable
 * @ingroup tools
 */
class prepared_coro {
public:

    ///construct uninitialized object
    prepared_coro() = default;
    ///construct with handle
    prepared_coro(std::coroutine_handle<> h):_h(h) {}
    template<typename X>
    prepared_coro(std::coroutine_handle<X> h):_h(h) {}
    ///move
    prepared_coro(prepared_coro &&other):_h(other.release()) {}
    ///move assign
    prepared_coro &operator=(prepared_coro &&other) {
        if (this != &other) {
            (*this)();
            _h = other.release();
        }
        return *this;
    }

    ///destructor - resumes coroutine if still in prepared state
    ~prepared_coro() {
        if (_h) {
            LIBCORO_TRACE_ON_RESUME(_h);
            _h.resume();
        }
    }

    ///release handle
    std::coroutine_handle<> release() {
        auto h = _h;
        _h = {};
        return h;
    }

    ///release handle to be used in function await_suspend()
    /**
     * @return returns handle, if no coroutine is ready, returns noop_coroutine
     */
    std::coroutine_handle<> symmetric_transfer() {
        if (_h) return release();
        else return std::noop_coroutine();
    }

    ///object can be used as callable (you can pass it to differen thread)
    void operator()() {
        auto h = release();
        if (h) {
            LIBCORO_TRACE_ON_RESUME(h);
            h.resume();
        }
    }

    ///test whether there is coroutine prepared
    operator bool() const {return _h != nullptr;}

    bool done() const {return _h.done();}

protected:
    std::coroutine_handle<> _h;

};


}





