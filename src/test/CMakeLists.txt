# Copyright (c) 2018 The Bitcoin developers

project(tapyrus-test)

# Process json files.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/data")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
function(gen_json_header NAME)
    set(HEADERS "")
    foreach(f ${ARGN})
        set(h "${CMAKE_CURRENT_BINARY_DIR}/${f}.h")

        # Get the proper name for the test variable.
        get_filename_component(TEST_NAME ${f} NAME_WE)
        add_custom_command(OUTPUT ${h}
            COMMAND Python3::Interpreter
            ARGS
                "${CMAKE_CURRENT_SOURCE_DIR}/data/generate_header.py"
                "${TEST_NAME}"
                "${CMAKE_CURRENT_SOURCE_DIR}/${f}" > ${h}
            MAIN_DEPENDENCY ${f}
            DEPENDS
                "data/generate_header.py"
            VERBATIM
        )
        list(APPEND HEADERS ${h})
    endforeach(f)
    set(${NAME} "${HEADERS}" PARENT_SCOPE)
endfunction()

gen_json_header(JSON_HEADERS
        data/base58_encode_decode.json
        data/key_io_invalid.json
        data/key_io_valid.json
        data/script_tests.json
        data/sighash.json
        data/tx_invalid.json
        data/tx_valid.json
)

# TIP-174 test vectors live under test/functional/data (shared with
# rpc_pstt.py, see test/functional/data/tip174_SOURCE.md for provenance) --
# pull them in from there rather than duplicating a copy under src/test/data
# that could silently drift from the fixtures the functional test asserts
# against.
set(TIP174_DATA_DIR "${CMAKE_SOURCE_DIR}/test/functional/data")
set(TIP174_JSON_HEADERS "")
foreach(f tip174_valid.json tip174_invalid.json)
    set(h "${CMAKE_CURRENT_BINARY_DIR}/data/${f}.h")
    get_filename_component(TEST_NAME ${f} NAME_WE)
    add_custom_command(OUTPUT ${h}
        COMMAND Python3::Interpreter
        ARGS
            "${CMAKE_CURRENT_SOURCE_DIR}/data/generate_header.py"
            "${TEST_NAME}"
            "${TIP174_DATA_DIR}/${f}" > ${h}
        MAIN_DEPENDENCY "${TIP174_DATA_DIR}/${f}"
        DEPENDS
            "data/generate_header.py"
        VERBATIM
    )
    list(APPEND TIP174_JSON_HEADERS ${h})
endforeach()

if(ENABLE_TESTS)
    enable_testing()
endif()

add_executable(test_tapyrus
        addrman_tests.cpp
        allocator_tests.cpp
        amount_tests.cpp
        arith_uint256_tests.cpp
        base32_tests.cpp
        base58_tests.cpp
        base64_tests.cpp
        bip32_tests.cpp
        block_tests.cpp
        blockencodings_tests.cpp
        bloom_tests.cpp
        bswap_tests.cpp
        chain_tests.cpp
        chainparams_tests.cpp
        federationparams_tests.cpp
        chainstate_tests.cpp
        checkdatasig_tests.cpp
        checkqueue_tests.cpp
        coins_tests.cpp
        coloridentifier_tests.cpp
        compress_tests.cpp
        crypto_tests.cpp
        cuckoocache_tests.cpp
        dbwrapper_tests.cpp
        denialofservice_tests.cpp
        descriptor_tests.cpp
        file_io_tests.cpp
        getarg_tests.cpp
        hash_tests.cpp
        key_io_tests.cpp
        key_tests.cpp
        limitedmap_tests.cpp
        main_tests.cpp
        mempool_tests.cpp
        merkle_tests.cpp
        pool_tests.cpp
        merkleblock_tests.cpp
        miner_tests.cpp
        multisig_tests.cpp
        net_tests.cpp
        netbase_tests.cpp
        pmt_tests.cpp
        policyestimator_tests.cpp
        prevector_tests.cpp
        pstt_tests.cpp
        raii_event_tests.cpp
        random_tests.cpp
        reverselock_tests.cpp
        rpc_tests.cpp
        sanity_tests.cpp
        scheduler_tests.cpp
        script_p2sh_tests.cpp
        script_risky_combinations_tests.cpp
        script_standard_tests.cpp
        script_tests.cpp
        scriptnum_tests.cpp
        serialize_tests.cpp
        sighash_tests.cpp
        sigopcount_tests.cpp
        skiplist_tests.cpp
        streams_tests.cpp
        test_tapyrus.cpp
        test_tapyrus_main.cpp
        test_keys_helper.cpp
        torcontrol_tests.cpp
        transaction_tests.cpp
        txdb_tests.cpp
        txindex_tests.cpp
        txpackage_tests.cpp
        txvalidation_tests.cpp
        txvalidationcache_tests.cpp
        uint256_tests.cpp
        util_tests.cpp
        validation_block_tests.cpp
        validation_flush_tests.cpp
        xfieldhistory_tests.cpp
        xfield_tests.cpp
        zmq_tests.cpp
        # Tests generated from JSON
        ${JSON_HEADERS}
        ${TIP174_JSON_HEADERS}
)

