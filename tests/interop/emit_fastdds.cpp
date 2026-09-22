// Requirements: ORT-INT-001, ORT-INT-005
#include <chrono>
#include <iostream>
#include <thread>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastrtps/types/DynamicTypeBuilderFactory.h>
#include <fastrtps/types/DynamicPubSubType.h>

int main(int argc, char** argv) {
  using namespace eprosima::fastdds::dds;
  auto* const factory = DomainParticipantFactory::get_instance();
  auto* const participant = factory->create_participant(
      43, PARTICIPANT_QOS_DEFAULT);
  if (participant == nullptr) {
    std::cerr << "Fast DDS participant creation failed\n";
    return 1;
  }
  if (argc > 1 && argv[1][0] == 'p') {
    using namespace eprosima::fastrtps::types;
    auto* const factory_type = DynamicTypeBuilderFactory::get_instance();
    auto builder = factory_type->create_struct_builder();
    builder->set_name("VendorProbe");
    builder->add_member(0, "value", factory_type->create_uint32_type());
    TypeSupport type(new DynamicPubSubType(builder->build()));
    if (type.register_type(participant) != ReturnCode_t::RETCODE_OK) {
      std::cerr << "Fast DDS type registration failed\n";
      return 1;
    }
    auto* const topic = participant->create_topic(
        "OpenRTDDSProbe", type.get_type_name(), TOPIC_QOS_DEFAULT);
    auto* const publisher = participant->create_publisher(PUBLISHER_QOS_DEFAULT);
    auto* const writer = topic && publisher
        ? publisher->create_datawriter(topic, DATAWRITER_QOS_DEFAULT)
        : nullptr;
    if (writer == nullptr) {
      std::cerr << "Fast DDS writer creation failed\n";
      return 1;
    }
    std::this_thread::sleep_for(std::chrono::seconds(5));
    participant->delete_contained_entities();
  } else {
    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
  return factory->delete_participant(participant) ==
                 ReturnCode_t::RETCODE_OK ? 0 : 1;
}
