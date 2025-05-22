/**
 * @file CreateFrameBuilder.hpp Specific FrameBuilder creator.
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_SRC_CREATEFRAMEBUILDER_HPP_
#define ASIOLIBS_SRC_CREATEFRAMEBUILDER_HPP_

#include "FrameBuilder.hpp"
#include "FakeWIBEthFrameBuilder.hpp"
#include "CRTBernFrameBuilder.hpp"
#include "CRTGrenobleFrameBuilder.hpp"

#include "datahandlinglibs/DataHandlingIssues.hpp"

#include <memory>

namespace dunedaq::asiolibs {

// FIXME (DTE): Add Doxygen
std::shared_ptr<FrameBuilder>
createFrameBuilder(const std::string& conn_uid)
{
  const auto datatypes = dunedaq::iomanager::IOManager::get()->get_datatypes(conn_uid);
  if (datatypes.size() != 1) {
    ers::error(dunedaq::datahandlinglibs::GenericConfigurationError(ERS_HERE,
      "Multiple output data types specified! Expected only a single type!"));
  }
  const std::string& raw_dt = *datatypes.begin();
  TLOG() << "Choosing specializations for FrameBuilder for output connection "
         << " [uid:" << conn_uid << " , data_type:" << raw_dt << ']';

  if (raw_dt.find("WIBEthFrame") != std::string::npos) {
    return std::make_shared<FakeWIBEthFrameBuilder>();
  } else if (raw_dt.find("CRTBernFrame") != std::string::npos) {
    return std::make_shared<CRTBernFrameBuilder>();
  } else if (raw_dt.find("CRTGrenobleFrame") != std::string::npos) {
    return std::make_shared<CRTGrenobleFrameBuilder>();
  }

  return nullptr;
}

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_SRC_CREATEFRAMEBUILDER_HPP_