target_include_directories(test_tapyrus
    PRIVATE
        ${Boost_INCLUDE_DIRS}
        ${CMAKE_INSTALL_PREFIX}/include
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/../leveldb>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/../leveldb/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/../leveldb/helpers/memenv>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/../secp256k1/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/../univalue/include>
        ${BerkeleyDB_INCLUDE_DIR}
        ${EVENT_INCLUDE_DIRS}
        ${ZMQ_INCLUDE_DIR}
    PUBLIC
        # To access the generated json headers.
        ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(test_tapyrus
    PRIVATE
        $<TARGET_NAME_IF_EXISTS:tapyrus_wallet>
        tapyrus_cli
        tapyrus_server
        tapyrus_common
        tapyrus_util
        tapyrus_consensus
        $<TARGET_NAME_IF_EXISTS:tapyrus_zmq>
        leveldb
        univalue
        secp256k1
        tapyrus_crypto
        Boost::headers
        $<TARGET_NAME_IF_EXISTS:libevent::extra>
        $<TARGET_NAME_IF_EXISTS:libevent::core>
        $<TARGET_NAME_IF_EXISTS:libevent::pthreads>
        $<TARGET_NAME_IF_EXISTS:USDT::headers>
)

# Fuzz test binaries -- gated behind BUILD_FUZZ_TEST (default OFF) so
# regular builds (including the full daily-test.yml matrix) never
# configure or build these; only daily-test.yml's dedicated fuzz-code-only
# and fuzz-script-sweep jobs turn this on.
if(BUILD_FUZZ_TEST)

# tapyrus_server/tapyrus_common/tapyrus_util/tapyrus_consensus have real
# circular symbol dependencies between them (e.g. script/interpreter.cpp
# in tapyrus_consensus calls GetSoftForkManager(), defined in
# federationparams.cpp in tapyrus_common; federationparams.cpp in turn
# calls gArgs/ArgsManager, defined in tapyrus_util) -- test_tapyrus above
# never hits this because it also links tapyrus_wallet/tapyrus_cli/
# tapyrus_zmq/libevent, which happen to pull in a superset of the same
# object files early enough that everything each library needs is
# already present in the link by the time it's needed. A standalone fuzz
# binary linking only the libraries it actually needs has no such luck:
# GNU ld/lld resolve each static archive in a single left-to-right pass,
# so a later archive's need for a symbol in an earlier, already-scanned
# archive is never revisited -- confirmed by fuzz_pstt_decode failing to
# link with "undefined reference to GetSoftForkManager()" until wrapped
# in --start-group/--end-group, which forces the linker to keep
# re-scanning the whole set until every symbol resolves. Not supported
# by Apple's ld64 the same way, but these fuzz binaries are Linux-only in
# CI (daily-test.yml's fuzz-code-only job runs on ubuntu-latest) so that
# doesn't matter here.
function(add_pstt_fuzz_target name source)
    # Deliberately NOT EXCLUDE_FROM_ALL: BUILD_FUZZ_TEST already gates
    # whether this function's callers even run (see the if(BUILD_FUZZ_TEST)
    # above), so once it's ON, a plain default build should actually
    # compile-check these harnesses -- otherwise daily-test.yml's native
    # matrix job turning BUILD_FUZZ_TEST=ON to compile-check
    # tapyrus-verify's --fuzz mode on every PR would silently skip
    # compile-checking these harnesses the exact same way, and a
    # signature drift here would go unnoticed until the next scheduled
    # fuzz-code-only run instead of failing the PR.
    add_executable(${name}
        fuzz/fuzz_code/${source}
    )
    target_include_directories(${name}
        PRIVATE
            ${Boost_INCLUDE_DIRS}
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/../univalue/include>
    )
    # CMake's fixed link-command template
    # (<COMPILER> <FLAGS> <LINK_OPTIONS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>)
    # always places LINK_OPTIONS (from target_link_options()) before the
    # object files and LINK_LIBRARIES (from target_link_libraries()) after
    # -- there is no way to get -Wl,--start-group/--end-group positioned
    # around specific libraries via target_link_options(), since group
    # markers need to sit in the LINK_LIBRARIES list itself, after the
    # object file that actually references these symbols (confirmed by
    # testing: a group placed via target_link_options still failed with
    # "undefined reference to GetSoftForkManager()", because it ran
    # before the fuzz harness's own .o -- the thing creating the need --
    # was even linked). Passing the raw -Wl,--start-group/--end-group
    # strings directly as target_link_libraries() arguments keeps them in
    # that correctly-positioned list instead.
    target_link_libraries(${name}
        PRIVATE
            core_interface
            leveldb
            univalue
            secp256k1
            tapyrus_crypto
            Boost::headers
            $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--start-group>
            tapyrus_server
            tapyrus_common
            tapyrus_util
            tapyrus_consensus
            $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--end-group>
    )
endfunction()

# One standalone executable per fuzz/fuzz_code/*_fuzz.cpp file --
# deliberately its own executable, not a source file of test_tapyrus
# above: a libFuzzer/AFL fuzz_test_file supplies its own weak
# main()/LLVMFuzzerTestOneInput, which would silently be shadowed by
# test_tapyrus_main.cpp's real main() if linked into that same binary.
#
# Target name is derived from the filename: fuzz/fuzz_code/pstt_parse_fuzz.cpp
# becomes fuzz_pstt_parse. Landing a new harness (e.g. one OSS-Fuzz-Gen
# drafted and a human reviewed, see contrib/fuzz/oss-fuzz/drafting/README.md)
# means only adding its source file under fuzz/fuzz_code/ -- nothing to
# register here, in daily-test.yml, or in contrib/fuzz/oss-fuzz/project/build.sh.
# Each file documents its own purpose in its own header comment (see e.g.
# pstt_signer_fuzz.cpp).
#
# fuzz_all is a phony aggregate CI builds instead of naming each target
# individually; the built binaries under bin/fuzz_* are themselves the
# list of what to run.
add_custom_target(fuzz_all)
file(GLOB pstt_fuzz_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/fuzz_code/*_fuzz.cpp)
foreach(pstt_fuzz_source_path ${pstt_fuzz_sources})
    get_filename_component(pstt_fuzz_source_name ${pstt_fuzz_source_path} NAME)
    string(REGEX REPLACE "_fuzz\\.cpp$" "" pstt_fuzz_stem ${pstt_fuzz_source_name})
    add_pstt_fuzz_target(fuzz_${pstt_fuzz_stem} ${pstt_fuzz_source_name})
    add_dependencies(fuzz_all fuzz_${pstt_fuzz_stem})
endforeach()

endif(BUILD_FUZZ_TEST)

# We need to detect if the BOOST_TEST_DYN_LINK flag is required.
check_cxx_source_compiles("
    #define BOOST_TEST_DYN_LINK
    #define BOOST_TEST_MAIN
    #include <boost/test/unit_test.hpp>
" BOOST_TEST_DYN_LINK)

if(BOOST_TEST_DYN_LINK)
    target_compile_definitions(test_tapyrus PRIVATE BOOST_TEST_DYN_LINK)
endif(BOOST_TEST_DYN_LINK)

if(ENABLE_WALLET)
    target_sources(test_tapyrus
        PRIVATE
            ../wallet/test/accounting_tests.cpp
            ../wallet/test/coinselector_tests.cpp
            ../wallet/test/pstt_wallet_tests.cpp
            ../wallet/test/wallet_crypto_tests.cpp
            ../wallet/test/wallet_test_fixture.cpp
            ../wallet/test/wallet_tests.cpp
            ../wallet/test/create_transaction_tests.cpp
            ../wallet/test/test_tapyrus_wallet.cpp
    )
endif()

# Function to extract and register individual Boost test suites
# This allows running each test suite independently with ctest
function(add_boost_test source_file)
  if(NOT EXISTS ${source_file})
    return()
  endif()

  file(READ "${source_file}" source_file_content)
  string(REGEX
    MATCHALL "(BOOST_FIXTURE_TEST_SUITE|BOOST_AUTO_TEST_SUITE)\\(([A-Za-z0-9_]+)"
    test_suite_macro "${source_file_content}"
  )
  list(TRANSFORM test_suite_macro
    REPLACE "(BOOST_FIXTURE_TEST_SUITE|BOOST_AUTO_TEST_SUITE)\\(" ""
  )
  foreach(test_suite_name IN LISTS test_suite_macro)
    add_test(NAME ${test_suite_name}
      COMMAND test_tapyrus --run_test=${test_suite_name} --catch_system_error=no --log_level=test_suite -- DEBUG_LOG_OUT
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_property(TEST ${test_suite_name} PROPERTY
      SKIP_REGULAR_EXPRESSION "no test cases matching filter"
    )
  endforeach()
endfunction()

# Function to register all test targets
function(add_all_test_targets)
  get_target_property(test_source_dir test_tapyrus SOURCE_DIR)
  get_target_property(test_sources test_tapyrus SOURCES)
  foreach(test_source ${test_sources})
    cmake_path(IS_RELATIVE test_source result)
    if(result)
      cmake_path(APPEND test_source_dir ${test_source} OUTPUT_VARIABLE test_source)
    endif()
    add_boost_test(${test_source})
  endforeach()
endfunction()

# Register all individual Boost test suites
add_all_test_targets()

# Set RPATH for the test executable
set_target_properties(test_tapyrus PROPERTIES
    INSTALL_RPATH "@executable_path/../lib;${CMAKE_BINARY_DIR}/lib"
    BUILD_WITH_INSTALL_RPATH TRUE
    SKIP_BUILD_RPATH FALSE
)
