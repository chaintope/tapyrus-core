# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

add_library(tapyrus_zmq STATIC EXCLUDE_FROM_ALL
  zmqabstractnotifier.cpp
  zmqnotificationinterface.cpp
  zmqpublishnotifier.cpp
  zmqrpc.cpp
)
target_compile_definitions(tapyrus_zmq
  PUBLIC
    ENABLE_ZMQ=1
)
target_link_libraries(tapyrus_zmq
  PRIVATE
    core_interface
    univalue
    zeromq
    Boost::headers
)

# Add Windows system libraries required by ZeroMQ
if(WIN32 OR MINGW)
  target_link_libraries(tapyrus_zmq
    PRIVATE
      ws2_32
      iphlpapi
  )
endif()

target_include_directories(tapyrus_zmq PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
