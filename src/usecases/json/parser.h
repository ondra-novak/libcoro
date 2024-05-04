#pragma once

#include "../../coro/async.h"
#include "../../coro/on_leave.h"

#include <iterator>
#include <span>
#include <string_view>
#include <concepts>
#include <vector>
#include <variant>

namespace coro_usecases {

namespace json {

template<typename T>
concept json_factory = requires(T obj,
        std::string_view s,
        bool b,
        std::span<typename T::value_type> a,
        std::span<typename T::key_type> k) {

    ///creates JSON value from string
    {obj.new_string(s)}->std::same_as<typename T::value_type>;
    ///creates JSON value from number - passed as string
    {obj.new_number(s)}->std::same_as<typename T::value_type>;
    ///creates JSON value from boolean
    {obj.new_bool(b)}->std::same_as<typename T::value_type>;
    ///creates JSON null
    {obj.new_null()}->std::same_as<typename T::value_type>;
    ///creates JSON array passing an span of JSON values
    {obj.new_array(a)}->std::same_as<typename T::value_type>;
    ///creates JSON key value (can be string), from a string
    {obj.new_key(s)}->std::same_as<typename T::key_type>;
    ///creates JSON object passing an span of JSON keys and JSON values
    {obj.new_object(k,a)}->std::same_as<typename T::value_type>;
};


class json_parse_error: public std::exception {
public:

    enum error_t {
        unexpected_eof,
        unexpected_character,
        unexpected_separator,
        invalid_number,
        invalid_keyword,
        expected_key_as_string,
        internal_error_invalid_state,
        invalid_unicode,
    };

    json_parse_error(error_t err, std::string_view unused): _err(err), _unused(unused) {}
    const char *what() const noexcept override {
        switch (_err) {
            default: return "unknown error";
            case unexpected_eof: return "unexpected eof";
            case unexpected_character: return "unexpected character";
            case unexpected_separator: return "unexpected separator";
            case invalid_number: return "invalid number";
            case invalid_keyword: return "invalid keyword";
            case expected_key_as_string: return "expected string as key";
            case internal_error_invalid_state: return "internal parser error";
            case invalid_unicode: return "invalid unicode";
        }
    }

