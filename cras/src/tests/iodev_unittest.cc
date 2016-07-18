// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>

extern "C" {
#include "cras_iodev.h"
#include "cras_rstream.h"
#include "dev_stream.h"
#include "utlist.h"
#include "cras_audio_area.h"

// Mock software volume scalers.
float softvol_scalers[101];
}

#define BUFFER_SIZE 8192

static int cras_iodev_list_disable_dev_called;
static int select_node_called;
static enum CRAS_STREAM_DIRECTION select_node_direction;
static cras_node_id_t select_node_id;
static struct cras_ionode *node_selected;
static size_t notify_nodes_changed_called;
static size_t notify_active_node_changed_called;
static size_t notify_node_volume_called;
static size_t notify_node_capture_gain_called;
static int dsp_context_new_sample_rate;
static const char *dsp_context_new_purpose;
static int dsp_context_free_called;
static int update_channel_layout_called;
static int update_channel_layout_return_val;
static int  set_swap_mode_for_node_called;
static int  set_swap_mode_for_node_enable;
static int notify_node_left_right_swapped_called;
static int cras_audio_format_set_channel_layout_called;
static unsigned int cras_system_get_volume_return;
static int cras_dsp_get_pipeline_called;
static int cras_dsp_get_pipeline_ret;
static int cras_dsp_put_pipeline_called;
static int cras_dsp_pipeline_get_source_buffer_called;
static int cras_dsp_pipeline_get_sink_buffer_called;
static float cras_dsp_pipeline_source_buffer[2][DSP_BUFFER_SIZE];
static float cras_dsp_pipeline_sink_buffer[2][DSP_BUFFER_SIZE];
static int cras_dsp_pipeline_get_delay_called;
static int cras_dsp_pipeline_apply_called;
static int cras_dsp_pipeline_apply_sample_count;
static unsigned int cras_mix_mute_count;
static unsigned int cras_dsp_num_input_channels_return;
static unsigned int cras_dsp_num_output_channels_return;
struct cras_dsp_context *cras_dsp_context_new_return;
static unsigned int rate_estimator_add_frames_num_frames;
static unsigned int rate_estimator_add_frames_called;
static int cras_system_get_mute_return;
static snd_pcm_format_t cras_scale_buffer_fmt;
static float cras_scale_buffer_scaler;
static unsigned int pre_dsp_hook_called;
static const uint8_t *pre_dsp_hook_frames;
static void *pre_dsp_hook_cb_data;
static unsigned int post_dsp_hook_called;
static const uint8_t *post_dsp_hook_frames;
static void *post_dsp_hook_cb_data;
static int iodev_buffer_size;
static long cras_system_get_capture_gain_ret_value;
static int is_open_ret;
static uint8_t audio_buffer[BUFFER_SIZE];
static struct cras_audio_area *audio_area;
static int dev_running_ret_value;
static unsigned int put_buffer_nframes;
static int output_should_wake_ret;
static int no_stream_called;
static int no_stream_enable;

// Iodev callback
int update_channel_layout(struct cras_iodev *iodev) {
  update_channel_layout_called = 1;
  return update_channel_layout_return_val;
}

// Iodev callback
int set_swap_mode_for_node(struct cras_iodev *iodev, struct cras_ionode *node,
                           int enable)
{
  set_swap_mode_for_node_called++;
  set_swap_mode_for_node_enable = enable;
  return 0;
}

void ResetStubData() {
  cras_iodev_list_disable_dev_called = 0;
  select_node_called = 0;
  notify_nodes_changed_called = 0;
  notify_active_node_changed_called = 0;
  notify_node_volume_called = 0;
  notify_node_capture_gain_called = 0;
  dsp_context_new_sample_rate = 0;
  dsp_context_new_purpose = NULL;
  dsp_context_free_called = 0;
  set_swap_mode_for_node_called = 0;
  set_swap_mode_for_node_enable = 0;
  notify_node_left_right_swapped_called = 0;
  cras_audio_format_set_channel_layout_called = 0;
  cras_dsp_get_pipeline_called = 0;
  cras_dsp_get_pipeline_ret = 0;
  cras_dsp_put_pipeline_called = 0;
  cras_dsp_pipeline_get_source_buffer_called = 0;
  cras_dsp_pipeline_get_sink_buffer_called = 0;
  memset(&cras_dsp_pipeline_source_buffer, 0,
         sizeof(cras_dsp_pipeline_source_buffer));
  memset(&cras_dsp_pipeline_sink_buffer, 0,
         sizeof(cras_dsp_pipeline_sink_buffer));
  cras_dsp_pipeline_get_delay_called = 0;
  cras_dsp_pipeline_apply_called = 0;
  cras_dsp_pipeline_apply_sample_count = 0;
  cras_dsp_num_input_channels_return = 2;
  cras_dsp_num_output_channels_return = 2;
  cras_dsp_context_new_return = NULL;
  rate_estimator_add_frames_num_frames = 0;
  rate_estimator_add_frames_called = 0;
  cras_system_get_mute_return = 0;
  cras_mix_mute_count = 0;
  pre_dsp_hook_called = 0;
  pre_dsp_hook_frames = NULL;
  post_dsp_hook_called = 0;
  post_dsp_hook_frames = NULL;
  iodev_buffer_size = 0;
  cras_system_get_capture_gain_ret_value = 0;
  is_open_ret = 0;
  // Assume there is some data in audio buffer.
  memset(audio_buffer, 0xff, sizeof(audio_buffer));
  if (audio_area) {
    free(audio_area);
    audio_area = NULL;
  }
  dev_running_ret_value = 0;
  put_buffer_nframes = 0;
  output_should_wake_ret= 0;
  no_stream_called = 0;
  no_stream_enable = 0;
}

