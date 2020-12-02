# Copyright (c) 2018 The Bitcoin developers

project(test-tapyrus-qt)

include(TestSuite)
create_test_suite(tapyrus-qt)
add_dependencies(check check-tapyrus-qt)

add_test_to_suite(tapyrus-qt test-tapyrus-qt
	addressbooktests.cpp
	compattests.cpp
	rpcnestedtests.cpp
	test_main.cpp
	uritests.cpp
	util.cpp
	
	# Test framework
	../../test/test_tapyrus.cpp
)

target_link_libraries(test-tapyrus-qt tapyrus-qt-base Qt5::Test)

# Wallet
if(BUILD_BITCOIN_WALLET)
	# Add wallet functionality to test_tapyrus_qt
	target_sources(test-tapyrus-qt
		PRIVATE
			wallettests.cpp
			../../wallet/test/wallet_test_fixture.cpp
	)

	target_link_libraries(test-tapyrus-qt wallet)
endif()
