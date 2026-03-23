/**
 * @file GenericReceiverConcept.hpp Generic IOManager Receiver concept
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_PLUGINS_GENERICRECEIVERCONCEPT_HPP_
#define ASIOLIBS_PLUGINS_GENERICRECEIVERCONCEPT_HPP_

#include "iomanager/Receiver.hpp"

namespace dunedaq::asiolibs {

class GenericReceiverConcept
{
public: 
    struct TypeErasedPayload {
      /**
       * @brief Keeps the payload's memory alive
       */          
      std::shared_ptr<const void> owner;
      /**
       * @brief Pointer to payload bytes
       */     
      const void* data;
      /**
       * @brief Number of bytes
       */     
      std::size_t size;
    };

    virtual ~GenericReceiverConcept() = default;
    virtual std::optional<TypeErasedPayload> try_receive(dunedaq::iomanager::Receiver::timeout_t timeout) = 0;
};   

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_PLUGINS_GENERICRECEIVERCONCEPT_HPP_
