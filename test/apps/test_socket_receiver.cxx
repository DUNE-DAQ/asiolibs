/**
 * @file test_socket_receiver.cpp
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "asiolibs/plugins/SocketReaderModule.hpp"

using namespace dunedaq::asiolibs;

SocketReaderModule::SocketType string_to_socket_type(const std::string& socket_type) {
    if (socket_type == "tcp") {
        return SocketReaderModule::SocketType::TCP;
    } else if (socket_type == "udp") {
        return SocketReaderModule::SocketType::UDP;
    }
    return SocketReaderModule::SocketType::INVALID;
}

int main(int argc, char** argv) {
    if (argc < 3) {
      std::cerr << "Usage: test_socket_receiver <port> <tcp|udp> [<address>]\n";
      return 1;
    }

    const unsigned short port = std::atoi(argv[1]);
    const auto socket_type = string_to_socket_type(argv[2]);

    std::optional<std::string> address;
    if(argc == 4) {
        address = argv[3];
    }

    SocketReaderModule socket_receiver("socket_receiver"); // TODO fix name
    nlohmann::json confobj;
    //socket_receiver.init(0);
    socket_receiver.do_configure(confobj);
    socket_receiver.do_start(confobj);

    std::cout << "The connection will be closed in 10 seconds...\n";
    std::this_thread::sleep_for(std::chrono::seconds(10));

    socket_receiver.do_stop(confobj);
    //socket_receiver.do_scrap(confobj);
}

