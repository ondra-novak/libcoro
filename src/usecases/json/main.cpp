#include "parser.h"
#include "serializer.h"
#include "simple_json.h"

#include "../../coro/future.h"

#include <iostream>

using namespace coro_usecases::json;
static_assert(json_factory<JsonFactory>);
static_assert(json_decomposer<JsonDecomposer>);


void test1() {
    std::string text = R"json(
    {
      "aaa":"bbb",
      "bool":true,
      "bool2":false,
      "utf-8":"\n\r\\\" ahoj \uD83D\uDE00",
      "n":null,
      "num1":10,
      "num2":-23,
      "num3":1.324,
      "num4":-12.980,
      "num5":1.8921e14,
      "num6":+1.333e-0007,
      "arr":[1,2,3,true,false,"hallo"],
      "obj":{"sub1":null},
      "arr2":[],
      "obj2":{
}
}
)json";

    auto source = [text,done = false]() mutable ->coro::future<std::string_view> {
        if (done) return "";
        else {
            done = true;
            return text;
        }};

    auto parser = json_parser<JsonFactory, decltype(source)>();
    auto res = parser(source).get();

    auto source2 = [text, done = false]() mutable -> std::string_view {
        if (done) return "";
        else {
            done = true;
            return text;
        }};

    auto parser2 = json_parser<JsonFactory, decltype(source2)>();
    res = parser2(source2).get();


    std::string out;
    serialize_json<JsonDecomposer>(res.first, [&](std::string_view z){
        out.append(z);
        return std::suspend_never();
    }).run();

    std::cout << out << std::endl;




}


int main() {
    test1();
}
