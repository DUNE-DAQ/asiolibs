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
#include "appfwk/ConfigurationManager.hpp"

#include "datahandlinglibs/utils/RateLimiter.hpp"

#include "fddetdataformats/WIBEthFrame.hpp"
#include "fdreadoutlibs/DUNEWIBEthTypeAdapter.hpp"

#include <boost/asio.hpp>

#include <string>
#include <memory>
#include <vector>

namespace dunedaq::asiolibs {

/**
 * @brief Buffer size based on WIBEthFrame
 */
constexpr int buffer_size = sizeof(fddetdataformats::WIBEthFrame);

/**
 * @brief Maximum packet sequence ID before reset
 */
constexpr uint64_t max_seq_id = 4095;

/**
 * @brief Timestamp difference between packets
 */
constexpr uint64_t timestamp_diff = 32 * 64;

/**
 * @brief Fake packet detector ID
 */
constexpr uint64_t fake_det_id = 3;

/**
 * @brief Fake packet stream ID
 */
constexpr uint64_t fake_stream_id = 0;

/**
 * @brief Fake packet block length
 */
constexpr uint64_t fake_block_length = 0x382;

/**
 * @brief Packet transmission rate in kHz
 */
//constexpr double packet_rate_khz = 30.5;

constexpr int wibeth_time_tick_diff = fdreadoutlibs::types::DUNEWIBEthTypeAdapter::expected_tick_difference;
constexpr double wibeth_rate_khz = 62500./wibeth_time_tick_diff;

/**
 * @brief Calculate the next fake sequence ID for a packet
 * @param seq_id Fake packet sequence ID
 */
void
fake_sequence_id(uint64_t& seq_id)
{
  seq_id = seq_id == max_seq_id ? 0 : ++seq_id;
}

/**
 * @brief Calculate the next fake timestamp for a packet
 * @param timestamp Fake packet timestamp
 */
void
fake_timestamp(uint64_t& timestamp)
{
  if (timestamp != 0) {
    timestamp += timestamp_diff;
  }
}

/**
 * @brief Create a fake packet
 * @param frame Fake packet
 * @param seq_id Fake packet sequence ID
 * @param timestamp Fake packet timestamp
 */
void
fake_data(fddetdataformats::WIBEthFrame& frame, uint64_t& seq_id, uint64_t& timestamp)
{
  frame.daq_header.det_id = fake_det_id;
  frame.daq_header.crate_id = 1;
  frame.daq_header.slot_id = 1;
  frame.daq_header.stream_id = fake_stream_id;
  fake_sequence_id(seq_id);
  frame.daq_header.seq_id = seq_id;
  frame.daq_header.block_length = fake_block_length;
  fake_timestamp(timestamp);
  frame.daq_header.timestamp = timestamp;
}

class FakeSocketWriterModule : public dunedaq::appfwk::DAQModule
{
public:
  /**
   * @brief FakeSocketWriterModule constructor
   * @param name DAQ module instance name
   */
  explicit FakeSocketWriterModule(const std::string& name);
  ~FakeSocketWriterModule() = default;

  FakeSocketWriterModule(const FakeSocketWriterModule&) = delete; ///< FakeSocketWriterModule is not copy-constructible
  FakeSocketWriterModule& operator=(const FakeSocketWriterModule&) =
    delete;                                                  ///< FakeSocketWriterModule is not copy-assignable
  FakeSocketWriterModule(FakeSocketWriterModule&&) = delete; ///< FakeSocketWriterModule is not move-constructible
  FakeSocketWriterModule& operator=(FakeSocketWriterModule&&) =
    delete; ///< FakeSocketWriterModule is not move-assignable

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

  class FakeTCPWriter
  {
  public:
    /**
     * @brief Creates and connects a TCP socket
     * @param io_context I/O context for socket creation
     * @param writer_config TCP writer configuration
     * @throws boost::system::system_error on failure
     */
    void configure(boost::asio::io_context& io_context, const WriterConfig& writer_config)
    {
      m_socket_stats = writer_config.socket_stats;

      while (true) {
        try {
          m_socket = std::make_unique<boost::asio::ip::tcp::socket>(io_context);

          m_socket->connect(boost::asio::ip::tcp::endpoint(
            boost::asio::ip::address::from_string(writer_config.remote_ip), writer_config.remote_port));
          break;
        } catch (const boost::system::system_error& e) {
          TLOG() << "Connection failed: " << e.what() << ". Retrying in 1 second...";
          std::this_thread::sleep_for(std::chrono::seconds(1));
        }
      }

      TLOG() << "Established TCP connection to " << writer_config.remote_ip << ":" << writer_config.remote_port;
    }

    /**
     * @brief Asynchronously reads data from the socket in a loop
     * @param writer_config TCP writer configuration
     * @return Coroutine handle
     */
    boost::asio::awaitable<void> start()
    {
      fddetdataformats::WIBEthFrame frame;
      uint64_t seq_id = 0;
      uint64_t timestamp = 0;

      datahandlinglibs::RateLimiter rate_limiter(wibeth_rate_khz);

      while (m_socket->is_open()) {
        fake_data(frame, seq_id, timestamp);

        const auto bytes_sent =
          co_await m_socket->async_send(boost::asio::buffer(&frame, buffer_size), boost::asio::use_awaitable);

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
     * @brief Statistics of socket traffic
     */
    std::shared_ptr<SocketStats> m_socket_stats;
  };

  class FakeUDPWriter
  {
  public:
    /**
     * @brief Creates a UDP socket
     * @param io_context I/O context for socket creation
     * @param writer_config UDP writer configuration
     */
    void configure(boost::asio::io_context& io_context, const WriterConfig& writer_config)
    {
      m_writer_config = writer_config;

      // Let the OS pick an available local IP and port for sending packets
      const boost::asio::ip::udp::endpoint sender_endpoint(boost::asio::ip::udp::v4(), 0);

      m_socket = std::make_unique<boost::asio::ip::udp::socket>(io_context, sender_endpoint);

      TLOG() << "Created UDP socket on " << m_socket->local_endpoint().address() << ":"
             << m_socket->local_endpoint().port();
    }

    /**
     * @brief Asynchronously writes data to the socket in a loop
     * @return Coroutine handle
     */
    boost::asio::awaitable<void> start()
    {
      boost::asio::ip::udp::endpoint receiver_endpoint(boost::asio::ip::address::from_string(m_writer_config.remote_ip),
                                                       m_writer_config.remote_port);

      fddetdataformats::WIBEthFrame frame;
      uint64_t seq_id = 0;
      uint64_t timestamp = 0;

      datahandlinglibs::RateLimiter rate_limiter(wibeth_rate_khz);

      while (m_socket->is_open()) {
        fake_data(frame, seq_id, timestamp);

        const auto bytes_sent = co_await m_socket->async_send_to(
          boost::asio::buffer(&frame, buffer_size), receiver_endpoint, boost::asio::use_awaitable);

        ++m_writer_config.socket_stats->packets_sent;
        m_writer_config.socket_stats->bytes_sent.fetch_add(bytes_sent);

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
  std::shared_ptr<appfwk::ConfigurationManager> m_cfg;
};

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_PLUGINS_FAKESOCKETWRITERMODULE_HPP_
