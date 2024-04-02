#pragma once
#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

namespace coro {


namespace _details {

template<std::size_t segment_size>
class stackful_allocator;
}

///Coroutine allocator emulates coroutine's stack to achieve stackful behaviour
/**
 * Allocator is passed to the coroutine as first argument. It is always carried as value, so
 * it must be copied. However the allocator acts as shared pointer, so copying is sharing. The
 * allocator manages coroutine's stack, every called sub-coroutine can use the same stack. You
 * only need to pass the instance to every sub-coroutine.
 *
 * The stack is managed similar way as stack in GO language. It is allocated in segments. Everytime
 * new coroutine is called, its frame is allocated in a segment. If there is no more space, new
 * segment is allocated from the heap. When coroutine exits, the memory is released.
 *
 * It is expected, that memory allocation is performed in reverse order then allocation. However
 * sometimes can happen that order of deallocation of topmost frames is reversed. This situation
 * is handled correctly, however the out of order deallocation doesn't free memory until top
 * of the stack is released reaching the out of order deallocated block.
 *
 * *
 * Note, the class is not MT safe, it is expecteded, that coroutines allocated on this stack are
 * not resumed concurently. You need to create new stack for such coroutine.
 *
 * @tparam segment_size size of segment and initial stack size
 *
 * @ingroup allocators
 */
template<std::size_t segment_size=8192>
class stackful {
public:

    stackful():_control(std::make_shared<_details::stackful_allocator<segment_size> >()) {}

    void *alloc(std::size_t sz) {
        return _control->alloc(_control, sz);
    }
    static void dealloc(void *ptr, std::size_t sz) {
        _details::stackful_allocator<segment_size>::dealloc(ptr, sz);
    }

    std::size_t get_alloc_size() const {
        return _control-> get_alloc_size();
    }
    std::size_t get_reserved() const {
       return _control-> get_reserved();
    }
    std::size_t get_segment_count() const {
        return _control-> get_segment_count();
    }
    std::size_t get_pending_deallocation_count() const {
        return _control->get_pending_deallocation_count();
    }

protected:
    std::shared_ptr<_details::stackful_allocator<segment_size> > _control;
};


namespace _details {

template<std::size_t segment_size>
class stackful_allocator {
public:


    stackful_allocator():_top_segment(0) {
        _segments.push_back({
            std::make_unique<segment>()
        });
    }

    struct segment {
        char space[segment_size];
    };

    struct segment_meta {
        std::unique_ptr<segment> _segment;
        unsigned int _usage = 0;
    };

    static void *alloc(const std::shared_ptr<stackful_allocator> &ptr, std::size_t sz) {
        auto me = ptr.get();
        if (me->_top_segment == 0 && me->_segments[0]._usage == 0) {
            me->_hold = ptr;
        }
        return me->do_alloc(sz);
    }

    void *do_alloc(std::size_t sz) {
        std::size_t need = sz + sizeof(void *);

        if (need > segment_size) {
            void *ptr = ::operator new(need);
            auto ref = reinterpret_cast<stackful_allocator **>(reinterpret_cast<char *>(ptr)+sz);
            *ref= nullptr;
            return ptr;
        }

        segment_meta &top = _segments[_top_segment];
        auto remain = segment_size - top._usage;
        if (need > remain) {
            ++_top_segment;
            if (_top_segment >= _segments.size()) {
                _segments.push_back({std::make_unique<segment>()});
            }
            return do_alloc(sz);
        }
        void *ptr = top._segment->space + top._usage;
        top._usage += static_cast<unsigned int>(need);
        auto ref = reinterpret_cast<stackful_allocator **>(reinterpret_cast<char *>(ptr)+sz);
        *ref= this;
        return ptr;
    }

    static void dealloc(void *ptr, std::size_t sz) noexcept {
        auto ref = reinterpret_cast<stackful_allocator **>(reinterpret_cast<char *>(ptr)+sz);
        if (*ref == nullptr) {
            ::operator delete(ptr);
            return;
        }
        (*ref)->do_dealloc(ptr,sz);
    }

    void do_dealloc(void *ptr, std::size_t sz) {
        bool rep = do_dealloc2(ptr, sz);
        if (rep) {
            do {
                auto iter =std::remove_if(_pending.begin(), _pending.end(), [&](const auto &p){
                    return do_dealloc2(p.first, p.second);
                });
                rep = iter != _pending.end();
                if (rep) {
                    _pending.erase(iter, _pending.end());
                }
            } while (rep);
        } else {
            _pending.push_back({ptr,sz});
        }

    }

    bool do_dealloc2(void *ptr, std::size_t sz) {
        std::size_t need = sz + sizeof(void *);
        segment_meta &top = _segments[_top_segment];
        if (top._usage < sz) throw std::runtime_error("[coro::stackful fatal] Stack is corrupted: (size > usage)");
        void *ref_ptr = top._segment->space + top._usage - need;
        if (ref_ptr != ptr) return false;
        top._usage -= static_cast<unsigned int>(need);
        if (top._usage == 0) {
            if (_top_segment == 0) {
                _hold.reset();
            } else {
                --_top_segment;
            }
        }
        return true;
    }

    std::size_t get_alloc_size() const {
        std::size_t s = 0;
        for (const auto &x: _segments) s += x._usage;
        return s;
    }
    std::size_t get_reserved() const {
       return _segments.size() * segment_size;
    }
    std::size_t get_segment_count() const {
        return _segments.size();
    }
    std::size_t get_pending_deallocation_count() const {
        return _pending.size();
    }


protected:
    std::shared_ptr<stackful_allocator> _hold;
    std::vector<segment_meta> _segments;
    std::vector<std::pair<void *, std::size_t> > _pending;
    std::size_t _top_segment = 0;

};

}

}
