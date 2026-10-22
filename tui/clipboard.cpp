#include "clipboard.hpp"

#include <fcntl.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <string>
#include <thread>

extern char** environ;

namespace aniparser::tui {
namespace {

bool write_clipboard_input(int fd, std::string_view text) {
  sigset_t blocked{};
  sigset_t previous{};
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGPIPE);
  if (pthread_sigmask(SIG_BLOCK, &blocked, &previous) != 0) return false;

  bool success = true;
  std::size_t offset = 0;
  while (offset < text.size()) {
    const auto count = ::write(fd, text.data() + offset, text.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      success = false;
      break;
    }
    offset += static_cast<std::size_t>(count);
  }

  if (!success && errno == EPIPE) {
    timespec no_wait{};
    (void)sigtimedwait(&blocked, nullptr, &no_wait);
  }
  (void)pthread_sigmask(SIG_SETMASK, &previous, nullptr);
  return success;
}

bool try_clipboard_command(const char* executable,
                           char* const arguments[],
                           std::string_view text) {
  int pipe_fds[2];
  if (::pipe(pipe_fds) != 0) return false;

  posix_spawn_file_actions_t actions;
  int error = ::posix_spawn_file_actions_init(&actions);
  if (error != 0) {
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
    return false;
  }
  if (error == 0)
    error =
        ::posix_spawn_file_actions_adddup2(&actions, pipe_fds[0], STDIN_FILENO);
  if (error == 0)
    error = ::posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
  if (error == 0)
    error = ::posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);
  if (error != 0) {
    ::posix_spawn_file_actions_destroy(&actions);
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
    return false;
  }

  pid_t child = -1;
  error =
      ::posix_spawnp(&child, executable, &actions, nullptr, arguments, environ);
  ::posix_spawn_file_actions_destroy(&actions);
  ::close(pipe_fds[0]);
  if (error != 0) {
    ::close(pipe_fds[1]);
    return false;
  }

  const bool written = write_clipboard_input(pipe_fds[1], text);
  ::close(pipe_fds[1]);
  try {
    std::thread([child] {
      int result = 0;
      while (::waitpid(child, &result, 0) < 0 && errno == EINTR) {}
    }).detach();
  } catch (...) {
    // The clipboard command remains usable even if reaping could not start.
  }
  return written;
}

} // namespace

bool copy_to_clipboard(std::string_view text) {
  if (text.empty()) return false;

  std::array<char*, 2> wayland_args{const_cast<char*>("wl-copy"), nullptr};
  if (try_clipboard_command("wl-copy", wayland_args.data(), text)) return true;

  std::array<char*, 4> xclip_args{const_cast<char*>("xclip"),
                                  const_cast<char*>("-selection"),
                                  const_cast<char*>("clipboard"), nullptr};
  if (try_clipboard_command("xclip", xclip_args.data(), text)) return true;

  std::array<char*, 3> xsel_args{const_cast<char*>("xsel"),
                                 const_cast<char*>("--clipboard"), nullptr};
  return try_clipboard_command("xsel", xsel_args.data(), text);
}

} // namespace aniparser::tui
