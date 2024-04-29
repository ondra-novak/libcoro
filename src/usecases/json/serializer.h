#pragma once

#include "../../coro/async.h"

#include <iterator>
#include <vector>

namespace coro_usecases {

namespace json {


enum class json_value_type {
    number,
    string,
    boolean,
    null,
    array,
    object
};


template<typename T>
concept json_decomposer = requires(T obj, const typename T::value_type &val, std::size_t index) {
    ///retrieve type of JSON value
    {obj.type(val)} -> std::same_as<json_value_type>;
    ///retrieve JSON value which is a string (don't need to validate the type)
    {obj.get_string(val)} -> std::same_as<std::string_view>;
    ///retrieve JSON value which is a number, but convert it to a string (don't need to validate the type)
    {obj.get_number(val)} -> std::same_as<std::string_view>;
    ///retrieve JSON value which is a bool  (don't need to validate the type)
    {obj.get_bool(val)} -> std::same_as<bool>;
    ///retrieve an iterator to first item of an array (don't need to validate the type)
    {obj.get_array_begin(val)} -> std::input_iterator;
    ///retrieve an iterator after last item of an array (don't need to validate the type)
    {obj.get_array_end(val)} -> std::input_iterator;
    ///iterator mus be comparable
    {obj.get_array_begin(val) == obj.get_array_end(val)};
    ///retrieve a value at given iterator
    {obj.get_value(obj.get_array_begin(val))} -> std::same_as<const typename T::value_type &>;
    ///retrieve an iterator to first item of an object (don't need to validate the type)
    {obj.get_object_begin(val)} -> std::input_iterator;
    ///retrieve an iterator after last item of an object(don't need to validate the type)
    {obj.get_object_end(val)} -> std::input_iterator;
    ///iterator mus be comparable
    {obj.get_object_begin(val) == obj.get_object_end(val)};
    ///retrieve a value at given iterator
    {obj.get_value(obj.get_object_begin(val))} -> std::same_as<const typename T::value_type &>;
    ///retrieve a key at given iterator
    {obj.get_key(obj.get_object_begin(val))} -> std::same_as<std::string_view>;
    ///retrieve size of array
    {obj.get_array_size(val)} -> std::convertible_to<std::size_t>;
    ///retrieve size of object
    {obj.get_object_size(val)} -> std::convertible_to<std::size_t>;
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

    using Node = typename JsonDecomp::value_type;
    using ArrayIter = decltype(decomp.get_array_begin(val));
    using ObjectIter = decltype(decomp.get_object_begin(val));
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
    std::size_t sz;
    std::string_view outstr;
    std::vector<char> strbuff;

    while (!stack.empty()) {
        State state = stack.back().state;
        switch (state) {
            case State::base: {
                const Node *ref = std::get<const Node *>(stack.back().data);
                stack.pop_back();
                switch (decomp.type(*ref)) {
                    case json_value_type::array: {
                       co_await target("[");
                       sz = decomp.get_array_size(*ref);
                       if (sz == 0) {
                           co_await target("]");
                       } else {
                           ArrayIter iter = decomp.get_array_begin(*ref);
                           ArrayIter end = decomp.get_array_end(*ref);
                           stack.push_back({State::array, ArrayRange{iter, end}});
                           stack.push_back({State::base, &decomp.get_value(iter)});
                       }
                    } break;
                    case json_value_type::object: {
                        co_await target("{");
                        sz = decomp.get_object_size(*ref);
                        if (sz == 0) {
                            co_await target("}");
                        } else {
                            ObjectIter iter = decomp.get_object_begin(*ref);
                            ObjectIter end = decomp.get_object_end(*ref);
                            stack.push_back({State::object, ObjectRange{iter, end}});
                            stack.push_back({State::key, &decomp.get_value(iter)});
                            stack.push_back({State::string});
                            outstr = decomp.get_key(iter);
                        }
                    } break;
                    case json_value_type::boolean: {
                        co_await target(decomp.get_bool(*ref)?"true":"false");
                    } break;
                    default:
                    case json_value_type::null: {
                        co_await target("null");
                    }break;
                    case json_value_type::number: {
                        co_await target(decomp.get_number(*ref));
                    } break;
                    case json_value_type::string: {
                        outstr = decomp.get_string(*ref);
                        stack.push_back({State::string});
                    } break;
                }
            }break;
            case State::array: {
                auto &[iter, end] = std::get<ArrayRange>(stack.back().data);
                ++iter;
                if (iter == end) {
                    co_await target("]");
                    stack.pop_back();
                } else {
                    co_await target(",");
                    stack.push_back({State::base, &decomp.get_value(iter)});
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
                    stack.push_back({State::key, &decomp.get_value(iter)});
                    stack.push_back({State::string});
                    outstr = decomp.get_key(iter);
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
