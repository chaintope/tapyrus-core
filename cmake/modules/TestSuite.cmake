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
	add_executable(${NAME} EXCLUDE_FROM_ALL ${ARGN})
	add_dependencies("check-${SUITE}" ${NAME})
endfunction(add_test_to_suite)


function(add_boost_test source_file)
	if(NOT EXISTS ${source_file})
		return()
	endif()

	file(READ "${source_file}" source_file_content)
	string(REGEX
			MATCH "(BOOST_FIXTURE_TEST_SUITE|BOOST_AUTO_TEST_SUITE)\\(([A-Za-z0-9_]+)"
			test_suite_macro "${source_file_content}"
	)
	string(REGEX
			REPLACE "(BOOST_FIXTURE_TEST_SUITE|BOOST_AUTO_TEST_SUITE)\\(" ""
			test_suite_name "${test_suite_macro}"
	)
	if(test_suite_name)
		add_test(NAME ${test_suite_name}
				COMMAND test_tapyrus --run_test=${test_suite_name} --catch_system_error=no
		)
		set_property(TEST ${test_suite_name} PROPERTY
				SKIP_REGULAR_EXPRESSION "no test cases matching filter" "Skipping"
		)
	endif()
endfunction()

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


