/**
 * @file FIXME (DTE): Refactor
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_PLUGINS_FRAMEBUILDER_HPP_
#define ASIOLIBS_PLUGINS_FRAMEBUILDER_HPP_

#include <utility>

namespace dunedaq::asiolibs {

class FrameBuilder
{
public: 
    virtual ~FrameBuilder() = default;
    virtual std::pair<const void*, std::size_t> build_frame() = 0;
};   

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_PLUGINS_FRAMEBUILDER_HPP_
