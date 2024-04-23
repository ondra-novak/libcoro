#pragma once

#include "../../coro/async.h"

#include <iterator>
#include <stack>

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
    using Iter = std::variant<std::monostate, ArrayIter, ObjectIter>;

    enum class State {
        base_node,
        array,
        object,
        key,
        string
    };

    struct Item {
        State st;
        const Node *ref = nullptr;
        Iter iter = {};
        Iter end = {};
    };

    std::stack<Item> st;
    st.push({State::base_node, &val});
    std::size_t sz;
    std::string_view outstr;
    std::vector<char> strbuff;

    while (!st.empty()) {
        const Node *ref = st.top().ref;
        State state = st.top().st;
        switch (state) {
            case State::base_node: {
                st.pop();
                switch (decomp.type(*ref)) {
                    case json_value_type::array: {
                       co_await target("[");
                       sz = decomp.get_array_size(*ref);
                       if (sz == 0) {
                           co_await target("]");
                       } else {
                           ArrayIter iter = decomp.get_array_begin(*ref);
                           ArrayIter end = decomp.get_array_end(*ref);
                           st.push({State::array, ref, iter, end});
                           st.push({State::base_node, &decomp.get_value(iter)});
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
                            st.push({State::object, ref, iter, end});
                            st.push({State::key, &decomp.get_value(iter)});
                            st.push({State::string});
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
                        st.push({State::string});
                    } break;
                }
            }break;
            case State::array: {
                ArrayIter &iter = std::get<ArrayIter>(st.top().iter);
                ArrayIter &end = std::get<ArrayIter>(st.top().end);
                ++iter;
                if (iter == end) {
                    co_await target("]");
                    st.pop();
                } else {
                    co_await target(",");
                    st.push({State::base_node, &decomp.get_value(iter)});
                }
            }break;
            case State::object: {
                ObjectIter &iter = std::get<ObjectIter>(st.top().iter);
                ObjectIter &end = std::get<ObjectIter>(st.top().end);
                ++iter;
                if (iter == end) {
                    co_await target("}");
                    st.pop();
                } else {
                    co_await target(",");
                    st.push({State::key, &decomp.get_value(iter)});
                    st.push({State::string});
                    outstr = decomp.get_key(iter);
                } break;
            }
            case State::key: {
                st.pop();
                co_await target(":");
                st.push({State::base_node,ref});
            }break;
            case State::string: {
                strbuff.push_back('"');
                utf8_to_json_string(outstr.begin(), outstr.end(), std::back_inserter(strbuff));
                strbuff.push_back('"');
                co_await target(std::string_view(strbuff.data(), strbuff.size()));
                strbuff.clear();
                st.pop();
            }break;
            default: {
                st.pop();
                break;
            }
        }
    }

}

}
}
