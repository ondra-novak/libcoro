#pragma once

#include <map>
#include <string>
#include <variant>
#include <vector>
#include <span>

namespace coro_usecases {

namespace json {




class Json;


using JsonArray = std::vector<Json>;
using JsonObject = std::map<std::string, Json>;

using JsonVariant = std::variant<
        std::nullptr_t,
        bool,
        std::string,
        double,
        JsonArray,
        JsonObject
>;

class Json: public JsonVariant {
public:
    using JsonVariant::JsonVariant;
};



class JsonFactory {
public:
    using value_type = Json;
    using key_type = std::string;
    static Json new_string(std::string_view s) {
        return Json(std::in_place_type<std::string>, s);
    }
    static Json new_number(std::string_view s)  {
        return Json(std::strtod(s.data(),nullptr));
    }
    static Json new_bool(bool b) {
        return Json(b);
    }
    static Json new_null() {
        return Json(nullptr);
    }
    static Json new_array(std::span<Json> items) {
        return Json(std::in_place_type<JsonArray>, items.begin(), items.end());
    }
    static Json new_object(std::span<std::string> keys, std::span<Json> items) {
        auto k = keys.begin();
        JsonObject m;
        for (auto &x: items) {
            m.emplace(std::move(*k), std::move(x));
            ++k;
        }
        return Json(std::move(m));
    }
    static std::string new_key(std::string_view str) {
        return std::string(str);
    }
};



class JsonDecomposer {
public:
    class number_type: public std::string_view {
    public:
        using std::string_view::string_view;
        number_type(std::string_view x):std::string_view(x) {}
    };
    class string_type: public std::string_view {
    public:
        using std::string_view::string_view;
        string_type(std::string_view x):std::string_view(x) {}
    };
    using value_type = Json;

    template<typename Iter>
    class range: std::pair<Iter,Iter> {
    public:
        using std::pair<Iter,Iter>::pair;
        Iter begin() const {return this->first;}
        Iter end() const {return this->second;}
    };

    using array_range = range<JsonArray::const_iterator>;
    using object_range =range<JsonObject::const_iterator>;

    template<typename Fn>
    auto visit(Fn &&fn, const Json &v) {
        return std::visit([&](const auto &x){
           using T = std::decay_t<decltype(x)>;
           if constexpr(std::is_same_v<T, std::nullptr_t>) {
               return fn(nullptr);
           } else if constexpr(std::is_same_v<T, bool>) {
               return fn(x);
           } else if constexpr(std::is_same_v<T, std::string>) {
               return fn(string_type(x));
           } else if constexpr(std::is_same_v<T, double>) {
               tmp = std::to_string(x);
               return fn(number_type(tmp));
           } else if constexpr(std::is_same_v<T, JsonArray>) {
               return fn(array_range(x.begin(), x.end()));
           } else if constexpr(std::is_same_v<T, JsonObject>) {
               return fn(object_range(x.begin(), x.end()));
           }
        },v);
    }

    static std::string_view key(const JsonObject::const_iterator &iter) {
        return iter->first;
    }
    static const Json & value(const JsonObject::const_iterator &iter) {
        return iter->second;
    }
    static const Json & value(const JsonArray::const_iterator &iter) {
        return *iter;
    }

protected:
    std::string tmp;

};
}
}
