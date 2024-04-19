#include "parser.h"
#include "serializer.h"
#include "simple_json.h"

#include "../../coro/future.h"

#include <iostream>

static_assert(json_factory<JsonFactory>);
static_assert(json_decomposer<JsonDecomposer>);


void test1() {
    std::string text = R"json(
    {
      "aaa":"bbb",
      "bool":true,
      "bool2":false,
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

    coro::future<std::string_view> data(text);
    auto res = parse_json<JsonFactory>(data).run();

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
