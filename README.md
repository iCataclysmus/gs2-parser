# gs2-parser

This is a compiler for the Graal Script 2 (GS2) language.

# Prerequisites

Before building, clone the repository and recursively clone the submodules:

```sh
git clone git@github.com:xtjoeytx/gs2-parser.git --recursive
```

# Building

You can build the project using CMake:

```sh
mkdir build
cd build
cmake ..
make -j $(nproc)
```

# Building (Wasm)

First, ensure you have Emscripten installed. Then, you can build the project using CMake:

```sh
mkdir build
cd build
emcmake cmake ..
make -j $(nproc)
```

The resulting `gs2test.js` file can be imported into a webpage.

# Running

The non-wasm build can be run using the following command:

```sh
$ ./gs2test ../scripts/asd2.txt
Argc: 2
Args: ../scripts/asd2.txt
Compiling file ../scripts/asd2.txt
Compiled in 0.001322 seconds
 -> saved to ../scripts/asd2.gs2bc
Total length of bytecode w/ headers:   160
```