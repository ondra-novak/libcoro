#pragma once
namespace coro {

///Exception is thrown on attempt to retrieve value after promise has been broken
class await_canceled_exception: public std::exception {
public:
    const char *what() const noexcept override {return "co_await canceled";}
};

///Exception is thrown on attempt to retrieve promise when the future is already pending
class still_pending_exception: public std::exception {
public:
    const char *what() const noexcept override {return "Operation is still pending";}
};

///Exception is thrown on attempt to get current scheduler in thread which is not managed by any scheduler
class no_active_scheduler: public std::exception {
public:
    const char *what() const noexcept override {return "No active scheduler";}
};


}


