/**
 * @file SocketWriterModule.hpp Boost.Asio-based socket writer plugin for low-bandwidth devices
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_PLUGINS_SOCKETWRITERMODULE_HPP_
#define ASIOLIBS_PLUGINS_SOCKETWRITERMODULE_HPP_

#include "appfwk/DAQModule.hpp"

#include <boost/asio.hpp>

#include <string>
#include <memory>
#include <vector>

namespace dunedaq::asiolibs {

class FrameBuilder;
class ConfigurationManager;
class SocketWriterModule : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief SocketWriterModule constructor
   * @param name DAQ module instance name
   */
  explicit SocketWriterModule(const std::string& name);
  ~SocketWriterModule() = default;

  SocketWriterModule(const SocketWriterModule&) = delete; ///< SocketWriterModule is not copy-constructible
  SocketWriterModule& operator=(const SocketWriterModule&) =
    delete;                                                  ///< SocketWriterModule is not copy-assignable
  SocketWriterModule(SocketWriterModule&&) = delete; ///< SocketWriterModule is not move-constructible
  SocketWriterModule& operator=(SocketWriterModule&&) =
    delete; ///< SocketWriterModule is not move-assignable

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
     * @brief Sent packets
     */
    std::atomic<uint64_t> packets_sent{ 0 };

    /**
     * @brief Sent bytes
     */
    std::atomic<uint64_t> bytes_sent{ 0 };
  };

  struct WriterConfig
  {
    /**
     * @brief Destination IP address
     */
    std::string remote_ip;

    /**
     * @brief Destination port number
     */
    ushort remote_port;

    /**
     * @brief Statistics of socket traffic
     */
    std::shared_ptr<SocketStats> socket_stats;
  };

  class TCPWriter
  {
  public:
    /**
     * @brief Creates and connects a TCP socket
     * @param io_context I/O context for socket creation
     * @param writer_config TCP writer configuration
     * @throws boost::system::system_error on failure
     */
    void configure(boost::asio::io_context& io_context, const WriterConfig& writer_config);

    /**
     * @brief Asynchronously sends frames to the socket in a loop
     * @param frame_builder Builds frames to be sent
     * @return Coroutine handle
     */
    boost::asio::awaitable<void> start(std::shared_ptr<FrameBuilder> frame_builder);

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
     * @brief Statistics of socket traffic
     */
    std::shared_ptr<SocketStats> m_socket_stats;
  };

  class UDPWriter
  {
  public:
    /**
     * @brief Creates a UDP socket
     * @param io_context I/O context for socket creation
     * @param writer_config UDP writer configuration
     */
    void configure(boost::asio::io_context& io_context, const WriterConfig& writer_config);

    /**
     * @brief Asynchronously sends frames to the socket in a loop
     * @param frame_builder Builds frames to be sent
     * @return Coroutine handle
     */
    boost::asio::awaitable<void> start(std::shared_ptr<FrameBuilder> frame_builder);

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
     * @brief Socket writer configuration
     */
    WriterConfig m_writer_config;
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
   * @brief Socket writers
   */
  std::vector<std::variant<TCPWriter, UDPWriter>> m_writers;

  /**
   * @brief Background thread to keep the I/O context running
   */
  std::jthread m_io_thread;

  /**
   * @brief Type of socket
   */
  SocketType m_socket_type{ SocketType::INVALID };

  /**
   * @brief Socket writer configurations
   */
  std::vector<WriterConfig> m_writer_configs;

  /**
   * @brief Builds frames to be sent
   */  
  std::shared_ptr<FrameBuilder> m_frame_builder;

  /**
   * @brief DAQ configuration data
   */
  std::shared_ptr<appfwk::ConfigurationManager> m_cfg;
};

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_PLUGINS_SOCKETWRITERMODULE_HPP_
