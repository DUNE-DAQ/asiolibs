/**
 * @file GenericReceiverModel.hpp Generic IOManager Receiver model
 *
 * This is part of the DUNE DAQ , copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef ASIOLIBS_SRC_GENERICRECEIVERMODEL_HPP_
#define ASIOLIBS_SRC_GENERICRECEIVERMODEL_HPP_

#include "GenericReceiverConcept.hpp"

namespace dunedaq::asiolibs {

template<class TargetPayloadType>
class GenericReceiverModel : public GenericReceiverConcept
{
public:
  explicit GenericReceiverModel(const std::string& raw_data_receiver_connection_name)
    : m_receiver(get_iom_receiver<TargetPayloadType>(raw_data_receiver_connection_name))
  {}

  std::optional<TypeErasedPayload> try_receive(dunedaq::iomanager::Receiver::timeout_t timeout) override {
    auto opt_payload = m_receiver->try_receive(timeout);
    if (opt_payload) {
      // Allocate the received payload on the heap with shared ownership,
      // so its lifetime can outlive this function and be tied to async sends.
      auto payload = std::make_shared<TargetPayloadType>(std::move(*opt_payload));
      return TypeErasedPayload{ std::reinterpret_pointer_cast<const void>(payload), payload.get(), sizeof(*payload) };
    }
    return std::nullopt;
  }
  
private:
  /**
   * @brief Generic IOManager Receiver
   */
  std::shared_ptr<iomanager::ReceiverConcept<TargetPayloadType>> m_receiver;
};

} // namespace dunedaq::asiolibs

#endif // ASIOLIBS_SRC_GENERICRECEIVERMODEL_HPP_
