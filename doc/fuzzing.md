Fuzz-testing Bitcoin Core
==========================

A special test harness `test_bitcoin_fuzzy` is provided to provide an easy
entry point for fuzzers and the like. In this document we'll describe how to
use it with AFL.

Building AFL
-------------

It is recommended to always use the latest version of afl:
```
wget http://lcamtuf.coredump.cx/afl/releases/afl-latest.tgz
tar -zxvf afl-latest.tgz
cd afl-<version>
make
export AFLPATH=$PWD
```

Instrumentation
----------------

To build Bitcoin Core using AFL instrumentation (this assumes that the
`AFLPATH` was set as above):
```
cmake -S . -B build -DCMAKE_C_COMPILER=${AFLPATH}/afl-gcc -DCMAKE_CXX_COMPILER=${AFLPATH}/afl-g++ -DENABLE_TESTS=ON
export AFL_HARDEN=1
cmake --build build --target test_bitcoin_fuzzy
```
We disable ccache in the CMake build because we don't want to pollute the ccache with instrumented
objects, and similarly don't want to use non-instrumented cached objects linked
in.

The fuzzing can be sped up significantly (~200x) by using `afl-clang-fast` and
`afl-clang-fast++` in place of `afl-gcc` and `afl-g++` when compiling. When
compiling using `afl-clang-fast`/`afl-clang-fast++` the resulting
`test_bitcoin_fuzzy` binary will be instrumented in such a way that the AFL
features "persistent mode" and "deferred forkserver" can be used. See
https://github.com/mcarpenter/afl/tree/master/llvm_mode for details.

Preparing fuzzing
------------------

AFL needs an input directory with examples, and an output directory where it
will place examples that it found. These can be anywhere in the file system,
we'll define environment variables to make it easy to reference them.

```
mkdir inputs
AFLIN=$PWD/inputs
mkdir outputs
AFLOUT=$PWD/outputs
```

Example inputs are available from:

- https://download.visucore.com/bitcoin/bitcoin_fuzzy_in.tar.xz
- http://strateman.ninja/fuzzing.tar.xz

Extract these (or other starting inputs) into the `inputs` directory before starting fuzzing.

Fuzzing
--------

To start the actual fuzzing use:
```
$AFLPATH/afl-fuzz -i ${AFLIN} -o ${AFLOUT} -m52 -- test/test_bitcoin_fuzzy
```

You may have to change a few kernel parameters to test optimally - `afl-fuzz`
will print an error and suggestion if so.
