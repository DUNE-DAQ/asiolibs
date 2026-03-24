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

#include "fddetdataformats/CRTBernFrame.hpp"
#include "fddetdataformats/CRTGrenobleFrame.hpp"

#include "datahandlinglibs/DataHandlingIssues.hpp"

#include <memory>
#include <string>

DUNE_DAQ_TYPESTRING(dunedaq::fddetdataformats::CRTBernFrame, "CRTBernFrame")
DUNE_DAQ_TYPESTRING(dunedaq::fddetdataformats::CRTGrenobleFrame, "CRTGrenobleFrame")

namespace dunedaq::asiolibs {
 
std::shared_ptr<GenericReceiverConcept>
createGenericReceiver(const std::string& receiver_connection_name)
{
  const auto datatypes = dunedaq::iomanager::IOManager::get()->get_datatypes(receiver_connection_name);
  if (datatypes.size() != 1) {
    ers::error(datahandlinglibs::GenericConfigurationError(ERS_HERE,
      "Multiple input data types specified! Expected only a single type!"));
  }
  const std::string dt = *datatypes.begin();
  TLOG() << "Choosing specializations for GenericReceiverConcept for input connection "
         << " [uid:" << receiver_connection_name << " , data_type:" << dt << ']';

  if (dt.find("CRTBernFrame") != std::string::npos) {
    return std::make_shared<GenericReceiverModel<fddetdataformats::CRTBernFrame>>(receiver_connection_name);
  } else if (dt.find("CRTGrenobleFrame") != std::string::npos) {
    return std::make_shared<GenericReceiverModel<fddetdataformats::CRTGrenobleFrame>>(receiver_connection_name);
  }

  return nullptr;
}

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_SRC_CREATEGENERICRECEIVER_HPP_
