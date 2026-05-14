/**
 * @file CreateGenericReceiver.hpp Specific GenericReceiverConcept creator.
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_SRC_CREATEGENERICRECEIVER_HPP_
#define ASIOLIBS_SRC_CREATEGENERICRECEIVER_HPP_

#include "GenericReceiverConcept.hpp"
#include "GenericReceiverModel.hpp"

#include "fdreadoutlibs/CRTGrenobleTypeAdapter.hpp"

#include "datahandlinglibs/DataHandlingIssues.hpp"

#include <memory>

DUNE_DAQ_TYPESTRING(dunedaq::fdreadoutlibs::types::CRTGrenobleTypeAdapter, "CRTGrenobleFrame")

namespace dunedaq::asiolibs {
 
std::shared_ptr<GenericReceiverConcept>
createGenericReceiver(const std::string& conn_uid, const std::string& raw_data_receiver_connection_name)
{
  const auto datatypes = dunedaq::iomanager::IOManager::get()->get_datatypes(conn_uid);
  if (datatypes.size() != 1) {
    ers::error(dunedaq::datahandlinglibs::GenericConfigurationError(ERS_HERE,
      "Multiple output data types specified! Expected only a single type!"));
  }
  const std::string raw_dt = *datatypes.begin();
  TLOG() << "Choosing specializations for GenericReceiverConcept for output connection "
         << " [uid:" << conn_uid << " , data_type:" << raw_dt << ']';

  if (raw_dt.find("CRTGrenobleFrame") != std::string::npos) {
    return std::make_shared<GenericReceiverModel<fdreadoutlibs::types::CRTGrenobleTypeAdapter>>(raw_data_receiver_connection_name);
  }

  return nullptr;
}

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_SRC_CREATEGENERICRECEIVER_HPP_
