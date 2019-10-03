#!/usr/bin/env bash

export MAKEJOBS=-j3
export RUN_TESTS=false
export RUN_BENCH=false  # Set to true for any one job that has debug enabled, to quickly check bench is not crashing or hitting assertions
export LC_ALL=C.UTF-8
export BOOST_TEST_RANDOM=1$RANDOM
export CCACHE_SIZE=100M
export CCACHE_TEMPDIR=/tmp/.ccache-temp
export CCACHE_COMPRESS=1
export CCACHE_DIR=$GITHUB_WORKSPACE/.ccache
export BASE_OUTDIR=$GITHUB_WORKSPACE/out
export SDK_URL=https://bitcoincore.org/depends-sources/sdks
export WINEDEBUG=fixme-all
export BUILD_PACKAGES="build-essential libtool autotools-dev automake pkg-config bsdmainutils curl git ca-certificates ccache"