/**
 * @file SourceModel.hpp FELIX CR's ELink concept wrapper
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_SRC_SOURCEMODEL_HPP_
#define ASIOLIBS_SRC_SOURCEMODEL_HPP_

#include "SourceConcept.hpp"


#include "iomanager/IOManager.hpp"
#include "iomanager/Sender.hpp"
#include "logging/Logging.hpp"

#include "asiolibs/opmon/SourceModel.pb.h"

// #include "datahandlinglibs/utils/ReusableThread.hpp"
#include "datahandlinglibs/DataMoveCallbackRegistry.hpp"
#include "datahandlinglibs/utils/BufferCopy.hpp" 

// #include <folly/ProducerConsumerQueue.h>
// #include <nlohmann/json.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace dunedaq::asiolibs {

template<class TargetPayloadType>
class SourceModel : public SourceConcept
{
public:
  using sink_t = iomanager::SenderConcept<TargetPayloadType>;
  using inherited = SourceConcept;
  using data_t = nlohmann::json;

  /**
   * @brief SourceModel Constructor
   * @param name Instance name for this SourceModel instance
   */
  SourceModel()
    : SourceConcept()
  {}
  ~SourceModel() {}

  void acquire_callback() override
  {
      if (m_callback_is_acquired) {
        TLOG_DEBUG(5) << "SourceModel callback is already acquired!";
      } else {
        // Getting DataMoveCBRegistry
        auto dmcbr = datahandlinglibs::DataMoveCallbackRegistry::get();
        m_sink_callback = dmcbr->get_callback<TargetPayloadType>(inherited::m_sink_conf);
        m_callback_is_acquired = true;
      }
  }

  // Process an incoming raw byte buffer and extract complete frames of type TargetPayloadType.
  void handle_daq_frame(char* buffer) override
  {
    // Materialize a real TargetPayloadType object by copying bytes from the buffer.
    // This is defined behavior, alignment-safe, and fast, without pointer vodoo
    // Previously reinterpret_cast to TargetPayloadType* introduced alignment traps 
    // “pretend there’s a constructed object there” UB. 
    TargetPayloadType frame;
    std::memcpy(&frame, buffer, m_expected_frame_size);

     // Pass by value (moved); no references into 'buffer', so no UAF.
    (*m_sink_callback)(std::move(frame));
  }

  std::size_t get_expected_frame_size() const override {
    return m_expected_frame_size;
  }
    
  void generate_opmon_data() override {

    opmon::SourceInfo info;
    info.set_leftover_bytes_encountered( m_leftover_bytes_encountered.exchange(0) );

    publish( std::move(info) );
  }
  
private:
  // Constants
  const std::size_t m_expected_frame_size = sizeof(TargetPayloadType);

  // Callback internals
  bool m_callback_is_acquired{ false };
  using sink_cb_t = std::shared_ptr<std::function<void(TargetPayloadType&&)>>;
  sink_cb_t m_sink_callback;

  // Stats  
  std::atomic<uint64_t> m_leftover_bytes_encountered{0}; // NOLINT(build/unsigned)

};

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_SRC_SOURCEMODEL_HPP_
