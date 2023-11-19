#pragma once

#include "types.h"

#include <algorithm>
#include <memory>
#include <coroutine>
#include <atomic>
#include <optional>
#include <new>

namespace coro {

///Declaration of notification target
/** Notification target (in short 'target') is special purpose object, which
 * handles notification between varous parts of the library. For example, a notification
 * about future being resolved is send to a target. The instance of the target is
 * often initialized by a suspended coroutine. However, the interface is public, so
 * it is possible for convinience to define own targets.
 *
 *
 * @tparam Subject Defines type of subject, which is transfered with the notification,
 * often contains notification data. It is specified by source if the notification
 *
 * Targets are designed to have trivail constructor and destructor, so they can
 * be declared inside a union, this is useful, when you need to have many targets,
 * but only one is always active. You can also use any_target structure for this
 * purpose
 *
 * to initialize the target, use functions: target_simple_activation(), target_callback_activation(), target_coroutine(), target_member_fn_activation()
 *
 * You can have custom target structure, you only need to comply to the concept 'target_type'
 */
template<typename Subject>
struct target {
    ///contains subject
    using subject_type = Subject;

    using subject_ptr = std::add_pointer_t<subject_type>;


    struct coro_data_t {
        void *coro_address;
        subject_ptr subject_storage;
    };

    ///contains declaration of reserved space
    union user_space {
        coro_data_t coro_data;
        char data[sizeof(coro_data_t)];
    };

    ///contains activation function.
    std::coroutine_handle<> (*fn)(Subject, const user_space *) noexcept;
    ///user space
    user_space user;
    ///pointer to next target when target is used in a linked list, otherwise nullptr
    mutable const target *next;

    ///activate the target
    /** The function delivers the notification and activates the target. It also
     * assumes, that target becomes invalid after the activation is finished.
     *
     * @param s subject carried with the notification
     *
     * @return the function returns coroutine_handle which should be initialized
     * to correct handle when activation cause resumption of the coroutine. So
     * the function should not call resume(), it should return the handle instead. This
     * helps to schedule the coroutine resumption. Otherwise, the function must
     * return nullptr as coroutine_handle (default initialized instance)
     *
     */
    std::coroutine_handle<> activate(Subject s) const {
        if (!fn) {
            if (user.coro_data.subject_storage) {
                std::construct_at(user.coro_data.subject_storage, std::forward<Subject>(s));
            }
            return std::coroutine_handle<>::from_address(user.coro_data.coro_address);
        }
        return fn(std::forward<Subject>(s), &user);
    }

    ///activates the target and possibly resumes the coroutine
    /**
     * @param s subject carried with the notification
     *
     * @note function possibly resumes associated coroutine. This function can be
     * called from a place, where special coroutine scheduling is not necessery. The
     * coroutine is resumed and processed before the function is returned
     */
    void activate_resume(Subject s) const {
        auto c = activate(std::forward<Subject>(s));
        if (c) c.resume();
    }


    ///Push the target to a linked list atomically
    /**
     * @param list head of linked list
     * @retval true success
     * @retval false linked list is disabled. This can happen, when list points
     * to disabled_target.
     */
    bool push_to(std::atomic<const target *> &list) const;