namespace {

//  Test fill_time_from_frames
TEST(IoDevTestSuite, FillTimeFromFramesNormal) {
  struct timespec ts;

  cras_iodev_fill_time_from_frames(12000, 48000, &ts);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

TEST(IoDevTestSuite, FillTimeFromFramesLong) {
  struct timespec ts;

  cras_iodev_fill_time_from_frames(120000 - 12000, 48000, &ts);
  EXPECT_EQ(2, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

TEST(IoDevTestSuite, FillTimeFromFramesShort) {
  struct timespec ts;

  cras_iodev_fill_time_from_frames(12000 - 12000, 48000, &ts);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_EQ(0, ts.tv_nsec);
}

class IoDevSetFormatTestSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      ResetStubData();
      sample_rates_[0] = 44100;
      sample_rates_[1] = 48000;
      sample_rates_[2] = 0;

      channel_counts_[0] = 2;
      channel_counts_[1] = 0;
      channel_counts_[2] = 0;

      pcm_formats_[0] = SND_PCM_FORMAT_S16_LE;
      pcm_formats_[1] = SND_PCM_FORMAT_S32_LE;
      pcm_formats_[2] = static_cast<snd_pcm_format_t>(0);

      update_channel_layout_called = 0;
      update_channel_layout_return_val = 0;

      memset(&iodev_, 0, sizeof(iodev_));
      iodev_.update_channel_layout = update_channel_layout;
      iodev_.supported_rates = sample_rates_;
      iodev_.supported_channel_counts = channel_counts_;
      iodev_.supported_formats = pcm_formats_;
      iodev_.dsp_context = NULL;

      cras_audio_format_set_channel_layout_called  = 0;
    }

    virtual void TearDown() {
      cras_iodev_free_format(&iodev_);
    }

    struct cras_iodev iodev_;
    size_t sample_rates_[3];
    size_t channel_counts_[3];
    snd_pcm_format_t pcm_formats_[3];
};

TEST_F(IoDevSetFormatTestSuite, SupportedFormatSecondary) {
  struct cras_audio_format fmt;
  int rc;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 48000;
  fmt.num_channels = 2;
  iodev_.direction = CRAS_STREAM_OUTPUT;
  ResetStubData();
  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, iodev_.ext_format->format);
  EXPECT_EQ(48000, iodev_.ext_format->frame_rate);
  EXPECT_EQ(2, iodev_.ext_format->num_channels);
  EXPECT_EQ(dsp_context_new_sample_rate, 48000);
  EXPECT_STREQ(dsp_context_new_purpose, "playback");
}

TEST_F(IoDevSetFormatTestSuite, SupportedFormat32bit) {
  struct cras_audio_format fmt;
  int rc;

  fmt.format = SND_PCM_FORMAT_S32_LE;
  fmt.frame_rate = 48000;
  fmt.num_channels = 2;
  iodev_.direction = CRAS_STREAM_OUTPUT;
  ResetStubData();
  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S32_LE, iodev_.ext_format->format);
  EXPECT_EQ(48000, iodev_.ext_format->frame_rate);
  EXPECT_EQ(2, iodev_.ext_format->num_channels);
  EXPECT_EQ(dsp_context_new_sample_rate, 48000);
  EXPECT_STREQ(dsp_context_new_purpose, "playback");
}

TEST_F(IoDevSetFormatTestSuite, SupportedFormatPrimary) {
  struct cras_audio_format fmt;
  int rc;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 44100;
  fmt.num_channels = 2;
  iodev_.direction = CRAS_STREAM_INPUT;
  ResetStubData();
  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, iodev_.ext_format->format);
  EXPECT_EQ(44100, iodev_.ext_format->frame_rate);
  EXPECT_EQ(2, iodev_.ext_format->num_channels);
  EXPECT_EQ(dsp_context_new_sample_rate, 44100);
  EXPECT_STREQ(dsp_context_new_purpose, "capture");
}

TEST_F(IoDevSetFormatTestSuite, SupportedFormatDivisor) {
  struct cras_audio_format fmt;
  int rc;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 96000;
  fmt.num_channels = 2;
  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, iodev_.ext_format->format);
  EXPECT_EQ(48000, iodev_.ext_format->frame_rate);
  EXPECT_EQ(2, iodev_.ext_format->num_channels);
}

TEST_F(IoDevSetFormatTestSuite, Supported96k) {
  struct cras_audio_format fmt;
  int rc;

  sample_rates_[0] = 48000;
  sample_rates_[1] = 96000;
  sample_rates_[2] = 0;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 96000;
  fmt.num_channels = 2;
  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, iodev_.ext_format->format);
  EXPECT_EQ(96000, iodev_.ext_format->frame_rate);
  EXPECT_EQ(2, iodev_.ext_format->num_channels);
}

TEST_F(IoDevSetFormatTestSuite, LimitLowRate) {
  struct cras_audio_format fmt;
  int rc;

  sample_rates_[0] = 48000;
  sample_rates_[1] = 8000;
  sample_rates_[2] = 0;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 8000;
  fmt.num_channels = 2;
  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, iodev_.ext_format->format);
  EXPECT_EQ(48000, iodev_.ext_format->frame_rate);
  EXPECT_EQ(2, iodev_.ext_format->num_channels);
}

TEST_F(IoDevSetFormatTestSuite, UnsupportedChannelCount) {
  struct cras_audio_format fmt;
  int rc;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 96000;
  fmt.num_channels = 1;
  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, iodev_.ext_format->format);
  EXPECT_EQ(48000, iodev_.ext_format->frame_rate);
  EXPECT_EQ(2, iodev_.ext_format->num_channels);
}

TEST_F(IoDevSetFormatTestSuite, SupportedFormatFallbackDefault) {
  struct cras_audio_format fmt;
  int rc;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 96008;
  fmt.num_channels = 2;
  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, iodev_.ext_format->format);
  EXPECT_EQ(44100, iodev_.ext_format->frame_rate);
  EXPECT_EQ(2, iodev_.ext_format->num_channels);
}

TEST_F(IoDevSetFormatTestSuite, OutputDSPChannleReduction) {
  struct cras_audio_format fmt;
  int rc;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 48000;
  fmt.num_channels = 2;

  iodev_.direction = CRAS_STREAM_OUTPUT;
  iodev_.supported_channel_counts[0] = 1;
  iodev_.supported_channel_counts[1] = 0;
  cras_dsp_context_new_return = reinterpret_cast<cras_dsp_context *>(0xf00);
  cras_dsp_get_pipeline_ret =  0xf01;
  cras_dsp_num_input_channels_return = 2;
  cras_dsp_num_output_channels_return = 1;
  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, iodev_.ext_format->format);
  EXPECT_EQ(48000, iodev_.ext_format->frame_rate);
  EXPECT_EQ(2, iodev_.ext_format->num_channels);
}

