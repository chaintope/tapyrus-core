# Allow to easily add flags for C and C++
include(CheckCXXCompilerFlag)
include(CheckCCompilerFlag)

function(add_c_compiler_flag)
	foreach(f ${ARGN})
		CHECK_C_COMPILER_FLAG(${f} FLAG_IS_SUPPORTED)
		if(FLAG_IS_SUPPORTED)
			string(APPEND CMAKE_C_FLAGS " ${f}")
		endif()
	endforeach()
	set(CMAKE_C_FLAGS ${CMAKE_C_FLAGS} PARENT_SCOPE)
endfunction()

function(add_cxx_compiler_flag)
	foreach(f ${ARGN})
		CHECK_CXX_COMPILER_FLAG(${f} FLAG_IS_SUPPORTED)
		if(FLAG_IS_SUPPORTED)
			string(APPEND CMAKE_CXX_FLAGS " ${f}")
		endif()
	endforeach()
	set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} PARENT_SCOPE)
endfunction()

macro(add_compiler_flag)
	add_c_compiler_flag(${ARGN})
	add_cxx_compiler_flag(${ARGN})
endmacro()

macro(remove_compiler_flags)
	foreach(f ${ARGN})
		string(REGEX REPLACE "${f}( |^)" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
		string(REGEX REPLACE "${f}( |^)" "" CMAKE_C_FLAGS ${CMAKE_C_FLAGS})
	endforeach()
endmacro()


function(add_macos_deploy_target)
	if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND TARGET tapyrus-qt)
		set(macos_app "Tapyrus-Qt.app")
		# Populate Contents subdirectory.
		configure_file(${PROJECT_SOURCE_DIR}/share/qt/Info.plist.in ${macos_app}/Contents/Info.plist)
		file(CONFIGURE OUTPUT ${macos_app}/Contents/PkgInfo CONTENT "APPL????")
		# Populate Contents/Resources subdirectory.
		file(CONFIGURE OUTPUT ${macos_app}/Contents/Resources/empty.lproj CONTENT "")
		configure_file(${PROJECT_SOURCE_DIR}/src/qt/res/icons/tapyrus.icns ${macos_app}/Contents/Resources/tapyrus.icns COPYONLY)
		file(CONFIGURE OUTPUT ${macos_app}/Contents/Resources/Base.lproj/InfoPlist.strings
				CONTENT "{ CFBundleDisplayName = \"@PACKAGE_NAME@\"; CFBundleName = \"@PACKAGE_NAME@\"; }"
		)

		add_custom_command(
				OUTPUT ${PROJECT_BINARY_DIR}/${macos_app}/Contents/MacOS/Tapyrus-Qt
				COMMAND ${CMAKE_COMMAND} --install ${PROJECT_BINARY_DIR} --config $<CONFIG> --component GUI --prefix ${macos_app}/Contents/MacOS --strip
				COMMAND ${CMAKE_COMMAND} -E rename ${macos_app}/Contents/MacOS/bin/$<TARGET_FILE_NAME:tapyrus-qt> ${macos_app}/Contents/MacOS/Tapyrus-Qt
				COMMAND ${CMAKE_COMMAND} -E rm -rf ${macos_app}/Contents/MacOS/bin
				VERBATIM
		)

		string(REPLACE " " "-" osx_volname ${PACKAGE_NAME})
		if(CMAKE_HOST_APPLE)
			add_custom_command(
					OUTPUT ${PROJECT_BINARY_DIR}/${osx_volname}.zip
					COMMAND ${PYTHON_COMMAND} ${PROJECT_SOURCE_DIR}/contrib/macdeploy/macdeployqtplus ${macos_app} ${osx_volname} -translations-dir=${QT_TRANSLATIONS_DIR} -zip
					DEPENDS ${PROJECT_BINARY_DIR}/${macos_app}/Contents/MacOS/Tapyrus-Qt
					VERBATIM
			)
			add_custom_target(deploydir
					DEPENDS ${PROJECT_BINARY_DIR}/${osx_volname}.zip
			)
			add_custom_target(deploy
					DEPENDS ${PROJECT_BINARY_DIR}/${osx_volname}.zip
			)
		else()
			add_custom_command(
					OUTPUT ${PROJECT_BINARY_DIR}/dist/${macos_app}/Contents/MacOS/Tapyrus-Qt
					COMMAND OBJDUMP=${CMAKE_OBJDUMP} ${PYTHON_COMMAND} ${PROJECT_SOURCE_DIR}/contrib/macdeploy/macdeployqtplus ${macos_app} ${osx_volname} -translations-dir=${QT_TRANSLATIONS_DIR}
					DEPENDS ${PROJECT_BINARY_DIR}/${macos_app}/Contents/MacOS/Tapyrus-Qt
					VERBATIM
			)
			add_custom_target(deploydir
					DEPENDS ${PROJECT_BINARY_DIR}/dist/${macos_app}/Contents/MacOS/Tapyrus-Qt
			)

			find_program(ZIP_COMMAND zip REQUIRED)
			add_custom_command(
					OUTPUT ${PROJECT_BINARY_DIR}/dist/${osx_volname}.zip
					WORKING_DIRECTORY dist
					COMMAND ${PROJECT_SOURCE_DIR}/cmake/script/macos_zip.sh ${ZIP_COMMAND} ${osx_volname}.zip
					VERBATIM
			)
			add_custom_target(deploy
					DEPENDS ${PROJECT_BINARY_DIR}/dist/${osx_volname}.zip
			)
		endif()
		add_dependencies(deploydir tapyrus-qt)
		add_dependencies(deploy deploydir)
	endif()
endfunction()
