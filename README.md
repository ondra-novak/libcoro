# LIBCORO 

C++20 support for coroutines and asynchronous operations

## Install - system wide

```
$ mkdir build
$ cd build
$ cmake ..
$ make all
$ sudo make install
```

When library is installed, you can use FIND_PACKAGE(libcoro) to add the library to the project


## Install - as submodule of a project

You can add libcoro as submodule to your project. Then you only need to include following lines into your CMakeLists.txt

```
set(LIBCORO_SUBMODULE_PATH "src/libcoro")
include(${LIBCORO_SUBMODULE_PATH}/library.cmake)
```

## Using the library

Because the library is **header only** you can just an include everywhere you need to use the library

```
#include <coro.h>
```

## Documentation

Doxygen: [https://ondra-novak.github.io/libcoro/](https://ondra-novak.github.io/libcoro/)



