#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/u_int32.hpp"

// Demonstrates: ORT-RMW-052, ORT-RMW-053, ORT-RMW-054
namespace {

[[nodiscard]] rclcpp::QoS qualification_qos(const bool reliable) {
  auto qos = rclcpp::QoS(rclcpp::KeepLast(4U)).durability_volatile();
  return reliable ? qos.reliable() : qos.best_effort();
}

class Talker final : public rclcpp::Node {
 public:
  explicit Talker(const bool reliable)
      : Node(reliable ? "openrtdds_reliable_cpp_talker"
                      : "openrtdds_best_effort_cpp_talker") {
    publisher_ = create_publisher<std_msgs::msg::UInt32>(
        "/openrtdds/qualification", qualification_qos(reliable));
    timer_ = create_wall_timer(std::chrono::milliseconds(20), [this] {
      std_msgs::msg::UInt32 message{};
      message.data = 42U;
      publisher_->publish(message);
    });
  }

 private:
  rclcpp::Publisher<std_msgs::msg::UInt32>::SharedPtr publisher_{};
  rclcpp::TimerBase::SharedPtr timer_{};
};

class Listener final : public rclcpp::Node {
 public:
  explicit Listener(const bool reliable)
      : Node(reliable ? "openrtdds_reliable_cpp_listener"
                      : "openrtdds_best_effort_cpp_listener"),
        qos_name_(reliable ? "reliable" : "best_effort") {
    subscription_ = create_subscription<std_msgs::msg::UInt32>(
        "/openrtdds/qualification", qualification_qos(reliable),
        [this](const std_msgs::msg::UInt32& message) {
          if (message.data != 42U) {
            return;
          }
          std::cout << "OPENRTDDS_ROS2_RECEIVED cpp " << qos_name_ << ' '
                    << message.data << std::endl;
          received_ = true;
          rclcpp::shutdown();
        });
  }

  [[nodiscard]] bool received() const noexcept { return received_; }

 private:
  rclcpp::Subscription<std_msgs::msg::UInt32>::SharedPtr subscription_{};
  const char* qos_name_{nullptr};
  bool received_{false};
};

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: ros2_talker_listener talker|listener "
                 "best_effort|reliable\n";
    return 2;
  }
  const std::string role{argv[1]};
  const std::string qos{argv[2]};
  if ((qos != "best_effort") && (qos != "reliable")) {
    std::cerr << "unknown QoS: " << qos << '\n';
    return 2;
  }
  const bool reliable = qos == "reliable";
  rclcpp::init(argc, argv);
  if (role == "talker") {
    rclcpp::spin(std::make_shared<Talker>(reliable));
    return 0;
  }
  if (role == "listener") {
    auto listener = std::make_shared<Listener>(reliable);
    rclcpp::spin(listener);
    return listener->received() ? 0 : 1;
  }
  std::cerr << "unknown role: " << role << '\n';
  rclcpp::shutdown();
  return 2;
}
