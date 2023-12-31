#pragma once
namespace coro {

///Exception is thrown on attempt to retrieve value after promise has been broken
class broken_promise_exception: public std::exception {
public:
    const char *what() const noexcept override {return "Broken promise";}
};

///Exception is thrown on attempt to retrieve promise when the future is already pending
class already_pending_exception: public std::exception {
public:
    const char *what() const noexcept override {return "Future is already pending";}
};

///Exception is thrown on attempt to get current scheduler in thread which is not managed by any scheduler
class no_active_scheduler: public std::exception {
public:
    const char *what() const noexcept override {return "No active scheduler";}
};


}


