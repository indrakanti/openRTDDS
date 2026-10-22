#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

#include "example_interfaces/srv/add_two_ints.hpp"
#include "rclcpp/rclcpp.hpp"

// Demonstrates: ORT-RMW-011, ORT-RMW-021
namespace {

using AddTwoInts = example_interfaces::srv::AddTwoInts;

class Server final : public rclcpp::Node {
 public:
  Server() : Node("openrtdds_add_server") {
    service_ = create_service<AddTwoInts>(
        "/openrtdds/add",
        [](const std::shared_ptr<AddTwoInts::Request> request,
           std::shared_ptr<AddTwoInts::Response> response) {
          response->sum = request->a + request->b;
          std::cout << "OPENRTDDS_SERVICE_RESPONSE " << response->sum
                    << std::endl;
        });
  }

 private:
  rclcpp::Service<AddTwoInts>::SharedPtr service_{};
};

class Client final : public rclcpp::Node {
 public:
  Client() : Node("openrtdds_add_client") {
    client_ = create_client<AddTwoInts>("/openrtdds/add");
    timer_ = create_wall_timer(std::chrono::milliseconds(20), [this] {
      if (sent_ || !client_->service_is_ready()) {
        return;
      }
      sent_ = true;
      auto request = std::make_shared<AddTwoInts::Request>();
      request->a = 20;
      request->b = 22;
      static_cast<void>(client_->async_send_request(
          request, [this](rclcpp::Client<AddTwoInts>::SharedFuture future) {
            const auto response = future.get();
            success_ = response->sum == 42;
            std::cout << "OPENRTDDS_CLIENT_RESPONSE " << response->sum
                      << std::endl;
            rclcpp::shutdown();
          }));
    });
  }

  [[nodiscard]] bool success() const noexcept { return success_; }

 private:
  rclcpp::Client<AddTwoInts>::SharedPtr client_{};
  rclcpp::TimerBase::SharedPtr timer_{};
  bool sent_{false};
  bool success_{false};
};

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: ros2_add_service server|client\n";
    return 2;
  }
  rclcpp::init(argc, argv);
  const std::string role{argv[1]};
  if (role == "server") {
    rclcpp::spin(std::make_shared<Server>());
    return 0;
  }
  if (role == "client") {
    auto client = std::make_shared<Client>();
    rclcpp::spin(client);
    return client->success() ? 0 : 1;
  }
  std::cerr << "unknown role: " << role << '\n';
  rclcpp::shutdown();
  return 2;
}
