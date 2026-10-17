#pragma once

#include <memory>
#include <string>

class Server;

// Newline-delimited JSON over a local Unix domain socket. Shared data itself
// remains in POSIX shared memory; IPC messages carry commands and handles.
class IpcServer {
 public:
  IpcServer(Server &server, std::string endpoint, std::string cache_name,
            std::string shared_memory_directory = {});
  ~IpcServer();
  IpcServer(const IpcServer &) = delete;
  IpcServer &operator=(const IpcServer &) = delete;

  bool start();
  void stop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
