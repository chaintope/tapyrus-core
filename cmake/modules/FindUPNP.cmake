# Try to find the UPNP - miniupnp librairy
# UPNP_FOUND - system has ZeroMQ lib
# UPNP_INCLUDE_DIR - the ZeroMQ include directory
# UPNP_LIBRARY - Libraries needed to use ZeroMQ

if(UPNP_INCLUDE_DIR AND UPNP_LIBRARY)
	# Already in cache, be silent
    set(UPNP_FIND_QUIETLY TRUE)
endif()

find_path(UPNP_INCLUDE_DIR NAMES miniupnpc.h)
find_library(UPNP_LIBRARY NAMES miniupnpc libminiupnpc-dev)
message(STATUS "UPNP lib: " ${UPNP_LIBRARY})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(UPNP DEFAULT_MSG UPNP_LIBRARY)

mark_as_advanced(UPNP_LIBRARY)

set(ZeroMQ_LIBRARIES ${UPNP_LIBRARY})
set(ZeroMQ_INCLUDE_DIRS ${UPNP_INCLUDE_DIR})
