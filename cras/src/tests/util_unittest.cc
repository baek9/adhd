// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cras_util.h"

namespace {

TEST(Util, SendRecvFileDescriptor) {
  int fd[2];
  int sock[2];
  char buf[6] = {0};
  int new_fd;

  /* Create a pipe and a pair of sockets. Then send the write end of
   * the pipe (fd[1]) through the socket, and receive it as
   * new_fd */
  ASSERT_EQ(0, pipe(fd));
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sock));

  ASSERT_EQ(5, cras_send_with_fd(sock[0], "hello", 5, fd[1]));
  ASSERT_EQ(5, cras_recv_with_fd(sock[1], buf, 5, &new_fd));
  ASSERT_STREQ("hello", buf);

  close(sock[0]);
  close(sock[1]);
  close(fd[1]);

  /* Send a character to the new_fd, and receive it from the read end
   * of the pipe (fd[0]) */
  ASSERT_EQ(1, write(new_fd, "a", 1));
  ASSERT_EQ(1, read(fd[0], buf, 1));
  ASSERT_EQ('a', buf[0]);

  close(fd[0]);
  close(new_fd);
}

TEST(Util, TimevalAfter) {
  struct timeval t0, t1;
  t0.tv_sec = 0;
  t0.tv_usec = 0;
  t1.tv_sec = 0;
  t1.tv_usec = 0;
  ASSERT_FALSE(timeval_after(&t0, &t1));
  ASSERT_FALSE(timeval_after(&t1, &t0));
  t0.tv_usec = 1;
  ASSERT_TRUE(timeval_after(&t0, &t1));
  ASSERT_FALSE(timeval_after(&t1, &t0));
  t1.tv_sec = 1;
  ASSERT_FALSE(timeval_after(&t0, &t1));
  ASSERT_TRUE(timeval_after(&t1, &t0));
}

TEST(Util, FramesToTime) {
  struct timespec t;

  cras_frames_to_time(24000, 48000, &t);
  EXPECT_EQ(0, t.tv_sec);
  EXPECT_EQ(500000000, t.tv_nsec);

  cras_frames_to_time(48000, 48000, &t);
  EXPECT_EQ(1, t.tv_sec);
  EXPECT_EQ(0, t.tv_nsec);

  cras_frames_to_time(60000, 48000, &t);
  EXPECT_EQ(1, t.tv_sec);
  EXPECT_EQ(250000000, t.tv_nsec);

  cras_frames_to_time(191999, 192000, &t);
  EXPECT_EQ(0, t.tv_sec);
  EXPECT_EQ(999994791, t.tv_nsec);
}

TEST(Util, TimeToFrames) {
  struct timespec t;
  unsigned int frames;

  t.tv_sec = 0;
  t.tv_nsec = 500000000;
  frames = cras_time_to_frames(&t, 48000);
  EXPECT_EQ(24000, frames);

  t.tv_sec = 1;
  t.tv_nsec = 500000000;
  frames = cras_time_to_frames(&t, 48000);
  EXPECT_EQ(72000, frames);

  t.tv_sec = 0;
  t.tv_nsec = 0;
  frames = cras_time_to_frames(&t, 48000);
  EXPECT_EQ(0, frames);
}

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