    ///Atomically retrieves a linked list and disables it
    /**
     * @param list list which should be retrieved and disabled atomically
     * @return non-atomic version of linked list ready to be processed (in current thread).
     */
    static const target *retrieve_and_disable(std::atomic<const target *> &list);
};

///Tags a target as dynamically allocated
/**
 * Dynamically allocated targets are always deleted after activation. So activation
 * function must call `delete this` as final operation.
 *
 * Targets that meet this requirement must inherit this class
 *
 * @tparam Target
 */
template<target_type Target>
struct target_allocated: target<typename Target::subject_type> {
    virtual ~target_allocated() {}
};

///This constant marks linked list as disabled
template<target_type Target>
constexpr target<typename Target::subject_type> disabled_target = {};


template<typename Subject>
bool target<Subject>::push_to(std::atomic<const target *> &list) const {
    do {
        next = list.load(std::memory_order_relaxed);
        if (next == &disabled_target<target>) return false;
    } while (!list.compare_exchange_strong(next, this));
    return true;
}

template<typename Subject>
const target<Subject> *target<Subject>::retrieve_and_disable(std::atomic<const target *> &list) {
    return list.exchange(&disabled_target<target>);
}

///Defines unique pointer to dynamically allocated targets
/**
 * This class simplifies the handling of dynamically allocated targets before they are registered for activation.
 */
template<target_type Target>
using unique_target = std::unique_ptr<target_allocated<Target> >;

///Initializes a target by simple callback function
/**
 * @param t target to initialize
 * @param fn a callback function which is called when target is activated. The function
 * must accept target's subject. The function must be const, it can contain a closure, however
 * the closure must be trivially copy constructible and trivially destructible and
 * its size is limited up to 16 bytes (2x size of a pointer). This allows to use
 * this or reference to a variable as a closure [this, &var],
 */
template<target_type Target, target_activation_function<Target, typename Target::subject_type> Fn>
void target_simple_activation(Target &t, Fn fn) {
    using Subject = typename Target::subject_type;
    using UserSpace = typename Target::user_space;
    new(t.user.data) Fn(std::move(fn));
    t.fn = [](Subject s,const UserSpace *user) noexcept -> std::coroutine_handle<> {
        const Fn &fn = *reinterpret_cast<const Fn *>(user->data);
        if constexpr(std::is_convertible_v<decltype(fn(std::forward<Subject>(s))), std::coroutine_handle<> >) {
            return fn(std::forward<Subject>(s));
        } else {
            fn(std::forward<Subject>(s));
            return nullptr;
        }
    };
    if constexpr(is_linked_list<Target>) {
        t.next = nullptr;
    }
}

///Initialize a target by member function callback
/**
 *
 * @tparam member_fn reference to a member function in form &ClassName::member_function
 * @param t target to initialize
 * @param obj pointer to a object which's member function would be called. The function
 * must accept the Subject
 *
 *
 */
template<auto member_fn, target_type Target>
void target_member_fn_activation(Target &t, _details::extract_object_type_t<decltype(member_fn)> *obj) {
    target_simple_activation(t, [obj](typename Target::subject_type subj) {
        return (obj->*member_fn)(std::forward<typename Target::subject_type>(subj));
    });
}


///Allocate a target with general callback activation
/**
 * The target is allocated on the heap.
 *
 * @param fn a callback function, which accepts Subject as an argument.
 *
 * @return unique pointer to allocated target.
 */
template<target_type Target, typename Fn>
unique_target<Target> target_callback_activation(Fn &&fn) {

    using Subject = typename Target::subject_type;
    using FnType = std::decay_t<Fn>;

    class callback_target : public target_allocated<Target> {
    public:
        callback_target(Fn &&fn):_fn(std::forward<Fn>(fn)) {
            target_simple_activation(*this, [&](Subject s) noexcept {
                if constexpr(std::is_invocable_v<FnType>) {
                    _fn();
                } else {
                    _fn(std::forward<Subject>(s));
                }
                delete this;
            });
        }

    protected:
        FnType _fn;
    };

    return std::make_unique<callback_target>(std::forward<Fn>(fn));

}

///Initializes a target, which activates a coroutine
/**
 * @param t target
 * @param h coroutine
 * @param store_subject (optional) pointer to memory space, where carried subject will be
 * stored. Note that space is always initialized by std::construct_at (so it is better
 * to put this inside an union). If this pointer is null, the subject is disposed.
 */
template<target_type Target>
void target_coroutine(Target &t, std::coroutine_handle<> h, typename Target::subject_ptr store_subject = nullptr) {
    t.fn = nullptr;
    t.user.coro_data.coro_address = h.address();
    t.user.coro_data.subject_storage = store_subject;
    if constexpr(is_linked_list<Target>) {
        t.next = nullptr;
    }

}
///represent synchronous waiting target
/**
 * You can register this target and then call the function wait() to sychronously
 * wait on notification
 *
 * @tparam Target type of target, which is inherited
 */
template<target_type Target>
class sync_target: public target<typename Target::subject_type> {
    std::atomic<bool> flag = {false};
public:
    using Subject = typename Target::subject_type;
    std::optional<std::decay_t<Subject> > subject;

    ///construct the target (automatically)
    sync_target() {
        target_simple_activation(*this, [this](Subject s) noexcept {
            subject.emplace(std::forward<Subject>(s));
            flag.store(true, std::memory_order_relaxed);
            flag.notify_all();
        });
    }

    ///waits until target is notified
    /**
     * @return reference to received subject
     */
    Subject &wait() {
        flag.wait(false, std::memory_order_relaxed);
        return *subject;
    }
};

///a container which can host any target type (just pure target<> not inherited or derived classes
/**
 * @tparam target_template any template variant of the target type, it assumes, that
 * all variants have a saome size, or  this variant is biggest. All possible target
 * types must be trivially constructible and trivially destructible
 *
 * @note only one target at time can be active
 */
template<typename target_template = target<void *> >
class any_target {
public:

    static_assert(std::is_trivially_constructible_v<target_template>
                && std::is_trivially_destructible_v<target_template>);


    ///Activate a target of a type and retrieve reference to it
    /**
     * It assumes, that any previously active target was invalidated
     * @tparam Target requested target type
     * @return reference to the target
     *
     */
    template<typename Target>
    Target &as() {
        static_assert(std::is_trivially_constructible_v<Target>
                   && std::is_trivially_destructible_v<Target>);
        static_assert(sizeof(Target) <= sizeof(_space), "Target cannot be used");
        return *std::launder(reinterpret_cast<Target *>(_space));
    }
    ///Activate a target of a type and retrieve reference to it
    /**
     * It assumes, that any previously active target was invalidated
     * @tparam Target requested target type
     * @return reference to the target
     *
     */
    template<typename Target>
    const Target &as() const {
        static_assert(std::is_trivially_constructible_v<Target>
                   && std::is_trivially_destructible_v<Target>);
        static_assert(sizeof(Target) <= sizeof(_space), "Target cannot be used");
        return *reinterpret_cast<const Target *>(_space);
    }

protected:
    char _space[sizeof(target_template)];

};

}

