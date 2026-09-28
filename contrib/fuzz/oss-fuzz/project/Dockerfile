# Draft OSS-Fuzz project Dockerfile for tapyrus-core -- see README.md in this
# directory for what this is (and isn't) used for.

# Pinned, not :latest -- google/oss-fuzz-base/base-builder tracks Google's
# own Clang trunk and gets rebuilt multiple times a week (confirmed: the
# image used throughout this Dockerfile's own debugging reported itself as
# "Clang 22.0.0", a dev snapshot, not a numbered release). Floating on
# :latest would mean every local rebuild risks a fresh toolchain change
# breaking the build out from under you mid-debugging, unrelated to
# anything in this repo. Bump this digest deliberately (fuzz_code_step1_
# build_image.py's --base-image flag can override it for a one-off test)
# rather than letting it drift silently.
ARG BASE_IMAGE=gcr.io/oss-fuzz-base/base-builder@sha256:8b4a73d83374b298a0a771eeec9a1d44ceff3416a678b7d7946303389d022aca
FROM ${BASE_IMAGE}

RUN apt-get update && apt-get install -y \
  build-essential cmake curl g++-multilib make \
  patch python3 libboost-dev libevent-dev libdb++-dev zip

# Pre-installed here (not by fuzz_code_step3_analyze.py at analysis time)
# so step 3 never repeats this pip install across runs against the same
# image -- the whole point of splitting build (infrequent) from analyze
# (frequent) is that analyze shouldn't pay setup costs step 1 already paid.
RUN cd /fuzz-introspector/src && python3 -m pip install -q -e .

# The base image's apt-installed libboost-dev is 1.71.0 (its Ubuntu base
# predates that version), but tapyrus-core's cmake/module/AddBoostIfNeeded.cmake
# requires >= 1.73.0 -- confirmed by actually running this Dockerfile's build
# and hitting CMake's "Found unsuitable version 1.71.0" error. Header-only
# usage (AddBoostIfNeeded.cmake never requests linkable components), so
# fetching a newer release's headers is enough -- no b2/bootstrap build
# needed. archives.boost.io (the current official distribution host,
# succeeding the deprecated jfrog/SourceForge mirrors) serves the classic
# pre-flattened source tarball with boost/config.hpp already at the
# top level -- confirmed directly, unlike GitHub's boostorg/boost release
# tarballs, which ship boost's post-modularization per-library submodule
# layout (headers under libs/<lib>/include/boost/...) with no flat boost/
# directory unless you run `b2 headers` yourself.
RUN curl -fsSL https://archives.boost.io/release/1.83.0/source/boost_1_83_0.tar.gz \
      -o /tmp/boost.tar.gz \
  && mkdir -p /opt/boost \
  && tar -xzf /tmp/boost.tar.gz -C /opt/boost --strip-components=1 \
  && rm /tmp/boost.tar.gz
ENV BOOST_INCLUDEDIR=/opt/boost

# This clone + submodule init is dead weight for this repo's own
# in-pipeline usage: fuzz_code_step2_start_container.py bind-mounts a live
# host checkout at this same /src/tapyrus-core path, which shadows
# whatever this RUN step put there -- confirmed directly (the container
# analyzes/builds the bind-mounted host source, never this cloned copy).
# Left in (rather than deleted) because it's still exactly what a genuine
# OSS-Fuzz submission needs: OSS-Fuzz's own infra clones this Dockerfile
# with no bind mount available, so it must fetch its own copy of the
# source to build. If you only use this Dockerfile via this repo's own
# step1/step2/step3 scripts, the git submodule update here never runs for
# your actual analysis -- the host's own src/secp256k1 submodule must
# already be initialized before step 2 bind-mounts it in (that script
# checks this and fails fast with a clear message rather than letting an
# uninitialized submodule surface later as a confusing CMake configure
# error inside the container).
RUN git clone --depth=1 https://github.com/chaintope/tapyrus-core.git tapyrus-core
WORKDIR tapyrus-core
RUN git submodule update --init --depth=1 src/secp256k1
COPY build.sh force_static_libs.cmake $SRC/
