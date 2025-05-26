/**
 * @file SocketWriterModule.cpp Boost.Asio-based socket writer plugin for low-bandwidth devices
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "SocketWriterModule.hpp"

#include "CreateFrameBuilder.hpp"

#include "appfwk/ConfigurationManager.hpp"
#include "appmodel/SocketDataSender.hpp"
#include "appmodel/NWDetDataSender.hpp"
#include "appmodel/SocketDataWriterModule.hpp"
#include "appmodel/SocketReceiver.hpp"
#include "appmodel/SocketWriterConf.hpp"
#include "confmodel/DetectorStream.hpp"
#include "confmodel/DetectorToDaqConnection.hpp"
#include "confmodel/QueueWithSourceId.hpp"

#include "datahandlinglibs/DataHandlingIssues.hpp"
#include "datahandlinglibs/utils/RateLimiter.hpp"

#include "asiolibs/opmon/SocketWriterModule.pb.h"

#include <string>
#include <memory>
#include <vector>
#include <utility>

namespace dunedaq::asiolibs {

/**
 * @brief Packet transmission rate in kHz
 */
constexpr double packet_rate_khz = 1;

SocketWriterModule::SocketWriterModule(const std::string& name)
  : DAQModule(name)
  , m_work_guard(boost::asio::make_work_guard(m_io_context))
{
  register_command("conf", &SocketWriterModule::do_configure);
  register_command("start", &SocketWriterModule::do_start);
  register_command("stop_trigger_sources", &SocketWriterModule::do_stop);
}

void
SocketWriterModule::init(const std::shared_ptr<appfwk::ConfigurationManager> mcfg)
{
  m_cfg = mcfg;
  auto* mdal = m_cfg->get_dal<appmodel::SocketDataWriterModule>(get_name());
  auto* module_conf = mdal->get_configuration()->cast<appmodel::SocketWriterConf>();

  const auto remote_ip = module_conf->get_remote_ip();

  m_socket_type = string_to_socket_type(module_conf->get_socket_type());
  if (m_socket_type != SocketType::TCP && m_socket_type != SocketType::UDP) {
    throw std::invalid_argument("Error: Only TCP and UDP are allowed!");
  }

  std::vector<const confmodel::DetectorToDaqConnection*> d2d_conns;
  for (auto* res : mdal->get_connections()) {
    auto* connection = res->cast<confmodel::DetectorToDaqConnection>();

    if (connection == nullptr) {
      dunedaq::datahandlinglibs::GenericConfigurationError err(
        ERS_HERE, "DetectorToDaqConnection configuration failed due expected but unavailable connection!");
      ers::fatal(err);
      throw err;
    }

    if (connection->disabled(*(m_cfg->session()))) {
      continue;
    }

    d2d_conns.push_back(connection);
  }

  for (auto* d2d_conn : d2d_conns) {
    auto* socket_receiver = d2d_conn->get_receiver()->cast<appmodel::SocketReceiver>();

    for (auto* sender : d2d_conn->get_senders()) {
      auto* nw_sender = sender->cast<appmodel::NWDetDataSender>();

      if (!nw_sender) {
        throw dunedaq::datahandlinglibs::InitializationError(
          ERS_HERE,
          fmt::format("Found {} of type {} in connection {} while expecting type NWDetDataSender",
                      socket_receiver->class_name(),
                      socket_receiver->UID(),
                      d2d_conn->UID()));
      }

      if (nw_sender->disabled(*(m_cfg->session()))) {
        continue;
      }

      if (nw_sender->get_contains().size() > 1) {
        dunedaq::datahandlinglibs::GenericConfigurationError err(ERS_HERE,
                                                                 "Multiple streams currently are not supported!");
        ers::fatal(err);
        throw err;
      }

      for (auto* res : nw_sender->get_contains()) {
        auto* det_stream = res->cast<confmodel::DetectorStream>();
        const auto* socket_sender = nw_sender->cast<appmodel::SocketDataSender>();
        m_writer_configs.emplace_back(remote_ip, socket_sender->get_port(), std::make_shared<SocketStats>());
      } 
    }
  }

  m_writers.reserve(m_writer_configs.size());
  if (m_socket_type == SocketType::TCP) {
    for (std::size_t i = 0; i < m_writer_configs.size(); ++i) {
      m_writers.emplace_back(TCPWriter());
    }
  } else {
    for (std::size_t i = 0; i < m_writer_configs.size(); ++i) {
      m_writers.emplace_back(UDPWriter());
    }
  }

  if (mdal->get_outputs().empty()) {
    auto err = dunedaq::datahandlinglibs::InitializationError(ERS_HERE,
                                                              "No outputs defined for socket reader in configuration.");
    ers::fatal(err);
    throw err;
  }

  for (auto* con : mdal->get_outputs()) {
    auto* queue = con->cast<confmodel::QueueWithSourceId>();
    if (queue == nullptr) {
      auto err = dunedaq::datahandlinglibs::InitializationError(ERS_HERE, "Outputs are not of type QueueWithGeoId.");
      ers::fatal(err);
      throw err;
    }  

    m_frame_builder = createFrameBuilder(queue->UID()); // FIXME (DTE): Overwriting doesn't make sense
  }       
}

