#pragma once
#include <exception>

namespace coro {

///Exception is thrown on attempt to retrieve value after promise has been broken
/**
 * @ingroup exceptions
 */
class await_canceled_exception: public std::exception {
public:
    const char *what() const noexcept override {return "co_await canceled";}
};

///Exception is thrown on attempt to retrieve promise when the future is already pending
/**
 * @ingroup exceptions
 */
class still_pending_exception: public std::exception {
public:
    const char *what() const noexcept override {return "Operation is still pending";}
};



}


