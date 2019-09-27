# Copyright (c) 2018 The Bitcoin developers

project(tapyrus-bench)

include_directories(.)

add_executable(tapyrus-bench
	EXCLUDE_FROM_ALL
	base58.cpp
	bench.cpp
	bench_tapyrus.cpp
	ccoins_caching.cpp
#	checkblock.cpp TODO Fix including bench/data/*.raw files
	checkqueue.cpp
	crypto_hash.cpp
	examples.cpp
	lockedpool.cpp
	mempool_eviction.cpp
	prevector.cpp
	rollingbloom.cpp
)

target_link_libraries(tapyrus-bench common tapyrusconsensus server)

add_custom_target(bench-tapyrus
	COMMAND
		./tapyrus-bench
	DEPENDS
	tapyrus-bench
)
