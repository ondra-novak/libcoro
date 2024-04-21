#pragma once

#include "../../coro/async.h"

#include <iterator>
#include <span>
#include <string_view>
#include <concepts>
#include <stack>

template<typename T>
concept json_factory = requires(T obj, std::string_view s, bool b,
        std::span<typename T::value_type> a) {

    {obj.new_string(s)}->std::same_as<typename T::value_type>;
    {obj.new_number(s)}->std::same_as<typename T::value_type>;
    {obj.new_bool(b)}->std::same_as<typename T::value_type>;
    {obj.new_null()}->std::same_as<typename T::value_type>;
    {obj.new_array(a)}->std::same_as<typename T::value_type>;
    {obj.new_object(a,a)}->std::same_as<typename T::value_type>;
};


template<coro::awaitable_r<std::string_view> Source>
class CharacterSource {
public:
    CharacterSource(Source &src):_src(src) {}

    bool await_ready() const {
        return _pos < _str.length() || _src.await_ready();
    }
    auto await_suspend(std::coroutine_handle<> h) {
        return _src.await_suspend(h);
    }
    int await_resume() {
        if (_pos >= _str.length()) {
            _str = _src.await_resume();
            _pos = 0;
            if (_str.empty()) return -1;
        }
        return static_cast<unsigned char>(_str[_pos++]);
    }
    std::string_view get_unused() const {
        return _str.substr(_pos);
    }
    void put_back() {
        --_pos;
    }

protected:
    Source &_src;
    std::string_view _str;
    std::size_t _pos;

};

class json_parse_error: public std::exception {
public:

    enum error_t {
        unexpected_eof_reading_string,
        unexpected_eof_reading_next_value,
        unexpected_eof_reading_array,
        unexpected_character,
        unexpected_separator,
        invalid_number,
        invalid_keyword,
        expected_key_as_string,
        internal_error_invalid_state

    };

    json_parse_error(error_t err, std::string_view unused): _err(err), _unused(unused) {}
    const char *what() const noexcept override {
        switch (_err) {
            default: return "unknown error";
            case unexpected_eof_reading_string: return "unexpected eof reading string";
            case unexpected_eof_reading_next_value: return "unexpected eof reading next value";
            case unexpected_eof_reading_array: return "unexpected eof reading an array";
            case unexpected_character: return "unexpected character";
            case unexpected_separator: return "unexpected separator";
            case invalid_number: return "invalid number";
            case invalid_keyword: return "invalid keyword";
            case expected_key_as_string: return "expected string as key";
            case internal_error_invalid_state: return "internal parser error";
        }
    }

    std::string_view get_unused() const {
        return _unused;
    }

protected:
    error_t _err;
    std::string_view _unused;
};

template<typename InIter, typename OutIter>
inline OutIter json_string_to_utf8(InIter beg, InIter end, OutIter output) {

    auto hex_to_int = [](char hex) {
        if (hex >= '0' && hex <= '9') {
            return hex - '0';
        } else if (hex >= 'A' && hex <= 'F') {
            return hex - 'A' + 10;
        } else if (hex >= 'a' && hex <= 'f') {
            return hex - 'a' + 10;
        }
        return 0;
    };


    for (auto it = beg; it != end; ++it) {
        if (*it == '\\') {
            ++it;
            if (it == end) return output;
            char c = *it;
            switch (c) {
                case '"':
                case '\\':
                case '/':
                    *output = c;
                    break;
                case 'b':
                    *output = '\b';
                    break;
                case 'f':
                    *output = '\f';
                    break;
                case 'n':
                    *output = '\n';
                    break;
                case 'r':
                    *output = '\r';
                    break;
                case 't':
                    *output = '\t';
                    break;
                case 'u': {
                    ++it;
                    int codepoint = 0;
                    for (int i = 0; i < 4 && it != end; ++i) {
                        codepoint = (codepoint << 4) | hex_to_int(*it);
                        ++it;
                    }
                    if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                        if (it == end || *it != '\\') return output;
                        ++it;
                        if (it == end || *it != 'u') return output;
                        ++it;
                        int second_codepoint = 0;
                        for (int i = 0; i < 4 && it != end; ++i) {
                            second_codepoint = (second_codepoint << 4) | hex_to_int(*it);
                            ++it;
                        }
                        codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (second_codepoint - 0xDC00);
                    }
                    --it;
                    if (codepoint <= 0x7F) {
                        *output = static_cast<char>(codepoint);
                    } else if (codepoint <= 0x7FF) {
                        *output++ = static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
                        *output = static_cast<char>(0x80 | (codepoint & 0x3F));
                    } else if (codepoint <= 0xFFFF) {
                        *output++ = static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
                        *output++ = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                        *output = static_cast<char>(0x80 | (codepoint & 0x3F));
                    } else if (codepoint <= 0x10FFFF) {
                        *output++ = static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
                        *output++ = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
                        *output++ = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                        *output = static_cast<char>(0x80 | (codepoint & 0x3F));
                    }
                }
                    break;
                default:
                    break;
            }
        } else {
            *output = *it;
        }
        ++output;
    }
    return output;
}

