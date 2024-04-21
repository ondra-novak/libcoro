#pragma once

#include "../../coro/async.h"

#include <iterator>
#include <stack>

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
    {obj.type(val)} -> std::same_as<json_value_type>;
    {obj.get_string(val)} -> std::same_as<std::string_view>;
    {obj.get_bool(val)} -> std::same_as<bool>;
    {obj.get_count(val)} -> std::convertible_to<std::size_t>;
    {obj.item_at(val, index)}->std::same_as<const typename T::value_type &>;
    {obj.key_at(val, index)}->std::same_as<std::string_view>;
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
        std::size_t pos = 0;
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
                       sz = decomp.get_count(*ref);
                       if (sz == 0) {
                           co_await target("]");
                       } else {
                           st.push({State::array, ref, 0});
                           st.push({State::base_node, &decomp.item_at(*ref, 0)});
                       }
                    } break;
                    case json_value_type::object: {
                        co_await target("{");
                        sz = decomp.get_count(*ref);
                        if (sz == 0) {
                            co_await target("}");
                        } else {
                            st.push({State::object, ref, 0});
                            st.push({State::key, &decomp.item_at(*ref,0)});
                            st.push({State::string});
                            outstr = decomp.key_at(*ref,0);
                        }
                    } break;
                    case json_value_type::boolean: {
                        co_await target(decomp.get_bool(*ref)?"true":"false");
                    } break;
                    case json_value_type::null: {
                        co_await target("null");
                    }
                    case json_value_type::number: {
                        co_await target(decomp.get_string(*ref));
                    } break;
                    case json_value_type::string: {
                        outstr = decomp.get_string(*ref);
                        st.push({State::string});
                    } break;
                }
            }break;
            case State::array: {
                std::size_t pos = ++st.top().pos;
                if (pos >= decomp.get_count(*ref)) {
                    co_await target("]");
                    st.pop();
                } else {
                    co_await target(",");
                    st.push({State::base_node, &decomp.item_at(*ref, pos)});
                }
            }break;
            case State::object: {
                std::size_t pos = ++st.top().pos;
                if (pos >= decomp.get_count(*ref)) {
                    co_await target("}");
                    st.pop();
                } else {
                    co_await target(",");
                    st.push({State::key, &decomp.item_at(*ref, pos)});
                    st.push({State::string});
                    outstr = decomp.key_at(*ref,pos);
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
