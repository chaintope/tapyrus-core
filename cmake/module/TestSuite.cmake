# Allow to easily build test suites

macro(create_test_suite NAME)
	enable_testing()
	set(TARGET "check-${NAME}")
	add_custom_target(${TARGET} COMMAND ${CMAKE_CTEST_COMMAND})
	message(STATUS "Adding ${TARGET}")

	# If the magic target check-all exists, attach to it.
	if(TARGET check-all)
		add_dependencies(check-all ${TARGET})
	endif()
endmacro(create_test_suite)

function(add_test_to_suite SUITE NAME)
	add_executable(${NAME} ${ARGN})

	# Set RPATH for the test executable
	set_target_properties(${NAME} PROPERTIES
		INSTALL_RPATH "${CMAKE_BINARY_DIR}/lib"
		BUILD_WITH_INSTALL_RPATH TRUE
		SKIP_BUILD_RPATH FALSE
	)

	add_test(NAME ${NAME} COMMAND ${NAME} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
	add_dependencies("check-${SUITE}" ${NAME})
endfunction(add_test_to_suite)
