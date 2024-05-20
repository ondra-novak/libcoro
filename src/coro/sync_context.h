#pragma once

#include "prepared_coro.h"
#include <atomic>
#include <thread>

namespace coro {

///helps synchronization between two threads when future is used
/**
 * Causes that writer is blocked until reader completes processing the data. You
 * need to use future::wait or future::get() with instance of this object.
 *
 * To use this object, simply declare the variable on stack. Variable is not copyable nor movable
 *
 * @note this object can't simulate coroutine by a thread if the resume is called in the
 * same thead as wait(). In this case, the thread cannot block the caller, and the thread continues
 * in execution once the caller finishes its work. In such case it is better to use callbacks
 *
 *
 */
class sync_context {
public:

    sync_context() = default;
    sync_context(const sync_context &) = delete;
    sync_context &operator=(const sync_context &) = delete;

    ///destructor automatically releases writer
    ~sync_context() {
        release();
    }


    ///Suspends current thread
    /**
     * You need to call get_resume_function() before suspend, otherwise thread is
     * suspended forever. Function automatically releases any writer.
     */
    void suspend() {
        _this_resume.store(false);
        release();
        _this_resume.wait(false);
    }

    ///Retrieves resume function, other thread can unblock this thread by calling this function
    /**
     * By calling the resume function, caller is blocked and it is eventually resumed either
     * manually or automatically at the beginning of next wait
     * @return resume function
     */
    auto get_resume_fn() {
        _thread_id = std::this_thread::get_id();
        return [this]()->prepared_coro{
            resume();
            return {};
        };
    }

    ///Manualy release writer.
    void release() {
        if (_other_resume) {
            _other_resume->store(true);
            _other_resume->notify_all();
            _other_resume = nullptr;
        }
    }


protected:
    std::atomic<bool> _this_resume;
    std::atomic<bool> *_other_resume = nullptr;
    std::thread::id _thread_id;


    void resume() {
        if (std::this_thread::get_id() == _thread_id) {
            _this_resume.store(true);
        } else {
            std::atomic<bool> flag = {false};
            _other_resume = &flag;
            _this_resume.store(true);
            _this_resume.notify_all();
            flag.wait(false);
        }
    }
};



}
