/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_UTIL_H_
#define CRAS_UTIL_H_

#include "cras_types.h"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))
#define max(a, b) ({ typeof(a) _a = (a); \
		     typeof(b) _b = (b); \
		     _a > _b ? _a : _b; })
#define min(a, b) ({ typeof(a) _a = (a); \
		     typeof(b) _b = (b); \
		     _a < _b ? _a : _b; })

#define assert_on_compile(e) ((void)sizeof(char[1 - 2 * !(e)]))
#define assert_on_compile_is_power_of_2(n) \
	assert_on_compile((n) != 0 && (((n) & ((n) - 1)) == 0))

/* Enables real time scheduling. */
int cras_set_rt_scheduling(int rt_lim);
/* Sets the priority. */
int cras_set_thread_priority(int priority);
/* Sets the niceness level of the current thread. */
int cras_set_nice_level(int nice);

/* Connects to the socket opened by the client for audio messages. One of these
 * is created per stream.  It is only used for high-priority, low-latency audio
 * messages (Get/Put samples). */
int cras_server_connect_to_client_socket(cras_stream_id_t stream_id);
/* Converts a buffer level from one sample rate to another. */
static inline size_t cras_frames_at_rate(size_t orig_rate, size_t orig_frames,
					 size_t act_rate)
{
	return (orig_frames * act_rate + orig_rate - 1) / orig_rate;
}

/* Makes a file descriptor non blocking. */
int cras_make_fd_nonblocking(int fd);

#endif /* CRAS_UTIL_H_ */