    std::string_view get_unused() const {
        return _unused;
    }

protected:
    error_t _err;
    std::string_view _unused;
};

///converts json string to utf-8 string
template<std::invocable<char> Output>
inline auto json_string_to_utf_8(Output output) {

    enum class State {
        character,  ///<next character is standard character
        special,    ///<next character is special character
        codepoint1,  ///<next character is unicode endpoint
        codepoint2,  ///<next character is unicode endpoint
        codepoint3,  ///<next character is unicode endpoint
        codepoint4,  ///<next character is unicode endpoint
    };

    return [output,
            state = State::character,
            codepoint = 0,
            first_codepoint = 0](char c) mutable -> bool {

        switch (state) {
            default:
                if (c == '"') return false;
                else if (c == '\\') state = State::special;
                else output(c);
                break;
            case State::special:
                state = State::character;
                switch (c) {
                    default:
                        output(c);
                        break;
                    case 'b':
                        output('\b');
                        break;
                    case 'f':
                        output('\f');
                        break;
                    case 'n':
                        output('\n');
                        break;
                    case 'r':
                        output('\r');
                        break;
                    case 't':
                        output('\t');
                        break;
                    case 'u':
                        state = State::codepoint1;
                        codepoint = 0;
                        break;
                    }
                break;
            case State::codepoint1:
            case State::codepoint2:
            case State::codepoint3:
            case State::codepoint4:
                codepoint = codepoint << 4;
                if (c >= '0' && c <= '9') codepoint |= (c - '0');
                else if (c >= 'A' && c <= 'F') codepoint |= (c - 'A' +  10);
                else if (c >= 'a' && c <= 'f') codepoint |= (c - 'a' +  10);
                else throw json_parse_error::invalid_unicode;
                if (state == State::codepoint4) {
                    state = State::character;
                    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
                        if (first_codepoint) {
                            if (first_codepoint > codepoint) std::swap(first_codepoint, codepoint);
                            codepoint = 0x10000 + ((first_codepoint - 0xD800) << 10) + (codepoint - 0xDC00);
                        } else {
                            first_codepoint = codepoint;
                            break;
                        }
                    }
                    if (codepoint <= 0x7F) {
                        output(static_cast<char>(codepoint));
                    } else if (codepoint <= 0x7FF) {
                        output(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
                        output(static_cast<char>(0x80 | (codepoint & 0x3F)));
                    } else if (codepoint <= 0xFFFF) {
                        output(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
                        output(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                        output(static_cast<char>(0x80 | (codepoint & 0x3F)));
                    } else if (codepoint <= 0x10FFFF) {
                        output(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
                        output(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
                        output(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                        output(static_cast<char>(0x80 | (codepoint & 0x3F)));
                    }
                    first_codepoint = 0;
                } else {
                    state = static_cast<State>(static_cast<int>(state)+1);
                }
                break;
        }
    return true;
    };
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
template<coro::coro_allocator Allocator, json_factory JsonFactory, std::invocable<> Source>
inline coro::async<std::pair<typename JsonFactory::value_type, std::string_view> >
            parse_json(Allocator &, Source source, JsonFactory fact = {}) {

    using SourceAwaiter = std::invoke_result_t<Source>;
    static_assert(coro::awaitable_r<SourceAwaiter, std::string_view>, "Source must returns string_view and must be avaitable");

    struct CharacterSource {
        //source reference (function)
        Source &_src;
        //current string
        std::string_view _str = {};
        //current position
        std::size_t _pos = 0;
        //signal true, if await_resume must process awaiter result
        bool _fetched = false;
        //space reserved to store source awaiter
        union {
            SourceAwaiter _srcawt;
        };

        CharacterSource(Source &src):_src(src) {}
        ~CharacterSource() {}

        bool await_ready()  {
            if (_pos < _str.length()) return true;
            //start lifetime of SourceAwaiter (return value of _src())
            new (&_srcawt) SourceAwaiter(_src());
            //new data fetched
            _fetched = true;
            //ask whether data are ready
            return _srcawt.await_ready();
        }
        auto await_suspend(std::coroutine_handle<> h) {
            //forward await_suspend to source awaiter
            return _srcawt.await_suspend(h);
        }
        char await_resume() {
            //if we fetched from source awaiter
            if (_fetched) {
                //action perfomed on exit (exception)
                coro::on_leave finally=[this]{
                    //clear the flag
                    _fetched = false;
                    //end of lifetime of source awaiter
                    _srcawt.~SourceAwaiter();
                };
                //reset the position
                _pos = 0;
                //read string (can throw)
                _str = _srcawt.await_resume();
                //detect EOF
                if (_str.empty()) throw json_parse_error::unexpected_eof;
            }
            //return characted and advance position
            return _str[_pos++];
        }
        //retrieve unused string
        std::string_view get_unused() const {
            return _str.substr(_pos);
        }
        //move position one back to reread last item
        void put_back() {
            --_pos;
        }
    };

    using Node = typename JsonFactory::value_type;
    using KeyNode = typename JsonFactory::key_type;

    enum class State {
        detect, //<detect next element
        key,    //<reading key (string)
        array_begin,  //<begin or array (item or ])
        array_cont,   //<continue of array (, or ])
        object_begin, //<begin of object (item or })
        object_cont,  //<continue of object (, or })
        object_key    //<finalize key (:)
    };

    struct Level {
        State _st;
        std::size_t _count = 0;
    };

    static constexpr std::string_view str_true ("true");
    static constexpr std::string_view str_false("false");
    static constexpr std::string_view str_null ("null");

    static constexpr auto is_number = [](int c) {return c >= '0' && c <= '9';};
    static constexpr auto is_e = [](int c) {return c == 'e' || c == 'E';};
    static constexpr auto is_sign = [](int c) {return c == '+' || c == '-';};


    std::vector<Node> items;        //stack of items
    std::vector<KeyNode> keys;      //stack of keys
    std::vector<Level> levels;      //stack of levels
    std::vector<char> strbuff;

    CharacterSource src{source};

    levels.push_back(Level{State::detect});

    char c;

    try {
        while (!levels.empty()) {
            do c = co_await src;  while (c>=0 && c <= 32);
            switch(levels.back()._st) {

                case State::detect: {
                    std::string_view keyword;

                    levels.pop_back();
                    switch (c) {
                        default:
                            if (is_number(c) || is_sign(c)) {

                                if (is_sign(c)) {
                                    strbuff.push_back(c);
                                    c = co_await src;
                                }
                                if (!is_number(c)) throw json_parse_error::invalid_number;
                                while (is_number(c)) {
                                    strbuff.push_back(c);
                                    c = co_await src;
                                }
                                try {
                                    if (c == '.') {
                                        strbuff.push_back(c);
                                        c = co_await src;
                                        if (!is_number(c)) throw json_parse_error::invalid_number;
                                        while (is_number(c)) {
                                            strbuff.push_back(c);
                                            c = co_await src;
                                        }
                                    }
                                    if (is_e(c)) {
                                        strbuff.push_back(c);
                                        c = co_await src;
                                        if (is_sign(c)) {
                                            strbuff.push_back(c);
                                            c = co_await src;
                                        }
                                        if (!is_number(c)) throw json_parse_error::invalid_number;
                                        while (is_number(c)) {
                                            strbuff.push_back(c);
                                            c = co_await src;
                                        }
                                    }
                                    src.put_back();
                                } catch (json_parse_error::error_t e) {
                                    //EOF on top level is OK, otherwise rethrow
                                    if (e != json_parse_error::unexpected_eof
                                            || !levels.empty()
                                            || !is_number(strbuff.back())) throw;
                                }
                                strbuff.push_back('\0'); //for easy parse
                                items.push_back(fact.new_number(std::string_view(strbuff.data(), strbuff.size()-1)));
                                strbuff.clear();
                            } else{
                                throw json_parse_error::unexpected_character;
                            }
                            continue;
                        case '"': {
                            auto strconv = json_string_to_utf_8([&](char c){strbuff.push_back(c);});
                            while (strconv(co_await src));
                            items.push_back(fact.new_string(std::string_view(strbuff.data(), strbuff.size())));
                            strbuff.clear();
                        } continue;
                        case str_true[0]: keyword = str_true.substr(1);
                                  items.push_back(fact.new_bool(true));
                                  break;
                        case str_false[0]: keyword = str_false.substr(1);
                                  items.push_back(fact.new_bool(false));
                                  break;
                        case str_null[0]: keyword = str_null.substr(1);
                                  items.push_back(fact.new_null());
                                  break;
                        case '{': levels.push_back({State::object_begin});
                                  continue;
                        case '[': levels.push_back({State::array_begin});
                                  continue;
                    }
                    for (char x: keyword) {
                        c = co_await src;
                        if (c != x) throw json_parse_error::invalid_keyword;
                    }
                }break;

            case State::key: {
                levels.pop_back();
                if (c != '"') throw json_parse_error::expected_key_as_string;
                auto strconv = json_string_to_utf_8([&](char c){strbuff.push_back(c);});
                while (strconv(co_await src));
                keys.push_back(fact.new_key(std::string_view(strbuff.data(), strbuff.size())));
                strbuff.clear();
            }
                break;

            case State::array_begin:
                levels.pop_back();
                if (c == ']') {
                    items.push_back(fact.new_array(std::span<Node>{}));
                } else {
                    src.put_back();
                    levels.push_back({State::array_cont});
                    levels.push_back({State::detect});
                }
             break;

            case State::array_cont:
                ++levels.back()._count;
                if (c == ']') {
                    auto iter = items.begin() + items.size() - levels.back()._count;
                    auto result = fact.new_array(std::span<Node>(iter, items.end()));
                    items.erase(iter,items.end());
                    items.push_back(std::move(result));
                    levels.pop_back();
                } else if (c == ',') {
                    levels.push_back({State::detect});
                } else {
                    throw json_parse_error::unexpected_separator;
                }
             break;

            case State::object_begin: {
                levels.pop_back();
                if (c == '}') {
                    items.push_back(fact.new_object(std::span<KeyNode>{},std::span<Node>{}));
                } else if (c == '"') {
                    src.put_back();
                    levels.push_back({State::object_cont});
                    levels.push_back({State::object_key});
                    levels.push_back({State::key});
                } else {
                    throw json_parse_error::expected_key_as_string;
                }
            } break;

            case State::object_key: {
                levels.pop_back();
                if (c != ':') {
                    throw json_parse_error::unexpected_separator;
                }
                levels.push_back({State::detect});
            } break;

            case State::object_cont: {
                ++levels.back()._count;
                if (c == '}') {
                    auto iter_n = items.begin() + items.size() - levels.back()._count;
                    auto iter_k = keys.begin() + keys.size() - levels.back()._count;
                    auto result = fact.new_object(std::span<KeyNode>(iter_k, keys.end()),std::span<Node>(iter_n, items.end()));
                    items.erase(iter_n,items.end());
                    keys.erase(iter_k,keys.end());
                    levels.pop_back();
                    items.push_back(std::move(result));
                } else if (c == ',') {
                    levels.push_back({State::object_key});
                    levels.push_back({State::key});
                } else {
                    throw json_parse_error::unexpected_separator;
                }
            } break;
            default:
                throw json_parse_error::internal_error_invalid_state;

            }
        }

        co_return std::pair{std::move(items.back()),src.get_unused()};

    } catch (const json_parse_error::error_t &e) {
        throw json_parse_error(e, src.get_unused());
    }
}

template<json_factory JsonFactory, std::invocable<> Source>
inline coro::async<std::pair<typename JsonFactory::value_type, std::string_view> >
            parse_json(Source &&source, JsonFactory &&fact = {}) {
    return parse_json(coro::standard_allocator, std::forward<Source>(source), std::forward<JsonFactory>(fact));
}


}

}

