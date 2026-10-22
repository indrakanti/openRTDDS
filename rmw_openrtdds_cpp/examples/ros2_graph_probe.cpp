#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/u_int32.hpp"

// Demonstrates: ORT-RMW-010, ORT-RMW-020, ORT-RMW-055, ORT-RMW-056
namespace {

using namespace std::chrono_literals;

[[nodiscard]] rclcpp::QoS graph_qos() {
  return rclcpp::QoS(rclcpp::KeepLast(2U)).best_effort().durability_volatile();
}

[[nodiscard]] bool subject_visible(rclcpp::Node& node) {
  bool node_found = false;
  for (const auto& name : node.get_node_names()) {
    if (name == "/openrtdds/graph_subject") {
      node_found = true;
      break;
    }
  }
  const auto topics = node.get_topic_names_and_types();
  const auto topic = topics.find("/openrtdds/graph_demo");
  bool type_found = false;
  if (topic != topics.end()) {
    for (const auto& type : topic->second) {
      if (type == "std_msgs/msg/UInt32") {
        type_found = true;
        break;
      }
    }
  }
  return node_found && type_found &&
         (node.count_publishers("/openrtdds/graph_demo") == 1U) &&
         (node.count_subscribers("/openrtdds/graph_demo") == 1U);
}

[[nodiscard]] bool subject_removed(rclcpp::Node& node) {
  for (const auto& name : node.get_node_names()) {
    if (name == "/openrtdds/graph_subject") {
      return false;
    }
  }
  return (node.count_publishers("/openrtdds/graph_demo") == 0U) &&
         (node.count_subscribers("/openrtdds/graph_demo") == 0U);
}

[[nodiscard]] int subject(const std::uint64_t hold_ms) {
  auto node = std::make_shared<rclcpp::Node>("graph_subject", "/openrtdds");
  auto publisher = node->create_publisher<std_msgs::msg::UInt32>(
      "/openrtdds/graph_demo", graph_qos());
  auto subscription = node->create_subscription<std_msgs::msg::UInt32>(
      "/openrtdds/graph_demo", graph_qos(),
      [](const std_msgs::msg::UInt32&) {});
  std::cout << "OPENRTDDS_GRAPH_SUBJECT_READY" << std::endl;
  std::this_thread::sleep_for(std::chrono::milliseconds(hold_ms));
  subscription.reset();
  publisher.reset();
  node.reset();
  std::this_thread::sleep_for(750ms);
  rclcpp::shutdown();
  return 0;
}

[[nodiscard]] int observer(const std::uint64_t timeout_ms) {
  auto node = std::make_shared<rclcpp::Node>("graph_observer", "/openrtdds");
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while ((std::chrono::steady_clock::now() < deadline) &&
         !subject_visible(*node)) {
    std::this_thread::sleep_for(20ms);
  }
  if (!subject_visible(*node)) {
    std::cerr << "graph subject was not discovered\n";
    rclcpp::shutdown();
    return 1;
  }
  std::cout << "OPENRTDDS_GRAPH_OBSERVED" << std::endl;
  while ((std::chrono::steady_clock::now() < deadline) &&
         !subject_removed(*node)) {
    std::this_thread::sleep_for(20ms);
  }
  if (!subject_removed(*node)) {
    std::cerr << "graph subject was not removed\n";
    rclcpp::shutdown();
    return 1;
  }
  std::cout << "OPENRTDDS_GRAPH_REMOVED" << std::endl;
  node.reset();
  rclcpp::shutdown();
  return 0;
}

[[nodiscard]] bool parse_milliseconds(const char* const text,
                                      std::uint64_t& value) {
  if ((text == nullptr) || (text[0] == '\0')) {
    return false;
  }
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(text, &end, 10);
  if ((end == text) || (*end != '\0')) {
    return false;
  }
  value = static_cast<std::uint64_t>(parsed);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: ros2_graph_probe subject|observer milliseconds\n";
    return 2;
  }
  std::uint64_t milliseconds = 0U;
  if (!parse_milliseconds(argv[2], milliseconds)) {
    std::cerr << "invalid duration\n";
    return 2;
  }
  const std::string role{argv[1]};
  rclcpp::init(argc, argv);
  if (role == "subject") {
    return subject(milliseconds);
  }
  if (role == "observer") {
    return observer(milliseconds);
  }
  std::cerr << "unknown role: " << role << '\n';
  rclcpp::shutdown();
  return 2;
}
