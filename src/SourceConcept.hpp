
/**
 * @file SourceConcept.hpp SourceConcept for constructors and
 * forwarding command args. Enforces the implementation to
 * queue in UDP JUMBO frames to be translated to TypeAdapters and
 * send them to corresponding sinks.
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_SRC_SOURCECONCEPT_HPP_
#define ASIOLIBS_SRC_SOURCECONCEPT_HPP_

//#include "DefaultParserImpl.hpp"

#include "opmonlib/MonitorableObject.hpp"
#include "appfwk/DAQModule.hpp"
#include "appmodel/DataMoveCallbackConf.hpp"
//#include "packetformat/detail/block_parser.hpp"
#include <nlohmann/json.hpp>

#include <memory>
#include <sstream>
#include <string>

namespace dunedaq {
  namespace asiolibs {

    class SourceConcept : public opmonlib::MonitorableObject
    {
    public:
      SourceConcept() {}
      virtual ~SourceConcept() {}

      SourceConcept(const SourceConcept&) = delete;            ///< SourceConcept is not copy-constructible
      SourceConcept& operator=(const SourceConcept&) = delete; ///< SourceConcept is not copy-assginable
      SourceConcept(SourceConcept&&) = delete;                 ///< SourceConcept is not move-constructible
      SourceConcept& operator=(SourceConcept&&) = delete;      ///< SourceConcept is not move-assignable

      //  virtual void init(const nlohmann::json& args) = 0;
      virtual void acquire_callback() = 0;
      //  virtual void conf(const nlohmann::json& args) = 0;
      //  virtual void start(const nlohmann::json& args) = 0;
      //  virtual void stop(const nlohmann::json& args) = 0;

      // Meant to process an incoming raw byte buffer and extract complete frames of arbitrary types in specialized models.
      virtual void handle_daq_frame(char* buffer) = 0;
      virtual std::size_t get_expected_frame_size() const = 0;

      void set_sink_config(const appmodel::DataMoveCallbackConf* sink_conf) 
      { 
        m_sink_conf = sink_conf; 
      }

      // Sink or destination related
      const appmodel::DataMoveCallbackConf* m_sink_conf;
    };

  } // namespace asiolibs
} // namespace dunedaq

#endif // ASIOLIBS_SRC_SOURCECONCEPT_HPP_
