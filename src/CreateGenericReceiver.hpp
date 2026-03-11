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

DUNE_DAQ_TYPESTRING(dunedaq::fddetdataformats::CRTBernFrame, "CRTBernFrame")
DUNE_DAQ_TYPESTRING(dunedaq::fddetdataformats::CRTGrenobleFrame, "CRTGrenobleFrame")

namespace dunedaq::asiolibs {
 
std::shared_ptr<GenericReceiverConcept>
createGenericReceiver(const std::string& raw_data_receiver_connection_name)
{
  const auto datatypes = dunedaq::iomanager::IOManager::get()->get_datatypes(raw_data_receiver_connection_name);
  if (datatypes.size() != 1) {
    ers::error(datahandlinglibs::GenericConfigurationError(ERS_HERE,
      "Multiple input data types specified! Expected only a single type!"));
  }
  const std::string raw_dt = *datatypes.begin();
  TLOG() << "Choosing specializations for GenericReceiverConcept for input connection "
         << " [uid:" << raw_data_receiver_connection_name << " , data_type:" << raw_dt << ']';

  if (raw_dt.find("CRTBernFrame") != std::string::npos) {
    return std::make_shared<GenericReceiverModel<fddetdataformats::CRTBernFrame>>(raw_data_receiver_connection_name);
  } else if (raw_dt.find("CRTGrenobleFrame") != std::string::npos) {
    return std::make_shared<GenericReceiverModel<fddetdataformats::CRTGrenobleFrame>>(raw_data_receiver_connection_name);
  }

  return nullptr;
}

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_SRC_CREATEGENERICRECEIVER_HPP_