TEST_F(IoDevSetFormatTestSuite, InputDSPChannleReduction) {
  struct cras_audio_format fmt;
  int rc;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 48000;
  fmt.num_channels = 2;

  iodev_.direction = CRAS_STREAM_INPUT;
  iodev_.supported_channel_counts[0] = 10;
  iodev_.supported_channel_counts[1] = 0;
  cras_dsp_context_new_return = reinterpret_cast<cras_dsp_context *>(0xf00);
  cras_dsp_get_pipeline_ret =  0xf01;
  cras_dsp_num_input_channels_return = 10;
  cras_dsp_num_output_channels_return = 2;
  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, iodev_.ext_format->format);
  EXPECT_EQ(48000, iodev_.ext_format->frame_rate);
  EXPECT_EQ(2, iodev_.ext_format->num_channels);
}

TEST_F(IoDevSetFormatTestSuite, UpdateChannelLayoutSuccess) {
  struct cras_audio_format fmt;
  int rc;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 48000;
  fmt.num_channels = 6;

  iodev_.supported_channel_counts[0] = 6;
  iodev_.supported_channel_counts[1] = 2;

  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, iodev_.ext_format->format);
  EXPECT_EQ(48000, iodev_.ext_format->frame_rate);
  EXPECT_EQ(6, iodev_.ext_format->num_channels);
}

TEST_F(IoDevSetFormatTestSuite, UpdateChannelLayoutFail) {
  static const int8_t stereo_layout[] =
      {0, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
  struct cras_audio_format fmt;
  int rc, i;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 48000;
  fmt.num_channels = 2;

  cras_dsp_context_new_return = reinterpret_cast<cras_dsp_context *>(0xf0f);

  update_channel_layout_return_val = -1;
  iodev_.supported_channel_counts[0] = 6;
  iodev_.supported_channel_counts[1] = 2;

  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, iodev_.ext_format->format);
  EXPECT_EQ(48000, iodev_.ext_format->frame_rate);
  EXPECT_EQ(2, iodev_.ext_format->num_channels);
  EXPECT_EQ(2, cras_audio_format_set_channel_layout_called);
  EXPECT_EQ(0, dsp_context_free_called);
  for (i = 0; i < CRAS_CH_MAX; i++)
    EXPECT_EQ(iodev_.format->channel_layout[i], stereo_layout[i]);
}

TEST_F(IoDevSetFormatTestSuite, UpdateChannelLayoutFail6ch) {
  static const int8_t default_6ch_layout[] =
      {0, 1, 2, 3, 4, 5, -1, -1, -1, -1, -1};
  struct cras_audio_format fmt;
  int rc, i;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 48000;
  fmt.num_channels = 6;

  cras_dsp_context_new_return = reinterpret_cast<cras_dsp_context *>(0xf0f);

  update_channel_layout_return_val = -1;
  iodev_.supported_channel_counts[0] = 6;
  iodev_.supported_channel_counts[1] = 2;

  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, iodev_.ext_format->format);
  EXPECT_EQ(48000, iodev_.ext_format->frame_rate);
  EXPECT_EQ(6, iodev_.ext_format->num_channels);
  EXPECT_EQ(2, cras_audio_format_set_channel_layout_called);
  EXPECT_EQ(0, dsp_context_free_called);
  for (i = 0; i < CRAS_CH_MAX; i++)
    EXPECT_EQ(iodev_.format->channel_layout[i], default_6ch_layout[i]);
}

// Put buffer tests

static int get_buffer(cras_iodev* iodev, struct cras_audio_area** area,
               unsigned int* num) {
  size_t sz = sizeof(*audio_area) + sizeof(struct cras_channel_area) * 2;

  audio_area = (cras_audio_area*)calloc(1, sz);
  audio_area->frames = *num;
  audio_area->num_channels = 2;
  audio_area->channels[0].buf = audio_buffer;
  channel_area_set_channel(&audio_area->channels[0], CRAS_CH_FL);
  audio_area->channels[0].step_bytes = 4;
  audio_area->channels[1].buf = audio_buffer + 2;
  channel_area_set_channel(&audio_area->channels[1], CRAS_CH_FR);
  audio_area->channels[1].step_bytes = 4;

  *area = audio_area;
  return 0;
}

static int put_buffer(struct cras_iodev *iodev, unsigned int nframes)
{
  put_buffer_nframes = nframes;
  if (audio_area) {
    free(audio_area);
    audio_area = NULL;
  }
  return 0;
}

static int no_stream(struct cras_iodev *odev, int enable)
{
  no_stream_called++;
  no_stream_enable = enable;
  // Use default no stream playback to test default behavior.
  return cras_iodev_default_no_stream_playback(odev, enable);
}

static int output_should_wake(const struct cras_iodev *odev)
{
  return output_should_wake_ret;
}

static int pre_dsp_hook(const uint8_t *frames, unsigned int nframes,
			const struct cras_audio_format *fmt, void *cb_data)
{
  pre_dsp_hook_called++;
  pre_dsp_hook_frames = frames;
  pre_dsp_hook_cb_data = cb_data;
  return 0;
}

static int post_dsp_hook(const uint8_t *frames, unsigned int nframes,
			 const struct cras_audio_format *fmt, void *cb_data)
{
  post_dsp_hook_called++;
  post_dsp_hook_frames = frames;
  post_dsp_hook_cb_data = cb_data;
  return 0;
}

static int dev_running(const struct cras_iodev *iodev)
{
  return dev_running_ret_value;
}

