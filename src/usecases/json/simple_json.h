#pragma once

#include <map>
#include <string>
#include <variant>
#include <vector>
#include <span>


class Json;


using JsonVariant = std::variant<
        std::nullptr_t,
        bool,
        std::string,
        double,
        std::vector<Json>,
        std::map<std::string, Json>
>;

class Json: public JsonVariant {
public:
    using JsonVariant::JsonVariant;
};



class JsonFactory {
public:
    using value_type = Json;
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
        return Json(std::in_place_type<std::vector<Json> >, items.begin(), items.end());
    }
    static Json new_object(std::span<Json> keys, std::span<Json> items) {
        auto k = keys.begin();
        std::map<std::string, Json> m;
        for (auto &x: items) {
            m.emplace(std::move(std::get<std::string>(*k)), std::move(x));
            ++k;
        }
        return Json(std::move(m));
    }
};


class JsonDecomposer {
public:
    using value_type = Json;
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
    std::string_view get_string(const Json &v) {
        if (std::holds_alternative<double>(v)) {
            tmp = std::to_string(std::get<double>(v));
            return tmp;
        } else if (std::holds_alternative<std::string>(v)) {
            return std::get<std::string>(v);
        } else {
            return {};
        }
    }

    static bool get_bool(const Json &v) {
        if (std::holds_alternative<bool>(v)) {
            return std::get<bool>(v);
        } else {
            return false;
        }
    }

    static std::size_t get_count(const Json &v) {
        if (std::holds_alternative<std::vector<Json> >(v)) {
            return std::get<std::vector<Json> >(v).size();
        } else if (std::holds_alternative<std::map<std::string, Json> >(v)) {
            return std::get<std::map<std::string, Json> >(v).size();
        } else {
            return 0;
        }
    }
    static const Json &item_at(const Json &v, std::size_t pos) {
        if (std::holds_alternative<std::vector<Json> >(v)) {
            return std::get<std::vector<Json> >(v).at(pos);
        } else if (std::holds_alternative<std::map<std::string, Json> >(v)) {
            auto beg = std::get<std::map<std::string, Json> >(v).begin();
            std::advance(beg, pos);
            return beg->second;
        } else {
            throw;
        }
    }
    static std::string_view key_at(const Json &v, std::size_t pos) {
        if (std::holds_alternative<std::map<std::string, Json> >(v)) {
            auto beg = std::get<std::map<std::string, Json> >(v).begin();
            std::advance(beg, pos);
            return beg->first;
        } else {
            return {};
        }
    }

    std::string tmp;

};
