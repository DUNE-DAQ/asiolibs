/**
 * @file SocketWriterModule.cpp Boost.Asio-based socket writer plugin for low-bandwidth devices
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "SocketWriterModule.hpp"

#include "CreateGenericCallback.hpp"

#include "appfwk/ConfigurationManager.hpp"
#include "appmodel/SocketDataSender.hpp"
#include "appmodel/NWDetDataReceiver.hpp"
#include "appmodel/SocketDetectorToDaqConnection.hpp"
#include "appmodel/SocketDataWriterModule.hpp"
#include "confmodel/DetectorStream.hpp"
#include "confmodel/QueueWithSourceId.hpp"
#include "confmodel/NetworkInterface.hpp"

#include "datahandlinglibs/DataHandlingIssues.hpp"

#include "asiolibs/opmon/SocketWriterModule.pb.h"

#include "asiolibs/AsioIssues.hpp"

#include <string>
#include <memory>
#include <vector>
#include <utility>

namespace dunedaq::asiolibs {

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
  auto* mdal = mcfg->get_dal<appmodel::SocketDataWriterModule>(get_name());

  if (mdal->get_raw_data_callbacks().empty()) {
    auto err = datahandlinglibs::InitializationError(ERS_HERE, "No inputs defined for socket writer in configuration.");
    ers::fatal(err);
    throw err;
  }

  for (auto con : mdal->get_raw_data_callbacks()) {
    m_raw_data_callback_confs.push_back(con);
  }  

  auto* d2d_conn = mdal->get_connections()[0]; // there's only 1 connection
  auto* socket_d2d_conn = d2d_conn->cast<appmodel::SocketDetectorToDaqConnection>();
  if (socket_d2d_conn == nullptr) {
    auto err = datahandlinglibs::InitializationError(ERS_HERE, "Connection is not of type SocketDetectorToDaqConnection.");
    ers::fatal(err);
    throw err;
  } 

  auto nw_receiver = socket_d2d_conn->get_net_receiver();
  auto remote_ip = nw_receiver->get_uses()->get_ip_address()[0];

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

    auto local_ip = socket_sender->get_uses()->get_ip_address()[0];
    auto local_port = socket_sender->get_local_port();
    auto remote_port = socket_sender->get_remote_port();
    
    m_writer_infos.emplace_back(
      std::make_shared<WriterInfo>(
        local_ip, local_port, remote_ip, remote_port, std::make_shared<SocketStats>()));
      
    if (string_to_socket_type(socket_sender->get_socket_type()) == SocketType::TCP) {
      m_writers.push_back(std::make_shared<std::variant<TCPWriter, UDPWriter>>(TCPWriter{}));
    } else {
      m_writers.push_back(std::make_shared<std::variant<TCPWriter, UDPWriter>>(UDPWriter{}));      
    }     

    // Loop over streams
    for (auto det_stream : socket_sender->get_streams()) {
      if (det_stream->is_disabled(*(mcfg->get_session()))) {
        continue;
      }
      auto sid = det_stream->get_source_id();
      m_sid_to_writer[sid] = m_writers.back();
    }      
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
SocketWriterModule::do_configure(const CommandData_t&)
{
  for (auto conf : m_raw_data_callback_confs) {
    auto sid = conf->get_source_id();

    auto cb = createGenericCallback(conf, 
      [this, sid](GenericCallbackConcept::TypeErasedPayload&& payload) {
        auto it = m_sid_to_writer.find(sid);
        if (it != m_sid_to_writer.end()) {
            std::visit([&](auto& writer) { writer.enqueue(std::move(payload)); }, *it->second);
        } else {
            TLOG() << "No socket writer found for source_id " << sid;
        }
      });

    m_raw_data_callbacks.push_back(std::move(cb));
  }

  for (std::size_t i = 0; i < m_writers.size(); ++i) {
    auto writer_info = m_writer_infos[i];
    std::visit([this, writer_info](auto& writer) { writer.configure(m_io_context, writer_info); }, *m_writers[i]);    
  }
}

void
SocketWriterModule::do_start(const CommandData_t&)
{
  for (const auto& writer_info : m_writer_infos) {
    // Reset opmon variables
    writer_info->socket_stats->sum_payloads = 0;
    writer_info->socket_stats->num_payloads = 0;
    writer_info->socket_stats->sum_bytes = 0;
    writer_info->socket_stats->stats_packet_count = 0;
  }

  m_t0 = std::chrono::steady_clock::now();

  m_io_thread = std::jthread([this] { m_io_context.run(); });
}

void
SocketWriterModule::do_stop(const CommandData_t&)
{
  for (auto& writer : m_writers) {
    std::visit([](auto& writer) { writer.stop(); }, *writer);
  }

  m_work_guard.reset();
}

void
SocketWriterModule::generate_opmon_data()
{
  for (const auto& writer_info : m_writer_infos) {
    opmon::SocketWriterStats stats;
    stats.set_sum_payloads(writer_info->socket_stats->sum_payloads.load());
    stats.set_num_payloads(writer_info->socket_stats->num_payloads.exchange(0));
    stats.set_sum_bytes(writer_info->socket_stats->sum_bytes.load());

    auto now = std::chrono::steady_clock::now();
    int new_packets = writer_info->socket_stats->stats_packet_count.exchange(0);
    double seconds = std::chrono::duration_cast<std::chrono::microseconds>(now - m_t0).count() / 1000000.;
    m_t0 = now;

    stats.set_rate_payloads_consumed(new_packets / seconds / 1000.);

    publish(std::move(stats), { { "socket-writer", writer_info->local_ip + ":" + std::to_string(writer_info->local_port) } });
  }
}

void
SocketWriterModule::TCPWriter::configure(boost::asio::io_context& io_context, std::shared_ptr<WriterInfo> writer_info)
{
  m_io_context = &io_context;
  m_strand = std::make_shared<boost::asio::strand<boost::asio::io_context::executor_type>>(io_context.get_executor());  

  m_socket_stats = writer_info->socket_stats;

  boost::asio::ip::address local_address;
  boost::asio::ip::address remote_address;

  try {
    local_address = boost::asio::ip::make_address(writer_info->local_ip);
    remote_address = boost::asio::ip::make_address(writer_info->remote_ip);
  } catch (const boost::system::system_error& e) {
    TLOG() << "Failed to configure TCP socket: " << e.what()
      << " (Local IP: " << writer_info->local_ip << ", Remote IP: " << writer_info->remote_ip  << ")";
    throw;
  }

  while (true) {
    try {
      m_socket = std::make_unique<boost::asio::ip::tcp::socket>(io_context);
      m_socket->open(boost::asio::ip::tcp::v4());      
      m_socket->bind(boost::asio::ip::tcp::endpoint(local_address, writer_info->local_port));
      m_socket->connect(boost::asio::ip::tcp::endpoint(remote_address, writer_info->remote_port));
      break;
    } catch (const boost::system::system_error& e) {
      TLOG() << "Failed to create TCP socket: " << e.what() << ". Retrying in 1 second..."
        << " (Local IP: " << writer_info->local_ip << ", Local port: " << writer_info->local_port
        << ", Remote IP: " << writer_info->remote_ip << ", Remote port: " << writer_info->remote_port << ")";      
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  TLOG() << "Established TCP connection from "
    << m_socket->local_endpoint().address().to_string() << ":" << m_socket->local_endpoint().port()
    << " to "
    << m_socket->remote_endpoint().address().to_string() << ":" << m_socket->remote_endpoint().port();  
}

void
SocketWriterModule::TCPWriter::enqueue(GenericCallbackConcept::TypeErasedPayload&& payload)
{
  boost::asio::post(*m_strand, [this, payload = std::move(payload)]() mutable {
    m_payloads.push(std::move(payload));
    if (m_payloads.size() == 1) { // queue was empty
      boost::asio::co_spawn(*m_strand, start(), boost::asio::detached);
    }
  });  
}

boost::asio::awaitable<void>
SocketWriterModule::TCPWriter::start()
{
  while (!m_payloads.empty()) {
    auto payload = std::move(m_payloads.front());
    m_payloads.pop();

    auto bytes_sent =
      co_await boost::asio::async_write(*m_socket, boost::asio::buffer(payload.data, payload.size), boost::asio::use_awaitable);

    ++m_socket_stats->num_payloads;
    ++m_socket_stats->sum_payloads;
    m_socket_stats->sum_bytes.fetch_add(bytes_sent);
    ++m_socket_stats->stats_packet_count;
  }
}

void
SocketWriterModule::TCPWriter::stop()
{
  if (m_socket && m_socket->is_open()) {
    m_socket->close();
  }
  TLOG() << "Closed TCP connection";
}

std::shared_ptr<SocketWriterModule::SocketStats>
SocketWriterModule::TCPWriter::get_socket_stats() const
{
  return m_socket_stats;  
}

void
SocketWriterModule::UDPWriter::configure(boost::asio::io_context& io_context, std::shared_ptr<WriterInfo> writer_info)
{
  m_io_context = &io_context;
  m_strand = std::make_shared<boost::asio::strand<boost::asio::io_context::executor_type>>(io_context.get_executor());  

  m_socket_stats = writer_info->socket_stats;

  try {
    boost::asio::ip::udp::endpoint writer_endpoint(
      boost::asio::ip::address::from_string(writer_info->local_ip), writer_info->local_port);

    m_socket = std::make_unique<boost::asio::ip::udp::socket>(io_context, writer_endpoint);     

    m_remote_endpoint = boost::asio::ip::udp::endpoint(
      boost::asio::ip::address::from_string(writer_info->remote_ip), writer_info->remote_port);

    TLOG() << "Created UDP socket on "
      << m_socket->local_endpoint().address() << ":" << m_socket->local_endpoint().port(); 

  } catch (const boost::system::system_error& e) {
    TLOG() << "Failed to create UDP socket: " << e.what()
      << " (Local IP: " << writer_info->local_ip << ", Local port: " << writer_info->local_port
      << ", Remote IP: " << writer_info->remote_ip << ", Remote port: " << writer_info->remote_port << ")";
    throw;
  }          
}

void
SocketWriterModule::UDPWriter::enqueue(GenericCallbackConcept::TypeErasedPayload&& payload)
{
  boost::asio::post(*m_strand, [this, payload = std::move(payload)]() mutable {
    m_payloads.push(std::move(payload));
    if (m_payloads.size() == 1) { // queue was empty
      boost::asio::co_spawn(*m_strand, start(), boost::asio::detached);
    }
  });    
}

boost::asio::awaitable<void>
SocketWriterModule::UDPWriter::start()
{
  while (!m_payloads.empty()) {
    auto payload = std::move(m_payloads.front());
    m_payloads.pop();   

    const auto bytes_sent = co_await m_socket->async_send_to(
        boost::asio::buffer(payload.data, payload.size), m_remote_endpoint, boost::asio::use_awaitable);

    ++m_socket_stats->num_payloads;
    ++m_socket_stats->sum_payloads;
    m_socket_stats->sum_bytes.fetch_add(bytes_sent);
    ++m_socket_stats->stats_packet_count;  
  }
}

void
SocketWriterModule::UDPWriter::stop()
{
  if (m_socket && m_socket->is_open()) {
    m_socket->close();
  }
  TLOG() << "Closed UDP socket";
}

std::shared_ptr<SocketWriterModule::SocketStats>
SocketWriterModule::UDPWriter::get_socket_stats() const
{
  return m_socket_stats;  
}

} // namespace dunedaq::asiolibs

DEFINE_DUNE_DAQ_MODULE(dunedaq::asiolibs::SocketWriterModule)