TEST(IoDevPutOutputBuffer, SystemMuted) {
  struct cras_audio_format fmt;
  struct cras_iodev iodev;
  uint8_t *frames = reinterpret_cast<uint8_t*>(0x44);
  int rc;

  ResetStubData();
  memset(&iodev, 0, sizeof(iodev));
  cras_system_get_mute_return = 1;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 48000;
  fmt.num_channels = 2;
  iodev.format = &fmt;
  iodev.put_buffer = put_buffer;

  rc = cras_iodev_put_output_buffer(&iodev, frames, 20);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(20, cras_mix_mute_count);
  EXPECT_EQ(20, put_buffer_nframes);
  EXPECT_EQ(20, rate_estimator_add_frames_num_frames);
}

TEST(IoDevPutOutputBuffer, NoDSP) {
  struct cras_audio_format fmt;
  struct cras_iodev iodev;
  uint8_t *frames = reinterpret_cast<uint8_t*>(0x44);
  int rc;

  ResetStubData();
  memset(&iodev, 0, sizeof(iodev));

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 48000;
  fmt.num_channels = 2;
  iodev.format = &fmt;
  iodev.put_buffer = put_buffer;

  rc = cras_iodev_put_output_buffer(&iodev, frames, 22);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_mix_mute_count);
  EXPECT_EQ(22, put_buffer_nframes);
  EXPECT_EQ(22, rate_estimator_add_frames_num_frames);
}

TEST(IoDevPutOutputBuffer, DSP) {
  struct cras_audio_format fmt;
  struct cras_iodev iodev;
  uint8_t *frames = reinterpret_cast<uint8_t*>(0x44);
  int rc;

  ResetStubData();
  memset(&iodev, 0, sizeof(iodev));
  iodev.dsp_context = reinterpret_cast<cras_dsp_context*>(0x15);
  cras_dsp_get_pipeline_ret = 0x25;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 48000;
  fmt.num_channels = 2;
  iodev.format = &fmt;
  iodev.put_buffer = put_buffer;
  cras_iodev_register_pre_dsp_hook(&iodev, pre_dsp_hook, (void *)0x1234);
  cras_iodev_register_post_dsp_hook(&iodev, post_dsp_hook, (void *)0x5678);

  rc = cras_iodev_put_output_buffer(&iodev, frames, 32);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_mix_mute_count);
  EXPECT_EQ(1, pre_dsp_hook_called);
  EXPECT_EQ(frames, pre_dsp_hook_frames);
  EXPECT_EQ((void *)0x1234, pre_dsp_hook_cb_data);
  EXPECT_EQ(1, post_dsp_hook_called);
  EXPECT_EQ((void *)0x5678, post_dsp_hook_cb_data);
  EXPECT_EQ(32, put_buffer_nframes);
  EXPECT_EQ(32, rate_estimator_add_frames_num_frames);
  EXPECT_EQ(32, cras_dsp_pipeline_apply_sample_count);
  EXPECT_EQ(cras_dsp_get_pipeline_called, cras_dsp_put_pipeline_called);
}

TEST(IoDevPutOutputBuffer, SoftVol) {
  struct cras_audio_format fmt;
  struct cras_iodev iodev;
  uint8_t *frames = reinterpret_cast<uint8_t*>(0x44);
  int rc;

  ResetStubData();
  memset(&iodev, 0, sizeof(iodev));
  iodev.software_volume_needed = 1;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 48000;
  fmt.num_channels = 2;
  iodev.format = &fmt;
  iodev.put_buffer = put_buffer;

  cras_system_get_volume_return = 13;
  softvol_scalers[13] = 0.435;

  rc = cras_iodev_put_output_buffer(&iodev, frames, 53);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_mix_mute_count);
  EXPECT_EQ(53, put_buffer_nframes);
  EXPECT_EQ(53, rate_estimator_add_frames_num_frames);
  EXPECT_EQ(softvol_scalers[13], cras_scale_buffer_scaler);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, cras_scale_buffer_fmt);
}

TEST(IoDevPutOutputBuffer, Scale32Bit) {
  struct cras_audio_format fmt;
  struct cras_iodev iodev;
  uint8_t *frames = reinterpret_cast<uint8_t*>(0x44);
  int rc;

  ResetStubData();
  memset(&iodev, 0, sizeof(iodev));
  iodev.software_volume_needed = 1;

  cras_system_get_volume_return = 13;
  softvol_scalers[13] = 0.435;

  fmt.format = SND_PCM_FORMAT_S32_LE;
  fmt.frame_rate = 48000;
  fmt.num_channels = 2;
  iodev.format = &fmt;
  iodev.put_buffer = put_buffer;

  rc = cras_iodev_put_output_buffer(&iodev, frames, 53);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, cras_mix_mute_count);
  EXPECT_EQ(53, put_buffer_nframes);
  EXPECT_EQ(53, rate_estimator_add_frames_num_frames);
  EXPECT_EQ(SND_PCM_FORMAT_S32_LE, cras_scale_buffer_fmt);
}

// frames queued/avail tests

static unsigned fr_queued = 0;

static int frames_queued(const struct cras_iodev *iodev)
{
  return fr_queued;
}

TEST(IoDevQueuedBuffer, ZeroMinBufferLevel) {
  struct cras_iodev iodev;
  int rc;

  ResetStubData();
  memset(&iodev, 0, sizeof(iodev));
  iodev.direction = CRAS_STREAM_OUTPUT;
  iodev.frames_queued = frames_queued;
  iodev.min_buffer_level = 0;
  iodev.buffer_size = 200;
  fr_queued = 50;

  rc = cras_iodev_frames_queued(&iodev);
  EXPECT_EQ(50, rc);
  rc = cras_iodev_buffer_avail(&iodev, rc);
  EXPECT_EQ(150, rc);
}

TEST(IoDevQueuedBuffer, NonZeroMinBufferLevel) {
  struct cras_iodev iodev;
  int rc;

  ResetStubData();
  memset(&iodev, 0, sizeof(iodev));
  iodev.direction = CRAS_STREAM_OUTPUT;
  iodev.frames_queued = frames_queued;
  iodev.min_buffer_level = 100;
  iodev.buffer_size = 200;
  fr_queued = 180;

  rc = cras_iodev_frames_queued(&iodev);
  EXPECT_EQ(80, rc);
  rc = cras_iodev_buffer_avail(&iodev, rc);
  EXPECT_EQ(20, rc);

  /* When fr_queued < min_buffer_level*/
  fr_queued = 80;
  rc = cras_iodev_frames_queued(&iodev);
  EXPECT_EQ(0, rc);
  rc = cras_iodev_buffer_avail(&iodev, rc);
  EXPECT_EQ(100, rc);
}

