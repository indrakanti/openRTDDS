// Requirements: ORT-INT-001
#include <chrono>
#include <iostream>
#include <thread>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>

int main() {
  using namespace eprosima::fastdds::dds;
  auto* const factory = DomainParticipantFactory::get_instance();
  auto* const participant = factory->create_participant(
      43, PARTICIPANT_QOS_DEFAULT);
  if (participant == nullptr) {
    std::cerr << "Fast DDS participant creation failed\n";
    return 1;
  }
  std::this_thread::sleep_for(std::chrono::seconds(5));
  return factory->delete_participant(participant) == RETCODE_OK ? 0 : 1;
}
