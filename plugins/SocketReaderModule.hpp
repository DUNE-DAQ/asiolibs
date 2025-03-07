/**
 * @file SocketReaderModule.hpp Boost.Asio-based socket reader plugin for low-bandwidth devices
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_PLUGINS_SOCKETREADERMODULE_HPP_
#define ASIOLIBS_PLUGINS_SOCKETREADERMODULE_HPP_

#include "SourceConcept.hpp"

#include "appfwk/DAQModule.hpp"

#include "fddetdataformats/WIBEthFrame.hpp"

#include <boost/array.hpp>
#include <boost/asio.hpp>

namespace dunedaq::asiolibs {

using sid_to_source_map_t = std::map<int, std::shared_ptr<SourceConcept>>;

/**
 * @brief Minimum valid payload size in bytes
 */
constexpr int min_expected_payload_size = 7000;

/**
 * @brief Buffer size based on WIBEthFrame
 */
constexpr int buffer_size = sizeof(fddetdataformats::WIBEthFrame);

/**
 * @brief Forwards the payload to get processed
 * @param sources Data sources
 * @param buffer Payload buffer
 * @param size Payload size
 * @param source_id Detector stream source ID
 */
void
handle_eth_payload(const sid_to_source_map_t& sources, char* buffer, std::size_t size, uint source_id)
{
  //auto* frame = reinterpret_cast<fddetdataformats::WIBEthFrame*>(buffer); //dte remove
  if (auto src_it = sources.find(source_id); src_it != sources.end()) {
    //TLOG() << "sequence_id = " << frame->daq_header.seq_id; //dte remove
    src_it->second->handle_payload(buffer, size);
  } else {
    TLOG() << "Unexpected StreamID in payload! (" << source_id << ")";
  }
}

class SocketReaderModule : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief SocketReaderModule constructor
   * @param name DAQ module instance name
   */
  explicit SocketReaderModule(const std::string& name);
  ~SocketReaderModule() = default;

  SocketReaderModule(const SocketReaderModule&) = delete;            ///< SocketReaderModule is not copy-constructible
  SocketReaderModule& operator=(const SocketReaderModule&) = delete; ///< SocketReaderModule is not copy-assignable
  SocketReaderModule(SocketReaderModule&&) = delete;                 ///< SocketReaderModule is not move-constructible
  SocketReaderModule& operator=(SocketReaderModule&&) = delete;      ///< SocketReaderModule is not move-assignable

  /**
   * @brief Handles initialization on boot
   * @param mcfg DAQ configuration data
   */
  void init(const std::shared_ptr<appfwk::ModuleConfiguration> mcfg) override;

