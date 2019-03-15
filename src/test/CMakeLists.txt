# Copyright (c) 2018 The Bitcoin developers

project(bitcoin-test)

# Process json files.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/data")

find_program(PYTHON python)
function(gen_json_header NAME)
	set(HEADERS "")
	foreach(f ${ARGN})
		set(h "${CMAKE_CURRENT_BINARY_DIR}/${f}.h")

		# Get the proper name for the test variable.
		get_filename_component(TEST_NAME ${f} NAME_WE)
		add_custom_command(OUTPUT ${h}
			COMMAND ${PYTHON}
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

include(TestSuite)
create_test_suite(bitcoin)
add_dependencies(check check-bitcoin)

add_test_to_suite(bitcoin test_bitcoin
		addrman_tests.cpp
		allocator_tests.cpp
		amount_tests.cpp
		arith_uint256_tests.cpp
		base32_tests.cpp
		base58_tests.cpp
		base64_tests.cpp
		bech32_tests.cpp
		bip32_tests.cpp
		blockchain_tests.cpp
		blockencodings_tests.cpp
		bloom_tests.cpp
		bswap_tests.cpp
		chainparams_tests.cpp
		checkqueue_tests.cpp
		coins_tests.cpp
		compress_tests.cpp
		crypto_tests.cpp
		cuckoocache_tests.cpp
		dbwrapper_tests.cpp
		denialofservice_tests.cpp
		descriptor_tests.cpp
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
		test_bitcoin.cpp
		test_bitcoin_fuzzy.cpp
		test_bitcoin_main.cpp
		test_keys_helper.cpp
		timedata_tests.cpp
		torcontrol_tests.cpp
		transaction_tests.cpp
		txindex_tests.cpp
		txvalidation_tests.cpp
		txvalidationcache_tests.cpp
		uint256_tests.cpp
		util_tests.cpp
		validation_block_tests.cpp
		versionbits_tests.cpp

	# Tests generated from JSON
	${JSON_HEADERS}
)

target_include_directories(test_bitcoin
	PUBLIC
		# To access the generated json headers.
		${CMAKE_CURRENT_BINARY_DIR}
)

find_package(Boost 1.58 REQUIRED unit_test_framework)

target_link_libraries(test_bitcoin Boost::unit_test_framework rpcclient server)

# We need to detect if the BOOST_TEST_DYN_LINK flag is required.
set(CMAKE_REQUIRED_LIBRARIES Boost::unit_test_framework)
check_cxx_source_compiles("
	#define BOOST_TEST_DYN_LINK
	#define BOOST_TEST_MAIN
	#include <boost/test/unit_test.hpp>
" BOOST_TEST_DYN_LINK)

if(BOOST_TEST_DYN_LINK)
	target_compile_definitions(test_bitcoin PRIVATE BOOST_TEST_DYN_LINK)
endif(BOOST_TEST_DYN_LINK)

if(BUILD_BITCOIN_WALLET)
	target_sources(test_bitcoin
		PRIVATE
			../wallet/test/accounting_tests.cpp
			../wallet/test/coinselector_tests.cpp
			../wallet/test/psbt_wallet_tests.cpp
			../wallet/test/wallet_crypto_tests.cpp
			../wallet/test/wallet_test_fixture.cpp
			../wallet/test/wallet_tests.cpp
	)
endif()
