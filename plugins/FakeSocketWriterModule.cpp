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
#include "appmodel/NWDetDataSender.hpp"
#include "appmodel/SocketDataWriterModule.hpp"
#include "appmodel/SocketReceiver.hpp"
#include "appmodel/SocketWriterConf.hpp"
#include "confmodel/DetectorStream.hpp"
#include "confmodel/DetectorToDaqConnection.hpp"

#include "datahandlinglibs/DataHandlingIssues.hpp"

#include "asiolibs/opmon/FakeSocketWriterModule.pb.h"

#include <string>
#include <memory>
#include <vector>
#include <utility>

namespace dunedaq::asiolibs {

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
        const auto* socket_sender = nw_sender->cast<appmodel::FakeSocketDataSender>();
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

} // namespace dunedaq::asiolibs

DEFINE_DUNE_DAQ_MODULE(dunedaq::asiolibs::FakeSocketWriterModule)
