/**
 * @file SocketReaderModule.hpp Boost.Asio-based socket reader plugin for low-bandwidth devices
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_PLUGINS_SOCKETREADERMODULE_HPP_
#define ASIOLIBS_PLUGINS_SOCKETREADERMODULE_HPP_

#include "appfwk/DAQModule.hpp"

#include <boost/array.hpp>
#include <boost/asio.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dunedaq::asiolibs {

class SourceConcept;

class SocketReaderModule : public dunedaq::appfwk::DAQModule
{
public:
  using sender_t = std::pair<std::string, uint32_t>;
  using sender_stream_pair_t = std::pair<sender_t, uint32_t>;
  using sender_source_map_t = std::map<sender_stream_pair_t, std::shared_ptr<SourceConcept>>;

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

    /**
     * @brief Counts packets since last opmon data generation
     */
    std::atomic<int> stats_packet_count{ 0 };        
  };

  struct ReaderInfo
  {
    /**
     * @brief Source IP address
     */
    std::string local_ip;

    /**
     * @brief Source port number
     */
    uint32_t local_port;

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
     * @param reader_info TCP reader info
     * @throws boost::system::system_error on failure
     */
    void configure(boost::asio::io_context& io_context, std::shared_ptr<ReaderInfo> reader_info);

    /**
     * @brief Asynchronously receives payloads from the socket in a loop
     * @param sources Data sources
     * @return Coroutine handle
     */
    boost::asio::awaitable<void> start(const sender_source_map_t& sender_to_source);

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
     * @brief Connected remote
     */    
    sender_t m_remote;

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
     * @param reader_info UDP reader info
     */
    void configure(boost::asio::io_context& io_context, std::shared_ptr<ReaderInfo> reader_info);

    /**
     * @brief Asynchronously receives payloads from the socket in a loop
     * @param sources Data sources
     * @return Coroutine handle
     */
    boost::asio::awaitable<void> start(const sender_source_map_t& sender_to_source);

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
    uint32_t m_source_id;

    /**
     * @brief Statistics of socket traffic
     */
    std::shared_ptr<SocketStats> m_socket_stats;
  };

  // Commands
  void do_configure(const CommandData_t&);
  void do_start(const CommandData_t&);
  void do_stop(const CommandData_t&);

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
  std::vector<std::shared_ptr<std::variant<TCPReader, UDPReader>>> m_readers;

  /**
   * @brief Background thread to keep the I/O context running
   */
  std::jthread m_io_thread;

  /**
   * @brief Socket reader infos
   */
  std::vector<std::shared_ptr<ReaderInfo>> m_reader_infos;

  // Sinks (SourceConcepts)
  using sid_to_source_map_t = std::map<uint32_t, std::shared_ptr<SourceConcept>>;
  /**
   * @brief Data sources
   */
  sid_to_source_map_t m_sources;

  /**
   * @brief Map between a pair of {sender, stream} and the corresponding source
   */    
  sender_source_map_t m_sender_to_source;  

  // RUN START T0
  /**
   * @brief Timestamp used to measure time between opmon reports
   */   
  std::chrono::time_point<std::chrono::high_resolution_clock> m_t0;    
};

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_PLUGINS_SOCKETREADERMODULE_HPP_
