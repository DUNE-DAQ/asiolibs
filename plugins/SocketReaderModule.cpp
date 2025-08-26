/**
 * @file SocketReaderModule.cpp Boost.Asio-based socket reader plugin for low-bandwidth devices
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "SocketReaderModule.hpp"

#include "CreateSource.hpp"

#include "appmodel/DataReaderModule.hpp"
#include "appmodel/NetworkDetectorToDaqConnection.hpp"
#include "appmodel/SocketDataSender.hpp"
#include "appmodel/NWDetDataSender.hpp"
#include "appmodel/SocketReaderConf.hpp"
#include "appmodel/SocketReceiver.hpp"
#include "confmodel/DetectorStream.hpp"
#include "confmodel/GeoId.hpp"
#include "confmodel/QueueWithSourceId.hpp"

#include "datahandlinglibs/DataHandlingIssues.hpp"

#include "asiolibs/opmon/SocketReaderModule.pb.h"

#include <string>
#include <vector>
#include <memory>
#include <utility>

namespace dunedaq::asiolibs {

SocketReaderModule::SocketReaderModule(const std::string& name)
  : DAQModule(name)
  , m_work_guard(boost::asio::make_work_guard(m_io_context))
{
  register_command("conf", &SocketReaderModule::do_configure);
  register_command("start", &SocketReaderModule::do_start);
  register_command("stop_trigger_sources", &SocketReaderModule::do_stop);
}

inline void
tokenize(std::string const& str, const char delim, std::vector<std::string>& out)
{
  std::size_t start;
  std::size_t end = 0;
  while ((start = str.find_first_not_of(delim, end)) != std::string::npos) {
    end = str.find(delim, start);
    out.push_back(str.substr(start, end - start));
  }
}

void
SocketReaderModule::init(const std::shared_ptr<appfwk::ConfigurationManager> mcfg)
{
  m_cfg = mcfg;
  auto* mdal = m_cfg->get_dal<appmodel::DataReaderModule>(get_name());
  auto* module_conf = mdal->get_configuration()->cast<appmodel::SocketReaderConf>();

  const auto local_ip = module_conf->get_local_ip();

  m_socket_type = string_to_socket_type(module_conf->get_socket_type());
  if (m_socket_type != SocketType::TCP && m_socket_type != SocketType::UDP) {
    throw std::invalid_argument("Error: Only TCP and UDP are allowed!");
  }

  std::vector<const appmodel::NetworkDetectorToDaqConnection*> d2d_conns;
  for (auto* connection : mdal->get_connections()) {

    if (connection->is_disabled(*(m_cfg->get_session()))) {
      continue;
    }

    auto net_connection = connection->cast<appmodel::NetworkDetectorToDaqConnection>();
    if (net_connection == nullptr) {
        throw dunedaq::datahandlinglibs::InitializationError(
          ERS_HERE,
          fmt::format("Found connection {} of type {} while expecting type NetworkDetectorToDaqConnection",
                      connection->UID(),
                      connection->class_name()));
    }
    d2d_conns.push_back(net_connection);
  }

  for (auto* d2d_conn : d2d_conns) {
    for (auto* sender : d2d_conn->get_net_senders()) {
      auto* socket_sender = sender->cast<appmodel::SocketDataSender>();

      if (!socket_sender) {
        throw dunedaq::datahandlinglibs::InitializationError(
          ERS_HERE,
          fmt::format("Found {} of type {} in connection {} while expecting type SocketDataSender",
                      sender->UID(),
                      sender->class_name(),
                      d2d_conn->UID()));
      }

      if (socket_sender->is_disabled(*(m_cfg->get_session()))) {
        continue;
      }

      if (socket_sender->get_streams().size() > 1) {
        dunedaq::datahandlinglibs::GenericConfigurationError err(ERS_HERE,
                                                                 "Multiple streams currently are not supported!");
        ers::fatal(err);
        throw err;
      }

      for (auto* det_stream : socket_sender->get_streams()) {
        m_reader_configs.emplace_back(
          local_ip, socket_sender->get_port(), det_stream->get_source_id(), std::make_shared<SocketStats>());
      }
    }
  }

  m_readers.reserve(m_reader_configs.size());
  if (m_socket_type == SocketType::TCP) {
    for (std::size_t i = 0; i < m_reader_configs.size(); ++i) {
      m_readers.emplace_back(TCPReader());
    }
  } else {
    for (std::size_t i = 0; i < m_reader_configs.size(); ++i) {
      m_readers.emplace_back(UDPReader());
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

    // Check for CB prefix indicating Callback use
    const char delim = '_';
    const std::string target = queue->UID();
    std::vector<std::string> words;
    tokenize(target, delim, words);

    bool callback_mode = false;
    if (words.front() == "cb") {
      callback_mode = true;
    }

    auto ptr = m_sources[queue->get_source_id()] = createSourceModel(queue->UID(), callback_mode);
    register_node(queue->UID(), ptr);
  }  
}

SocketReaderModule::SocketType
SocketReaderModule::string_to_socket_type(const std::string& socket_type) const
{
  if (socket_type == "TCP") {
    return SocketReaderModule::SocketType::TCP;
  } else if (socket_type == "UDP") {
    return SocketReaderModule::SocketType::UDP;
  }
  return SocketReaderModule::SocketType::INVALID;
}

void
SocketReaderModule::do_configure(const CommandData_t&)
{
  for (std::size_t i = 0; i < m_readers.size(); ++i) {
    const auto reader_config = m_reader_configs[i];
    std::visit([this, reader_config](auto& reader) { reader.configure(m_io_context, reader_config); }, m_readers[i]);
  }
}

void
SocketReaderModule::do_start(const CommandData_t&)
{
  // Setup callbacks on all sourcemodels
  for (auto& [sourceid, source] : m_sources) {
    source->acquire_callback();
  }

  m_io_thread = std::jthread([this] { m_io_context.run(); });

  for (auto& reader : m_readers) {
    boost::asio::co_spawn(m_io_context,
                          std::visit([this](auto& reader) { return reader.start(m_sources); }, reader),
                          boost::asio::detached);
  }
}

void
SocketReaderModule::do_stop(const CommandData_t&)
{
  for (auto& reader : m_readers) {
    std::visit([](auto& reader) { reader.stop(); }, reader);
  }

  m_work_guard.reset();
}

void
SocketReaderModule::generate_opmon_data()
{
  for (const auto& reader_config : m_reader_configs) {
    opmon::SocketReaderStats stats;
    stats.set_packets_received(reader_config.socket_stats->packets_received.load());
    stats.set_bytes_received(reader_config.socket_stats->bytes_received.load());
    publish(std::move(stats), { { "socket-reader", std::to_string(reader_config.local_port) } });
  }
}

void
SocketReaderModule::TCPReader::configure(boost::asio::io_context& io_context, const ReaderConfig& reader_config)
{
  m_source_id = reader_config.source_id;
  m_socket_stats = reader_config.socket_stats;

  m_socket = std::make_unique<boost::asio::ip::tcp::socket>(io_context);

  boost::asio::ip::tcp::acceptor acceptor(
    io_context,
    boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(reader_config.local_ip),
                                   reader_config.local_port));

  TLOG() << "Waiting for TCP connection at " << reader_config.local_ip << ":" << reader_config.local_port;

  acceptor.accept(*m_socket);

  TLOG() << "Established TCP connection from " << m_socket->remote_endpoint().address() << ":"
         << m_socket->remote_endpoint().port();
}

boost::asio::awaitable<void>
SocketReaderModule::TCPReader::start(const sid_to_source_map_t& sources)
{
  // FIXME (DTE): Just pass the relevant source instead of all sources
  const auto src_it = sources.find(m_source_id);
  if (src_it == sources.end()) {
    TLOG() << "Unexpected source ID! (" << m_source_id << ")";
    co_return;
  }

  const auto buffer_size = src_it->second->get_target_payload_size();
  std::vector<char> buffer(buffer_size);

  while (m_socket->is_open()) {
    const auto bytes_received =
      co_await boost::asio::async_read(*m_socket,
                                       boost::asio::buffer(buffer),
                                       boost::asio::use_awaitable);
    ++m_socket_stats->packets_received;
    m_socket_stats->bytes_received.fetch_add(bytes_received);
    src_it->second->handle_payload(buffer.data(), bytes_received);
  }
}

void
SocketReaderModule::TCPReader::stop()
{
  m_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both);
  m_socket->close();
  TLOG() << "Shutdown TCP connection";
}

void
SocketReaderModule::UDPReader::configure(boost::asio::io_context& io_context, const ReaderConfig& reader_config)
{
  m_source_id = reader_config.source_id;
  m_socket_stats = reader_config.socket_stats;

  const auto receiver_endpoint = boost::asio::ip::udp::endpoint(
    boost::asio::ip::address::from_string(reader_config.local_ip), reader_config.local_port);
  m_socket = std::make_unique<boost::asio::ip::udp::socket>(io_context, receiver_endpoint);

  TLOG() << "Created UDP socket on " << reader_config.local_ip << ":" << reader_config.local_port;
}

boost::asio::awaitable<void>
SocketReaderModule::UDPReader::start(const sid_to_source_map_t& sources)
{
  const auto src_it = sources.find(m_source_id);
  if (src_it == sources.end()) {
    TLOG() << "Unexpected source ID! (" << m_source_id << ")";
    co_return;
  }

  const auto buffer_size = src_it->second->get_target_payload_size();
  std::vector<char> buffer(buffer_size);
  boost::asio::ip::udp::endpoint sender_endpoint;

  while (m_socket->is_open()) {
    std::size_t bytes_received = co_await m_socket->async_receive_from(
      boost::asio::buffer(buffer), sender_endpoint, boost::asio::use_awaitable);

    ++m_socket_stats->packets_received;
    m_socket_stats->bytes_received.fetch_add(bytes_received);

    if (bytes_received == buffer_size) [[likely]] {
      src_it->second->handle_payload(buffer.data(), bytes_received);
    } else {
      TLOG() << "Payload is smaller than " << buffer_size << " (" << bytes_received << ")";
    }
  }
}

void
SocketReaderModule::UDPReader::stop()
{
  m_socket->close();
  TLOG() << "Closed UDP socket";
}

} // namespace dunedaq::asiolibs

DEFINE_DUNE_DAQ_MODULE(dunedaq::asiolibs::SocketReaderModule)