static void update_active_node(struct cras_iodev *iodev,
                               unsigned node_idx,
                               unsigned dev_enabled)
{
}

static void dev_set_volume(struct cras_iodev *iodev)
{
}

static void dev_set_capture_gain(struct cras_iodev *iodev)
{
}

TEST(IoNodePlug, PlugUnplugNode) {
  struct cras_iodev iodev;
  struct cras_ionode ionode, ionode2;

  memset(&iodev, 0, sizeof(iodev));
  memset(&ionode, 0, sizeof(ionode));
  memset(&ionode2, 0, sizeof(ionode2));
  iodev.direction = CRAS_STREAM_INPUT;
  iodev.update_active_node = update_active_node;
  ionode.dev = &iodev;
  cras_iodev_add_node(&iodev, &ionode);
  ionode2.dev = &iodev;
  cras_iodev_add_node(&iodev, &ionode2);
  cras_iodev_set_active_node(&iodev, &ionode);
  ResetStubData();
  cras_iodev_set_node_attr(&ionode, IONODE_ATTR_PLUGGED, 1);
  EXPECT_EQ(0, cras_iodev_list_disable_dev_called);
  cras_iodev_set_node_attr(&ionode, IONODE_ATTR_PLUGGED, 0);
  EXPECT_EQ(1, cras_iodev_list_disable_dev_called);

  /* Unplug non-active node shouldn't disable iodev. */
  cras_iodev_set_node_attr(&ionode2, IONODE_ATTR_PLUGGED, 1);
  EXPECT_EQ(1, cras_iodev_list_disable_dev_called);
  cras_iodev_set_node_attr(&ionode2, IONODE_ATTR_PLUGGED, 0);
  EXPECT_EQ(1, cras_iodev_list_disable_dev_called);
}

TEST(IoDev, AddRemoveNode) {
  struct cras_iodev iodev;
  struct cras_ionode ionode;

  memset(&iodev, 0, sizeof(iodev));
  memset(&ionode, 0, sizeof(ionode));
  ResetStubData();
  EXPECT_EQ(0, notify_nodes_changed_called);
  cras_iodev_add_node(&iodev, &ionode);
  EXPECT_EQ(1, notify_nodes_changed_called);
  cras_iodev_rm_node(&iodev, &ionode);
  EXPECT_EQ(2, notify_nodes_changed_called);
}

TEST(IoDev, SetActiveNode) {
  struct cras_iodev iodev;
  struct cras_ionode ionode;

  memset(&iodev, 0, sizeof(iodev));
  memset(&ionode, 0, sizeof(ionode));
  ResetStubData();
  EXPECT_EQ(0, notify_active_node_changed_called);
  cras_iodev_set_active_node(&iodev, &ionode);
  EXPECT_EQ(1, notify_active_node_changed_called);
}

TEST(IoDev, SetNodeVolume) {
  struct cras_iodev iodev;
  struct cras_ionode ionode;

  memset(&iodev, 0, sizeof(iodev));
  memset(&ionode, 0, sizeof(ionode));
  iodev.set_volume = dev_set_volume;
  iodev.set_capture_gain = dev_set_capture_gain;
  ionode.dev = &iodev;
  ResetStubData();
  cras_iodev_set_node_attr(&ionode, IONODE_ATTR_VOLUME, 10);
  EXPECT_EQ(1, notify_node_volume_called);
  iodev.direction = CRAS_STREAM_INPUT;
  cras_iodev_set_node_attr(&ionode, IONODE_ATTR_CAPTURE_GAIN, 10);
  EXPECT_EQ(1, notify_node_capture_gain_called);
}

TEST(IoDev, SetNodeSwapLeftRight) {
  struct cras_iodev iodev;
  struct cras_ionode ionode;

  memset(&iodev, 0, sizeof(iodev));
  memset(&ionode, 0, sizeof(ionode));
  iodev.set_swap_mode_for_node = set_swap_mode_for_node;
  ionode.dev = &iodev;
  ResetStubData();
  cras_iodev_set_node_attr(&ionode, IONODE_ATTR_SWAP_LEFT_RIGHT, 1);
  EXPECT_EQ(1, set_swap_mode_for_node_called);
  EXPECT_EQ(1, set_swap_mode_for_node_enable);
  EXPECT_EQ(1, ionode.left_right_swapped);
  EXPECT_EQ(1, notify_node_left_right_swapped_called);
  cras_iodev_set_node_attr(&ionode, IONODE_ATTR_SWAP_LEFT_RIGHT, 0);
  EXPECT_EQ(2, set_swap_mode_for_node_called);
  EXPECT_EQ(0, set_swap_mode_for_node_enable);
  EXPECT_EQ(0, ionode.left_right_swapped);
  EXPECT_EQ(2, notify_node_left_right_swapped_called);
}


// Test software volume changes for default output.
TEST(IoDev, SoftwareVolume) {
  struct cras_iodev iodev;
  struct cras_ionode ionode;

  memset(&iodev, 0, sizeof(iodev));
  memset(&ionode, 0, sizeof(ionode));
  ResetStubData();

  iodev.nodes = &ionode;
  iodev.active_node = &ionode;
  iodev.active_node->dev = &iodev;

  iodev.active_node->volume = 100;
  iodev.software_volume_needed = 0;


  softvol_scalers[80] = 0.5;
  softvol_scalers[70] = 0.3;

  // Check that system volume changes software volume if needed.
  cras_system_get_volume_return = 80;
  // system_volume - 100 + node_volume = 80 - 100 + 100 = 80
  EXPECT_FLOAT_EQ(0.5, cras_iodev_get_software_volume_scaler(&iodev));

  // Check that node volume changes software volume if needed.
  iodev.active_node->volume = 90;
  // system_volume - 100 + node_volume = 80 - 100 + 90 = 70
  EXPECT_FLOAT_EQ(0.3, cras_iodev_get_software_volume_scaler(&iodev));
}

