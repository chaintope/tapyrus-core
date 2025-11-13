###
# Create config.ini file for tests
###
set(abs_top_srcdir ${CMAKE_SOURCE_DIR})
set(abs_top_builddir ${CMAKE_BINARY_DIR})
if(CMAKE_SYSTEM_NAME MATCHES "Windows")
	set(EXEEXT ".exe")
endif()

if(NOT ENABLE_WALLET)
	set(ENABLE_WALLET_TRUE "#")
endif()

if(NOT BUILD_UTILS)
	set(BUILD_UTILS_TRUE "#")
endif()

if(NOT ENABLE_ZMQ)
	set(ENABLE_ZMQ_TRUE "#")
endif()

# Create build ini file in build and source dir
configure_file(config.ini.in config.ini USE_SOURCE_PERMISSIONS @ONLY)
configure_file(config.ini.in ${abs_top_srcdir}/test/config.ini USE_SOURCE_PERMISSIONS @ONLY)

###
# Setup symlinks for testing
### 
macro(make_link src dest)
  add_custom_command(
    OUTPUT "${dest}"
    COMMAND ${CMAKE_COMMAND} -E create_symlink "${src}" "${dest}"
    COMMENT "make_link ${src} -> ${dest}"
    MAIN_DEPENDENCY "${src}"
  )
endmacro()

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/functional/)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/util/)

make_link(
	${CMAKE_CURRENT_SOURCE_DIR}/functional/test_runner.py
	${CMAKE_CURRENT_BINARY_DIR}/functional/test_runner.py
)
make_link(
	${CMAKE_CURRENT_SOURCE_DIR}/functional/create_cache.py
	${CMAKE_CURRENT_BINARY_DIR}/functional/create_cache.py
)
make_link(
	${CMAKE_CURRENT_SOURCE_DIR}/util/tapyrus-util-test.py
	${CMAKE_CURRENT_BINARY_DIR}/util/tapyrus-util-test.py
)

if(NOT TARGET check-all)
	add_custom_target(check-all)
endif()

if(NOT TARGET check)
	add_custom_target(check)
endif()

add_custom_target(check-functional
	COMMAND
		./functional/test_runner.py
	DEPENDS
		../src/tapyrusd
		../src/tapyrus-cli
		${CMAKE_CURRENT_BINARY_DIR}/functional/test_runner.py
		${CMAKE_CURRENT_BINARY_DIR}/functional/create_cache.py
)

add_dependencies(check-all check-functional)

if(BUILD_UTILS)
	add_custom_target(check-tapyrus-util
		COMMAND
			${CMAKE_COMMAND} -E echo "Running test/util/tapyrus-util-test.py..."
		COMMAND
			./util/tapyrus-util-test.py
		DEPENDS
			../src/tapyrus-tx
			${CMAKE_CURRENT_BINARY_DIR}/util/tapyrus-util-test.py
	)

	add_dependencies(check check-tapyrus-util)

	# Add util tests as a ctest
	add_test(NAME tapyrus_util_test
		COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/util/tapyrus-util-test.py
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	)
endif()