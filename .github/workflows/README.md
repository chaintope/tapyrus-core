# Tapyrus Core Cmake CI

There are two different CI

## Smoke test

This CI is aimed to give quick feedback after every checkin and every PR. So the duration of this CI is kept under 30 minutes. This CI uses native compilation in Linux and MacOS in both x86_64 and arm64 architectures. There are a total of four jobs. All of these jobs use the following settings:

1. Compiler - clang
2. Dependencies - os libraries(installed during setup stage)
3. Executables compiled - tapyrusd, tapyrus-cli, tapyrus-genesis, tapyrus-tx, tapyrus-test, bench_tapyrus.
4. Tests - unit tests, util tests, benchmark test, functional tests that take less time


## Daily test

The daily CI test is a comprehensive test that runs many combinations of build and tests. It takes four hours to complete and is run once a day according to the schedule.

1. Compiler - clang on Macos, gcc on linux
2. Dependencies - depends folder built locally
3. Functional test - all functional tests are executes
4. Debug mode - linux and macos
5. USDT Built and test(in progress) - linux RelWithDebInfo mode
6. Qt Gui - built and tested in Debug and RelWithDebInfo modes


## Atrefacts

Both CI upload the benchmark test result and functional test logs as artefacts. Note that functional test logs are empty when the test succeeds. Core dumps if present are analyzed and the result are uploaded.

