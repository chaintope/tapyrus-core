#!/usr/bin/env bash
# Copyright (c) 2019-2022 The Bitcoin Core developers
# Copyright (c) 2025 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
export LC_ALL=C
set -e -o pipefail
export TZ=UTC

# Although Guix _does_ set umask when building its own packages (in our case,
# this is all packages in manifest.scm), it does not set it for `guix
# shell`. It does make sense for at least `guix shell --container`
# to set umask, so if that change gets merged upstream and we bump the
# time-machine to a commit which includes the aforementioned change, we can
# remove this line.
#
# This line should be placed before any commands which creates files.
umask 0022

if [ -n "$V" ]; then
    # Print both unexpanded (-v) and expanded (-x) forms of commands as they are
    # read from this file.
    set -vx
    # Set VERBOSE for CMake-based builds
    export VERBOSE="$V"
fi

# Check that required environment variables are set
cat << EOF
Required environment variables as seen inside the container:
    DIST_ARCHIVE_BASE: ${DIST_ARCHIVE_BASE:?not set}
    DISTNAME: ${DISTNAME:?not set}
    HOST: ${HOST:?not set}
    SOURCE_DATE_EPOCH: ${SOURCE_DATE_EPOCH:?not set}
    JOBS: ${JOBS:?not set}
    DISTSRC: ${DISTSRC:?not set}
    OUTDIR: ${OUTDIR:?not set}
EOF

ACTUAL_OUTDIR="${OUTDIR}"
OUTDIR="${DISTSRC}/output"

#####################
# Environment Setup #
#####################

# The depends folder also serves as a base-prefix for depends packages for
# $HOSTs after successfully building.
BASEPREFIX="${PWD}/depends"

# Given a package name and an output name, return the path of that output in our
# current guix environment
store_path() {
    grep --extended-regexp "/[^-]{32}-${1}-[^-]+${2:+-${2}}" "${GUIX_ENVIRONMENT}/manifest" \
        | head --lines=1 \
        | sed --expression='s|\x29*$||' \
              --expression='s|^[[:space:]]*"||' \
              --expression='s|"[[:space:]]*$||'
}


# Set environment variables to point the NATIVE toolchain to the right
# includes/libs
NATIVE_GCC="$(store_path gcc-toolchain)"
NATIVE_GCC_STATIC="$(store_path gcc-toolchain static)"

unset LIBRARY_PATH
unset CPATH
unset C_INCLUDE_PATH
unset CPLUS_INCLUDE_PATH
unset OBJC_INCLUDE_PATH
unset OBJCPLUS_INCLUDE_PATH

export LIBRARY_PATH="${NATIVE_GCC}/lib:${NATIVE_GCC_STATIC}/lib"
export C_INCLUDE_PATH="${NATIVE_GCC}/include"
export CPLUS_INCLUDE_PATH="${NATIVE_GCC}/include/c++:${NATIVE_GCC}/include"
export OBJC_INCLUDE_PATH="${NATIVE_GCC}/include"
export OBJCPLUS_INCLUDE_PATH="${NATIVE_GCC}/include/c++:${NATIVE_GCC}/include"

