#!/bin/bash

# Before Install
RETRY() { for i in {1..3};do $*&&break;sleep 1;done }
export PATH=$(echo $PATH | tr ':' "\n" | sed '/\/opt\/python/d' | tr "\n" ":" | sed "s|::|:|g")

# Install
env | grep -E '^(CCACHE_|WINEDEBUG|LC_ALL|BOOST_TEST_RANDOM|CONFIG_SHELL)' | tee /tmp/env
if [ -n "$DPKG_ADD_ARCH" ]; then sudo dpkg --add-architecture "$DPKG_ADD_ARCH" ; fi
RETRY sudo apt-get update
RETRY sudo apt-get install --no-install-recommends --no-upgrade -qq $PACKAGES $BUILD_PACKAGES linux-headers-generic

# Before Script
echo \> \$HOME/.tapyrus  # Make sure default datadir does not exist and is never read by creating a dummy file
mkdir -p depends/SDKs depends/sdk-sources
export XCODE_VERSION=15.0
export XCODE_BUILD_ID=15A240d
OSX_SDK_BASENAME="Xcode-${XCODE_VERSION}-${XCODE_BUILD_ID}-extracted-SDK-with-libcxx-headers"

if [ -n "$XCODE_VERSION" ] && [ ! -d "depends/SDKs/${OSX_SDK_BASENAME}" ]; then
  OSX_SDK_FILENAME="${OSX_SDK_BASENAME}.tar.gz"
  OSX_SDK_PATH="depends/sdk-sources/${OSX_SDK_FILENAME}"
  if [ ! -f "$OSX_SDK_PATH" ]; then
    RETRY curl --location --fail "${SDK_URL}/${OSX_SDK_FILENAME}" -o "$OSX_SDK_PATH"
  fi
  tar -C "depends/SDKs" -xf "$OSX_SDK_PATH"
fi
if [[ $HOST = *-mingw32 ]]; then sudo update-alternatives --set $HOST-g++ $(which $HOST-g++-posix); fi
if [ -z "$NO_DEPENDS" ]; then CONFIG_SHELL= make $MAKEJOBS -C depends HOST=$HOST $DEP_OPTS; fi

# Script
export COMMIT_LOG=`git log --format=fuller -1`
OUTDIR=$BASE_OUTDIR/$GITHUB_SHA/$HOST
BITCOIN_CONFIG_ALL="--disable-dependency-tracking --prefix=${GITHUB_WORKSPACE}/depends/$HOST --bindir=$OUTDIR/bin --libdir=$OUTDIR/lib"
if [ -z "$NO_DEPENDS" ]; then ccache --max-size=$CCACHE_SIZE; fi
test -n "$CONFIG_SHELL" && bash -c "$CONFIG_SHELL" -c "./autogen.sh" || ./autogen.sh
mkdir build && cd build
../configure --cache-file=config.cache $BITCOIN_CONFIG_ALL $BITCOIN_CONFIG || ( cat config.log && false)
make distdir VERSION=$HOST
cd tapyrus-core-$HOST
./configure --cache-file=../config.cache $BITCOIN_CONFIG_ALL $BITCOIN_CONFIG || ( cat config.log && false)
make $MAKEJOBS $GOAL || ( echo "Build failure. Verbose build follows." && make $GOAL V=1 ; false )
if [ "$RUN_TESTS" = "true" ]; then LD_LIBRARY_PATH=${GITHUB_WORKSPACE}/depends/$HOST/lib make $MAKEJOBS check VERBOSE=1; fi
if [ "$RUN_BENCH" = "true" ]; then LD_LIBRARY_PATH=${GITHUB_WORKSPACE}/depends/$HOST/lib $OUTDIR/bin/bench_tapyrus -scaling=0.001 ; fi
if [ "$DEBUD_MODE" = "true" ]; then debugscripts="--debugscripts"; fi
if [ "$RUN_TESTS" = "true" ]; then test/functional/test_runner.py --combinedlogslen=4000 --coverage --failfast --quiet --extended ${debugscripts}; fi