private:
  enum class SocketType
  {
    TCP,
    UDP,
    INVALID
  };

  struct SocketStats {
    /**
     * @brief Received packets
     */  
    std::atomic<uint64_t> packets_received{ 0 };

    /**
     * @brief Received bytes
     */  
    std::atomic<uint64_t> bytes_received{ 0 };
  };  

  struct ReaderConfig
  {
    /**
     * @brief Source IP address
     */
    std::string local_ip;

    /**
     * @brief Source port number
     */
    ushort local_port;

    /**
     * @brief Detector stream source ID
     */
    uint source_id;

    /**
     * @brief Statistics of socket traffic
     */  
    std::shared_ptr<SocketStats> socket_stats;    
  };

  class TCPReader
  {
  public:
    /**
     * @brief Asynchronously creates and connects a TCP socket
     * @param io_context I/O context for socket creation
     * @param reader_config TCP reader configuration
     * @throws boost::system::system_error on failure
     */
    void configure(boost::asio::io_context& io_context, const ReaderConfig& reader_config)
    {
      m_source_id = reader_config.source_id;
      m_socket_stats = reader_config.socket_stats;

      m_socket = std::make_unique<boost::asio::ip::tcp::socket>(io_context);

      boost::asio::ip::tcp::acceptor acceptor(io_context,
        boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(reader_config.local_ip), reader_config.local_port));

      TLOG() << "Waiting for TCP connection at " << reader_config.local_ip << ":" << reader_config.local_port;

      acceptor.accept(*m_socket);

      TLOG() << "Established TCP connection from " << m_socket->remote_endpoint().address() << ":" << m_socket->remote_endpoint().port();
    }

    /**
     * @brief Asynchronously reads data from the socket in a loop
     * @param sources Data sources
     * @return Coroutine handle
     */
    boost::asio::awaitable<void> start(const sid_to_source_map_t& sources)
    {
      boost::array<char, buffer_size> buffer;

      while (m_socket->is_open()) {
        const auto bytes_received =
          co_await boost::asio::async_read(*m_socket,
                                           boost::asio::buffer(buffer),
                                           boost::asio::transfer_at_least(min_expected_payload_size),
                                           boost::asio::use_awaitable);
        ++m_socket_stats->packets_received;
        m_socket_stats->bytes_received.fetch_add(bytes_received);
        handle_eth_payload(sources, buffer.data(), bytes_received, m_source_id);
      }
    }

    /**
     * @brief Closes the socket
     */
    void stop()
    {
      m_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both);
      m_socket->close();
      TLOG() << "Shutdown TCP connection";
    }

  private:
    /**
     * @brief TCP socket
     */
    std::unique_ptr<boost::asio::ip::tcp::socket> m_socket;

    /**
     * @brief Detector stream source ID
     */
    uint m_source_id;

    /**
     * @brief Statistics of socket traffic
     */      
    std::shared_ptr<SocketStats> m_socket_stats;
  };

  class UDPReader
  {
  public:
    /**
     * @brief Creates a UDP socket
     * @param io_context I/O context for socket creation
     * @param reader_config UDP reader configuration
     */
    void configure(boost::asio::io_context& io_context, const ReaderConfig& reader_config)
    {
      m_source_id = reader_config.source_id;
      m_socket_stats = reader_config.socket_stats;

      const auto receiver_endpoint = boost::asio::ip::udp::endpoint(
                              boost::asio::ip::address::from_string(reader_config.local_ip), reader_config.local_port);
      m_socket = std::make_unique<boost::asio::ip::udp::socket>(io_context, receiver_endpoint);

      TLOG() << "Created UDP socket on " << reader_config.local_ip << ":" << reader_config.local_port;
    }

    /**
     * @brief Asynchronously reads data from the socket in a loop
     * @param sources Data sources
     * @return Coroutine handle
     */
    boost::asio::awaitable<void> start(const sid_to_source_map_t& sources)
    {
      boost::array<char, buffer_size> buffer;
      boost::asio::ip::udp::endpoint sender_endpoint;

      while (m_socket->is_open()) {
        std::size_t bytes_received = co_await m_socket->async_receive_from(
          boost::asio::buffer(buffer), sender_endpoint, boost::asio::use_awaitable);

        ++m_socket_stats->packets_received;
        m_socket_stats->bytes_received.fetch_add(bytes_received);          

        if (bytes_received > min_expected_payload_size) [[likely]] { // RS FIXME: do proper check on data length later
          handle_eth_payload(sources, buffer.data(), bytes_received, m_source_id);
        } else {
          TLOG() << "Payload is smaller than " << min_expected_payload_size << " (" << bytes_received << ")";
          
        }
      }
    }

    /**
     * @brief Closes the socket
     */
    void stop()
    {
      m_socket->close();
      TLOG() << "Closed UDP socket";
    }

  private:
    /**
     * @brief UDP socket
     */
    std::unique_ptr<boost::asio::ip::udp::socket> m_socket;

    /**
     * @brief Detector stream source ID
     */
    uint m_source_id;

    /**
     * @brief Statistics of socket traffic
     */      
    std::shared_ptr<SocketStats> m_socket_stats;    
  };

  // Commands
  void do_configure(const data_t&);
  void do_start(const data_t&);
  void do_stop(const data_t&);

  void generate_opmon_data() override;

  /**
   * @brief Converts a socket type string to an enum
   * @param socket_type Socket type as a string
   * @return Corresponding SocketType enum
   */
  SocketType string_to_socket_type(const std::string& socket_type) const;

  /**
   * @brief I/O context for socket operations
   */
  boost::asio::io_context m_io_context;

  /**
   * @brief Prevents I/O context from exiting prematurely
   */
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type> m_work_guard;

  /**
   * @brief Socket readers
   */
  std::vector<std::variant<TCPReader, UDPReader>> m_readers;

  /**
   * @brief Background thread to keep the I/O context running
   */
  std::jthread m_io_thread;

  /**
   * @brief Type of socket
   */
  SocketType m_socket_type{ SocketType::INVALID };

  /**
   * @brief Socket reader configurations
   */
  std::vector<ReaderConfig> m_reader_configs;  

  // Internals
  /**
   * @brief DAQ configuration data
   */
  std::shared_ptr<appfwk::ModuleConfiguration> m_cfg;

  // Sinks (SourceConcepts)
  /**
   * @brief Data sources
   */
  sid_to_source_map_t m_sources;

};

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_PLUGINS_SOCKETREADERMODULE_HPP_
