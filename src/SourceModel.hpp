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

  bool handle_payload(char* message, std::size_t size) // NOLINT(build/unsigned)
  {
    bool push_out = true;
    if (push_out) {

      TargetPayloadType& target_payload = *reinterpret_cast<TargetPayloadType*>(message);
  
        (*m_sink_callback)(std::move(target_payload));

    } else {
      TargetPayloadType target_payload;
      uint32_t bytes_copied = 0;
      datahandlinglibs::buffer_copy(message, size, static_cast<void*>(&target_payload), bytes_copied, sizeof(target_payload));
    }

    return true;
  }

  std::size_t get_target_payload_size() const override {
    TargetPayloadType target_payload; 
    return target_payload.get_frame_size(); // TODO (DTE): Could be a static function?
  }
    
  void generate_opmon_data() override {

    opmon::SourceInfo info;
    info.set_dropped_frames( m_dropped_packets.load() ); 

    publish( std::move(info) );
  }
  
private:
  // Callback internals
  bool m_callback_is_acquired{ false };
  using sink_cb_t = std::shared_ptr<std::function<void(TargetPayloadType&&)>>;
  sink_cb_t m_sink_callback;

  std::atomic<uint64_t> m_dropped_packets{0};

};

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_SRC_SOURCEMODEL_HPP_
