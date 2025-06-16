/**
 * @file SocketWriterModule.cpp Boost.Asio-based socket writer plugin for low-bandwidth devices
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "SocketWriterModule.hpp"

#include "CreateGenericReceiver.hpp"

#include "appfwk/ConfigurationManager.hpp"
#include "appmodel/SocketDataSender.hpp"
#include "appmodel/NWDetDataSender.hpp"
#include "appmodel/SocketReceiver.hpp"
#include "appmodel/SocketWriterConf.hpp"
#include "confmodel/DetectorStream.hpp"
#include "confmodel/DetectorToDaqConnection.hpp"
#include "confmodel/QueueWithSourceId.hpp"

#include "datahandlinglibs/DataHandlingIssues.hpp"
#include "datahandlinglibs/DataMoveCallbackRegistry.hpp"

#include "asiolibs/opmon/SocketWriterModule.pb.h"

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
SocketWriterModule::get_dal_inputs(const dunedaq::appmodel::SocketDataWriterModule* mdal)
{
  if (mdal->get_inputs().empty()) {
    auto err = dunedaq::datahandlinglibs::InitializationError(ERS_HERE,
                                                              "No inputs defined for socket writer in configuration.");
    ers::fatal(err);
    throw err;
  }

  for (auto* input : mdal->get_inputs()) {
    if (input->get_data_type() != "DataRequest") {
      m_raw_data_receiver_connection_name = input->UID();
      // Parse for prefix
      std::string conn_name = input->UID(); 
      const char delim = '_';
      std::vector<std::string> words;
      std::size_t start;
      std::size_t end = 0;
      while ((start = conn_name.find_first_not_of(delim, end)) != std::string::npos) {
        end = conn_name.find(delim, start);
        words.push_back(conn_name.substr(start, end - start));
      }

      TLOG_DEBUG() << "Initialize connection based on uid: " 
        << m_raw_data_receiver_connection_name << " front word: " << words.front();

      std::string cb_prefix("cb");
      if (words.front() == cb_prefix) {
        m_callback_mode = true;
      }

      if (!m_callback_mode) {
        m_raw_receiver_timeout_ms = std::chrono::milliseconds(input->get_recv_timeout_ms());
      }

      auto* queue = input->cast<confmodel::QueueWithSourceId>();
      if (queue == nullptr) {
        auto err = dunedaq::datahandlinglibs::InitializationError(ERS_HERE, "Inputs are not of type QueueWithGeoId.");
        ers::fatal(err);
        throw err;
      }  

      m_raw_data_receiver = createGenericReceiver(queue->UID(), m_raw_data_receiver_connection_name); // FIXME (DTE): Overwriting doesn't make sense      
    }
  }    
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

  get_dal_inputs(mdal);

  // Raw input connection sensibility check
  if (!m_callback_mode && m_raw_data_receiver == nullptr) {
    TLOG() << "Non callback mode, and receiver is unset!";
    //ers::error(ConfigurationError(ERS_HERE, m_sourceid, "Non callback mode, and receiver is unset!"));
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
SocketWriterModule::run_consume()
{
  TLOG() << "Consumer thread started..."; // TODO (DTE): Make debug logs

  while (m_run_marker.load()) {
    // Try to acquire data

    const auto opt_payload = m_raw_data_receiver->try_receive(m_raw_receiver_timeout_ms);

    if (opt_payload) {
      auto& payload = opt_payload.value();
      consume_payload(payload);    
    } else {
      for (const auto& writer_config : m_writer_configs) {
        ++writer_config.socket_stats->rawq_timeout_count;
      }
      // Protection against a zero sleep becoming a yield
      if (m_raw_receiver_sleep_us != std::chrono::microseconds::zero())
        std::this_thread::sleep_for(m_raw_receiver_sleep_us);
    }
  }

  TLOG() << "Consumer thread joins... ";
}

void
SocketWriterModule::consume_payload(const std::pair<const void*, std::size_t>& payload)
{
    for (std::size_t i = 0; i < m_writers.size(); ++i) {
      boost::asio::co_spawn(
        m_io_context, std::visit([this, &payload](auto& writer) { return writer.start(payload); }, m_writers[i]), boost::asio::detached);
    }  
}

void
SocketWriterModule::do_configure(const data_t&)
{
  // Register callbacks if operating in that mode.
  if (m_callback_mode) {
    // Configure and register consume callback
    m_consume_callback = std::bind(&SocketWriterModule::consume_payload, this, std::placeholders::_1);
 
    // Register callback
    auto dmcbr = datahandlinglibs::DataMoveCallbackRegistry::get();
    dmcbr->register_callback<std::pair<const void*, std::size_t>>(m_raw_data_receiver_connection_name, m_consume_callback);
  }

  for (std::size_t i = 0; i < m_writers.size(); ++i) {
    const auto& writer_config = m_writer_configs[i];
    std::visit([this, &writer_config](auto& writer) { writer.configure(m_io_context, writer_config); }, m_writers[i]);
  }
}

void
SocketWriterModule::do_start(const data_t&)
{
  for (const auto& writer_config : m_writer_configs) {
    // Reset opmon variables
    writer_config.socket_stats->sum_payloads = 0;
    writer_config.socket_stats->num_payloads = 0;
    writer_config.socket_stats->sum_bytes = 0;
    writer_config.socket_stats->rawq_timeout_count = 0;
    writer_config.socket_stats->stats_packet_count = 0;
  }

  m_t0 = std::chrono::high_resolution_clock::now();

  if (!m_callback_mode) {
    m_run_marker.store(true);
    m_consumer_thread.set_work(&SocketWriterModule::run_consume, this);
  }

  m_io_thread = std::jthread([this] { m_io_context.run(); });
}

void
SocketWriterModule::do_stop(const data_t&)
{
  if (!m_callback_mode) {
    m_run_marker.store(false);
    while (!m_consumer_thread.get_readiness()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

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
    stats.set_sum_payloads(writer_config.socket_stats->sum_payloads.load());
    stats.set_num_payloads(writer_config.socket_stats->num_payloads.exchange(0));
    stats.set_sum_bytes(writer_config.socket_stats->sum_bytes.load());
    stats.set_num_data_input_timeouts(writer_config.socket_stats->rawq_timeout_count.exchange(0));

    auto now = std::chrono::high_resolution_clock::now();
    int new_packets = writer_config.socket_stats->stats_packet_count.exchange(0);
    double seconds = std::chrono::duration_cast<std::chrono::microseconds>(now - m_t0).count() / 1000000.;
    m_t0 = now;

    stats.set_rate_payloads_consumed(new_packets / seconds / 1000.);

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
SocketWriterModule::TCPWriter::start(const std::pair<const void*, std::size_t>& payload) // TODO (DTE): Rename
{
  const auto bytes_sent =
    co_await m_socket->async_send(boost::asio::buffer(payload.first, payload.second), boost::asio::use_awaitable);
  ++m_socket_stats->num_payloads;
  ++m_socket_stats->sum_payloads;
  m_socket_stats->sum_bytes.fetch_add(bytes_sent);
  ++m_socket_stats->stats_packet_count;
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
SocketWriterModule::UDPWriter::start(const std::pair<const void*, std::size_t>& payload)
{
  boost::asio::ip::udp::endpoint receiver_endpoint(boost::asio::ip::address::from_string(m_writer_config.remote_ip),
                                                   m_writer_config.remote_port);
  const auto bytes_sent = co_await m_socket->async_send_to(
    boost::asio::buffer(payload.first, payload.second), receiver_endpoint, boost::asio::use_awaitable);
  ++m_writer_config.socket_stats->num_payloads;
  ++m_writer_config.socket_stats->sum_payloads;
  m_writer_config.socket_stats->sum_bytes.fetch_add(bytes_sent);
  ++m_writer_config.socket_stats->stats_packet_count;  
}

void
SocketWriterModule::UDPWriter::stop()
{
  m_socket->close();
  TLOG() << "Closed UDP socket";
}

} // namespace dunedaq::asiolibs

DEFINE_DUNE_DAQ_MODULE(dunedaq::asiolibs::SocketWriterModule)
