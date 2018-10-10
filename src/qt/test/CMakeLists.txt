# Copyright (c) 2018 The Bitcoin developers

project(test_tapyrus_qt)

include(TestSuite)
create_test_suite(tapyrus_qt)
add_dependencies(check check-tapyrus_qt)

add_test_to_suite(tapyrus_qt test_tapyrus_qt
	compattests.cpp
	rpcnestedtests.cpp
	test_main.cpp
	uritests.cpp
	
	# Test framework
	../../test/test_tapyrus.cpp
)

target_link_libraries(test_tapyrus_qt tapyrus_qt-base Qt5::Test)

# Wallet
if(BUILD_BITCOIN_WALLET)
	# Add wallet functionality to test_tapyrus_qt
	target_sources(test_tapyrus_qt
		PRIVATE
			wallettests.cpp
			../../wallet/test/wallet_test_fixture.cpp
	)

	target_link_libraries(test_tapyrus_qt wallet)
endif()
