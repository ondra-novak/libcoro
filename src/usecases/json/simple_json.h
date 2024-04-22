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
    using value_type = Json;
    using key_type = std::string;
    static json_value_type type(const Json &v) {
        static constexpr json_value_type types[] = {
                json_value_type::null,
                json_value_type::boolean,
                json_value_type::string,
                json_value_type::number,
                json_value_type::array,
                json_value_type::object,
        };;
        return types[v.index()];
    }
    std::string_view get_number(const Json &v) {
        tmp = std::to_string(std::get<double>(v));
        return tmp;
    }


    std::string_view get_string(const Json &v) {
        return std::get<std::string>(v);
    }

    static bool get_bool(const Json &v) {
       return std::get<bool>(v);
    }

    static auto get_array_begin(const Json &v) {
        return std::get<JsonArray>(v).begin();
    }
    static auto get_array_end(const Json &v) {
        return std::get<JsonArray>(v).end();
    }
    static const Json &get_value(const JsonArray::const_iterator &iter) {
        return *iter;
    }
    static auto get_object_begin(const Json &v) {
        return std::get<JsonObject>(v).begin();
    }
    static auto get_object_end(const Json &v) {
        return std::get<JsonObject>(v).end();
    }
    static const Json &get_value(const JsonObject::const_iterator &iter) {
        return iter->second;
    }
    static std::string_view get_key(const JsonObject::const_iterator &iter) {
        return iter->first;
    }
    static std::size_t get_array_size(const Json &v) {
        return std::get<JsonArray>(v).size();
    }
    static std::size_t get_object_size(const Json &v) {
        return std::get<JsonObject>(v).size();
    }


    std::string tmp;

};
}
}
