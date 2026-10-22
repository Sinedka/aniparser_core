#pragma once

#include <nlohmann/json.hpp>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aniparser::tui {
using Json = nlohmann::json;

class IpcClient {
  public:
  ~IpcClient() {
    if (fd_ >= 0) ::close(fd_);
  }

  bool connect_to(const std::string &path) {
    if (fd_ >= 0) return path_ == path;
    if (path.size() >= sizeof(sockaddr_un{}.sun_path)) return false;
    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) return false;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&address),
                  sizeof(address)) != 0) {
      ::close(fd_);
      fd_ = -1;
      return false;
    }
    path_ = path;
    return true;
  }

  Json request(std::string method, Json fields = Json::object()) {
    if (fd_ < 0) throw std::runtime_error("IPC is not connected");
    const auto id = ++next_id_;
    Json request{{"id", id}, {"method", std::move(method)}};
    for (auto it = fields.begin(); it != fields.end(); ++it)
      request[it.key()] = it.value();
    auto bytes = request.dump() + '\n';
    std::size_t sent = 0;
    while (sent < bytes.size()) {
      const auto count =
          ::send(fd_, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
      if (count <= 0) throw std::runtime_error("IPC write failed");
      sent += static_cast<std::size_t>(count);
    }
    for (;;) {
      auto message = read_message();
      if (message.value("id", int64_t{-1}) == id && !message.contains("event"))
        return message;
      inbox_.push_back(std::move(message));
    }
  }

  std::vector<Json> poll_messages() {
    std::vector<Json> messages;
    while (!inbox_.empty()) {
      messages.push_back(std::move(inbox_.front()));
      inbox_.pop_front();
    }
    pollfd descriptor{fd_, POLLIN, 0};
    if (fd_ >= 0 && ::poll(&descriptor, 1, 0) > 0 &&
        (descriptor.revents & POLLIN)) {
      char buffer[8192];
      const auto count = ::recv(fd_, buffer, sizeof(buffer), MSG_DONTWAIT);
      if (count > 0) input_.append(buffer, static_cast<std::size_t>(count));
    }
    for (;;) {
      const auto newline = input_.find('\n');
      if (newline == std::string::npos) break;
      auto line = input_.substr(0, newline);
      input_.erase(0, newline + 1);
      if (!line.empty()) messages.push_back(Json::parse(line));
    }
    return messages;
  }

  private:
  Json read_message() {
    for (;;) {
      const auto newline = input_.find('\n');
      if (newline != std::string::npos) {
        auto line = input_.substr(0, newline);
        input_.erase(0, newline + 1);
        return Json::parse(line);
      }
      char buffer[8192];
      const auto count = ::recv(fd_, buffer, sizeof(buffer), 0);
      if (count <= 0) throw std::runtime_error("IPC connection closed");
      input_.append(buffer, static_cast<std::size_t>(count));
    }
  }

  int fd_ = -1;
  int64_t next_id_ = 0;
  std::string path_;
  std::string input_;
  std::deque<Json> inbox_;
};

} // namespace aniparser::tui
