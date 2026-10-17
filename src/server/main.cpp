#include "ipc_server.hpp"
#include "server.hpp"

#if defined(_WIN32)
#include <WindowsShmSegmentProvider.hpp>
#elif defined(__ANDROID__)
#include <FileShmSegmentProvider.hpp>
#else
#include <PosixShmSegmentProvider.hpp>
#endif

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <vector>
#endif

namespace {
volatile std::sig_atomic_t stop_requested = 0;
void request_stop(int) { stop_requested = 1; }
#if defined(_WIN32)
std::string default_cache_directory() {
  const DWORD length = ::GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
  if (!length) return "C:/Aniparser/cache";
  std::vector<wchar_t> value(length);
  const DWORD written = ::GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), length);
  if (!written || written >= length) return "C:/Aniparser/cache";
  const auto utf8 = (std::filesystem::path(std::wstring(value.data(), written)) /
                     L"Aniparser/cache").u8string();
  return {reinterpret_cast<const char *>(utf8.data()), utf8.size()};
}
#endif
}

int main(int argc, char **argv) {
  const std::string cache_name = argc > 1 ? argv[1] : "aniparser";
  const std::string socket_path = argc > 2 ? argv[2] :
#if defined(_WIN32)
      R"(\\.\pipe\aniparser)";
#elif defined(__ANDROID__)
      "";
#else
      "/tmp/aniparser.sock";
#endif

  std::string shared_memory_directory;
#if defined(_WIN32)
  shared_memory_directory = argc > 3 ? argv[3] : default_cache_directory();
  shm::WindowsShmSegmentProvider provider(shared_memory_directory);
#elif defined(__ANDROID__)
  if (argc <= 3) {
    std::cerr << "Android startup must provide the app-private cache directory\n";
    return 2;
  }
  shared_memory_directory = argv[3];
  shm::FileShmSegmentProvider provider(shared_memory_directory);
#else
  shm::PosixShmSegmentProvider provider;
#endif
  Core core(provider, cache_name);
  if (!core.initialize()) {
    std::cerr << "Failed to initialize cache\n";
    return 1;
  }

  Server server(core);
  IpcServer ipc(server, socket_path, cache_name, shared_memory_directory);
  if (!ipc.start()) {
    std::cerr << "Failed to listen on IPC socket: " << socket_path << '\n';
    return 1;
  }

  std::signal(SIGINT, request_stop);
  std::signal(SIGTERM, request_stop);
  std::cout << "IPC server listening on " << socket_path << '\n';
  while (!stop_requested)
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  ipc.stop();
  return 0;
}
