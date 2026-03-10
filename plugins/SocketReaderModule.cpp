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
#include "appmodel/SocketDetectorToDaqConnection.hpp"
#include "appmodel/SocketDataSender.hpp"
#include "appmodel/NWDetDataReceiver.hpp"
#include "confmodel/DetectorStream.hpp"
#include "confmodel/QueueWithSourceId.hpp"
#include "confmodel/GeoId.hpp"
#include "confmodel/NetworkInterface.hpp"

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
  auto* mdal = mcfg->get_dal<appmodel::DataReaderModule>(get_name());

  if (mdal->get_raw_data_callbacks().empty()) {
    auto err = datahandlinglibs::InitializationError(ERS_HERE, "No outputs defined for socket reader in configuration.");
    ers::fatal(err);
    throw err;
  }

  // Loop over output queues, extract source ids and create source model objects
  for (auto con : mdal->get_raw_data_callbacks()) {
    // TODO: add nullpointer check against misconfiguration
    auto ptr = m_sources[con->get_source_id()] = createSourceModel(con);
    register_node(con->UID(), ptr);
  }

  auto* d2d_conn = mdal->get_connections()[0]; // there's only 1 connection
  auto* socket_d2d_conn = d2d_conn->cast<appmodel::SocketDetectorToDaqConnection>();
  if (socket_d2d_conn == nullptr) {
    auto err = datahandlinglibs::InitializationError(ERS_HERE, "Connection is not of type SocketDetectorToDaqConnection.");
    ers::fatal(err);
    throw err;
  }  

  auto nw_receiver = socket_d2d_conn->get_net_receiver();
  auto local_ip = nw_receiver->get_uses()->get_ip_address()[0];

  for (auto nw_sender : socket_d2d_conn->get_net_senders()) {
    if (nw_sender->is_disabled(*(mcfg->get_session()))) {
      continue;
    }

    auto* socket_sender = nw_sender->cast<appmodel::SocketDataSender>();
    if (socket_sender == nullptr) {
      auto err = datahandlinglibs::InitializationError(ERS_HERE, "Sender is not of type SocketDataSender.");
      ers::fatal(err);
      throw err;
    }  

    auto local_port = socket_sender->get_remote_port();
    
    m_reader_infos.emplace_back(
      std::make_shared<ReaderInfo>(
        local_ip, local_port, std::make_shared<SocketStats>()));
    
    if (string_to_socket_type(socket_sender->get_socket_type()) == SocketType::TCP) {
      m_readers.push_back(std::make_shared<std::variant<TCPReader, UDPReader>>(TCPReader{}));
    } else {
      m_readers.push_back(std::make_shared<std::variant<TCPReader, UDPReader>>(UDPReader{}));
    } 
    
    auto remote_ip = socket_sender->get_uses()->get_ip_address()[0];
    auto remote_port = socket_sender->get_local_port();

    // Loop over streams
    for (auto det_stream : socket_sender->get_streams()) {
      if (det_stream->is_disabled(*(mcfg->get_session()))) {
        continue;
      }

      sender_stream_pair_t sender_stream_pair = { { remote_ip, remote_port}, det_stream->get_geo_id()->get_stream_id() };
      m_sender_to_source[sender_stream_pair] = m_sources[det_stream->get_source_id()];
    }        
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
    auto reader_info = m_reader_infos[i];
    std::visit([this, reader_info](auto& reader) { reader.configure(m_io_context, reader_info); }, *m_readers[i]);
  }
}

void
SocketReaderModule::do_start(const CommandData_t&)
{
  for (const auto& reader_info : m_reader_infos) {
    // Reset opmon variables
    reader_info->socket_stats->packets_received = 0;
    reader_info->socket_stats->bytes_received = 0;
    reader_info->socket_stats->stats_packet_count = 0;
  }

  m_t0 = std::chrono::steady_clock::now();

  // Setup callbacks on all sourcemodels
  for (auto& [_, source] : m_sources) {
    source->acquire_callback();
  }

  m_io_thread = std::jthread([this] { m_io_context.run(); });

  for (auto& reader : m_readers) {
    boost::asio::co_spawn(m_io_context,
                          std::visit([this](auto& reader) { return reader.start(m_sender_to_source); }, *reader),
                          boost::asio::detached);
  }
}

void
SocketReaderModule::do_stop(const CommandData_t&)
{
  for (auto& reader : m_readers) {
    std::visit([](auto& reader) { reader.stop(); }, *reader);
  }

  m_work_guard.reset();
}

void
SocketReaderModule::generate_opmon_data()
{
  for (const auto& reader_info : m_reader_infos) {
    opmon::SocketReaderStats stats;
    stats.set_packets_received(reader_info->socket_stats->packets_received.load());
    stats.set_bytes_received(reader_info->socket_stats->bytes_received.load());

    auto now = std::chrono::steady_clock::now();
    int new_packets = reader_info->socket_stats->stats_packet_count.exchange(0);
    double seconds = std::chrono::duration_cast<std::chrono::microseconds>(now - m_t0).count() / 1000000.;
    m_t0 = now;

    publish(std::move(stats), { { "socket-reader", reader_info->local_ip + ":" + std::to_string(reader_info->local_port) } });
  }
}

