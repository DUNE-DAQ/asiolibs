// TODO lazim mi bilmiyorum, belki base class olusturulabilir, duplicate

/**
 * @file IfaceWrapper.hpp IfaceWrapper for holding resources of 
 * a socket controlled NIC interface/port
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_SRC_IFACEWRAPPER_HPP_
#define ASIOLIBS_SRC_IFACEWRAPPER_HPP_

#include "opmonlib/MonitorableObject.hpp"

namespace dunedaq {
  
namespace asiolibs {

class IfaceWrapper : public opmonlib::MonitorableObject
{
public:

  void stop();

  void enable_flow() { m_enable_flow.store(true);}
  void disable_flow() { m_enable_flow.store(false);}

protected:

private:

  std::atomic<bool> m_enable_flow{ false };

};

} // namespace asiolibs
} // namespace dunedaq

#endif // ASIOLIBS_SRC_IFACEWRAPPER_HPP_