# Set environment variables to point the CROSS toolchain to the right
# includes/libs for $HOST
case "$HOST" in
    *mingw*)
        # Determine output paths to use in CROSS_* environment variables
        CROSS_GLIBC="$(store_path "mingw-w64-x86_64-winpthreads")"
        CROSS_GCC="$(store_path "gcc-cross-${HOST}")"
        CROSS_GCC_LIB_STORE="$(store_path "gcc-cross-${HOST}" lib)"
        CROSS_GCC_LIBS=( "${CROSS_GCC_LIB_STORE}/lib/gcc/${HOST}"/* ) # This expands to an array of directories...
        CROSS_GCC_LIB="${CROSS_GCC_LIBS[0]}" # ...we just want the first one (there should only be one)

        # The search path ordering is generally:
        #    1. gcc-related search paths
        #    2. libc-related search paths
        #    2. kernel-header-related search paths (not applicable to mingw-w64 hosts)
        export CROSS_C_INCLUDE_PATH="${CROSS_GCC_LIB}/include:${CROSS_GCC_LIB}/include-fixed:${CROSS_GLIBC}/include"
        export CROSS_CPLUS_INCLUDE_PATH="${CROSS_GCC}/include/c++:${CROSS_GCC}/include/c++/${HOST}:${CROSS_GCC}/include/c++/backward:${CROSS_C_INCLUDE_PATH}"
        export CROSS_LIBRARY_PATH="${CROSS_GCC_LIB_STORE}/lib:${CROSS_GCC_LIB}:${CROSS_GLIBC}/lib"
        ;;
    *darwin*)
        # The CROSS toolchain for darwin uses the SDK and ignores environment variables.
        # See depends/hosts/darwin.mk for more details.
        ;;
    *linux*)
        CROSS_GLIBC="$(store_path "glibc-cross-${HOST}")"
        CROSS_GLIBC_STATIC="$(store_path "glibc-cross-${HOST}" static)"
        CROSS_KERNEL="$(store_path "linux-libre-headers-cross-${HOST}")"
        CROSS_GCC="$(store_path "gcc-cross-${HOST}")"
        CROSS_GCC_LIB_STORE="$(store_path "gcc-cross-${HOST}" lib)"
        CROSS_GCC_LIBS=( "${CROSS_GCC_LIB_STORE}/lib/gcc/${HOST}"/* ) # This expands to an array of directories...
        CROSS_GCC_LIB="${CROSS_GCC_LIBS[0]}" # ...we just want the first one (there should only be one)

        export CROSS_C_INCLUDE_PATH="${CROSS_GCC_LIB}/include:${CROSS_GCC_LIB}/include-fixed:${CROSS_GLIBC}/include:${CROSS_KERNEL}/include"
        export CROSS_CPLUS_INCLUDE_PATH="${CROSS_GCC}/include/c++:${CROSS_GCC}/include/c++/${HOST}:${CROSS_GCC}/include/c++/backward:${CROSS_C_INCLUDE_PATH}"
        export CROSS_LIBRARY_PATH="${CROSS_GCC_LIB_STORE}/lib:${CROSS_GCC_LIB}:${CROSS_GLIBC}/lib:${CROSS_GLIBC_STATIC}/lib"
        ;;
    *)
        exit 1 ;;
esac

# Sanity check CROSS_*_PATH directories
IFS=':' read -ra PATHS <<< "${CROSS_C_INCLUDE_PATH}:${CROSS_CPLUS_INCLUDE_PATH}:${CROSS_LIBRARY_PATH}"
for p in "${PATHS[@]}"; do
    if [ -n "$p" ] && [ ! -d "$p" ]; then
        echo "'$p' doesn't exist or isn't a directory... Aborting..."
        exit 1
    fi
done

# Disable Guix ld auto-rpath behavior
case "$HOST" in
    *darwin*)
        # The auto-rpath behavior is necessary for darwin builds as some native
        # tools built by depends refer to and depend on Guix-built native
        # libraries
        #
        # After the native packages in depends are built, the ld wrapper should
        # no longer affect our build, as clang would instead reach for
        # x86_64-apple-darwin-ld from cctools
        ;;
    *) export GUIX_LD_WRAPPER_DISABLE_RPATH=yes ;;
esac

# Make /usr/bin if it doesn't exist
[ -e /usr/bin ] || mkdir -p /usr/bin

# Symlink file and env to a conventional path
[ -e /usr/bin/file ] || ln -s --no-dereference "$(command -v file)" /usr/bin/file
[ -e /usr/bin/env ]  || ln -s --no-dereference "$(command -v env)"  /usr/bin/env

# Determine the correct value for -Wl,--dynamic-linker for the current $HOST
case "$HOST" in
    *linux*)
        case "$HOST" in
            x86_64-linux-gnu)      glibc_dynamic_linker=/lib64/ld-linux-x86-64.so.2 ;;
            arm-linux-gnueabihf)   glibc_dynamic_linker=/lib/ld-linux-armhf.so.3 ;;
            aarch64-linux-gnu)     glibc_dynamic_linker=/lib/ld-linux-aarch64.so.1 ;;
            riscv64-linux-gnu)     glibc_dynamic_linker=/lib/ld-linux-riscv64-lp64d.so.1 ;;
            powerpc64-linux-gnu)   glibc_dynamic_linker=/lib64/ld64.so.1 ;;
            powerpc64le-linux-gnu) glibc_dynamic_linker=/lib64/ld64.so.2 ;;
            *)                     exit 1 ;;
        esac
        ;;
esac

# Environment variables for determinism
export TAR_OPTIONS="--owner=0 --group=0 --numeric-owner --mtime='@${SOURCE_DATE_EPOCH}' --sort=name"
export TZ="UTC"
case "$HOST" in
    *darwin*)
        # cctools AR, unlike GNU binutils AR, does not have a deterministic mode
        # or a configure flag to enable determinism by default, it only
        # understands if this env-var is set or not. See:
        #
        # https://github.com/tpoechtrager/cctools-port/blob/55562e4073dea0fbfd0b20e0bf69ffe6390c7f97/cctools/ar/archive.c#L334
        export ZERO_AR_DATE=yes
        ;;
esac

####################
# Depends Building #
####################

make -C depends install_cmake --jobs="$JOBS" HOST="$HOST" \
                                   ${V:+V=1} \
                                   ${SOURCES_PATH+SOURCES_PATH="$SOURCES_PATH"} \
                                   ${BASE_CACHE+BASE_CACHE="$BASE_CACHE"} \
                                   ${SDK_PATH+SDK_PATH="$SDK_PATH"}


###########################
# Source Tarball Building #
###########################

GIT_ARCHIVE="${DIST_ARCHIVE_BASE}/${DISTNAME}.tar.gz"

# Create the source tarball if not already there
if [ ! -e "$GIT_ARCHIVE" ]; then
    mkdir -p "$(dirname "$GIT_ARCHIVE")"

    # Initialize submodules first
    git submodule update --init --recursive

    # Create a temporary directory and copy everything
    temp_dir=$(mktemp -d)
    git archive --format=tar --prefix="${DISTNAME}/" HEAD | tar -x -C "$temp_dir"

    # Copy submodules manually
    git submodule foreach --recursive '
        echo "Adding submodule: $displaypath"
        mkdir -p "'"$temp_dir/${DISTNAME}"'/$displaypath"
        git archive HEAD | tar -x -C "'"$temp_dir/${DISTNAME}"'/$displaypath"
    '

    # Create final archive
    tar -czf "$GIT_ARCHIVE" -C "$temp_dir" "${DISTNAME}"
    rm -rf "$temp_dir"
fi

mkdir -p "$OUTDIR"

###########################
# Binary Tarball Building #
###########################

# CMAKE FLAGS for Tapyrus Core
CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=RelWithDebInfo"
CMAKE_FLAGS+=" -DENABLE_GUI=ON"
CMAKE_FLAGS+=" -DENABLE_WALLET=ON" 
CMAKE_FLAGS+=" -DWITH_BDB=ON"
CMAKE_FLAGS+=" -DENABLE_ZMQ=ON"
CMAKE_FLAGS+=" -DENABLE_USDT=OFF"
CMAKE_FLAGS+=" -DENABLE_TESTS=OFF"
CMAKE_FLAGS+=" -DENABLE_BENCH=OFF"
CMAKE_FLAGS+=" -DBUILD_UTILS=ON"

# CFLAGS
HOST_CFLAGS="-O2 -g"
HOST_CFLAGS+=$(find /gnu/store -maxdepth 1 -mindepth 1 -type d -exec echo -n " -ffile-prefix-map={}=/usr" \;)
case "$HOST" in
    *linux*)  HOST_CFLAGS+=" -ffile-prefix-map=${PWD}=." ;;
    *mingw*)  HOST_CFLAGS+=" -fno-ident" ;;
    *darwin*) unset HOST_CFLAGS ;;
esac

# CXXFLAGS
HOST_CXXFLAGS="$HOST_CFLAGS"

case "$HOST" in
    arm-linux-gnueabihf) HOST_CXXFLAGS="${HOST_CXXFLAGS} -Wno-psabi" ;;
esac

# LDFLAGS
case "$HOST" in
    *linux*)  HOST_LDFLAGS="-Wl,--as-needed -Wl,--dynamic-linker=$glibc_dynamic_linker -static-libstdc++ -Wl,-O2" ;;
    *mingw*)  HOST_LDFLAGS="-Wl,--no-insert-timestamp" ;;
esac

# Make $HOST-specific native binaries from depends available in $PATH
export PATH="${BASEPREFIX}/${HOST}/native/bin:${PATH}"
mkdir -p "$DISTSRC"
(
    cd "$DISTSRC"

    # Extract the source tarball
    tar --strip-components=1 -xf "${GIT_ARCHIVE}"

    # Debug toolchain setup
    echo "BASEPREFIX: ${BASEPREFIX}"
    echo "HOST: ${HOST}"
    echo "Toolchain file: ${BASEPREFIX}/${HOST}/toolchain.cmake"
    ls -la "${BASEPREFIX}/${HOST}/" || echo "Host directory does not exist"

    # Check if toolchain file exists (should be generated by depends build)
    if [ ! -f "${BASEPREFIX}/${HOST}/toolchain.cmake" ]; then
        echo "ERROR: Toolchain file not found. Depends build with install_cmake target may have failed."
        exit 1
    else
        echo "Found CMake toolchain file"
    fi

    # Create unified toolchain path for consistency with CI/Docker
    # This ensures all build systems can find the toolchain at the same relative path
    UNIFIED_TOOLCHAIN_PATH="${BASEPREFIX}/toolchain-${HOST}.cmake"
    ln -sf "${HOST}/toolchain.cmake" "${UNIFIED_TOOLCHAIN_PATH}"
    echo "Created unified toolchain symlink at: ${UNIFIED_TOOLCHAIN_PATH}"

    # Set PKG_CONFIG_PATH for depends-built packages
    export PKG_CONFIG_PATH="${BASEPREFIX}/${HOST}/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
    echo "PKG_CONFIG_PATH: ${PKG_CONFIG_PATH}"
    
    # Debug: check if libevent.pc exists
    if [ -f "${BASEPREFIX}/${HOST}/lib/pkgconfig/libevent.pc" ]; then
        echo "Found libevent.pc"
        pkg-config --exists libevent && echo "pkg-config can find libevent" || echo "pkg-config cannot find libevent"
    else
        echo "libevent.pc not found, checking for alternative locations..."
        find "${BASEPREFIX}/${HOST}" -name "*.pc" | grep -i event || echo "No event-related .pc files found"
    fi
    
    # Configure with CMake using depends toolchain
    # shellcheck disable=SC2086
    cmake -S . -B build \
        -DCMAKE_TOOLCHAIN_FILE="${BASEPREFIX}/${HOST}/toolchain.cmake" \
        -DCMAKE_MODULE_PATH="${PWD}/cmake" \
        -DCMAKE_MAKE_PROGRAM="$(command -v make)" \
        -DCMAKE_PREFIX_PATH="${BASEPREFIX}/${HOST}" \
        ${CMAKE_FLAGS} \
        ${HOST_CFLAGS:+"-DCMAKE_C_FLAGS=${HOST_CFLAGS}"} \
        ${HOST_CXXFLAGS:+"-DCMAKE_CXX_FLAGS=${HOST_CXXFLAGS}"} \
        ${HOST_LDFLAGS:+"-DCMAKE_EXE_LINKER_FLAGS=${HOST_LDFLAGS}"}

    # Build Tapyrus Core
    cmake --build build --parallel -j"$JOBS" ${V:+--verbose}

    # Perform basic security checks on a series of executables.
    #cmake --build build -j 1 --target check-security ${V:+--verbose}
    # Check that executables only contain allowed version symbols.
    #cmake --build build -j 1 --target check-symbols ${V:+--verbose}

    mkdir -p "$OUTDIR"

    # Make the os-specific installers
    case "$HOST" in
        *mingw*)
            cmake --build build --target deploy ${V:+--verbose}
            # Copy the Windows installer to the output directory
            find build -name "*-win64-setup.exe" -exec cp {} "${OUTDIR}/${DISTNAME}-win64-setup-unsigned.exe" \;
            ;;
    esac

    # Setup the directory where our Tapyrus Core build for HOST will be
    # installed. This directory will also later serve as the input for our
    # binary tarballs.
    INSTALLPATH="${PWD}/installed/${DISTNAME}"
    mkdir -p "${INSTALLPATH}"
    # Install built Tapyrus Core to $INSTALLPATH
    cmake --install build --prefix "${INSTALLPATH}" ${V:+--verbose}

    case "$HOST" in
        *darwin*)
            cmake --build build --target deploydir ${V:+--verbose}
            mkdir -p "unsigned-app-${HOST}"
            cp  --target-directory="unsigned-app-${HOST}" \
                contrib/macdeploy/detached-sig-create.sh
            mv --target-directory="unsigned-app-${HOST}" build/dist
            (
                cd "unsigned-app-${HOST}"
                find . -print0 \
                    | sort --zero-terminated \
                    | tar --create --no-recursion --mode='u+rw,go+r-w,a+X' --null --files-from=- \
                    | gzip -9n > "${OUTDIR}/${DISTNAME}-${HOST}-unsigned.tar.gz" \
                    || ( rm -f "${OUTDIR}/${DISTNAME}-${HOST}-unsigned.tar.gz" && exit 1 )
            )
            cmake --build build --target deploy ${V:+--verbose}
            # Note: OSX_ZIP variable handling may need adjustment for CMake build
            ;;
    esac
    (
        cd installed

        case "$HOST" in
            *mingw*)
                # Only move DLLs if they exist (they may not exist with static linking)
                if ls "$DISTNAME"/bin/*.dll 1> /dev/null 2>&1; then
                    mkdir -p "$DISTNAME"/lib
                    mv --target-directory="$DISTNAME"/lib/ "$DISTNAME"/bin/*.dll
                fi
                ;;
        esac

        # Prune libtool and object archives
        find . -name "lib*.la" -delete
        find . -name "lib*.a" -delete

        # Prune pkg-config files
        rm -rf "${DISTNAME}/lib/pkgconfig"

        case "$HOST" in
            *darwin*) ;;
            *)
                # Split binaries and libraries from their debug symbols
                {
                    find "${DISTNAME}/bin" -type f -executable -print0
                    find "${DISTNAME}/lib" -type f -print0 2>/dev/null || true
                } | xargs -0 -P"$JOBS" -I{} "${DISTSRC}/build/split-debug.sh" {} {} {}.dbg

                # Move debug files to a separate directory structure
                mkdir -p "${DISTNAME}-debug"
                find "${DISTNAME}" -name "*.dbg" -type f | while read -r dbgfile; do
                    # Get relative path from DISTNAME
                    relpath="${dbgfile#"${DISTNAME}"/}"
                    # Create directory structure in debug folder
                    mkdir -p "${DISTNAME}-debug/$(dirname "$relpath")"
                    # Move the debug file
                    mv "$dbgfile" "${DISTNAME}-debug/$relpath"
                done
                ;;
        esac

        case "$HOST" in
            *mingw*)
                cp "${DISTSRC}/doc/README_windows.txt" "${DISTNAME}/readme.txt"
                ;;
            *linux*)
                cp "${DISTSRC}/README.md" "${DISTNAME}/"
                ;;
        esac

        # copy over the example tapyrus.conf file. this file will be a stub
        cp "${DISTSRC}/share/examples/tapyrus.conf" "${DISTNAME}/"

        cp -r "${DISTSRC}/share/rpcauth" "${DISTNAME}/share/"

        # Finally, deterministically produce {non-,}debug binary tarballs ready
        # for release
        case "$HOST" in
            *mingw*)
                # Create main distribution archive (without debug symbols)
                find "${DISTNAME}" -print0 \
                    | xargs -0r touch --no-dereference --date="@${SOURCE_DATE_EPOCH}"
                find "${DISTNAME}" \
                    | sort \
                    | zip -X@ "${OUTDIR}/${DISTNAME}-${HOST//x86_64-w64-mingw32/win64}.zip" \
                    || ( rm -f "${OUTDIR}/${DISTNAME}-${HOST//x86_64-w64-mingw32/win64}.zip" && exit 1 )
                # Create debug symbols archive
                if [ -d "${DISTNAME}-debug" ]; then
                    find "${DISTNAME}-debug" -print0 \
                        | xargs -0r touch --no-dereference --date="@${SOURCE_DATE_EPOCH}"
                    find "${DISTNAME}-debug" \
                        | sort \
                        | zip -X@ "${OUTDIR}/${DISTNAME}-${HOST//x86_64-w64-mingw32/win64}-debug.zip" \
                        || ( rm -f "${OUTDIR}/${DISTNAME}-${HOST//x86_64-w64-mingw32/win64}-debug.zip" && exit 1 )
                fi
                ;;
            *linux*)
                # Create main distribution archive (without debug symbols)
                find "${DISTNAME}" -print0 \
                    | sort --zero-terminated \
                    | tar --create --no-recursion --mode='u+rw,go+r-w,a+X' --null --files-from=- \
                    | gzip -9n > "${OUTDIR}/${DISTNAME}-${HOST}.tar.gz" \
                    || ( rm -f "${OUTDIR}/${DISTNAME}-${HOST}.tar.gz" && exit 1 )
                # Create debug symbols archive
                if [ -d "${DISTNAME}-debug" ]; then
                    find "${DISTNAME}-debug" -print0 \
                        | sort --zero-terminated \
                        | tar --create --no-recursion --mode='u+rw,go+r-w,a+X' --null --files-from=- \
                        | gzip -9n > "${OUTDIR}/${DISTNAME}-${HOST}-debug.tar.gz" \
                        || ( rm -f "${OUTDIR}/${DISTNAME}-${HOST}-debug.tar.gz" && exit 1 )
                fi
                ;;
            *darwin*)
                find "${DISTNAME}" -print0 \
                    | sort --zero-terminated \
                    | tar --create --no-recursion --mode='u+rw,go+r-w,a+X' --null --files-from=- \
                    | gzip -9n > "${OUTDIR}/${DISTNAME}-${HOST}.tar.gz" \
                    || ( rm -f "${OUTDIR}/${DISTNAME}-${HOST}.tar.gz" && exit 1 )
                ;;
        esac
    )  # $DISTSRC/installed

    case "$HOST" in
        *mingw*)
            cp -rf --target-directory=. contrib/windeploy
            (
                cd ./windeploy
                mkdir -p unsigned
                cp --target-directory=unsigned/ "${OUTDIR}/${DISTNAME}-win64-setup-unsigned.exe"
                find . -print0 \
                    | sort --zero-terminated \
                    | tar --create --no-recursion --mode='u+rw,go+r-w,a+X' --null --files-from=- \
                    | gzip -9n > "${OUTDIR}/${DISTNAME}-win64-unsigned.tar.gz" \
                    || ( rm -f "${OUTDIR}/${DISTNAME}-win64-unsigned.tar.gz" && exit 1 )
            )
            ;;
    esac
)  # $DISTSRC

rm -rf "$ACTUAL_OUTDIR"
mv --no-target-directory "$OUTDIR" "$ACTUAL_OUTDIR" \
    || ( rm -rf "$ACTUAL_OUTDIR" && exit 1 )

(
    cd /outdir-base
    {
        echo "$GIT_ARCHIVE"
        find "$ACTUAL_OUTDIR" -type f
    } | xargs realpath --relative-base="$PWD" \
      | xargs sha256sum \
      | sort -k2 \
      | sponge "$ACTUAL_OUTDIR"/SHA256SUMS.part
)