SocketWriterModule::SocketType
SocketWriterModule::string_to_socket_type(const std::string& socket_type) const
{
  if (socket_type == "TCP") {
    return SocketWriterModule::SocketType::TCP;
  } else if (socket_type == "UDP") {
    return SocketWriterModule::SocketType::UDP;
  }
  return SocketWriterModule::SocketType::INVALID;
}

void
SocketWriterModule::do_configure(const data_t&)
{
  for (std::size_t i = 0; i < m_writers.size(); ++i) {
    const auto& writer_config = m_writer_configs[i];
    std::visit([this, &writer_config](auto& writer) { writer.configure(m_io_context, writer_config); }, m_writers[i]);
  }
}

void
SocketWriterModule::do_start(const data_t&)
{
  m_io_thread = std::jthread([this] { m_io_context.run(); });

  for (std::size_t i = 0; i < m_writers.size(); ++i) {
    boost::asio::co_spawn(
      m_io_context, std::visit([this](auto& writer) { return writer.start(m_frame_builder); }, m_writers[i]), boost::asio::detached);
  }
}

void
SocketWriterModule::do_stop(const data_t&)
{
  for (auto& writer : m_writers) {
    std::visit([](auto& writer) { writer.stop(); }, writer);
  }

  m_work_guard.reset();
}

void
SocketWriterModule::generate_opmon_data()
{
  for (const auto& writer_config : m_writer_configs) {
    opmon::SocketWriterStats stats;
    stats.set_packets_sent(writer_config.socket_stats->packets_sent.load());
    stats.set_bytes_sent(writer_config.socket_stats->bytes_sent.load());
    publish(std::move(stats), { { "socket-writer", std::to_string(writer_config.remote_port) } });
  }
}

void
SocketWriterModule::TCPWriter::configure(boost::asio::io_context& io_context, const WriterConfig& writer_config)
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
SocketWriterModule::TCPWriter::start(std::shared_ptr<FrameBuilder> frame_builder)
{
  datahandlinglibs::RateLimiter rate_limiter(packet_rate_khz);

  while (m_socket->is_open()) {
    const auto [buffer, buffer_size]= frame_builder->build_frame();

    const auto bytes_sent =
      co_await m_socket->async_send(boost::asio::buffer(buffer, buffer_size), boost::asio::use_awaitable);

    ++m_socket_stats->packets_sent;
    m_socket_stats->bytes_sent.fetch_add(bytes_sent);

    rate_limiter.limit();
  }
}

void
SocketWriterModule::TCPWriter::stop()
{
  m_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both);
  m_socket->close();
  TLOG() << "Shutdown TCP connection";
}

void
SocketWriterModule::UDPWriter::configure(boost::asio::io_context& io_context, const WriterConfig& writer_config)
{
  m_writer_config = writer_config;

  // Let the OS pick an available local IP and port for sending packets
  const boost::asio::ip::udp::endpoint sender_endpoint(boost::asio::ip::udp::v4(), 0);

  m_socket = std::make_unique<boost::asio::ip::udp::socket>(io_context, sender_endpoint);

  TLOG() << "Created UDP socket on " << m_socket->local_endpoint().address() << ":"
         << m_socket->local_endpoint().port();
}

boost::asio::awaitable<void>
SocketWriterModule::UDPWriter::start(std::shared_ptr<FrameBuilder> frame_builder)
{
  boost::asio::ip::udp::endpoint receiver_endpoint(boost::asio::ip::address::from_string(m_writer_config.remote_ip),
                                                   m_writer_config.remote_port);

  datahandlinglibs::RateLimiter rate_limiter(packet_rate_khz);

  while (m_socket->is_open()) {
    const auto [buffer, buffer_size]= frame_builder->build_frame();
    
    const auto bytes_sent = co_await m_socket->async_send_to(
      boost::asio::buffer(buffer, buffer_size), receiver_endpoint, boost::asio::use_awaitable);

    ++m_writer_config.socket_stats->packets_sent;
    m_writer_config.socket_stats->bytes_sent.fetch_add(bytes_sent);

    rate_limiter.limit();
  }
}

void
SocketWriterModule::UDPWriter::stop()
{
  m_socket->close();
  TLOG() << "Closed UDP socket";
}

} // namespace dunedaq::asiolibs

DEFINE_DUNE_DAQ_MODULE(dunedaq::asiolibs::SocketWriterModule)
