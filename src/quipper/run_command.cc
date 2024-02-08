// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "run_command.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>

#include "base/logging.h"

namespace quipper {

namespace {

bool CloseFdOnExec(int fd) {
  int fd_flags = fcntl(fd, F_GETFD);
  if (fd_flags == -1) {
    PLOG(ERROR) << "F_GETFD";
    return false;
  }
  if (fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC)) {
    PLOG(ERROR) << "F_SETFD FD_CLOEXEC";
    return false;
  }
  return true;
}

void ReadFromFd(int fd, std::vector<char>* output) {
  static const int kReadSize = 4096;
  ssize_t read_sz;
  size_t read_off = output->size();
  do {
    output->resize(read_off + kReadSize);
    do {
      read_sz = read(fd, output->data() + read_off, kReadSize);
    } while (read_sz < 0 && errno == EINTR);
    if (read_sz < 0) {
      PLOG(FATAL) << "read";
    }
    read_off += read_sz;
  } while (read_sz > 0);
  output->resize(read_off);
}

class SigintHandler {
 public:
  SigintHandler() : handler_({}), child_(0) {
    int ret = sigemptyset(&handler_.sa_mask);
    DCHECK_EQ(ret, 0);

    ret = sigemptyset(&sigint_sigset_);
    DCHECK_EQ(ret, 0);
    ret = sigaddset(&sigint_sigset_, SIGINT);
    DCHECK_EQ(ret, 0);

    // Block SIGINT until child process is forked.
    ret = sigprocmask(SIG_BLOCK, &sigint_sigset_, nullptr);
    DCHECK_EQ(ret, 0);
  }

  void OnForked(pid_t child) {
    child_ = child;

    DCHECK(!g_signal_handler);
    g_signal_handler = this;

    int ret;
    if (child_ > 0) {
      // For the parent process (quipper), handle SIGINT to forward it to the
      // perf process and let quipper itself run. This is so that sending SIGINT
      // to the quipper process can be used to terminate a perf collection
      // session ending up with a well-formed quipper proto as output.
      handler_.sa_handler = &HandleSignal;
      ret = sigaction(SIGINT, &handler_, nullptr);
      DCHECK_EQ(ret, 0);
    } else if (child_ == 0) {
      // For the child process (perf), put it into a new process group with
      // the child as the group leader so that 1) the parent (quipper) can send
      // or forward SIGINT to the whole process group; and 2) SIGINT on CTRL-C
      // in the terminal is not delivered to this process group.
      ret = setpgid(0 /* sets pgid of current process */,
                    0 /* uses the current pid as pgid */);
      // This should not fail in the child just after fork().
      DCHECK_EQ(ret, 0);

      // Terminate the child process if the parent process ever dies
      // prematurely.
      ret = prctl(PR_SET_PDEATHSIG, SIGTERM);
      // It's OK to continue the child if it ever fails.
      DCHECK_EQ(ret, 0);
    }

    // Both parent and child unblock the signal. Now it's safe to have SIGINT
    // delivered to quipper and perf.
    ret = sigprocmask(SIG_UNBLOCK, &sigint_sigset_, nullptr);
    DCHECK_EQ(ret, 0);
  }

  ~SigintHandler() {
    // Destructor is never called in child because child either calls execvp()
    // or std::_Exit() on execution failure. Either way the destructor is not
    // called.
    DCHECK_GT(child_, 0);

    // Ignore SIGINT until the process exits so quipper can finish data
    // conversion if SIGINT arrives after the perf duration elapsed.
    // sigaction() is the first action in the destructor so that we won't run
    // the signal handler with this object partially destroyed.
    handler_.sa_handler = SIG_IGN;
    int ret = sigaction(SIGINT, &handler_, nullptr);
    DCHECK_EQ(ret, 0);
    g_signal_handler = nullptr;
  }

  // Returns 0 if exit_status indicates that the process terminates on SIGINT,
  // or -(termination signal) if the process terminates because of other
  // unexpected signals.
  static int GetSignaledExitStatus(int exit_status) {
    int term_signal = WTERMSIG(exit_status);
    // Treat SIGINT or SIGTERM as normal termination. If perf forks, it will
    // send SIGTERM to the child process, wait until it dies and then send
    // out the child exit signal as its exit status.
    if (term_signal == SIGTERM || term_signal == SIGINT) {
      return 0;
    } else {
      return -term_signal;
    }
  }