// Test software gain scaler.
TEST(IoDev, SoftwareGain) {
  struct cras_iodev iodev;
  struct cras_ionode ionode;

  memset(&iodev, 0, sizeof(iodev));
  memset(&ionode, 0, sizeof(ionode));
  ResetStubData();

  iodev.nodes = &ionode;
  iodev.active_node = &ionode;
  iodev.active_node->dev = &iodev;

  ionode.capture_gain= 400;
  ionode.software_volume_needed = 1;
  ionode.max_software_gain = 3000;

  // Check that system volume changes software volume if needed.
  cras_system_get_capture_gain_ret_value = 2000;
  // system_gain + node_gain = 2000 + 400  = 2400
  // 2400 dBm is 15.848931
  EXPECT_FLOAT_EQ(15.848931, cras_iodev_get_software_gain_scaler(&iodev));
  EXPECT_FLOAT_EQ(3000, cras_iodev_maximum_software_gain(&iodev));

  // Software gain scaler should be 1.0 if software gain is not needed.
  ionode.software_volume_needed = 0;
  EXPECT_FLOAT_EQ(1.0, cras_iodev_get_software_gain_scaler(&iodev));
  EXPECT_FLOAT_EQ(0, cras_iodev_maximum_software_gain(&iodev));
}

static int open_dev(struct cras_iodev *iodev) {
  iodev->buffer_size = iodev_buffer_size;
  return 0;
}

TEST(IoDev, OpenDevice) {
  struct cras_iodev iodev;

  memset(&iodev, 0, sizeof(iodev));
  iodev.open_dev = open_dev;
  ResetStubData();

  // Test that this flag is cleared after cras_iodev_open.
  iodev.no_stream_state = 1;

  iodev_buffer_size = 1024;
  cras_iodev_open(&iodev, 240);
  EXPECT_EQ(0, iodev.max_cb_level);
  EXPECT_EQ(240, iodev.min_cb_level);
  EXPECT_EQ(0, iodev.no_stream_state);
}

TEST(IoDev, AddRmStream) {
  struct cras_iodev iodev;
  struct cras_rstream rstream1, rstream2;
  struct dev_stream stream1, stream2;

  memset(&iodev, 0, sizeof(iodev));
  iodev.open_dev = open_dev;
  rstream1.cb_threshold = 800;
  stream1.stream = &rstream1;
  rstream2.cb_threshold = 400;
  stream2.stream = &rstream2;
  ResetStubData();

  iodev_buffer_size = 1024;
  cras_iodev_open(&iodev, rstream1.cb_threshold);
  EXPECT_EQ(0, iodev.max_cb_level);
  EXPECT_EQ(512, iodev.min_cb_level);

  /* min_cb_level should not exceed half the buffer size. */
  cras_iodev_add_stream(&iodev, &stream1);
  EXPECT_EQ(800, iodev.max_cb_level);
  EXPECT_EQ(512, iodev.min_cb_level);

  cras_iodev_add_stream(&iodev, &stream2);
  EXPECT_EQ(800, iodev.max_cb_level);
  EXPECT_EQ(400, iodev.min_cb_level);

  cras_iodev_rm_stream(&iodev, &rstream1);
  EXPECT_EQ(400, iodev.max_cb_level);
  EXPECT_EQ(400, iodev.min_cb_level);

  /* When all streams are removed, keep the last min_cb_level for draining. */
  cras_iodev_rm_stream(&iodev, &rstream2);
  EXPECT_EQ(0, iodev.max_cb_level);
  EXPECT_EQ(400, iodev.min_cb_level);
}

static int start(const struct cras_iodev *iodev) {
  return 0;
}

static int is_open(const struct cras_iodev *iodev) {
  return is_open_ret;
}

TEST(IoDev, StartDevice) {
  struct cras_iodev iodev;

  memset(&iodev, 0, sizeof(iodev));
  iodev.is_open = is_open;
  iodev.start = start;

  // Start fails if it is called in closed state.
  is_open_ret = 0;
  ASSERT_TRUE(cras_iodev_start(&iodev));

  // Start can only be called in open state.
  is_open_ret = 1;
  EXPECT_EQ(0, cras_iodev_start(&iodev));
}

TEST(IoDev, FillZeros) {
  struct cras_iodev iodev;
  struct cras_audio_format fmt;
  unsigned int frames = 50;
  int16_t *zeros;
  int rc;

  ResetStubData();

  memset(&iodev, 0, sizeof(iodev));
  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 48000;
  fmt.num_channels = 2;
  iodev.ext_format = &fmt;
  iodev.get_buffer = get_buffer;
  iodev.put_buffer = put_buffer;

  iodev.direction = CRAS_STREAM_INPUT;
  rc = cras_iodev_fill_odev_zeros(&iodev, frames);
  EXPECT_EQ(-EINVAL, rc);

  iodev.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_fill_odev_zeros(&iodev, frames);

  EXPECT_EQ(0, rc);
  EXPECT_EQ(frames, put_buffer_nframes);
  zeros = (int16_t *)calloc(frames * 2, sizeof(*zeros));
  rc = memcmp(audio_buffer, zeros, frames * 2 * 2);
  free(zeros);
  EXPECT_EQ(0, rc);
}

