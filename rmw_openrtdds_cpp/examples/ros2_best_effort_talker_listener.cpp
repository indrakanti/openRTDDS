#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/u_int32.hpp"

// Demonstrates: ORT-RMW-051, ORT-RMW-052
namespace {

[[nodiscard]] rclcpp::QoS qualification_qos() {
  return rclcpp::QoS(rclcpp::KeepLast(4U))
      .best_effort()
      .durability_volatile();
}

class Talker final : public rclcpp::Node {
 public:
  Talker() : Node("openrtdds_best_effort_talker") {
    publisher_ = create_publisher<std_msgs::msg::UInt32>(
        "/openrtdds/qualification", qualification_qos());
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
  Listener() : Node("openrtdds_best_effort_listener") {
    subscription_ = create_subscription<std_msgs::msg::UInt32>(
        "/openrtdds/qualification", qualification_qos(),
        [this](const std_msgs::msg::UInt32& message) {
          if (message.data != 42U) {
            return;
          }
          std::cout << "OPENRTDDS_ROS2_RECEIVED " << message.data
                    << std::endl;
          received_ = true;
          rclcpp::shutdown();
        });
  }

  [[nodiscard]] bool received() const noexcept { return received_; }

 private:
  rclcpp::Subscription<std_msgs::msg::UInt32>::SharedPtr subscription_{};
  bool received_{false};
};

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: ros2_best_effort_talker_listener talker|listener\n";
    return 2;
  }
  rclcpp::init(argc, argv);
  const std::string role{argv[1]};
  if (role == "talker") {
    rclcpp::spin(std::make_shared<Talker>());
    return 0;
  }
  if (role == "listener") {
    auto listener = std::make_shared<Listener>();
    rclcpp::spin(listener);
    return listener->received() ? 0 : 1;
  }
  std::cerr << "unknown role: " << role << '\n';
  rclcpp::shutdown();
  return 2;
}
