/**
 * @file FakeSocketWriterModule.hpp Boost.Asio-based fake socket writer plugin for low-bandwidth devices
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_PLUGINS_FAKESOCKETWRITERMODULE_HPP_
#define ASIOLIBS_PLUGINS_FAKESOCKETWRITERMODULE_HPP_

#include "SourceConcept.hpp"

#include "appfwk/DAQModule.hpp"

#include "datahandlinglibs/utils/RateLimiter.hpp"

#include "fddetdataformats/WIBEthFrame.hpp"

#include <boost/asio.hpp>

namespace dunedaq::asiolibs {

using sid_to_source_map_t = std::map<int, std::shared_ptr<SourceConcept>>;

/**
 * @brief Buffer size based on WIBEthFrame
 */
constexpr int buffer_size = sizeof(fddetdataformats::WIBEthFrame);

class FakeSocketWriterModule : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief FakeSocketWriterModule constructor
   * @param name DAQ module instance name
   */
  explicit FakeSocketWriterModule(const std::string& name);
  ~FakeSocketWriterModule() = default;

  FakeSocketWriterModule(const FakeSocketWriterModule&) = delete;            ///< FakeSocketWriterModule is not copy-constructible
  FakeSocketWriterModule& operator=(const FakeSocketWriterModule&) = delete; ///< FakeSocketWriterModule is not copy-assignable
  FakeSocketWriterModule(FakeSocketWriterModule&&) = delete;                 ///< FakeSocketWriterModule is not move-constructible
  FakeSocketWriterModule& operator=(FakeSocketWriterModule&&) = delete;      ///< FakeSocketWriterModule is not move-assignable

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
     * @brief Destination IP
     */
    std::optional<std::string> address;

    /**
     * @brief Port number
     */
    ushort port;

    /**
     * @brief Statistics of socket traffic
     */  
    std::shared_ptr<SocketStats> socket_stats;      
  };

  class FakeTCPWriter
  {
  public:
    /**
     * @brief Asynchronously creates and connects a TCP socket
     * @param io_context I/O context for socket creation
     * @param writer_config TCP writer configuration
     * @return Coroutine handle
     * @throws boost::system::system_error on failure
     */
    boost::asio::awaitable<void> configure(boost::asio::io_context& io_context, const WriterConfig& writer_config)
    {
    }

    /**
     * @brief Asynchronously reads data from the socket in a loop
     * @param sources Data sources
     * @return Coroutine handle
     */
    boost::asio::awaitable<void> start()
    {
    }

    /**
     * @brief Closes the socket
     */
    void stop()
    {      
    }

  private:
    /**
     * @brief TCP socket
     */
    std::unique_ptr<boost::asio::ip::tcp::socket> m_socket;

    /**
     * @brief Statistics of socket traffic
     */  
    std::shared_ptr<SocketStats> m_socket_stats;      

    uint64_t fake_timestamp() {
      static uint64_t timestamp = 0;
      if (timestamp != 0) {
        timestamp += 32 * 64;
      }      
      return timestamp;
    }

    uint64_t fake_sequence_id() {
      static uint64_t seq_id = 0;
      if (seq_id == 4095) {
        seq_id = 0;
      } else {
        ++seq_id;
      }
      return seq_id;
    }

    void fake_data(fddetdataformats::WIBEthFrame& frame) {
      constexpr uint64_t fake_det_id = 3; // dte: what is the significance of this number
      constexpr uint64_t fake_stream_id = 0;
      constexpr uint64_t fake_block_length = 0x382; // dte: what is the significance of this number

      frame.daq_header.det_id = fake_det_id;
      frame.daq_header.stream_id = fake_stream_id;
      frame.daq_header.seq_id = fake_sequence_id();
      frame.daq_header.block_length = fake_block_length;
      frame.daq_header.timestamp = fake_timestamp(); 
    }
  };

  class FakeUDPWriter
  {
  public:
    /**
     * @brief Creates a UDP socket
     * @param io_context I/O context for socket creation
     * @param reader_config UDP reader configuration
     */
    void configure(boost::asio::io_context& io_context, const WriterConfig& reader_config)
    {
      const auto endpoint = reader_config.address
                              ? boost::asio::ip::udp::endpoint(
                                  boost::asio::ip::address::from_string(*reader_config.address), reader_config.port)
                              : boost::asio::ip::udp::endpoint(boost::asio::ip::address_v4::any(), reader_config.port);
      m_socket = std::make_unique<boost::asio::ip::udp::socket>(io_context, endpoint);

      TLOG() << "Created UDP socket on " << (reader_config.address ? *reader_config.address : "0.0.0.0") << ":"
                  << reader_config.port;
    }

    /**
     * @brief Asynchronously writes data to the socket in a loop
     * @return Coroutine handle
     */
    boost::asio::awaitable<void> start()
    {
      fddetdataformats::WIBEthFrame frame;
      boost::asio::ip::udp::endpoint receiver_endpoint;
      constexpr auto rate_limit_khz = 30.5;

      datahandlinglibs::RateLimiter rate_limiter(rate_limit_khz);

      while (m_socket->is_open()) {
        fake_data(frame);

        const auto bytes_sent = co_await m_socket->async_send_to(
          boost::asio::buffer(&frame, buffer_size), receiver_endpoint, boost::asio::use_awaitable);

        ++m_socket_stats->packets_sent;
        m_socket_stats->bytes_sent.fetch_add(bytes_sent);

        rate_limiter.limit();
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
     * @brief Statistics of socket traffic
     */  
    std::shared_ptr<SocketStats> m_socket_stats;      

    uint64_t fake_timestamp() {
      static uint64_t timestamp = 0;
      if (timestamp != 0) {
        timestamp += 32 * 64;
      }      
      return timestamp;
    }

    uint64_t fake_sequence_id() {
      static uint64_t seq_id = 0;
      if (seq_id == 4095) {
        seq_id = 0;
      } else {
        ++seq_id;
      }
      return seq_id;
    }

    void fake_data(fddetdataformats::WIBEthFrame& frame) {
      constexpr uint64_t fake_det_id = 3; // dte: what is the significance of this number
      constexpr uint64_t fake_stream_id = 0;
      constexpr uint64_t fake_block_length = 0x382; // dte: what is the significance of this number

      frame.daq_header.det_id = fake_det_id;
      frame.daq_header.stream_id = fake_stream_id;
      frame.daq_header.seq_id = fake_sequence_id();
      frame.daq_header.block_length = fake_block_length;
      frame.daq_header.timestamp = fake_timestamp(); 
    }
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
  std::vector<std::variant<FakeTCPWriter, FakeUDPWriter>> m_writers;

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

#endif // ASIOLIBS_PLUGINS_FAKESOCKETWRITERMODULE_HPP_