TEST(IoDev, NoStreamPlaybackRunning) {
  struct cras_iodev iodev;
  struct cras_audio_format fmt;
  unsigned int hw_level = 50;
  unsigned int min_cb_level = 240;
  unsigned int zeros_to_fill;
  int16_t *zeros;
  int rc;

  memset(&iodev, 0, sizeof(iodev));

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 48000;
  fmt.num_channels = 2;
  iodev.ext_format = &fmt;
  iodev.min_cb_level = min_cb_level;
  iodev.get_buffer = get_buffer;
  iodev.put_buffer = put_buffer;
  iodev.dev_running = dev_running;
  iodev.frames_queued = frames_queued;
  iodev.min_buffer_level = 0;
  iodev.direction = CRAS_STREAM_OUTPUT;
  iodev.buffer_size = BUFFER_SIZE;
  iodev.no_stream = no_stream;

  ResetStubData();

  // Device is running. hw_level is less than target.
  // Need to fill to callback level * 2;
  dev_running_ret_value = 1;
  fr_queued = hw_level;
  zeros_to_fill = min_cb_level * 2 - hw_level;

  rc = cras_iodev_no_stream_playback(&iodev, 1);

  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, no_stream_called);
  EXPECT_EQ(1, no_stream_enable);
  EXPECT_EQ(1, iodev.no_stream_state);
  EXPECT_EQ(zeros_to_fill, put_buffer_nframes);
  zeros = (int16_t *)calloc(zeros_to_fill * 2, sizeof(*zeros));
  EXPECT_EQ(0, memcmp(audio_buffer, zeros, zeros_to_fill * 2 * 2));
  free(zeros);

  ResetStubData();

  // Device is running. hw_level is not less than target.
  // No need to fill zeros.
  dev_running_ret_value = 1;
  hw_level = min_cb_level * 2;
  fr_queued = hw_level;
  zeros_to_fill = 0;

  rc = cras_iodev_no_stream_playback(&iodev, 1);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, no_stream_called);
  EXPECT_EQ(1, no_stream_enable);
  EXPECT_EQ(1, iodev.no_stream_state);
  EXPECT_EQ(zeros_to_fill, put_buffer_nframes);

  ResetStubData();

  // Device is running. Resume normal playback.
  dev_running_ret_value = 1;
  rc = cras_iodev_no_stream_playback(&iodev, 0);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, no_stream_called);
  EXPECT_EQ(0, no_stream_enable);
  EXPECT_EQ(0, iodev.no_stream_state);
}

TEST(IoDev, OutputDeviceShouldWake) {
  struct cras_iodev iodev;
  int rc;

  memset(&iodev, 0, sizeof(iodev));
  iodev.dev_running = dev_running;

  ResetStubData();

  // Device is not running. No need to wake for this device.
  dev_running_ret_value = 0;
  rc = cras_iodev_odev_should_wake(&iodev);
  EXPECT_EQ(0, rc);

  // Device is running. Need to wake for this device.
  dev_running_ret_value = 1;
  rc = cras_iodev_odev_should_wake(&iodev);
  EXPECT_EQ(1, rc);

  // Device is running. Device has output_should_wake ops.
  iodev.output_should_wake = output_should_wake;
  output_should_wake_ret = 0;
  rc = cras_iodev_odev_should_wake(&iodev);
  EXPECT_EQ(0, rc);

  // Device is running. Device has output_should_wake ops.
  output_should_wake_ret = 1;
  rc = cras_iodev_odev_should_wake(&iodev);
  EXPECT_EQ(1, rc);

  // Ignore input device.
  iodev.direction = CRAS_STREAM_INPUT;
  rc = cras_iodev_odev_should_wake(&iodev);
  EXPECT_EQ(0, rc);
}

TEST(IoDev, FramesToPlayInSleep) {
  struct cras_iodev iodev;
  unsigned int min_cb_level = 240, hw_level;
  unsigned int got_hw_level, got_frames;

  memset(&iodev, 0, sizeof(iodev));
  iodev.dev_running = dev_running;
  iodev.frames_queued = frames_queued;
  iodev.min_buffer_level = 0;
  iodev.direction = CRAS_STREAM_OUTPUT;
  iodev.buffer_size = BUFFER_SIZE;
  iodev.min_cb_level = min_cb_level;

  ResetStubData();

  // Device is running. There is at least one stream for this device.
  // hw_level is greater than min_cb_level.
  dev_running_ret_value = 1;
  hw_level = min_cb_level + 50;
  fr_queued = hw_level;
  iodev.streams = reinterpret_cast<struct dev_stream *>(0x1);

  got_frames = cras_iodev_frames_to_play_in_sleep(&iodev, &got_hw_level);
  EXPECT_EQ(hw_level, got_hw_level);
  EXPECT_EQ(hw_level, got_frames);

  // Device is running. There is no stream for this device.
  // hw_level is greater than min_cb_level.
  iodev.streams = NULL;

  got_frames = cras_iodev_frames_to_play_in_sleep(&iodev, &got_hw_level);
  EXPECT_EQ(hw_level, got_hw_level);
  EXPECT_EQ(hw_level - min_cb_level, got_frames);

  // Device is running. There is no stream for this device.
  // hw_level is less than min_cb_level.
  iodev.streams = NULL;
  hw_level = min_cb_level - 50;
  fr_queued = hw_level;

  got_frames = cras_iodev_frames_to_play_in_sleep(&iodev, &got_hw_level);
  EXPECT_EQ(hw_level, got_hw_level);
  EXPECT_EQ(0, got_frames);
}

extern "C" {

//  From libpthread.
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void*), void *arg) {
  return 0;
}

int pthread_join(pthread_t thread, void **value_ptr) {
  return 0;
}

// From audio_thread
struct cras_fmt_conv *audio_thread_get_global_remix_converter()
{
  return NULL;
}

// Fromt fmt_conv
void cras_channel_remix_convert(struct cras_fmt_conv *conv,
    uint8_t *in_buf,
    size_t frames)
{
}

// From buffer_share
struct buffer_share *buffer_share_create(unsigned int buf_sz) {
  return NULL;
}

void buffer_share_destroy(struct buffer_share *mix)
{
}

int buffer_share_offset_update(struct buffer_share *mix, unsigned int id,
                               unsigned int frames) {
  return 0;
}

unsigned int buffer_share_get_new_write_point(struct buffer_share *mix) {
  return 0;
}

int buffer_share_add_id(struct buffer_share *mix, unsigned int id) {
  return 0;
}

int buffer_share_rm_id(struct buffer_share *mix, unsigned int id) {
  return 0;
}

unsigned int buffer_share_id_offset(const struct buffer_share *mix,
                                    unsigned int id)
{
  return 0;
}

