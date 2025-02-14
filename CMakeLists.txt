cmake_minimum_required(VERSION 3.12)
project(asiolibs VERSION 0.1.0)

find_package(daq-cmake REQUIRED)

daq_setup_environment()

find_package(appfwk REQUIRED)
find_package(logging REQUIRED)
find_package(fddetdataformats REQUIRED)
find_package(datahandlinglibs REQUIRED)
find_package(fdreadoutlibs REQUIRED)
find_package(opmonlib REQUIRED)

daq_protobuf_codegen(opmon/*.proto)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2")

##############################################################################
# Dependency sets
set(DUNEDAQ_DEPENDENCIES datahandlinglibs::datahandlinglibs fddetdataformats::fddetdataformats fdreadoutlibs::fdreadoutlibs)

##############################################################################
# Main library
daq_add_library(*.cpp LINK_LIBRARIES appfwk::appfwk ${DUNEDAQ_DEPENDENCIES})

# Plugins
daq_add_plugin(SocketReaderModule duneDAQModule LINK_LIBRARIES asiolibs)

##############################################################################
# Integration tests
#daq_add_application(asiolibs_test_socket_receiver test_socket_receiver.cxx TEST LINK_LIBRARIES asiolibs opmonlib::opmonlib)

##############################################################################

daq_install()