void
SocketReaderModule::TCPReader::configure(boost::asio::io_context& io_context, std::shared_ptr<ReaderInfo> reader_info)
{
  m_socket_stats = reader_info->socket_stats;

  m_socket = std::make_unique<boost::asio::ip::tcp::socket>(io_context);

  boost::asio::ip::address local_address;

  try {
    local_address = boost::asio::ip::make_address(reader_info->local_ip);
  } catch (const boost::system::system_error& e) {
    TLOG() << "Failed to configure TCP socket: " << e.what()
      << " (Local IP: " << reader_info->local_ip << ")";
    throw;
  }

  boost::asio::ip::tcp::acceptor acceptor(
    io_context, boost::asio::ip::tcp::endpoint(local_address, reader_info->local_port));

  TLOG() << "Waiting for TCP connection at " << reader_info->local_ip << ":" << reader_info->local_port;

  acceptor.accept(*m_socket);

  m_remote = { m_socket->remote_endpoint().address().to_string(), m_socket->remote_endpoint().port() } ;

  TLOG() << "Established TCP connection from "
    << m_socket->local_endpoint().address().to_string() << ":" << m_socket->local_endpoint().port()
    << " to "
    << m_remote.first << ":" << m_remote.second;
}

boost::asio::awaitable<void>
SocketReaderModule::TCPReader::start(const sender_source_map_t& sender_to_source)
{
  const auto buffer_size = sender_to_source.begin()->second->get_target_payload_size();
  std::vector<char> buffer(buffer_size);

  while (m_socket->is_open()) {
    const auto bytes_received =
      co_await boost::asio::async_read(*m_socket,
                                       boost::asio::buffer(buffer),
                                       boost::asio::use_awaitable);

    const auto* daq_header = reinterpret_cast<const dunedaq::detdataformats::DAQEthHeader*>(buffer.data());
    
    auto stream_id = (unsigned)daq_header->stream_id;
    sender_stream_pair_t sender_stream_pair = { m_remote, stream_id };
    
    auto src_it = sender_to_source.find(sender_stream_pair);    
    if (src_it == sender_to_source.end()) {
      TLOG() << "Unexpected sender-stream combination! (" << m_remote.first << ":" << m_remote.second << ", " << stream_id << ")";
      continue;
    }
    
    src_it->second->handle_payload(buffer.data(), bytes_received);

    ++m_socket_stats->packets_received;
    m_socket_stats->bytes_received.fetch_add(bytes_received);
    ++m_socket_stats->stats_packet_count;
  }
}

void
SocketReaderModule::TCPReader::stop()
{
  if (m_socket && m_socket->is_open()) {
    m_socket->close();
  }
  TLOG() << "Closed TCP connection";
}

void
SocketReaderModule::UDPReader::configure(boost::asio::io_context& io_context, std::shared_ptr<ReaderInfo> reader_info)
{
  m_socket_stats = reader_info->socket_stats;

  try {
    boost::asio::ip::udp::endpoint writer_endpoint(
      boost::asio::ip::address::from_string(reader_info->local_ip), reader_info->local_port);

    m_socket = std::make_unique<boost::asio::ip::udp::socket>(io_context, writer_endpoint);

    TLOG() << "Created UDP socket on "
      << m_socket->local_endpoint().address() << ":" << m_socket->local_endpoint().port();      

  } catch (const boost::system::system_error& e) {
    TLOG() << "Failed to configure UDP socket: " << e.what()
      << " (Local IP: " << reader_info->local_ip << ", Local port: " << reader_info->local_port << ")";
    throw;
  }  
}

boost::asio::awaitable<void>
SocketReaderModule::UDPReader::start(const sender_source_map_t& sender_to_source)
{
  const auto buffer_size = sender_to_source.begin()->second->get_target_payload_size();
  std::vector<char> buffer(buffer_size);
  boost::asio::ip::udp::endpoint sender_endpoint;

  while (m_socket->is_open()) {
    std::size_t bytes_received = co_await m_socket->async_receive_from(
      boost::asio::buffer(buffer), sender_endpoint, boost::asio::use_awaitable);

    const auto* daq_header = reinterpret_cast<const dunedaq::detdataformats::DAQEthHeader*>(buffer.data());

    auto stream_id = (unsigned)daq_header->stream_id;

    sender_t remote = { sender_endpoint.address().to_string(), sender_endpoint.port() } ;
    sender_stream_pair_t sender_stream_pair = { remote, stream_id };
    
    auto src_it = sender_to_source.find(sender_stream_pair);
    
    if (src_it == sender_to_source.end()) {
      TLOG() << "Unexpected sender-stream combination! (" << remote.first << ":" << remote.second << ", " << stream_id << ")";
      continue;
    }
    
    if (bytes_received == buffer_size) [[likely]] {
      src_it->second->handle_payload(buffer.data(), bytes_received);
    } else {
      TLOG() << "Payload is smaller than " << buffer_size << " (" << bytes_received << ")";
    }

    ++m_socket_stats->packets_received;
    m_socket_stats->bytes_received.fetch_add(bytes_received);
    ++m_socket_stats->stats_packet_count;    
  }
}

void
SocketReaderModule::UDPReader::stop()
{
  if (m_socket && m_socket->is_open()) {
    m_socket->close();
  }
  TLOG() << "Closed UDP socket";
}

} // namespace dunedaq::asiolibs

DEFINE_DUNE_DAQ_MODULE(dunedaq::asiolibs::SocketReaderModule)
