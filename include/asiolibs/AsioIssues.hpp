/**
 * @file AsioIssues.hpp ASIO related ERS issues
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_INCLUDE_ASIOLIBS_ASIOISSUES_HPP_
#define ASIOLIBS_INCLUDE_ASIOLIBS_ASIOISSUES_HPP_

#include <ers/Issue.hpp>
#include "logging/Logging.hpp" // NOTE: if ISSUES ARE DECLARED BEFORE include logging/Logging.hpp, TLOG_DEBUG<<issue wont work.

namespace dunedaq {

ERS_DECLARE_ISSUE(asiolibs,
                  InvalidRawReceiverTimeout,
                  "recv_timeout_ms is 0 or missing in the configuration. The default value " << raw_receiver_timeout_ms << " will be used.",
                  ((int)raw_receiver_timeout_ms))

} // namespace dunedaq

#endif // ASIOLIBS_INCLUDE_ASIOLIBS_ASIOISSUES_HPP_
