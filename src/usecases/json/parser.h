#pragma once

#include "../../coro/generator.h"
#include "../../coro/async.h"
#include "../../coro/on_leave.h"
#include "../../coro/make_awaitable.h"


#include <iterator>
#include <span>
#include <string_view>
#include <concepts>
#include <vector>
#include <variant>
#include <cstring>

namespace coro_usecases {

namespace json {

template<typename T>
concept json_factory = requires(T obj,
        std::string_view s,
        bool b,
        std::span<typename T::value_type> a,
        std::span<typename T::key_type> k) {

    ///creates JSON value from string
    {obj.new_string(s)}->coro::maybe_awaitable<typename T::value_type>;
    ///creates JSON value from number - passed as string
    {obj.new_number(s)}->coro::maybe_awaitable<typename T::value_type>;
    ///creates JSON value from boolean
    {obj.new_bool(b)}->coro::maybe_awaitable<typename T::value_type>;
    ///creates JSON null
    {obj.new_null()}->coro::maybe_awaitable<typename T::value_type>;
    ///creates JSON array passing an span of JSON values
    {obj.new_array(a)}->coro::maybe_awaitable<typename T::value_type>;
    ///creates JSON key value (can be string), from a string
    {obj.new_key(s)}->coro::maybe_awaitable<typename T::key_type>;
    ///creates JSON object passing an span of JSON keys and JSON values
    {obj.new_object(k,a)}->coro::maybe_awaitable<typename T::value_type>;
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

template<typename Source>
struct CharacterSource {

    using SourceAwaiter = coro::make_awaitable<Source>;
    static_assert(coro::awaitable_r<SourceAwaiter, std::string_view>, "Source must returns string_view and must be avaitable");


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
        //clear the flag
        _fetched = false;

        if (_pos < _str.length()) [[likely]] {
            return true;
        }
        //start lifetime of SourceAwaiter (return value of _src())
        new (&_srcawt) SourceAwaiter(_src);
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
        if (_fetched) [[unlikely]]{
            //action perfomed on exit (exception)
            coro::on_leave finally=[this]{
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
    template<std::invocable<char> Condition>
    void copy_until(Condition &&cond, std::vector<char> &cont) {
        const char *b = _str.data();
        const char *c = b +_pos;
        const char *e = b +_str.length();
        while (c != e && cond(*c)) ++c;
        std::size_t fnd = c- b;
        if (fnd == _pos) return;
        auto sz = (fnd-_pos);
        auto bufsz = cont.size();
        cont.resize(bufsz+sz);
        std::copy(_str.data()+_pos, _str.data()+_pos+sz, cont.data()+bufsz);
        _pos = fnd;

    }
};


///Construct JSON parser / generator
/**
 * result of this function is generator with argument, where argument is source
 * @tparam Allocator coro allocator to allocate coroutine frame
 * @tparam JsonFactory type resposible to create JSON objects
 * @tparam Source type, which acts as function returning awaitable, which returns std::string_view
 * @param Allocator allocator instance
 * @param fact json factory instance
 * @return returns function, which is called with instance of Source. The function returns
 * future, which eventually returns pair of result and unprocessed input.
 *
 * @note you need to avoid destruction of the parser, before it generates result. You
 * can reuse parser to parse multiple JSONs in sequence (never in parallel).
 */
template<coro::coro_allocator Allocator, json_factory JsonFactory, std::invocable<> Source>
inline coro::generator<std::pair<typename JsonFactory::value_type, std::string_view>(Source &) >
            json_parser(Allocator &, JsonFactory fact = {}) {




    using Node = typename JsonFactory::value_type;
    using KeyNode = typename JsonFactory::key_type;

    enum class State {
        detect, //<detect next element
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
    std::exception_ptr e;
    bool key_req = false;

    char c;
    strbuff.reserve(100);

    do {


        auto &[source] = co_yield coro::fetch_args;
        CharacterSource src{source};

        levels.push_back(Level{State::detect});

        try {
            while (!levels.empty()) {
                do {
                    c = co_await src;
                } while (c == '\r' || c == '\n' || c == ' ' || c == '\t');
                switch(levels.back()._st) {

                    case State::detect: {
                        std::string_view keyword;

                        levels.pop_back();
                        switch (c) {
                            default:
                                if (c == '-' || is_number(c)) {
                                    static constexpr auto cond = [](char c) {
                                        return (c != ',' && c != '}' && c != ']' );
                                    };
                                    do {
                                        strbuff.push_back(c);
                                        src.copy_until(cond, strbuff);
                                        c = co_await src;
                                    } while (cond(c));
                                    strbuff.push_back('\0');
                                    std::size_t pos =0;
                                    if (strbuff[pos] == '-') ++pos;
                                    if (strbuff[pos] == '0') ++pos;
                                    else if (is_number(strbuff[pos])) {
                                        ++pos;
                                        while (is_number(strbuff[pos])) ++pos;
                                    } else {
                                        throw json_parse_error::invalid_number;
                                    }
                                    if (strbuff[pos] == '.') {
                                        ++pos;
                                        if (is_number(strbuff[pos])) {
                                            ++pos;
                                            while (is_number(strbuff[pos])) ++pos;
                                        } else {
                                            throw json_parse_error::invalid_number;
                                        }
                                    }
                                    if (is_e(strbuff[pos])) {
                                        ++pos;
                                        if (is_sign(strbuff[pos])) {
                                            ++pos;
                                        }
                                        if (is_number(strbuff[pos])) {
                                            ++pos;
                                            while (is_number(strbuff[pos])) ++pos;
                                        } else {
                                            throw json_parse_error::invalid_number;
                                        }
                                    }
                                    auto end_of_numb = pos;
                                    while (pos < strbuff.size()) {
                                        if (static_cast<unsigned char>(strbuff[pos])>32) {
                                            throw json_parse_error::invalid_number;
                                        }
                                        ++pos;
                                    }
                                    items.push_back(co_await coro::make_awaitable([&]{
                                        return fact.new_number(std::string_view(strbuff.data(), end_of_numb));
                                    }));
                                    strbuff.clear();
                                    src.put_back();
                                } else{
                                    throw json_parse_error::unexpected_character;
                                }
                                continue;
                            case '"': {
                                int first_codepoint = 0;
                                do {
                                    src.copy_until([](char c){return c != '"' && c != '\\';}, strbuff);
                                    c = co_await src;
                                    if (c == '"') break;
                                    if (c == '\\') {
                                        c = co_await src;
                                        switch (c) {
                                              default: break;
                                              case 'b':c = '\b';break;
                                              case 'f':c = '\f';break;
                                              case 'n':c = '\n';break;
                                              case 'r':c = '\r';break;
                                              case 't':c = '\t';break;
                                              case 'u': {
                                                  int codepoint = 0;
                                                  for (int i = 0; i < 4; ++i) {
                                                      unsigned char c = co_await src;
                                                      if (c >= 'A') {
                                                          c = (c | 0x20) - 87;
                                                          if (c > 0xF) throw json_parse_error::invalid_unicode;

                                                      } else {
                                                          c -= '0';
                                                          if (c > 9) throw json_parse_error::invalid_unicode;;
                                                      }

                                                      codepoint=(codepoint << 4) | c;
                                                  }
                                                  if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
                                                      if (first_codepoint) {
                                                          if (first_codepoint > codepoint) std::swap(first_codepoint, codepoint);
                                                          codepoint = 0x10000 + ((first_codepoint - 0xD800) << 10) + (codepoint - 0xDC00);
                                                      } else {
                                                          first_codepoint = codepoint;
                                                          continue;
                                                      }
                                                  }
                                                  if (codepoint <= 0x7F) {
                                                      strbuff.push_back(static_cast<char>(codepoint));
                                                  } else if (codepoint <= 0x7FF) {
                                                      strbuff.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
                                                      strbuff.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                                                  } else if (codepoint <= 0xFFFF) {
                                                      strbuff.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
                                                      strbuff.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                                                      strbuff.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                                                  } else if (codepoint <= 0x10FFFF) {
                                                      strbuff.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
                                                      strbuff.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
                                                      strbuff.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                                                      strbuff.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                                                  }
                                                  first_codepoint = 0;
                                              }continue;
                                        }
                                    }
                                    strbuff.push_back(c);
                                } while (true);

                                if (key_req) {
                                    key_req = false;
                                    keys.push_back(co_await coro::make_awaitable([&]{
                                        return fact.new_key(std::string_view(strbuff.data(), strbuff.size()));
                                    }));
                                } else {
                                    items.push_back(co_await coro::make_awaitable([&]{
                                        return fact.new_string(std::string_view(strbuff.data(), strbuff.size()));
                                    }));

                                }
                                strbuff.clear();
                            } continue;
                            case str_true[0]: keyword = str_true.substr(1);
                                      items.push_back(co_await coro::make_awaitable([&]{
                                          return fact.new_bool(true);
                                      }));
                                      break;
                            case str_false[0]: keyword = str_false.substr(1);
                                      items.push_back(co_await coro::make_awaitable([&]{
                                          return fact.new_bool(false);
                                      }));
                                      break;
                            case str_null[0]: keyword = str_null.substr(1);
                                      items.push_back(co_await coro::make_awaitable([&]{
                                          return fact.new_null();
                                      }));
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


                case State::array_begin:
                    levels.pop_back();
                    if (c == ']') {
                        items.push_back(co_await coro::make_awaitable([&]{
                            return fact.new_array(std::span<Node>{});
                        }));
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
                        auto result = co_await coro::make_awaitable([&]{
                            return fact.new_array(std::span<Node>(iter, items.end()));
                        });
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
                        items.push_back(co_await coro::make_awaitable([&]{
                            return fact.new_object(std::span<KeyNode>{},std::span<Node>{});
                        }));
                    } else if (c == '"') {
                        src.put_back();
                        levels.push_back({State::object_cont});
                        levels.push_back({State::object_key});
                        levels.push_back({State::detect});
                        key_req = true;
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
                        auto result = co_await coro::make_awaitable([&]{
                            return fact.new_object(std::span<KeyNode>(iter_k, keys.end()),std::span<Node>(iter_n, items.end()));
                        });
                        items.erase(iter_n,items.end());
                        keys.erase(iter_k,keys.end());
                        levels.pop_back();
                        items.push_back(std::move(result));
                    } else if (c == ',') {
                        levels.push_back({State::object_key});
                        levels.push_back({State::detect});
                        key_req = true;
                    } else {
                        throw json_parse_error::unexpected_separator;
                    }
                } break;
                default:
                    throw json_parse_error::internal_error_invalid_state;

                }
            }

            co_yield std::pair{std::move(items.back()),src.get_unused()};

            items.clear();
            levels.clear();
            keys.clear();

            continue;

        } catch (const json_parse_error::error_t &ep) {
            e = std::make_exception_ptr(json_parse_error(ep, src.get_unused()));
        }
        co_yield e;

        items.clear();
        levels.clear();
        keys.clear();

    } while (true);
}

template<json_factory JsonFactory, std::invocable<> Source>
inline coro::generator<std::pair<typename JsonFactory::value_type, std::string_view>(Source &) >
            json_parser(JsonFactory fact = {}) {

    return json_parser<const coro::std_allocator, JsonFactory, Source>(coro::standard_allocator, std::forward<JsonFactory>(fact));
}


}

}

