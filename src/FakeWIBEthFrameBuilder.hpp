/**
 * @file FIXME (DTE): Refactor
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_PLUGINS_FAKEWIBETHFRAMEBUILDER_HPP_
#define ASIOLIBS_PLUGINS_FAKEWIBETHFRAMEBUILDER_HPP_

#include "FrameBuilder.hpp"

#include "fddetdataformats/WIBEthFrame.hpp"

namespace dunedaq::asiolibs {

class FakeWIBEthFrameBuilder : public FrameBuilder
{
public: 
  std::pair<const void*, std::size_t>
  build_frame() override
  {
    fake_data();
    return { &m_frame, sizeof(m_frame) }; 
  }

private:
  /**
   * @brief Maximum packet sequence ID before reset
   */
  static constexpr uint64_t max_seq_id = 4095;

  /**
   * @brief Timestamp difference between packets
   */
  static constexpr uint64_t timestamp_diff = 32 * 64;

  /**
   * @brief Fake packet detector ID
   */
  static constexpr uint64_t fake_det_id = 3;

  /**
   * @brief Fake packet stream ID
   */
  static constexpr uint64_t fake_stream_id = 0;

  /**
   * @brief Fake packet block length
   */
  static constexpr uint64_t fake_block_length = 0x382;

  void
  fake_sequence_id()
  {
    m_seq_id = m_seq_id == max_seq_id ? 0 : ++m_seq_id;
  }

  void
  fake_timestamp()
  {
    if (m_first_packet) {
      m_first_packet = false;
      auto time_now = std::chrono::system_clock::now().time_since_epoch();
      uint64_t current_time = // NOLINT (build/unsigned)
      std::chrono::duration_cast<std::chrono::microseconds>(time_now).count();
      // FIXME: where do I get the clockspeed from?
      // ts_0 = (m_conf.clock_speed_hz / 100000) * current_time;
      m_timestamp = 625 * current_time / 10;    
    } else {
      m_timestamp += timestamp_diff;
    }
  }

  void
  fake_adc()
  {
    for (int time = 0; time < 64; ++time) {
      for (int channel = 0; channel < 64; ++channel) {
        m_frame.set_adc(channel, time, 0); 
      }
      if (time != 0) {
        m_frame.set_adc(0, time, 666);
      }
    }
  }

  void
  fake_data()
  {
    m_frame.daq_header.det_id = fake_det_id;
    m_frame.daq_header.crate_id = 1;
    m_frame.daq_header.slot_id = 1;
    m_frame.daq_header.stream_id = fake_stream_id;
    fake_sequence_id();
    m_frame.daq_header.seq_id = m_seq_id;
    m_frame.daq_header.block_length = fake_block_length;
    fake_timestamp();
    m_frame.daq_header.timestamp = m_timestamp;
    fake_adc();
  }

  fddetdataformats::WIBEthFrame m_frame;
  uint64_t m_seq_id = 0;
  uint64_t m_timestamp = 0;    
  bool m_first_packet = true;
};   

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_PLUGINS_FAKEWIBETHFRAMEBUILDER_HPP_
