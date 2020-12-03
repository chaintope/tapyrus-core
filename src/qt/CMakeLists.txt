# Copyright (c) 2018 The Bitcoin developers

project(tapyrus-qt)

# This ensure that AUTOMOC doesn't run on generated files.
cmake_policy(SET CMP0071 OLD)

include(BrewHelper)
find_brew_prefix(QT5_PREFIX qt5)

set(QT_REQUIRED_COMPONENTS Core Widgets Network DBus Test)
find_package(Qt5 COMPONENTS ${QT_REQUIRED_COMPONENTS} REQUIRED HINTS "${QT5_PREFIX}")

find_package(QREncode REQUIRED)

# Find out more about Qt. This is similar to
# http://code.qt.io/cgit/qt/qtwebkit.git/tree/Source/cmake/OptionsQt.cmake
get_target_property(QT_CORE_TYPE Qt5::Core TYPE)
if(QT_CORE_TYPE MATCHES STATIC)
	set(QT_STATIC_BUILD ON)
endif()

set(STATIC_DEPENDENCIES_CMAKE_FILE "${CMAKE_BINARY_DIR}/QtStaticDependencies.cmake")
if(EXISTS ${STATIC_DEPENDENCIES_CMAKE_FILE})
	file(REMOVE ${STATIC_DEPENDENCIES_CMAKE_FILE})
endif()

set(CONVERT_PRL_PATH "${CONTRIB_PATH}/qt/convert-prl-libs-to-cmake.pl")
macro(CONVERT_PRL_LIBS_TO_CMAKE _qt_component)
	if(TARGET Qt5::${_qt_component})
		get_target_property(_lib_location Qt5::${_qt_component} LOCATION)
		execute_process(COMMAND ${PERL_EXECUTABLE} "${CONVERT_PRL_PATH}"
			--lib ${_lib_location}
			--out ${STATIC_DEPENDENCIES_CMAKE_FILE}
			--component ${_qt_component}
			--compiler ${CMAKE_CXX_COMPILER_ID}
		)
	endif()
endmacro()

if(QT_STATIC_BUILD)
	foreach(qt_module ${QT_REQUIRED_COMPONENTS})
		CONVERT_PRL_LIBS_TO_CMAKE(${qt_module})
	endforeach()
    # HACK: We must explicitly add LIB path of the Qt installation
    # to correctly find qtpcre
    link_directories(${_qt5_install_prefix}/../)

	# Now that we generated the dependencies, import them.
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CONVERT_PRL_PATH}")
	if(NOT EXISTS ${STATIC_DEPENDENCIES_CMAKE_FILE})
		message(FATAL_ERROR "Unable to find ${STATIC_DEPENDENCIES_CMAKE_FILE}")
	endif()
	include(${STATIC_DEPENDENCIES_CMAKE_FILE})
	list(REMOVE_DUPLICATES STATIC_LIB_DEPENDENCIES)
endif()

# Localisation
add_subdirectory(locale)

add_custom_command(OUTPUT temp_tapyrus_locale.qrc
	COMMAND cmake
	ARGS
		-E copy
		"${CMAKE_CURRENT_SOURCE_DIR}/tapyrus_locale.qrc"
		temp_tapyrus_locale.qrc
	MAIN_DEPENDENCY tapyrus_locale.qrc
	VERBATIM
)

add_custom_command(OUTPUT qrc_tapyrus_locale.cpp
	COMMAND Qt5::rcc
	ARGS
		temp_tapyrus_locale.qrc
		-name tapyrus_locale
		-o qrc_tapyrus_locale.cpp
	MAIN_DEPENDENCY temp_tapyrus_locale.qrc
	DEPENDS locales
	VERBATIM
)

# UI elements
qt5_wrap_ui(UI_GENERATED_HEADERS
	forms/addressbookpage.ui
	forms/askpassphrasedialog.ui
	forms/coincontroldialog.ui
	forms/editaddressdialog.ui
	forms/helpmessagedialog.ui
	forms/intro.ui
	forms/modaloverlay.ui
	forms/openuridialog.ui
	forms/optionsdialog.ui
	forms/overviewpage.ui
	forms/receivecoinsdialog.ui
	forms/receiverequestdialog.ui
	forms/debugwindow.ui
	forms/sendcoinsdialog.ui
	forms/sendcoinsentry.ui
	forms/signverifymessagedialog.ui
	forms/transactiondescdialog.ui
)

# Qt MOC
set(CMAKE_AUTOMOC ON)

# Handle qrc resources
qt5_add_resources(QRC_BITCOIN_CPP tapyrus.qrc)

add_library(tapyrus-qt-base
	bantablemodel.cpp
	callback.h
	tapyrusaddressvalidator.cpp
	tapyrusamountfield.cpp
	tapyrusgui.cpp
	tapyrusunits.cpp
	clientmodel.cpp
	csvmodelwriter.cpp
	guiutil.cpp
	intro.cpp
	modaloverlay.cpp
	networkstyle.cpp
	notificator.cpp
	optionsdialog.cpp
	optionsmodel.cpp
	peertablemodel.cpp
	platformstyle.cpp
	qvalidatedlineedit.cpp
	qvaluecombobox.cpp
	rpcconsole.cpp
	splashscreen.cpp
	trafficgraphwidget.cpp
	utilitydialog.cpp

	# Handle ui files
	${UI_GENERATED_HEADERS}

	# Translations
	${BITCOIN_QM_FILES}

	# Handle qrc files
	${QRC_BITCOIN_CPP}
	qrc_tapyrus_locale.cpp
)

# Windows support
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	target_sources(tapyrus-qt-base PRIVATE winshutdownmonitor.cpp)
endif()

target_include_directories(tapyrus-qt-base
	PUBLIC
		.
		${CMAKE_CURRENT_BINARY_DIR}
		${CMAKE_CURRENT_BINARY_DIR}/forms
)

target_link_libraries(tapyrus-qt-base
	server
	rpcclient
	Qt5::Widgets
	Qt5::Network
	Qt5::DBus
	qrencode
)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set_property(TARGET tapyrus-qt-base PROPERTY AUTOMOC_MOC_OPTIONS "-DQ_OS_MAC")
endif()

# Wallet
if(BUILD_TAPYRUS_WALLET)
	# Automoc option.
	set(AUTOMOC_MOC_OPTIONS -DENABLE_WALLET=1)

	# Add wallet functionality to tapyrus-qt
	target_sources(tapyrus-qt-base
		PRIVATE
			addressbookpage.cpp
			addresstablemodel.cpp
			askpassphrasedialog.cpp
			coincontroldialog.cpp
			coincontroltreewidget.cpp
			editaddressdialog.cpp
			openuridialog.cpp
			overviewpage.cpp
			paymentserver.cpp
			receivecoinsdialog.cpp
			receiverequestdialog.cpp
			recentrequeststablemodel.cpp
			sendcoinsdialog.cpp
			sendcoinsentry.cpp
			signverifymessagedialog.cpp
			transactiondesc.cpp
			transactiondescdialog.cpp
			transactionfilterproxy.cpp
			transactionrecord.cpp
			transactiontablemodel.cpp
			transactionview.cpp
			walletframe.cpp
			walletmodel.cpp
			walletmodeltransaction.cpp
			walletview.cpp
	)

	target_link_libraries(tapyrus-qt-base wallet)
endif()

# The executable
add_executable(tapyrus-qt tapyrus.cpp)
target_link_libraries(tapyrus-qt tapyrus-qt-base)

# Test tests
add_subdirectory(test)
