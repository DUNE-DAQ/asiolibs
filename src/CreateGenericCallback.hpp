/**
 * @file CreateGenericCallback.hpp Specific GenericCallbackConcept creator.
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_SRC_CREATEGENERICCALLBACK_HPP_
#define ASIOLIBS_SRC_CREATEGENERICCALLBACK_HPP_

#include "GenericCallbackConcept.hpp"
#include "GenericCallbackModel.hpp"

#include "fddetdataformats/CRTBernFrame.hpp"
#include "fddetdataformats/CRTGrenobleFrame.hpp"

#include <memory>

DUNE_DAQ_TYPESTRING(dunedaq::fddetdataformats::CRTBernFrame, "CRTBernFrame")
DUNE_DAQ_TYPESTRING(dunedaq::fddetdataformats::CRTGrenobleFrame, "CRTGrenobleFrame")

namespace dunedaq::asiolibs {
 
std::shared_ptr<GenericCallbackConcept>
createGenericCallback(const appmodel::DataMoveCallbackConf* conf, GenericCallbackConcept::TypeErasedCallback&& cb)
{
  auto datatype = conf->get_data_type();
  TLOG() << "Choosing specializations for GenericCallbackModel for input connection "
         << " [uid:" << conf->UID() << " , data_type:" << datatype << ']';

  if (datatype.find("CRTBernFrame") != std::string::npos) {
    return std::make_shared<GenericCallbackModel<fddetdataformats::CRTBernFrame>>(conf, std::move(cb));
  } else if (datatype.find("CRTGrenobleFrame") != std::string::npos) {
    return std::make_shared<GenericCallbackModel<fddetdataformats::CRTGrenobleFrame>>(conf, std::move(cb));
  }

  return nullptr;
}

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_SRC_CREATEGENERICCALLBACK_HPP_
