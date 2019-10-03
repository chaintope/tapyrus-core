#!/bin/bash

# Before Install
RETRY() { for i in {1..3};do $*&&break;sleep 1;done }
export PATH=$(echo $PATH | tr ':' "\n" | sed '/\/opt\/python/d' | tr "\n" ":" | sed "s|::|:|g")

# Install
env | grep -E '^(CCACHE_|WINEDEBUG|LC_ALL|BOOST_TEST_RANDOM|CONFIG_SHELL)' | tee /tmp/env
if [ -n "$DPKG_ADD_ARCH" ]; then dpkg --add-architecture "$DPKG_ADD_ARCH" ; fi
RETRY sudo apt-get update
RETRY sudo apt-get install --no-install-recommends --no-upgrade -qq $PACKAGES $BUILD_PACKAGES

# Before Script
echo \> \$HOME/.bitcoin  # Make sure default datadir does not exist and is never read by creating a dummy file
mkdir -p depends/SDKs depends/sdk-sources
if [ -n "$OSX_SDK" -a ! -f depends/sdk-sources/MacOSX${OSX_SDK}.sdk.tar.gz ]; then curl --location --fail $SDK_URL/MacOSX${OSX_SDK}.sdk.tar.gz -o depends/sdk-sources/MacOSX${OSX_SDK}.sdk.tar.gz; fi
if [ -n "$OSX_SDK" -a -f depends/sdk-sources/MacOSX${OSX_SDK}.sdk.tar.gz ]; then tar -C depends/SDKs -xf depends/sdk-sources/MacOSX${OSX_SDK}.sdk.tar.gz; fi
if [[ $HOST = *-mingw32 ]]; then update-alternatives --set $HOST-g++ $(which $HOST-g++-posix); fi
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
cd tapyrus-$HOST
./configure --cache-file=../config.cache $BITCOIN_CONFIG_ALL $BITCOIN_CONFIG || ( cat config.log && false)
make $MAKEJOBS $GOAL || ( echo "Build failure. Verbose build follows." && make $GOAL V=1 ; false )
if [ "$RUN_TESTS" = "true" ]; then LD_LIBRARY_PATH=${GITHUB_WORKSPACE}/depends/$HOST/lib make $MAKEJOBS check VERBOSE=1; fi
if [ "$RUN_BENCH" = "true" ]; then LD_LIBRARY_PATH=${GITHUB_WORKSPACE}/depends/$HOST/lib $OUTDIR/bin/bench_tapyrus -scaling=0.001 ; fi
if [ "$TRAVIS_EVENT_TYPE" = "cron" ]; then extended="--extended --exclude feature_pruning,feature_dbcrash"; fi
if [ "$RUN_TESTS" = "true" ]; then test/functional/test_runner.py --combinedlogslen=4000 --coverage --failfast --quiet ${extended}; fi
