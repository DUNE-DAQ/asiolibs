/**
 * @file FIXME (DTE): Refactor
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_PLUGINS_CRTGRENOBLEFRAMEBUILDER_HPP_
#define ASIOLIBS_PLUGINS_CRTGRENOBLEFRAMEBUILDER_HPP_

#include "FrameBuilder.hpp"

#include "fddetdataformats/CRTGrenobleFrame.hpp"

namespace dunedaq::asiolibs {

class CRTGrenobleFrameBuilder : public FrameBuilder
{
public: 
  std::pair<const void*, std::size_t>
  build_frame() override
  {
    // TODO: To be filled by the CRT experts
    return { &m_frame, sizeof(m_frame) }; 
  }

private:
    fddetdataformats::CRTGrenobleFrame m_frame;
};   

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_PLUGINS_CRTGRENOBLEFRAMEBUILDER_HPP_
