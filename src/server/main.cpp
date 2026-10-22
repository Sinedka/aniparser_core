#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include "ipc_server.hpp"
#include "server.hpp"

namespace {
volatile std::sig_atomic_t stop_requested = 0;
void request_stop(int) { stop_requested = 1; }
} // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
  const std::string endpoint = argc > 1 ? argv[1] : R"(\\.\pipe\aniparser)";
#elif defined(__ANDROID__)
  if (argc < 2) {
    std::cerr << "Android startup requires an IPC endpoint\n";
    return 2;
  }
  const std::string endpoint = argv[1];
#else
  const std::string endpoint = argc > 1 ? argv[1] : "/tmp/aniparser.sock";
#endif

  Core core;
  Server server(core);
  IpcServer ipc(server, endpoint);
  if (!ipc.start()) {
    std::cerr << "Failed to listen on IPC endpoint: " << endpoint << '\n';
    return 1;
  }
  std::signal(SIGINT, request_stop);
  std::signal(SIGTERM, request_stop);
  std::cout << "IPC server listening on " << endpoint << '\n';
  while (!stop_requested)
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  ipc.stop();
  return 0;
}
