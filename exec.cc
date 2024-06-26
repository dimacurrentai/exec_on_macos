#include <iostream>
#include <thread>
#include <vector>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <crt_externs.h>
#endif  // __APPLE__

template <typename F>
static void POPEN2_PASS_ARGS(const std::vector<std::string>& in, F&& f) {
  std::vector<std::vector<char>> mutable_in;
  mutable_in.resize(in.size());
  for (size_t i = 0u; i < in.size(); ++i) {
    mutable_in[i].assign(in[i].c_str(), in[i].c_str() + in[i].length() + 1u);
  }
  std::vector<char*> out;
  for (auto& e : mutable_in) {
    out.push_back(&e[0]);
  }
  out.push_back(nullptr);
  f(&out[0]);
}

inline void POPEN2_CREATE_PIPE(int r[2]) {
  if (::pipe(r)) {
    std::cerr << "FATAL: " << __LINE__ << std::endl;
    ::abort();
  }
}

int main() {
  int pipe_stdin[2];
  int pipe_stdout[2];
  int pipe_stderr[2];

  POPEN2_CREATE_PIPE(pipe_stdin);
  POPEN2_CREATE_PIPE(pipe_stdout);
  POPEN2_CREATE_PIPE(pipe_stderr);

  pid_t const pid = ::fork();

  if (pid < 0) {
    std::cerr << "FATAL: " << __LINE__ << std::endl;
    ::abort();
  }

  if (pid == 0) {
    // Child.
    ::close(pipe_stdin[1]);
    ::dup2(pipe_stdin[0], 0);
    ::close(pipe_stdout[0]);
    ::dup2(pipe_stdout[1], 1);
    ::close(pipe_stderr[0]);
    ::dup2(pipe_stderr[1], 2);
    POPEN2_PASS_ARGS({"bash", "-c", "(echo foo; sleep 1; echo bar)"}, [&](char* const* argv_to_pass) {
      int const r = ::execvp(argv_to_pass[0], argv_to_pass);
      std::cerr << "FATAL: " << __LINE__ << " R=" << r << ", errno=" << errno << std::endl;
      ::perror("execvp");
      ::abort();
    });
  } else {
    // Parent.
    int pipe_terminate_signal[2];
    POPEN2_CREATE_PIPE(pipe_terminate_signal);

    ::close(pipe_stdin[0]);
    ::close(pipe_stdout[1]);
    ::close(pipe_stderr[1]);

    std::thread waitpid_signaler_thread([&]() {
      ::waitpid(pid, NULL, 0);
      std::cerr << "DEBUG: Waitpid done!" << std::endl;
      char c = '\n';
      if (::write(pipe_terminate_signal[1], &c, 1) != 1) {
        std::cerr << "FATAL: " << __LINE__ << std::endl;
        ::abort();
      }
    });

    struct pollfd fds[2];
    fds[0].fd = pipe_terminate_signal[0];
    fds[0].events = POLLIN;
    fds[1].fd = pipe_stdout[0];
    fds[1].events = POLLIN;

    char buf[1000];
    while (true) {
      ::poll(fds, 2, -1);
      if (fds[0].revents & POLLIN) {
        std::cerr << "DEBUG: ::poll() confirms it's time to terminate." << std::endl;
        // The call to `::waitpid()` from the spawned thread has succeeded, the child is done, time to wrap up.
        break;
      } else if (fds[1].revents & POLLIN) {
        std::cerr << "DEBUG: ::poll() confirms activity in stdout." << std::endl;
        ssize_t const n = ::read(pipe_stdout[0], buf, sizeof(buf) - 1);
        if (n < 0) {
          std::cerr << "DEBUG: can not ::read() from stdout, terminating." << std::endl;
          // Assume that once the pipe can't be read from, the child process has died.
          break;
        } else if (n > 0) {
          buf[n] = '\0';
          std::cout << buf << std::flush;
        } else {
          std::cerr << "DEBUG: ::read() from stdout returned zero, waiting." << std::endl;
          // Somehow this `::read()` is a non-blocking call.
          // Should not happen.
          // Won't hurt to sleep for a bit to not overload the CPU.
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
      }
    }

    waitpid_signaler_thread.join();

    ::close(pipe_stdin[1]);
    ::close(pipe_stdout[0]);
    ::close(pipe_stderr[0]);

    ::close(pipe_terminate_signal[0]);
    ::close(pipe_terminate_signal[1]);
  }
}
