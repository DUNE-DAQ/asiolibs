/**
 * @file SocketReaderModule.cpp Socket-based data receiver for low bandwidth electronics
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "SocketReaderModule.hpp"

#include "confmodel/DetectorToDaqConnection.hpp"

#include "appmodel/DataReaderModule.hpp"
#include "appmodel/NWDetDataSender.hpp"
#include "appmodel/SocketReaderConf.hpp"
#include "appmodel/SocketReceiver.hpp"
#include "confmodel/QueueWithSourceId.hpp"

#include "datahandlinglibs/DataHandlingIssues.hpp"
#include "datahandlinglibs/utils/BufferCopy.hpp" 

#include "CreateSource.hpp"
#include "IfaceWrapper.hpp"

namespace dunedaq::asiolibs {

SocketReaderModule::SocketReaderModule(const std::string& name)
  : DAQModule(name),
    m_io_context(),
    m_work_guard(boost::asio::make_work_guard(m_io_context)),
    m_socket_type(SocketType::INVALID)
  {     
  register_command("conf", &SocketReaderModule::do_configure);
  register_command("start", &SocketReaderModule::do_start);
  register_command("stop_trigger_sources", &SocketReaderModule::do_stop);
  register_command("scrap", &SocketReaderModule::do_scrap);
}

// TODO fix duplication
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

SocketReaderModule::SocketType
SocketReaderModule::string_to_socket_type(const std::string& socket_type) {
    if (socket_type == "TCP") {
        return SocketReaderModule::SocketType::TCP;
    } else if (socket_type == "UDP") {
        return SocketReaderModule::SocketType::UDP;
    }
    return SocketReaderModule::SocketType::INVALID;
}

// TODO fix duplication?
void
SocketReaderModule::init(const std::shared_ptr<appfwk::ModuleConfiguration> mcfg)
{
  m_cfg = mcfg;
  auto mdal = m_cfg->module<appmodel::DataReaderModule>(get_name());
  auto module_conf = mdal->get_configuration()->cast<appmodel::SocketReaderConf>(); 

  std::vector<const confmodel::DetectorToDaqConnection*> d2d_conns;
  auto res_set = mdal->get_connections();

  for (auto res : res_set) {
    auto connection = res->cast<confmodel::DetectorToDaqConnection>();
    if (connection == nullptr) {
      dunedaq::datahandlinglibs::GenericConfigurationError err(
          ERS_HERE, "DetectorToDaqConnection configuration failed due expected but unavailable connection!"
        );
      ers::fatal(err);
      throw err;      
    }
    if (connection->disabled(*(m_cfg->configuration_manager()->session()))) {
	    continue;
    }

    d2d_conns.push_back(connection);
  }

  std::vector<const appmodel::NWDetDataSender*> nw_senders;

  for (auto d2d_conn : d2d_conns) {
    auto dpdk_receiver = d2d_conn->get_receiver()->cast<appmodel::SocketReceiver>();
    auto senders = d2d_conn->get_senders();
    for ( auto sender : d2d_conn->get_senders() ) {
      auto nw_sender = sender->cast<appmodel::NWDetDataSender>();
      if ( !nw_sender ) {
        throw dunedaq::datahandlinglibs::InitializationError(
          ERS_HERE, fmt::format("Found {} of type {} in connection {} while expecting type NWDetDataSender", dpdk_receiver->class_name(), dpdk_receiver->UID(), d2d_conn->UID())
        );
      }

      if ( nw_sender->disabled(*(m_cfg->configuration_manager()->session())) ) {
        continue;
      }
      nw_senders.push_back(nw_sender);
    }
  }  

  auto port = module_conf->get_port();

  for(const auto& nw_sender : nw_senders) {
    for (const auto& det_stream : nw_sender->get_contains() ) {
      m_ports.push_back(port++);
    }
  }

  m_socket_type = string_to_socket_type(module_conf->get_socket_type());
  m_address = module_conf->get_ip_address();

  for(const auto port : m_ports) {
    switch(m_socket_type) { // bu degisecek, liste olacak cunku
      case SocketType::TCP: {
        if(!m_address) {
          throw std::invalid_argument("Error: TCP requires an IP address!");
        }
        m_receivers.emplace_back(TCPReceiver());
        break;
      }
      case SocketType::UDP: {
        m_receivers.emplace_back(UDPReceiver());
        break;
      }
      default: {
        throw std::invalid_argument("Error: Only TCP and UDP are allowed!");
        break; 
      }
    }    
  }

  if (mdal->get_outputs().empty()) {
    auto err = dunedaq::datahandlinglibs::InitializationError(ERS_HERE, "No outputs defined for socket receiver in configuration.");
    ers::fatal(err);
    throw err;
  }

  for (auto con : mdal->get_outputs()) {
    auto queue = con->cast<confmodel::QueueWithSourceId>();
    if(queue == nullptr) {
      auto err = dunedaq::datahandlinglibs::InitializationError(ERS_HERE, "Outputs are not of type QueueWithGeoId.");
      ers::fatal(err);
      throw err;
    }

    // Check for CB prefix indicating Callback use
    const char delim = '_';
    std::string target = queue->UID();
    std::vector<std::string> words;
    tokenize(target, delim, words);
    int sourceid = -1;

    bool callback_mode = false;
    if (words.front() == "cb") {
      callback_mode = true;
    }

    auto ptr = m_sources[queue->get_source_id()] = createSourceModel(queue->UID(), callback_mode);
    register_node( queue->UID(), ptr );
    //m_sources[queue->get_source_id()]->init(); 
  }
}

void
SocketReaderModule::do_configure(const data_t&)
{
  TLOG(6) << "deniztest configure";
  for(int i = 0; i < m_receivers.size(); ++i) {
    const auto port = m_ports[i];
    std::visit([this, port](auto& receiver) { receiver.configure(m_io_context, port, m_address); }, m_receivers[i]);
  }
}

void
SocketReaderModule::do_start(const data_t&)
{
  TLOG(6) << "deniztest start";

  m_io_thread = std::jthread([this] { m_io_context.run(); });

  for(auto& receiver : m_receivers) {
    boost::asio::co_spawn(m_io_context, std::visit([](auto& receiver) { return receiver.start(); }, receiver), boost::asio::detached);
  }
}

void
SocketReaderModule::do_stop(const data_t&)
{
  TLOG(6) << "deniztest stop";
  for(auto& receiver : m_receivers) {
    std::visit([](auto& receiver) { receiver.stop(); }, receiver);

  }

  m_work_guard.reset();
  //for (auto& [iface_id, iface] : m_ifaces) {
  //  iface->disable_flow();
  //}
}


void
SocketReaderModule::do_scrap(const data_t&)
{
  TLOG() << get_name() << ": Entering do_scrap() method";
  if (m_run_marker.load()) {
    TLOG() << "Raising stop through variables!";
    set_running(false);
    //TLOG() << "Stopping iface wrappers.";
    //for (auto& [iface_id, iface] : m_ifaces) {
    //  iface->stop();
    //}
    //ealutils::wait_for_lcores();
    //TLOG() << "Stoppped DPDK lcore processors and internal threads...";
  } else {
    //TLOG(5) << "DPDK lcore processor is already stopped!";
  }
}

void 
SocketReaderModule::set_running(bool should_run)
{
  bool was_running = m_run_marker.exchange(should_run);
  TLOG(5) << "Active state was toggled from " << was_running << " to " << should_run;
}

} // namespace dunedaq::asiolibs

DEFINE_DUNE_DAQ_MODULE(dunedaq::asiolibs::SocketReaderModule)
