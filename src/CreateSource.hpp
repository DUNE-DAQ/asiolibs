/**
 * @file CreateSource.hpp Specific SourceConcept creator.
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_SRC_CREATESOURCE_HPP_
#define ASIOLIBS_SRC_CREATESOURCE_HPP_

#include "SourceConcept.hpp"
#include "SourceModel.hpp"
#include "datahandlinglibs/DataHandlingIssues.hpp"

#include "fdreadoutlibs/DUNEWIBEthTypeAdapter.hpp"
#include "fdreadoutlibs/TDEFrameTypeAdapter.hpp"
#include "fdreadoutlibs/CRTBernTypeAdapter.hpp"
#include "fdreadoutlibs/CRTGrenobleTypeAdapter.hpp"

#include <memory>
#include <string>

namespace dunedaq {

DUNE_DAQ_TYPESTRING(dunedaq::fdreadoutlibs::types::DUNEWIBEthTypeAdapter, "WIBEthFrame")
DUNE_DAQ_TYPESTRING(dunedaq::fdreadoutlibs::types::TDEFrameTypeAdapter, "TDEFrame")
DUNE_DAQ_TYPESTRING(dunedaq::fdreadoutlibs::types::CRTBernTypeAdapter, "CRTBernFrame")
DUNE_DAQ_TYPESTRING(dunedaq::fdreadoutlibs::types::CRTGrenobleTypeAdapter, "CRTGrenobleFrame")

namespace asiolibs {

std::shared_ptr<SourceConcept>
createSourceModel(const appmodel::RawDataCallbackConf* conf)
{
  auto datatype = conf->get_data_type();
  TLOG() << "Choosing specializations for SourceModel for output connection "
         << " [uid:" << conf->UID() << " , data_type:" << datatype << ']';

  if (datatype.find("WIBEthFrame") != std::string::npos) {
    // Create Model
    auto source_model = std::make_shared<SourceModel<fdreadoutlibs::types::DUNEWIBEthTypeAdapter>>();

    // For callback acquisition later (lazy)
    source_model->set_sink_config(conf);

    // Get parser and sink
    //auto& parser = source_model->get_parser();
    //auto& sink = source_model->get_sink();
    //auto& error_sink = source_model->get_error_sink();

    // Modify parser as needed...
    //parser.process_chunk_func = parsers::fixsizedChunkInto<fdreadoutlibs::types::ProtoWIBSuperChunkTypeAdapter>(sink);
    //if (error_sink != nullptr) {
    //  parser.process_chunk_with_error_func = parsers::errorChunkIntoSink(error_sink);
    //}
    // parser.process_block_func = ...

    // Return with setup model
    return source_model;

  } else if (datatype.find("TDEFrame") != std::string::npos) {
    // WIB2 specific char arrays
    auto source_model = std::make_shared<SourceModel<fdreadoutlibs::types::TDEFrameTypeAdapter>>();
    source_model->set_sink_config(conf);
    //auto& parser = source_model->get_parser();
    //parser.process_chunk_func = parsers::fixsizedChunkInto<fdreadoutlibs::types::DUNEWIBSuperChunkTypeAdapter>(sink);
    return source_model;
  } else if (datatype.find("CRTBernFrame") != std::string::npos) {
    auto source_model = std::make_shared<SourceModel<fdreadoutlibs::types::CRTBernTypeAdapter>>();
    source_model->set_sink_config(conf);
    return source_model;
  } else if (datatype.find("CRTGrenobleFrame") != std::string::npos) {
    auto source_model = std::make_shared<SourceModel<fdreadoutlibs::types::CRTGrenobleTypeAdapter>>();
    source_model->set_sink_config(conf);
    return source_model;
  }

  return nullptr;
}

} // namespace asiolibs
} // namespace dunedaq

#endif // ASIOLIBS_SRC_CREATESOURCE_HPP_
