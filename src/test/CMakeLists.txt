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
        bech32_tests.cpp
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
        merkleblock_tests.cpp
        miner_tests.cpp
        multisig_tests.cpp
        net_tests.cpp
        netbase_tests.cpp
        pmt_tests.cpp
        policyestimator_tests.cpp
        prevector_tests.cpp
        raii_event_tests.cpp
        random_tests.cpp
        reverselock_tests.cpp
        rpc_tests.cpp
        sanity_tests.cpp
        scheduler_tests.cpp
        script_p2sh_tests.cpp
        script_standard_tests.cpp
        script_tests.cpp
        scriptnum_tests.cpp
        serialize_tests.cpp
        sighash_tests.cpp
        sigopcount_tests.cpp
        skiplist_tests.cpp
        streams_tests.cpp
        test_tapyrus.cpp
        test_tapyrus_fuzzy.cpp
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
        xfieldhistory_tests.cpp
        xfield_tests.cpp
        # Tests generated from JSON
        ${JSON_HEADERS}
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
            ../wallet/test/psbt_wallet_tests.cpp
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
