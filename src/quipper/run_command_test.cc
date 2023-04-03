// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "run_command.h"

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <vector>

#include "compat/test.h"

namespace quipper {

class RunCommandTest : public ::testing::Test {
 public:
  void TearDown() override {
    // Reset signal handler after each RunCommand() call.
    (void)signal(SIGINT, SIG_DFL);
  }
};

TEST_F(RunCommandTest, StoresStdout) {
  std::vector<char> output;
  EXPECT_EQ(0, RunCommand({"/bin/sh", "-c", "echo 'Hello, world!'"}, &output));
  std::string output_str(output.begin(), output.end());
  EXPECT_EQ("Hello, world!\n", output_str);
}

TEST_F(RunCommandTest, RunsFromPath) {
  std::vector<char> output;
  EXPECT_EQ(0, RunCommand({"sh", "-c", "echo 'Hello, world!'"}, &output));
  std::string output_str(output.begin(), output.end());
  EXPECT_EQ("Hello, world!\n", output_str);
}

TEST_F(RunCommandTest, LargeStdout) {
  std::vector<char> output;
  EXPECT_EQ(0,
            RunCommand({"dd", "if=/dev/zero", "bs=5", "count=4096"}, &output));
  EXPECT_EQ(5 * 4096, output.size());
  EXPECT_EQ('\0', output[0]);
  EXPECT_EQ('\0', output[1]);
  EXPECT_EQ('\0', *output.rbegin());
}

TEST_F(RunCommandTest, StdoutToDevnull) {
  EXPECT_EQ(0, RunCommand({"/bin/sh", "-c", "echo 'Hello, world!'"}, nullptr));
}

TEST_F(RunCommandTest, StderrIsNotStored) {
  std::vector<char> output;
  EXPECT_EQ(0,
            RunCommand({"/bin/sh", "-c", "echo 'Hello, void!' >&2"}, &output));
  EXPECT_EQ(0, output.size());
}

TEST_F(RunCommandTest, NoSuchExecutable) {
  std::vector<char> output;
  int ret = RunCommand({"/doesnt-exist/not-bin/true"}, &output);
  int save_errno = errno;
  EXPECT_EQ(-1, ret);
  EXPECT_EQ(ENOENT, save_errno);
}

TEST_F(RunCommandTest, SignalIgnore) {
  struct sigaction handler {
    {
      SIG_DFL
    }
  };

  EXPECT_EQ(0, RunCommand({"/bin/sh", "-c", "echo 'Hello, world!'"}, nullptr));

  int ret = sigaction(SIGINT, nullptr, &handler);
  EXPECT_EQ(0, ret);
  EXPECT_EQ(handler.sa_handler, SIG_IGN);
}

struct ThreadArg {
  decltype(sigaction::sa_handler) sig_handler_;
  int pipe_fds_[2];
  std::atomic<bool> command_done_;
};

static void* SignalSender(void* thread_arg) {
  ThreadArg* arg = static_cast<ThreadArg*>(thread_arg);

  struct sigaction handler;
  int ret = sigaction(SIGINT, nullptr, &handler);
  if (ret) return reinterpret_cast<void*>(errno);

  // Wait until SIGINT is installed.
  while (handler.sa_handler == arg->sig_handler_ && !arg->command_done_) {
    (void)sched_yield();

    ret = sigaction(SIGINT, nullptr, &handler);
    if (ret) return reinterpret_cast<void*>(errno);
  }

  // Close the write end in the parent process. The child process automatically
  // close the write end after it execvp().
  close(arg->pipe_fds_[1]);

  char buf[1];
  // We expect to get EOF.
  if (read(arg->pipe_fds_[0], buf, sizeof(buf)))
    return reinterpret_cast<void*>(errno);

  ret = kill(getpid(), SIGINT);
  if (ret) return reinterpret_cast<void*>(errno);

  return (void*)0;
}

TEST_F(RunCommandTest, SendInterrupt) {
  std::vector<char> output;

  struct sigaction old_handler;
  EXPECT_EQ(0, sigaction(SIGINT, nullptr, &old_handler));

  ThreadArg thread_arg{old_handler.sa_handler};
  EXPECT_EQ(0, pipe2(thread_arg.pipe_fds_, O_CLOEXEC));
  thread_arg.command_done_ = false;

  pthread_t tid;
  int ret = pthread_create(&tid, nullptr, &SignalSender, &thread_arg);
  EXPECT_EQ(0, ret);

  EXPECT_EQ(0, RunCommand({"/bin/sh", "-c", "/bin/sleep 30 && echo 'Done!'"},
                          &output));
  thread_arg.command_done_ = true;

  void* thread_ret = (void*)-1;
  EXPECT_EQ(0, pthread_join(tid, &thread_ret));
  EXPECT_EQ((void*)0, thread_ret);

  EXPECT_EQ(0, output.size());
}

}  // namespace quipper
