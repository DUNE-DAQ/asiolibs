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
    virtual ~GenericReceiverConcept() = default;
    virtual std::optional<std::pair<const void*, std::size_t>> try_receive(dunedaq::iomanager::Receiver::timeout_t timeout) = 0;
};   

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_PLUGINS_GENERICRECEIVERCONCEPT_HPP_
