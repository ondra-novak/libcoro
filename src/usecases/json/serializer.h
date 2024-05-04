#pragma once

#include "../../coro/async.h"

#include <iterator>
#include <vector>

namespace coro_usecases {

namespace json {


template<typename StringType, typename NumberType, typename ArrayRange, typename ObjectRange>
struct json_visitor_placeholder {
    struct RetVal {};

    RetVal operator()(std::nullptr_t) const;
    RetVal operator()(bool) const;
    RetVal operator()(StringType) const;
    RetVal operator()(NumberType) const;
    RetVal operator()(ArrayRange) const;
    RetVal operator()(ObjectRange) const;
};

template<typename T>
concept json_decomposer = requires(T obj, json_visitor_placeholder<
        typename std::decay_t<T>::string_type,
        typename std::decay_t<T>::number_type,
        typename std::decay_t<T>::array_range,
        typename std::decay_t<T>::object_range
> visitor, typename std::decay_t<T>::value_type value,
        typename std::decay_t<T>::array_range array_range,
        typename std::decay_t<T>::object_range object_range) {
    requires std::is_convertible_v<typename std::decay_t<T>::string_type, std::string_view>;
    requires std::is_convertible_v<typename std::decay_t<T>::number_type, std::string_view>;
    {array_range.begin()}->std::input_or_output_iterator;
    {array_range.end()}->std::input_or_output_iterator;
    {object_range.begin()}->std::input_or_output_iterator;
    {object_range.end()}->std::input_or_output_iterator;
    {object_range.begin() == object_range.end()} -> std::convertible_to<bool>;
    {array_range.begin() == array_range.end()} -> std::convertible_to<bool>;
    {obj.visit(visitor, value)}->std::same_as<std::invoke_result_t<decltype(visitor), std::nullptr_t> >;
    {obj.key(object_range.begin())}->std::convertible_to<std::string_view>;
    {obj.value(object_range.begin())}->std::convertible_to<const typename std::decay_t<T>::value_type &>;
    {obj.value(array_range.begin())}->std::convertible_to<const typename std::decay_t<T>::value_type &>;

};

template<typename InIter, typename OutIter>
inline OutIter utf8_to_json_string(InIter beg, InIter end, OutIter iter) {

    auto escape = [&](char c) {
        *iter = '\\'; ++iter;
        *iter = c; ++iter;
    };

    auto hex = [](int val) {
        return static_cast<char>(val <= 10?('0'+val):('A'+val-10));
    };

    while (beg != end) {
         char c = *beg++;
          switch (c) {
              case '"': escape('"');break;
              case '\\':escape('\\');break;
              case '\b':escape('b');break;
              case '\f':escape('f');break;
              case '\n':escape('n');break;
              case '\r':escape('r');break;
              case '\t':escape('t');break;
              default:
                  if (c>= 0 && c < 0x20) {
                      escape('u');
                      unsigned int val = c;
                      *iter = '0'; ++iter;
                      *iter = '0'; ++iter;
                      *iter = hex(val >> 4);++iter;
                      *iter = hex(val & 0xF);++iter;
                  } else {
                      *iter = c; ++iter;
                  }
                  break;
          }
    }

    return iter;

}


///Serialize JSON object
/**
 * @tparam JsonDecomp Json object decomposer, see json_decomposer concept definition
 * @tparam Target a function which accepts std::string_view and returns awaitable object.
 * return value of awaitable is ignored. You can return std::suspend_never for synchronous
 * writter.
 * @param val value to serialize
 * @param target function which writes output
 * @param decomp instance of JsonDecomposer (optional)
 * @return
 */
template<json_decomposer JsonDecomp, std::invocable<std::string_view> Target>
inline coro::async<void> serialize_json(
        const typename JsonDecomp::value_type &val,
        Target &&target,
        JsonDecomp &&decomp = {}) {

    static_assert(coro::awaitable<std::invoke_result_t<Target, std::string_view> >, "Result of Target must be awaitable");

    using JsonInfo = std::decay_t<JsonDecomp>;

    using Node = typename JsonDecomp::value_type;
    using ArrayIter = decltype(std::declval<typename JsonInfo::array_range>().begin());
    using ObjectIter = decltype(std::declval<typename JsonInfo::object_range>().begin());
    using ArrayRange = std::pair<ArrayIter,ArrayIter>;
    using ObjectRange = std::pair<ObjectIter,ObjectIter>;
    using LevelData = std::variant<std::monostate, ArrayRange, ObjectRange, const Node *>;

    enum class State {
        base,
        array,
        object,
        key,
        string
    };

    struct Level {
        State state;
        LevelData data = {};
    };

    std::vector<Level> stack;
    stack.push_back({State::base, &val});
    std::string_view outstr;
    std::vector<char> strbuff;

    while (!stack.empty()) {
        State state = stack.back().state;
        switch (state) {
            case State::base: {
                const Node *ref = std::get<const Node *>(stack.back().data);
                stack.pop_back();
                auto to_write = decomp.visit([&](auto x)->std::string_view{
                    using Type = std::decay_t<decltype(x)>;
                    if constexpr(std::is_same_v<Type, std::nullptr_t>) {
                        return "null";
                    } else if constexpr(std::is_same_v<Type, bool>) {
                        return x?"true":"false";
                    } else if constexpr(std::is_same_v<Type, typename JsonInfo::number_type>) {
                        return x;
                    } else if constexpr(std::is_same_v<Type, typename JsonInfo::string_type>) {
                        outstr = x;
                        stack.push_back({State::string});
                        return {};
                    } else if constexpr(std::is_same_v<Type, typename JsonInfo::array_range>) {
                        ArrayIter beg = x.begin();
                        ArrayIter end = x.end();
                        if (beg == end) return "[]";
                        else {
                            stack.push_back({State::array, ArrayRange{beg, end}});
                            stack.push_back({State::base, &decomp.value(beg)});
                        }
                        return "[";
                    } else {
                        static_assert(std::is_same_v<Type, typename JsonInfo::object_range>);
                        ObjectIter beg = x.begin();
                        ObjectIter end = x.end();
                        if (beg == end) return "{}";
                        else {
                            stack.push_back({State::object, ObjectRange{beg, end}});
                            stack.push_back({State::key, &decomp.value(beg)});
                            stack.push_back({State::string});
                            outstr = decomp.key(beg);
                        }
                        return "{";
                    }
                }, *ref);
                if (!to_write.empty()) co_await target(to_write);
            }break;
            case State::array: {
                auto &[iter, end] = std::get<ArrayRange>(stack.back().data);
                ++iter;
                if (iter == end) {
                    co_await target("]");
                    stack.pop_back();
                } else {
                    co_await target(",");
                    stack.push_back({State::base, &decomp.value(iter)});
                }
            }break;
            case State::object: {
                auto &[iter, end] = std::get<ObjectRange>(stack.back().data);
                ++iter;
                if (iter == end) {
                    co_await target("}");
                    stack.pop_back();
                } else {
                    co_await target(",");
                    stack.push_back({State::key, &decomp.value(iter)});
                    stack.push_back({State::string});
                    outstr = decomp.key(iter);
                } break;
            }
            case State::key: {
                const Node *ref = std::get<const Node *>(stack.back().data);
                stack.pop_back();
                co_await target(":");
                stack.push_back({State::base,ref});
            }break;
            case State::string: {
                strbuff.push_back('"');
                utf8_to_json_string(outstr.begin(), outstr.end(), std::back_inserter(strbuff));
                strbuff.push_back('"');
                co_await target(std::string_view(strbuff.data(), strbuff.size()));
                strbuff.clear();
                stack.pop_back();
            }break;
            default: {
                stack.pop_back();
                break;
            }
        }
    }

}

}
}
