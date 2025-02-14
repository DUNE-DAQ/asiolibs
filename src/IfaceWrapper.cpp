/**
 * @file IfaceWrapper.cpp Socket-based Interface wrapper
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "IfaceWrapper.hpp"

namespace dunedaq {
namespace asiolibs {

//-----------------------------------------------------------------------------
void
IfaceWrapper::stop()
{
  //m_enable_flow.store(false);
  //m_lcore_quit_signal.store(true);
  // Stop GARP sender thread  
  //if (m_garp_thread.joinable()) {
  //  m_garp_thread.join();
  //} else {
  //  TLOG() << "GARP thrad is not joinable!";
  //}
}

} // namespace asiolibs
} // namespace dunedaq

// 
#include "detail/IfaceWrapper.hxx"
