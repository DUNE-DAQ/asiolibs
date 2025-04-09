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

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dunedaq::asiolibs {

using sid_to_source_map_t = std::map<int, std::shared_ptr<SourceConcept>>;

/**
 * @brief Forwards the payload to get processed
 * @param sources Data sources
 * @param buffer Payload buffer
 * @param size Payload size
 * @param source_id Detector stream source ID
 */
void handle_eth_payload(const sid_to_source_map_t& sources, char* buffer, std::size_t size, uint source_id);

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
  void init(const std::shared_ptr<appfwk::ConfigurationManager> mcfg) override;

private:
  enum class SocketType
  {
    TCP,
    UDP,
    INVALID
  };

  struct SocketStats
  {
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
    void configure(boost::asio::io_context& io_context, const ReaderConfig& reader_config);

    /**
     * @brief Asynchronously reads data from the socket in a loop
     * @param sources Data sources
     * @return Coroutine handle
     */
    boost::asio::awaitable<void> start(const sid_to_source_map_t& sources);

    /**
     * @brief Closes the socket
     */
    void stop();

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
    void configure(boost::asio::io_context& io_context, const ReaderConfig& reader_config);

    /**
     * @brief Asynchronously reads data from the socket in a loop
     * @param sources Data sources
     * @return Coroutine handle
     */
    boost::asio::awaitable<void> start(const sid_to_source_map_t& sources);

    /**
     * @brief Closes the socket
     */
    void stop();

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
  std::shared_ptr<appfwk::ConfigurationManager> m_cfg;

  // Sinks (SourceConcepts)
  /**
   * @brief Data sources
   */
  sid_to_source_map_t m_sources;
};

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_PLUGINS_SOCKETREADERMODULE_HPP_
