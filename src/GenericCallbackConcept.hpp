/**
 * @file GenericCallbackConcept.hpp Generic callback concept
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_PLUGINS_GENERICCALLBACKCONCEPT_HPP_
#define ASIOLIBS_PLUGINS_GENERICCALLBACKCONCEPT_HPP_

#include <memory>
#include <functional>

namespace dunedaq::asiolibs {

class GenericCallbackConcept
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

  using TypeErasedCallback = std::function<void(TypeErasedPayload&&)>;

  virtual ~GenericCallbackConcept() = default;
};   

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_PLUGINS_GENERICCALLBACKCONCEPT_HPP_
