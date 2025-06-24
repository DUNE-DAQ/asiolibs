/**
 * @file FakeSocketWriterModule.cpp Boost.Asio-based fake socket writer plugin for low-bandwidth devices
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "FakeSocketWriterModule.hpp"

#include "CreateSource.hpp"

#include "appmodel/FakeSocketDataSender.hpp"
#include "appmodel/NetworkDetectorToDaqConnection.hpp"
#include "appmodel/NWDetDataSender.hpp"
#include "appmodel/SocketDataWriterModule.hpp"
#include "appmodel/SocketReceiver.hpp"
#include "appmodel/SocketWriterConf.hpp"
#include "confmodel/DetectorStream.hpp"

#include "datahandlinglibs/DataHandlingIssues.hpp"

#include "asiolibs/opmon/FakeSocketWriterModule.pb.h"

#include <string>
#include <memory>
#include <vector>
#include <utility>

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
constexpr double packet_rate_khz = 1;
  
void
fake_sequence_id(uint64_t& seq_id)
{
  seq_id = seq_id == max_seq_id ? 0 : ++seq_id;
}

void
fake_timestamp(uint64_t& timestamp)
{
  static bool first_packet = true;
  if (first_packet) {
    first_packet = false;
    auto time_now = std::chrono::system_clock::now().time_since_epoch();
    uint64_t current_time = // NOLINT (build/unsigned)
      std::chrono::duration_cast<std::chrono::microseconds>(time_now).count();
    // FIXME: where do I get the clockspeed from?
    // ts_0 = (m_conf.clock_speed_hz / 100000) * current_time;
    timestamp = 625 * current_time / 10;    
  } else {
    timestamp += timestamp_diff;
  }
}

void
fake_adc(fddetdataformats::WIBEthFrame& frame)
{
  for (int time = 0; time < 64; ++time) {
    for (int channel = 0; channel < 64; ++channel) {
      frame.set_adc(channel, time, 0); 
    }
    if (time != 0) {
      frame.set_adc(0, time, 666);
    }
  }
}

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
  fake_adc(frame);
}

FakeSocketWriterModule::FakeSocketWriterModule(const std::string& name)
  : DAQModule(name)
  , m_work_guard(boost::asio::make_work_guard(m_io_context))
{
  register_command("conf", &FakeSocketWriterModule::do_configure);
  register_command("start", &FakeSocketWriterModule::do_start);
  register_command("stop_trigger_sources", &FakeSocketWriterModule::do_stop);
}

void
FakeSocketWriterModule::init(const std::shared_ptr<appfwk::ConfigurationManager> mcfg)
{
  m_cfg = mcfg;
  auto* mdal = m_cfg->get_dal<appmodel::SocketDataWriterModule>(get_name());
  auto* module_conf = mdal->get_configuration()->cast<appmodel::SocketWriterConf>();

  const auto remote_ip = module_conf->get_remote_ip();

  m_socket_type = string_to_socket_type(module_conf->get_socket_type());
  if (m_socket_type != SocketType::TCP && m_socket_type != SocketType::UDP) {
    throw std::invalid_argument("Error: Only TCP and UDP are allowed!");
  }

  std::vector<const appmodel::NetworkDetectorToDaqConnection*> d2d_conns;
  for (auto* connection : mdal->get_connections()) {
    auto* nw_connection = connection->cast<appmodel::NetworkDetectorToDaqConnection>();

    if (nw_connection == nullptr) {
      dunedaq::datahandlinglibs::GenericConfigurationError err(
          ERS_HERE,
          fmt::format("Found connection {} of type {} while expecting type NetworkDetectorToDaqConnection",
                      connection->UID(),
                      connection->class_name()));
      ers::fatal(err);
      throw err;
    }

    if (connection->is_disabled(*(m_cfg->session()))) {
      continue;
    }

    d2d_conns.push_back(nw_connection);
  }

  for (auto* d2d_conn : d2d_conns) {
    auto* socket_receiver = d2d_conn->get_net_receiver();

    for (auto* nw_sender : d2d_conn->get_net_senders()) {

      if (nw_sender->is_disabled(*(m_cfg->session()))) {
        continue;
      }

      if (nw_sender->get_streams().size() > 1) {
        dunedaq::datahandlinglibs::GenericConfigurationError err(ERS_HERE,
                                                                 "Multiple streams currently are not supported!");
        ers::fatal(err);
        throw err;
      }

      for (auto* det_stream : nw_sender->get_streams()) {
        const auto* socket_sender = nw_sender->cast<appmodel::FakeSocketDataSender>();
        if (!socket_sender) {
          throw dunedaq::datahandlinglibs::InitializationError(
            ERS_HERE,
            fmt::format("Found {} of type {} in connection {} while expecting type FakeSocketDataSender",
                      nw_sender->UID(),
                      nw_sender->class_name(),
                      d2d_conn->UID()));
        }
        m_writer_configs.emplace_back(remote_ip, socket_sender->get_port(), std::make_shared<SocketStats>());
      }
    }
  }

  m_writers.reserve(m_writer_configs.size());
  if (m_socket_type == SocketType::TCP) {
    for (std::size_t i = 0; i < m_writer_configs.size(); ++i) {
      m_writers.emplace_back(FakeTCPWriter());
    }
  } else {
    for (std::size_t i = 0; i < m_writer_configs.size(); ++i) {
      m_writers.emplace_back(FakeUDPWriter());
    }
  }
}

FakeSocketWriterModule::SocketType
FakeSocketWriterModule::string_to_socket_type(const std::string& socket_type) const
{
  if (socket_type == "TCP") {
    return FakeSocketWriterModule::SocketType::TCP;
  } else if (socket_type == "UDP") {
    return FakeSocketWriterModule::SocketType::UDP;
  }
  return FakeSocketWriterModule::SocketType::INVALID;
}

void
FakeSocketWriterModule::do_configure(const data_t&)
{
  for (std::size_t i = 0; i < m_writers.size(); ++i) {
    const auto& writer_config = m_writer_configs[i];
    std::visit([this, &writer_config](auto& writer) { writer.configure(m_io_context, writer_config); }, m_writers[i]);
  }
}

void
FakeSocketWriterModule::do_start(const data_t&)
{
  m_io_thread = std::jthread([this] { m_io_context.run(); });

  for (std::size_t i = 0; i < m_writers.size(); ++i) {
    boost::asio::co_spawn(
      m_io_context, std::visit([this](auto& writer) { return writer.start(); }, m_writers[i]), boost::asio::detached);
  }
}

void
FakeSocketWriterModule::do_stop(const data_t&)
{
  for (auto& writer : m_writers) {
    std::visit([](auto& writer) { writer.stop(); }, writer);
  }

  m_work_guard.reset();
}

void
FakeSocketWriterModule::generate_opmon_data()
{
  for (const auto& writer_config : m_writer_configs) {
    opmon::SocketWriterStats stats;
    stats.set_packets_sent(writer_config.socket_stats->packets_sent.load());
    stats.set_bytes_sent(writer_config.socket_stats->bytes_sent.load());
    publish(std::move(stats), { { "socket-writer", std::to_string(writer_config.remote_port) } });
  }
}

void
FakeSocketWriterModule::FakeTCPWriter::configure(boost::asio::io_context& io_context, const WriterConfig& writer_config)
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

boost::asio::awaitable<void>
FakeSocketWriterModule::FakeTCPWriter::start()
{
  fddetdataformats::WIBEthFrame frame;
  uint64_t seq_id = 0;
  uint64_t timestamp = 0;

  datahandlinglibs::RateLimiter rate_limiter(packet_rate_khz);

  while (m_socket->is_open()) {
    fake_data(frame, seq_id, timestamp);

    const auto bytes_sent =
      co_await m_socket->async_send(boost::asio::buffer(&frame, buffer_size), boost::asio::use_awaitable);

    ++m_socket_stats->packets_sent;
    m_socket_stats->bytes_sent.fetch_add(bytes_sent);

    rate_limiter.limit();
  }
}

void
FakeSocketWriterModule::FakeTCPWriter::stop()
{
  m_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both);
  m_socket->close();
  TLOG() << "Shutdown TCP connection";
}

void
FakeSocketWriterModule::FakeUDPWriter::configure(boost::asio::io_context& io_context, const WriterConfig& writer_config)
{
  m_writer_config = writer_config;

  // Let the OS pick an available local IP and port for sending packets
  const boost::asio::ip::udp::endpoint sender_endpoint(boost::asio::ip::udp::v4(), 0);

  m_socket = std::make_unique<boost::asio::ip::udp::socket>(io_context, sender_endpoint);

  TLOG() << "Created UDP socket on " << m_socket->local_endpoint().address() << ":"
         << m_socket->local_endpoint().port();
}

boost::asio::awaitable<void>
FakeSocketWriterModule::FakeUDPWriter::start()
{
  boost::asio::ip::udp::endpoint receiver_endpoint(boost::asio::ip::address::from_string(m_writer_config.remote_ip),
                                                   m_writer_config.remote_port);

  fddetdataformats::WIBEthFrame frame;
  uint64_t seq_id = 0;
  uint64_t timestamp = 0;

  datahandlinglibs::RateLimiter rate_limiter(packet_rate_khz);

  while (m_socket->is_open()) {
    fake_data(frame, seq_id, timestamp);
    
    const auto bytes_sent = co_await m_socket->async_send_to(
      boost::asio::buffer(&frame, buffer_size), receiver_endpoint, boost::asio::use_awaitable);

    ++m_writer_config.socket_stats->packets_sent;
    m_writer_config.socket_stats->bytes_sent.fetch_add(bytes_sent);

    rate_limiter.limit();
  }
}

void
FakeSocketWriterModule::FakeUDPWriter::stop()
{
  m_socket->close();
  TLOG() << "Closed UDP socket";
}

} // namespace dunedaq::asiolibs

DEFINE_DUNE_DAQ_MODULE(dunedaq::asiolibs::FakeSocketWriterModule)
