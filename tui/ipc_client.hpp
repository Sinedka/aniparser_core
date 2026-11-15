#pragma once

#include <nlohmann/json.hpp>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aniparser::tui {
using Json = nlohmann::json;

class IpcClient {
  public:
  explicit IpcClient(
      std::chrono::milliseconds timeout = std::chrono::seconds(100))
    : timeout_(timeout) {}
  IpcClient(const IpcClient &) = delete;
  IpcClient &operator=(const IpcClient &) = delete;
  ~IpcClient() { disconnect(); }

  // Called between short poll waits. Returning false cancels the request and
  // closes the connection, so a late reply cannot be mistaken for a new one.
  std::function<bool()> on_wait;

  bool connect_to(const std::string &path) {
    if (fd_ >= 0) return path_ == path;
    if (path.empty() || path.find('\0') != std::string::npos ||
        path.size() >= sizeof(sockaddr_un{}.sun_path))
      return false;
    fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd_ < 0) return false;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&address),
                  sizeof(address)) != 0) {
      if (errno != EINPROGRESS) {
        disconnect();
        return false;
      }
      try {
        wait_for(POLLOUT, Clock::now() + std::chrono::seconds(5));
        int error = 0;
        socklen_t size = sizeof(error);
        if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &size) != 0 ||
            error != 0) {
          disconnect();
          return false;
        }
      } catch (...) {
        disconnect();
        return false;
      }
    }
    path_ = path;
    return true;
  }

  Json request(std::string method, Json fields = Json::object()) {
    if (fd_ < 0) throw std::runtime_error("IPC is disconnected");
    if (next_id_ == std::numeric_limits<std::int64_t>::max())
      throw std::runtime_error("IPC request ID exhausted");
    const auto id = ++next_id_;
    Json message{{"id", id}, {"method", std::move(method)}};
    for (auto it = fields.begin(); it != fields.end(); ++it)
      message[it.key()] = it.value();
    const auto bytes = message.dump() + '\n';
    const auto deadline = Clock::now() + timeout_;
    try {
      std::size_t sent = 0;
      while (sent < bytes.size()) {
        wait_for(POLLOUT, deadline);
        const auto count = ::send(fd_, bytes.data() + sent, bytes.size() - sent,
                                  MSG_NOSIGNAL | MSG_DONTWAIT);
        if (count < 0 &&
            (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
          continue;
        if (count <= 0) throw std::runtime_error("IPC write failed");
        sent += static_cast<std::size_t>(count);
      }
      for (;;) {
        auto reply = read_message(deadline);
        if (reply.value("id", std::int64_t{-1}) == id &&
            !reply.contains("event"))
          return reply;
        inbox_bytes_ += reply.dump().size();
        if (inbox_.size() >= 512 || inbox_bytes_ > kBufferLimit)
          throw std::runtime_error("IPC event buffer exceeded");
        inbox_.push_back(std::move(reply));
      }
    } catch (...) {
      disconnect();
      throw;
    }
  }

  std::vector<Json> poll_messages() {
    std::vector<Json> messages;
    while (!inbox_.empty()) {
      messages.push_back(std::move(inbox_.front()));
      inbox_.pop_front();
    }
    inbox_bytes_ = 0;
    if (fd_ < 0) return messages;
    try {
      pollfd descriptor{fd_, POLLIN, 0};
      if (::poll(&descriptor, 1, 0) > 0) {
        if (descriptor.revents & POLLIN) receive();
        else if (descriptor.revents & (POLLHUP | POLLERR | POLLNVAL))
          throw std::runtime_error("IPC connection closed");
      }
      for (;;) {
        const auto newline = input_.find('\n');
        if (newline == std::string::npos) break;
        auto line = input_.substr(0, newline);
        input_.erase(0, newline + 1);
        if (!line.empty()) messages.push_back(Json::parse(line));
      }
    } catch (...) {
      disconnect();
      throw;
    }
    return messages;
  }

  private:
  using Clock = std::chrono::steady_clock;
  static constexpr std::size_t kBufferLimit = 16 * 1024 * 1024;
  void disconnect() {
    if (fd_ >= 0) ::close(std::exchange(fd_, -1));
    input_.clear();
    inbox_.clear();
    inbox_bytes_ = 0;
    path_.clear();
  }
  void wait_for(short events, Clock::time_point deadline) {
    for (;;) {
      if (on_wait && !on_wait())
        throw std::runtime_error("IPC request cancelled; disconnected");
      if (Clock::now() >= deadline)
        throw std::runtime_error("IPC request timed out; disconnected");
      pollfd descriptor{fd_, events, 0};
      const auto result = ::poll(&descriptor, 1, 50);
      if (result < 0 && errno == EINTR) continue;
      if (result < 0) throw std::runtime_error("IPC poll failed");
      if (descriptor.revents & events) return;
      if (descriptor.revents & (POLLHUP | POLLERR | POLLNVAL))
        throw std::runtime_error("IPC connection closed");
    }
  }
  void receive() {
    char buffer[8192];
    const auto count = ::recv(fd_, buffer, sizeof(buffer), MSG_DONTWAIT);
    if (count < 0 &&
        (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
      return;
    if (count <= 0) throw std::runtime_error("IPC connection closed");
    input_.append(buffer, static_cast<std::size_t>(count));
    if (input_.size() > kBufferLimit)
      throw std::runtime_error("IPC response exceeds 16 MiB");
  }
  Json read_message(Clock::time_point deadline) {
    for (;;) {
      const auto newline = input_.find('\n');
      if (newline != std::string::npos) {
        auto line = input_.substr(0, newline);
        input_.erase(0, newline + 1);
        return Json::parse(line);
      }
      wait_for(POLLIN, deadline);
      receive();
    }
  }

  int fd_ = -1;
  std::int64_t next_id_ = 0;
  std::chrono::milliseconds timeout_;
  std::string path_, input_;
  std::deque<Json> inbox_;
  std::size_t inbox_bytes_ = 0;
};
} // namespace aniparser::tui