 private:
  static void HandleSignal(int signo) {
    // We should only receive SIGINT.
    DCHECK_EQ(signo, SIGINT);
    DCHECK(!!g_signal_handler);

    // Forward the signal to the child process group. It's OK that the child
    // process group may be not existent (errno == ESRCH) if SIGINT is sent
    // after perf exits.
    (void)killpg(g_signal_handler->child_, signo);
  }

  static SigintHandler* g_signal_handler;

  struct sigaction handler_;
  sigset_t sigint_sigset_;
  pid_t child_;
};

SigintHandler* SigintHandler::g_signal_handler;

}  // namespace

int RunCommand(const std::vector<std::string>& command,
               std::vector<char>* output) {
  std::vector<char*> c_str_cmd;
  c_str_cmd.reserve(command.size() + 1);
  for (const auto& c : command) {
    // This cast is safe: POSIX states that exec shall not modify argv nor the
    // strings pointed to by argv.
    c_str_cmd.push_back(const_cast<char*>(c.c_str()));
  }
  c_str_cmd.push_back(nullptr);

  // Create pipe for stdout:
  int output_pipefd[2];
  if (output) {
    if (pipe(output_pipefd)) {
      PLOG(ERROR) << "pipe";
      return -1;
    }
  }

  // Pipe for the child to return errno if exec fails:
  int errno_pipefd[2];
  if (pipe(errno_pipefd)) {
    PLOG(ERROR) << "pipe for errno";
    return -1;
  }
  if (!CloseFdOnExec(errno_pipefd[1])) return -1;

  // Initialize the parent and child process *atomically* to signal delivery:
  // fork(), sigaction() and setpgid() are all done with SIGINT blocked so that
  // SIGINT will be delivered either before or after these action are done, not
  // between these actions.
  SigintHandler signal_handler;
  const pid_t child = fork();
  signal_handler.OnForked(child);

  if (child == 0) {
    close(errno_pipefd[0]);

    if (output) {
      if (close(output_pipefd[0]) < 0) {
        PLOG(FATAL) << "close read end of pipe";
      }
    }

    int devnull_fd = open("/dev/null", O_WRONLY);
    if (devnull_fd < 0) {
      PLOG(FATAL) << "open /dev/null";
    }

    if (dup2(output ? output_pipefd[1] : devnull_fd, 1) < 0) {
      PLOG(FATAL) << "dup2 stdout";
    }

    if (dup2(devnull_fd, 2) < 0) {
      PLOG(FATAL) << "dup2 stderr";
    }

    if (close(devnull_fd) < 0) {
      PLOG(FATAL) << "close /dev/null";
    }

    execvp(c_str_cmd[0], c_str_cmd.data());
    int exec_errno = errno;

    // exec failed... Write errno to a pipe so parent can retrieve it.
    int ret;
    do {
      ret = write(errno_pipefd[1], &exec_errno, sizeof(exec_errno));
    } while (ret < 0 && errno == EINTR);
    close(errno_pipefd[1]);

    std::_Exit(EXIT_FAILURE);
  }

  if (close(errno_pipefd[1])) {
    PLOG(FATAL) << "close write end of errno pipe";
  }
  if (output) {
    if (close(output_pipefd[1]) < 0) {
      PLOG(FATAL) << "close write end of pipe";
    }
  }

  // Check for errno:
  int child_exec_errno;
  int read_errno_res;
  do {
    read_errno_res =
        read(errno_pipefd[0], &child_exec_errno, sizeof(child_exec_errno));
  } while (read_errno_res < 0 && errno == EINTR);
  if (read_errno_res < 0) {
    PLOG(FATAL) << "read errno";
  }
  if (close(errno_pipefd[0])) {
    PLOG(FATAL) << "close errno";
  }

  if (read_errno_res > 0) {
    // exec failed in the child.
    while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {
    }
    errno = child_exec_errno;
    return -1;
  }

  // Read stdout from pipe.
  if (output) {
    ReadFromFd(output_pipefd[0], output);
    if (close(output_pipefd[0])) {
      PLOG(FATAL) << "close output";
    }
  }

  // Wait for child.
  int exit_status;
  while (waitpid(child, &exit_status, 0) < 0 && errno == EINTR) {
  }
  errno = 0;

  if (WIFEXITED(exit_status)) {
    return WEXITSTATUS(exit_status);
  } else if (WIFSIGNALED(exit_status)) {
    return signal_handler.GetSignaledExitStatus(exit_status);
  }
  return -1;
}

}  // namespace quipper
