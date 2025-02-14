/**
 * @file SocketReaderModule.hpp Socket-based data receiver for low-bandwidth electronics
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_PLUGINS_SOCKETRECEIVER_HPP_
#define ASIOLIBS_PLUGINS_SOCKETRECEIVER_HPP_

#include "appfwk/DAQModule.hpp"

#include "SourceConcept.hpp"
#include "IfaceWrapper.hpp"
#include "fddetdataformats/WIBEthFrame.hpp"

#include <boost/array.hpp>
#include <boost/asio.hpp>

// TODO: exception safety

namespace dunedaq::asiolibs {

class SocketReaderModule : public dunedaq::appfwk::DAQModule
{
public:
  enum class SocketType {
    TCP,
    UDP,
    INVALID
  };

  SocketReaderModule(const std::string& name);
  ~SocketReaderModule() = default;

  SocketReaderModule(const SocketReaderModule&) = delete;            ///< SocketReaderModule is not copy-constructible
  SocketReaderModule& operator=(const SocketReaderModule&) = delete; ///< SocketReaderModule is not copy-assignable
  SocketReaderModule(SocketReaderModule&&) = delete;                 ///< SocketReaderModule is not move-constructible
  SocketReaderModule& operator=(SocketReaderModule&&) = delete;      ///< SocketReaderModule is not move-assignable

  void init(const std::shared_ptr<appfwk::ModuleConfiguration> mfcg) override;
  
  // Commands
  // todo simdilik public
  void do_configure(const data_t&);
  void do_start(const data_t&);
  void do_stop(const data_t&);
  void do_scrap(const data_t&);

private:

  class TCPReceiver 
  {
    public:
      void configure(boost::asio::io_context& io_context, unsigned short port, const std::optional<std::string>& address) {
        m_socket = std::make_unique<boost::asio::ip::tcp::socket>(io_context);
        m_socket->connect(boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(*address), port));
      }

      boost::asio::awaitable<void> start() {
        co_await boost::asio::async_read(*m_socket, boost::asio::buffer(m_buffer), boost::asio::use_awaitable);
        auto* frame = reinterpret_cast<fddetdataformats::WIBEthFrame*>(m_buffer.data());
        std::cout << frame->daq_header;   
        // // Setup callbacks on all sourcemodels
        // for (auto& [sourceid, source] : m_sources) {
        //   source->acquire_callback();
        // }
        // for (auto& [iface_id, iface] : m_ifaces) {
        //   iface->enable_flow();
        // }           
      }

      void stop() {
        m_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both);
        m_socket->close();        
      }

    private:
      std::unique_ptr<boost::asio::ip::tcp::socket> m_socket;
      boost::array<char, sizeof(fddetdataformats::WIBEthFrame)> m_buffer;
  };

  class UDPReceiver 
  {
    public:
      void configure(boost::asio::io_context& io_context, unsigned short port, const std::optional<std::string>& address) {
        const auto endpoint = address ? 
            boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string(*address), port)
          : boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), port);
        m_socket = std::make_unique<boost::asio::ip::udp::socket>(io_context, endpoint);
      }

      boost::asio::awaitable<void> start() {
        boost::asio::ip::udp::endpoint sender_endpoint;
        while (m_socket->is_open()) {
          co_await m_socket->async_receive_from(boost::asio::buffer(m_buffer), sender_endpoint, boost::asio::use_awaitable);
          auto* frame = reinterpret_cast<fddetdataformats::WIBEthFrame*>(m_buffer.data());
          std::cout << frame->daq_header;
        }
        // // Setup callbacks on all sourcemodels
        // for (auto& [sourceid, source] : m_sources) {
        //   source->acquire_callback();
        // }
        // for (auto& [iface_id, iface] : m_ifaces) {
        //   iface->enable_flow();
        // }           
      }  

      void stop() {
        m_socket->close();
      }

    private:
      std::unique_ptr<boost::asio::ip::udp::socket> m_socket;   
      boost::array<char, sizeof(fddetdataformats::WIBEthFrame)> m_buffer;
  };

  void set_running(bool /*should_run*/);

  SocketType string_to_socket_type(const std::string& socket_type);

  boost::asio::io_context m_io_context;
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type> m_work_guard;
  std::vector<std::variant<TCPReceiver, UDPReceiver>> m_receivers;
  std::jthread m_io_thread;
  std::optional<std::string> m_address;
  std::vector<unsigned short> m_ports;
  SocketType m_socket_type;

  // Internals
  std::shared_ptr<appfwk::ModuleConfiguration> m_cfg;
  
//   int m_running = 0;
  std::atomic<bool> m_run_marker;

//   // Interfaces (logical ID, MAC) -> IfaceWrapper
//   std::map<std::string, uint16_t> m_mac_to_id_map;
//   std::map<std::string, uint16_t> m_pci_to_id_map;
//  std::map<uint16_t, std::shared_ptr<IfaceWrapper>> m_ifaces;

  // TODO duplication and necessity
  // Sinks (SourceConcepts)
  using sid_to_source_map_t = std::map<int, std::shared_ptr<SourceConcept>>;
  sid_to_source_map_t m_sources;

  // Comment of Monitoring
  // Both SourceConcepts and IfaceWrappers are Monitorable Objecets
  // Both quantities are available for the ReaderModule and both are registered.
  // There is no loop because the Sources passed to the Wrappers are not registered in the wrapper  
};

} // namespace dunedaq::asiolibs


#endif // ASIOLIBS_PLUGINS_SOCKETRECEIVER_HPP_