///parse JSON format as coroutine
/**
 * @tparam JsonFactory Json factory - see json_factory concept. The factory must define value_type which
 * contains type of Json note object. This object is returned
 * @tparam Source Awaitable object, which returns std::string_view everytime co_await is used
 * @param source instance of source
 * @param fact instance of factory (optional)
 * @return returns pair {result object, unprocessed output}
 */
template<json_factory JsonFactory, coro::awaitable_r<std::string_view> Source>
inline coro::async<std::pair<typename JsonFactory::value_type, std::string_view> >
            parse_json(Source &&source, JsonFactory &&fact = {}) {

    using Node = typename JsonFactory::value_type;

    enum class State {
        base_node,
        token_check,
        number_node,
        array_begin,
        array_cont,
        object_begin,
        object_cont,
        object_key
    };

    struct Item {
        State _st;
        std::size_t _count = 0;
    };

    std::vector<Node> items;
    std::vector<Node> keys;

    std::optional<Node> result;
    std::stack<Item> stack;
    stack.push(Item{State::base_node});
    CharacterSource src(source);
    std::vector<char> strbuff;
    std::vector<char> strbuff2;
    std::string_view keyword;
    bool result_is_string = false;


    while (!stack.empty()) {
        int c;
        do {
            c = co_await src;
            if (c == -1) throw json_parse_error(json_parse_error::unexpected_eof_reading_next_value, src.get_unused());
        } while (c < 33);
        switch(stack.top()._st) {
            case State::base_node: {
                stack.pop();
                switch (c) {
                    default:
                        if ((c >= '0' && c <= '9') || c == '+' || c == '-') {
                            src.put_back();
                            stack.push({State::number_node});
                        } else{
                            throw json_parse_error(json_parse_error::unexpected_character, src.get_unused());
                        }
                        break;
                    case '"': {
                        c = co_await src;
                        while (c != '"') {
                            if (c == -1) throw json_parse_error(json_parse_error::unexpected_eof_reading_string, src.get_unused());
                            strbuff.push_back(static_cast<char>(c));
                            if (c == '\\') {
                                c = co_await src;
                                if (c == -1) throw json_parse_error(json_parse_error::unexpected_eof_reading_string, src.get_unused());
                                strbuff.push_back(static_cast<char>(c));
                            }
                            c = co_await src;
                        }
                        json_string_to_utf8(strbuff.begin(), strbuff.end(), std::back_inserter(strbuff2));
                        result = fact.new_string(std::string_view(strbuff2.data(), strbuff2.size()));
                        strbuff.clear();
                        strbuff2.clear();
                        result_is_string = true;
                    } break;
                    case 't': keyword = "true";
                              src.put_back();
                              stack.push({State::token_check});
                              result = fact.new_bool(true);
                              break;
                    case 'f': keyword = "false";
                              src.put_back();
                              stack.push({State::token_check});
                              result = fact.new_bool(false);
                              break;
                    case 'n': keyword = "null";
                              src.put_back();
                              stack.push({State::token_check});
                              result = fact.new_null();
                              break;
                    case '{': stack.push({State::object_begin});
                              break;
                    case '[': stack.push({State::array_begin});
                              break;
                }
            }break;
        case State::number_node: {
            if (c == '+' || c == '-') {
                strbuff.push_back(static_cast<char>(c));
                c = co_await src;
            }
            if (!(c >= '0' && c <= '9')) throw json_parse_error(json_parse_error::invalid_number, src.get_unused());
            while (c >= '0' && c <= '9') {
                strbuff.push_back(static_cast<char>(c));
                c = co_await src;
            }
            if (c == '.') {
                strbuff.push_back(static_cast<char>(c));
                c = co_await src;
                if (!(c >= '0' && c <= '9')) throw json_parse_error(json_parse_error::invalid_number, src.get_unused());
                while (c >= '0' && c <= '9') {
                    strbuff.push_back(static_cast<char>(c));
                    c = co_await src;
                }
            }
            if (c == 'E' || c == 'e') {
                strbuff.push_back(static_cast<char>(c));
                c = co_await src;
                if (c == '+' || c == '-') {
                    strbuff.push_back(static_cast<char>(c));
                    c = co_await src;
                }
                if (!(c >= '0' && c <= '9')) throw json_parse_error(json_parse_error::invalid_number, src.get_unused());
                while (c >= '0' && c <= '9') {
                    strbuff.push_back(static_cast<char>(c));
                    c = co_await src;
                }
            }
            if (c != -1) {
                src.put_back();
            }
            strbuff.push_back('\0'); //for easy parse
            result = fact.new_number(std::string_view(strbuff.data(), strbuff.size()-1));
            strbuff.clear();
            stack.pop();
        } break;
        case State::token_check:
            src.put_back();
            for (char x: keyword) {
                c = co_await src;
                if (c != x) throw json_parse_error(json_parse_error::invalid_keyword, src.get_unused());
            }
            stack.pop();
        break;
        case State::array_begin: {
            stack.pop();
            if (c == ']') {
                result = fact.new_array(std::span<Node>{});
            } else {
                src.put_back();
                stack.push({State::array_cont});
                stack.push({State::base_node});
            }
        } break;
        case State::array_cont: {
            items.push_back(std::move(*result));
            ++stack.top()._count;
            result.reset();
            if (c == ']') {
                auto iter = items.begin() + items.size() - stack.top()._count;
                result = fact.new_array(std::span<Node>(iter, items.end()));
                items.erase(iter,items.end());
                stack.pop();
            } else if (c == ',') {
                stack.push({State::base_node});
            } else {
                throw json_parse_error(json_parse_error::unexpected_separator, src.get_unused());
            }
        } break;
        case State::object_begin: {
            stack.pop();
            if (c == '}') {
                result = fact.new_object(std::span<Node>{},std::span<Node>{});
            } else {
                src.put_back();
                stack.push({State::object_cont});
                stack.push({State::object_key});
                stack.push({State::base_node});
                result_is_string = false;
            }
        } break;
        case State::object_key: {
            stack.pop();
            if (!result_is_string) {
                throw json_parse_error(json_parse_error::expected_key_as_string, src.get_unused());
            }
            if (c != ':') {
                throw json_parse_error(json_parse_error::unexpected_separator, src.get_unused());
            }
            keys.push_back(std::move(*result));
            result.reset();
            stack.push({State::base_node});
        } break;
        case State::object_cont: {
            items.push_back(std::move(*result));
            ++stack.top()._count;
            result.reset();
            if (c == '}') {
                auto iter_n = items.begin() + items.size() - stack.top()._count;
                auto iter_k = keys.begin() + keys.size() - stack.top()._count;
                result = fact.new_object(std::span<Node>(iter_k, keys.end()),std::span<Node>(iter_n, items.end()));
                items.erase(iter_n,items.end());
                keys.erase(iter_k,keys.end());
                stack.pop();
            } else if (c == ',') {
                stack.push({State::object_key});
                stack.push({State::base_node});
                result_is_string = false;
            } else {
                throw json_parse_error(json_parse_error::unexpected_separator, src.get_unused());
            }
        } break;
        default:
            throw json_parse_error(json_parse_error::internal_error_invalid_state, src.get_unused());

        }
    }


    co_return std::pair{std::move(*result),src.get_unused()};
}


