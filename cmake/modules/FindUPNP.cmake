# Try to find the UPNP - miniupnp librairy
# UPNP_FOUND - system has ZeroMQ lib
# UPNP_INCLUDE_DIR - the ZeroMQ include directory
# UPNP_LIBRARY - Libraries needed to use ZeroMQ
set(BREW_HINT_UPNP)
if(CMAKE_HOST_APPLE)
    include(BrewHelper)
    find_brew_prefix(BREW_HINT_UPNP miniupnpc)
endif()

if(UPNP_INCLUDE_DIR AND UPNP_LIBRARY)
	# Already in cache, be silent
    set(UPNP_FIND_QUIETLY TRUE)
endif()

find_path(UPNP_INCLUDE_DIR NAMES miniupnpc.h HINTS ${BREW_HINT_UPNP}/include/miniupnpc)
find_library(UPNP_LIBRARY NAMES miniupnpc libminiupnpc-dev)
message(STATUS "UPNP lib: " ${UPNP_LIBRARY})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(UPNP DEFAULT_MSG UPNP_INCLUDE_DIR UPNP_LIBRARY)

mark_as_advanced(UPNP_INCLUDE_DIR UPNP_LIBRARY)

set(UPNP_LIBRARIES ${UPNP_LIBRARY})
set(UPNP_INCLUDE_DIRS ${UPNP_INCLUDE_DIR})
