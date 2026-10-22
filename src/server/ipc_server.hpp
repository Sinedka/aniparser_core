#pragma once

#include <memory>
#include <string>

class Server;

// Newline-delimited JSON transport over the platform-local IPC endpoint.
class IpcServer {
  public:
  IpcServer(Server &server, std::string endpoint);
  ~IpcServer();
  IpcServer(const IpcServer &) = delete;
  IpcServer &operator=(const IpcServer &) = delete;

  bool start();
  void stop();

  private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