// From cras_system_state.
void cras_system_state_stream_added(enum CRAS_STREAM_DIRECTION direction) {
}

void cras_system_state_stream_removed(enum CRAS_STREAM_DIRECTION direction) {
}

// From cras_dsp
struct cras_dsp_context *cras_dsp_context_new(int sample_rate,
                                              const char *purpose)
{
  dsp_context_new_sample_rate = sample_rate;
  dsp_context_new_purpose = purpose;
  return cras_dsp_context_new_return;
}

void cras_dsp_context_free(struct cras_dsp_context *ctx)
{
  dsp_context_free_called++;
}

void cras_dsp_load_pipeline(struct cras_dsp_context *ctx)
{
}

void cras_dsp_set_variable(struct cras_dsp_context *ctx, const char *key,
                           const char *value)
{
}

struct pipeline *cras_dsp_get_pipeline(struct cras_dsp_context *ctx)
{
  cras_dsp_get_pipeline_called++;
  return reinterpret_cast<struct pipeline *>(cras_dsp_get_pipeline_ret);
}

void cras_dsp_put_pipeline(struct cras_dsp_context *ctx)
{
  cras_dsp_put_pipeline_called++;
}

float *cras_dsp_pipeline_get_source_buffer(struct pipeline *pipeline,
					   int index)
{
  cras_dsp_pipeline_get_source_buffer_called++;
  return cras_dsp_pipeline_source_buffer[index];
}

float *cras_dsp_pipeline_get_sink_buffer(struct pipeline *pipeline, int index)
{
  cras_dsp_pipeline_get_sink_buffer_called++;
  return cras_dsp_pipeline_sink_buffer[index];
}

int cras_dsp_pipeline_get_delay(struct pipeline *pipeline)
{
  cras_dsp_pipeline_get_delay_called++;
  return 0;
}

void cras_dsp_pipeline_apply(struct pipeline *pipeline,
			     uint8_t *buf, unsigned int frames)
{
  cras_dsp_pipeline_apply_called++;
  cras_dsp_pipeline_apply_sample_count = frames;
}

void cras_dsp_pipeline_add_statistic(struct pipeline *pipeline,
                                     const struct timespec *time_delta,
                                     int samples)
{
}

unsigned int cras_dsp_num_output_channels(const struct cras_dsp_context *ctx)
{
	return cras_dsp_num_output_channels_return;
}

unsigned int cras_dsp_num_input_channels(const struct cras_dsp_context *ctx)
{
	return cras_dsp_num_input_channels_return;
}

// From audio thread
int audio_thread_post_message(struct audio_thread *thread,
                              struct audio_thread_msg *msg) {
  return 0;
}

void cras_iodev_list_select_node(enum CRAS_STREAM_DIRECTION direction,
                                 cras_node_id_t node_id)
{
  select_node_called++;
  select_node_direction = direction;
  select_node_id = node_id;
}

int cras_iodev_list_node_selected(struct cras_ionode *node)
{
  return node == node_selected;
}

void cras_iodev_list_disable_dev(struct cras_iodev *dev)
{
  cras_iodev_list_disable_dev_called++;
}

void cras_iodev_list_notify_nodes_changed()
{
  notify_nodes_changed_called++;
}

void cras_iodev_list_notify_active_node_changed(
				enum CRAS_STREAM_DIRECTION direction)
{
  notify_active_node_changed_called++;
}

void cras_iodev_list_notify_node_volume(struct cras_ionode *node)
{
	notify_node_volume_called++;
}

void cras_iodev_list_notify_node_capture_gain(struct cras_ionode *node)
{
	notify_node_capture_gain_called++;
}

void cras_iodev_list_notify_node_left_right_swapped(struct cras_ionode *node)
{
  notify_node_left_right_swapped_called++;
}

struct cras_audio_area *cras_audio_area_create(int num_channels) {
	return NULL;
}

void cras_audio_area_destroy(struct cras_audio_area *area) {
}

void cras_audio_area_config_channels(struct cras_audio_area *area,
                                     const struct cras_audio_format *fmt) {
}

int cras_audio_format_set_channel_layout(struct cras_audio_format *format,
					 const int8_t layout[CRAS_CH_MAX])
{
  int i;
  cras_audio_format_set_channel_layout_called++;
  for (i = 0; i < CRAS_CH_MAX; i++)
    format->channel_layout[i] = layout[i];
  return 0;
}

float softvol_get_scaler(unsigned int volume_index)
{
	return softvol_scalers[volume_index];
}

size_t cras_system_get_volume() {
  return cras_system_get_volume_return;
}

long cras_system_get_capture_gain() {
  return cras_system_get_capture_gain_ret_value;
}

int cras_system_get_mute() {
  return cras_system_get_mute_return;
}

int cras_system_get_capture_mute() {
  return 0;
}

void cras_scale_buffer(snd_pcm_format_t fmt, uint8_t *buffer,
                       unsigned int count, float scaler) {
  cras_scale_buffer_fmt = fmt;
  cras_scale_buffer_scaler = scaler;
}

size_t cras_mix_mute_buffer(uint8_t *dst,
                            size_t frame_bytes,
                            size_t count) {
  cras_mix_mute_count = count;
  return count;
}

struct rate_estimator *rate_estimator_create(unsigned int rate,
                                             const struct timespec *window_size,
                                             double smooth_factor) {
  return NULL;
}

void rate_estimator_destroy(struct rate_estimator *re) {
}

void rate_estimator_add_frames(struct rate_estimator *re, int fr) {
  rate_estimator_add_frames_called++;
  rate_estimator_add_frames_num_frames = fr;
}

int rate_estimator_check(struct rate_estimator *re, int level,
                         struct timespec *now) {
  return 0;
}

void rate_estimator_reset_rate(struct rate_estimator *re, unsigned int rate) {
}

double rate_estimator_get_rate(struct rate_estimator *re) {
  return 0.0;
}

unsigned int dev_stream_cb_threshold(const struct dev_stream *dev_stream) {
  if (dev_stream->stream)
    return dev_stream->stream->cb_threshold;
  return 0;
}

}  // extern "C"
}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
