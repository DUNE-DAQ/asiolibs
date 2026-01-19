cmake_minimum_required(VERSION 3.12)
project(asiolibs VERSION 1.2.1)

find_package(daq-cmake REQUIRED)

daq_setup_environment()

find_package(ers REQUIRED)
find_package(appfwk REQUIRED)
find_package(logging REQUIRED)
find_package(fddetdataformats REQUIRED)
find_package(datahandlinglibs REQUIRED)
find_package(fdreadoutlibs REQUIRED)
find_package(opmonlib REQUIRED)

daq_protobuf_codegen(opmon/*.proto)

##############################################################################
# Dependency sets
set(DUNEDAQ_DEPENDENCIES datahandlinglibs::datahandlinglibs fddetdataformats::fddetdataformats fdreadoutlibs::fdreadoutlibs ers::ers)

##############################################################################
# Main library
daq_add_library(*.cpp LINK_LIBRARIES ${DUNEDAQ_DEPENDENCIES})

##############################################################################
# Plugins
daq_add_plugin(SocketReaderModule duneDAQModule LINK_LIBRARIES asiolibs opmonlib::opmonlib)
daq_add_plugin(SocketWriterModule duneDAQModule LINK_LIBRARIES asiolibs opmonlib::opmonlib)

daq_install()
