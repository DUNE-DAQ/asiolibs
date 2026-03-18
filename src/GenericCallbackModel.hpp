/**
 * @file GenericCallbackModel.hpp Generic callback model
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_SRC_GENERICCALLBACKMODEL_HPP_
#define ASIOLIBS_SRC_GENERICCALLBACKMODEL_HPP_

#include "GenericCallbackConcept.hpp"
#include "datahandlinglibs/DataMoveCallbackRegistry.hpp"

#include <utility>
#include <memory>

namespace dunedaq::asiolibs {

template<class TargetPayloadType>
class GenericCallbackModel : public GenericCallbackConcept
{
public:
  GenericCallbackModel(const appmodel::DataMoveCallbackConf* conf, GenericCallbackConcept::TypeErasedCallback&& erased_cb) {
    auto dmcbr = datahandlinglibs::DataMoveCallbackRegistry::get();

    auto cb = [erased_cb = std::move(erased_cb)](TargetPayloadType&& payload) {
      // Allocate the received payload on the heap with shared ownership,
      // so its lifetime can outlive this function and be tied to async sends.      
      auto owner = std::make_shared<TargetPayloadType>(std::move(payload));

      TypeErasedPayload erased_payload {
        owner,
        owner.get(),
        sizeof(TargetPayloadType)
      };        

      erased_cb(std::move(erased_payload));
    };

    dmcbr->register_callback<TargetPayloadType>(conf, std::move(cb));    
  }
  
};

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_SRC_GENERICCALLBACKMODEL_HPP_
