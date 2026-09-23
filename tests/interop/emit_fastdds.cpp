// Requirements: ORT-INT-001, ORT-INT-005, ORT-INT-006
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/qos/DataWriterQos.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastrtps/transport/UDPv4TransportDescriptor.h>
#include "VendorProbe.h"
#include "VendorProbePubSubTypes.h"

int main(int argc, char** argv) {
  using namespace eprosima::fastdds::dds;
  auto* const factory = DomainParticipantFactory::get_instance();
  DomainParticipantQos participant_qos = PARTICIPANT_QOS_DEFAULT;
  participant_qos.transport().use_builtin_transports = false;
  participant_qos.transport().user_transports.push_back(
      std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>());
  auto* const participant = factory->create_participant(
      43, participant_qos);
  if (participant == nullptr) {
    std::cerr << "Fast DDS participant creation failed\n";
    return 1;
  }
  const std::string mode = argc > 1 ? argv[1] : "participant";
  if (mode == "publish" || mode == "publish-data" ||
      mode == "subscribe-data") {
    TypeSupport type(new VendorProbePubSubType());
    if (type.register_type(participant) != ReturnCode_t::RETCODE_OK) {
      std::cerr << "Fast DDS type registration failed\n";
      return 1;
    }
    auto* const topic = participant->create_topic(
        "OpenRTDDSProbe", type.get_type_name(), TOPIC_QOS_DEFAULT);
    if (topic == nullptr) {
      std::cerr << "Fast DDS topic creation failed\n";
      return 1;
    }
    if (mode == "subscribe-data") {
      auto* const subscriber = participant->create_subscriber(
          SUBSCRIBER_QOS_DEFAULT);
      DataReaderQos reader_qos = DATAREADER_QOS_DEFAULT;
      reader_qos.reliability().kind = BEST_EFFORT_RELIABILITY_QOS;
      auto* const reader = subscriber
          ? subscriber->create_datareader(topic, reader_qos) : nullptr;
      if (reader == nullptr) {
        std::cerr << "Fast DDS reader creation failed\n";
        return 1;
      }
      std::this_thread::sleep_for(std::chrono::seconds(5));
    } else {
      auto* const publisher = participant->create_publisher(
          PUBLISHER_QOS_DEFAULT);
      DataWriterQos writer_qos = DATAWRITER_QOS_DEFAULT;
      writer_qos.reliability().kind = BEST_EFFORT_RELIABILITY_QOS;
      auto* const writer = publisher
          ? publisher->create_datawriter(topic, writer_qos) : nullptr;
      if (writer == nullptr) {
        std::cerr << "Fast DDS writer creation failed\n";
        return 1;
      }
      if (mode == "publish-data") {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        VendorProbe sample;
        sample.value(0x4F525444U);
        if (!writer->write(&sample)) {
          std::cerr << "Fast DDS writer rejected sample\n";
          return 1;
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
      } else {
        std::this_thread::sleep_for(std::chrono::seconds(5));
      }
    }
    participant->delete_contained_entities();
  } else {
    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
  return factory->delete_participant(participant) ==
                 ReturnCode_t::RETCODE_OK ? 0 : 1;
}
