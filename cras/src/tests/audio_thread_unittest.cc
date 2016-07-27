// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern "C" {
#include "audio_thread.c"
#include "cras_audio_area.h"
}

#include <gtest/gtest.h>

#define MAX_CALLS 10
#define BUFFER_SIZE 8192
#define FIRST_CB_LEVEL 480

static unsigned int cras_rstream_dev_offset_called;
static unsigned int cras_rstream_dev_offset_ret[MAX_CALLS];
static const struct cras_rstream *cras_rstream_dev_offset_rstream_val[MAX_CALLS];
static unsigned int cras_rstream_dev_offset_dev_id_val[MAX_CALLS];
static unsigned int cras_rstream_dev_offset_update_called;
static const struct cras_rstream *cras_rstream_dev_offset_update_rstream_val[MAX_CALLS];
static unsigned int cras_rstream_dev_offset_update_frames_val[MAX_CALLS];
static unsigned int cras_rstream_dev_offset_update_dev_id_val[MAX_CALLS];
static int cras_iodev_start_called;
static int cras_iodev_all_streams_written_ret;
static struct cras_audio_area *cras_iodev_get_output_buffer_area;
static int cras_iodev_put_output_buffer_called;
static unsigned int cras_iodev_put_output_buffer_nframes;
static unsigned int cras_iodev_fill_odev_zeros_frames;
static unsigned int cras_iodev_no_stream_playback_called;
static unsigned int cras_iodev_no_stream_playback_enable;
static int dev_stream_playback_frames_ret;

void ResetGlobalStubData() {
  cras_rstream_dev_offset_called = 0;
  cras_rstream_dev_offset_update_called = 0;
  for (int i = 0; i < MAX_CALLS; i++) {
    cras_rstream_dev_offset_ret[i] = 0;
    cras_rstream_dev_offset_rstream_val[i] = NULL;
    cras_rstream_dev_offset_dev_id_val[i] = 0;
    cras_rstream_dev_offset_update_rstream_val[i] = NULL;
    cras_rstream_dev_offset_update_frames_val[i] = 0;
    cras_rstream_dev_offset_update_dev_id_val[i] = 0;
  }
  cras_iodev_start_called = 0;
  cras_iodev_all_streams_written_ret = 0;
  if (cras_iodev_get_output_buffer_area) {
    free(cras_iodev_get_output_buffer_area);
    cras_iodev_get_output_buffer_area = NULL;
  }
  cras_iodev_put_output_buffer_called = 0;
  cras_iodev_put_output_buffer_nframes = 0;
  cras_iodev_fill_odev_zeros_frames = 0;
  cras_iodev_no_stream_playback_called = 0;
  cras_iodev_no_stream_playback_enable = 0;
  dev_stream_playback_frames_ret = 0;
}

// Test streams and devices manipulation.
class StreamDeviceSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      thread_ = audio_thread_create();
      ResetStubData();
    }

    virtual void TearDown() {
    }

    virtual void SetupDevice(cras_iodev *iodev,
                             enum CRAS_STREAM_DIRECTION direction) {
      memset(iodev, 0, sizeof(*iodev));
      iodev->info.idx = ++device_id_;
      iodev->direction = direction;
      iodev->open_dev = open_dev;
      iodev->close_dev = close_dev;
      iodev->dev_running = dev_running;
      iodev->is_open = is_open;
      iodev->frames_queued = frames_queued;
      iodev->delay_frames = delay_frames;
      iodev->get_buffer = get_buffer;
      iodev->put_buffer = put_buffer;
      iodev->flush_buffer = flush_buffer;
      iodev->ext_format = &format_;
      iodev->buffer_size = BUFFER_SIZE;
      iodev->min_cb_level = FIRST_CB_LEVEL;
    }

    void ResetStubData() {
      device_id_ = 0;
      open_dev_called_ = 0;
      close_dev_called_ = 0;
      dev_running_called_ = 0;
      dev_running_ret = 0;
      is_open_ = 0;
      frames_queued_ = 0;
      delay_frames_ = 0;
      audio_buffer_size_ = 0;
    }

    void SetupRstream(struct cras_rstream *rstream,
                      enum CRAS_STREAM_DIRECTION direction) {
      memset(rstream, 0, sizeof(*rstream));
      rstream->direction = direction;
      rstream->cb_threshold = 480;
      rstream->shm.area = static_cast<cras_audio_shm_area*>(
          calloc(1, sizeof(rstream->shm.area)));
    }

    void TearDownRstream(struct cras_rstream *rstream) {
      free(rstream->shm.area);
    }

    void SetupPinnedStream(struct cras_rstream *rstream,
                           enum CRAS_STREAM_DIRECTION direction,
                           cras_iodev* pin_to_dev) {
      SetupRstream(rstream, direction);
      rstream->is_pinned = 1;
      rstream->pinned_dev_idx = pin_to_dev->info.idx;
    }

    static int open_dev(cras_iodev* iodev) {
      open_dev_called_++;
      return 0;
    }

    static int close_dev(cras_iodev* iodev) {
      close_dev_called_++;
      return 0;
    }

    static int dev_running(const cras_iodev* iodev) {
      dev_running_called_++;
      return dev_running_ret;
    }

    static int is_open(const cras_iodev* iodev) {
      return is_open_;
    }

    static int frames_queued(const cras_iodev* iodev) {
      return frames_queued_;
    }

    static int delay_frames(const cras_iodev* iodev) {
      return delay_frames_;
    }

    static int get_buffer(cras_iodev* iodev,
                          struct cras_audio_area** area,
                          unsigned int* num) {
      size_t sz = sizeof(*area_) + sizeof(struct cras_channel_area) * 2;

      if (audio_buffer_size_ < *num)
        *num = audio_buffer_size_;

      area_ = (cras_audio_area*)calloc(1, sz);
      area_->frames = *num;
      area_->num_channels = 2;
      area_->channels[0].buf = audio_buffer_;
      channel_area_set_channel(&area_->channels[0], CRAS_CH_FL);
      area_->channels[0].step_bytes = 4;
      area_->channels[1].buf = audio_buffer_ + 2;
      channel_area_set_channel(&area_->channels[1], CRAS_CH_FR);
      area_->channels[1].step_bytes = 4;

      *area = area_;
      return 0;
    }

    static int put_buffer(cras_iodev* iodev, unsigned int num) {
      free(area_);
      return 0;
    }

    static int flush_buffer(cras_iodev *iodev) {
      return 0;
    }

    int device_id_;
    struct audio_thread *thread_;

    static int open_dev_called_;
    static int close_dev_called_;
    static int dev_running_called_;
    static int is_open_;
    static int frames_queued_;
    static int delay_frames_;
    static struct cras_audio_format format_;
    static struct cras_audio_area *area_;
    static uint8_t audio_buffer_[BUFFER_SIZE];
    static unsigned int audio_buffer_size_;
    static int dev_running_ret;
};

int StreamDeviceSuite::open_dev_called_;
int StreamDeviceSuite::close_dev_called_;
int StreamDeviceSuite::dev_running_called_;
int StreamDeviceSuite::dev_running_ret;
int StreamDeviceSuite::is_open_;
int StreamDeviceSuite::frames_queued_;
int StreamDeviceSuite::delay_frames_;
struct cras_audio_format StreamDeviceSuite::format_;
struct cras_audio_area *StreamDeviceSuite::area_;
uint8_t StreamDeviceSuite::audio_buffer_[8192];
unsigned int StreamDeviceSuite::audio_buffer_size_;

TEST_F(StreamDeviceSuite, AddRemoveOpenOutputDevice) {
  struct cras_iodev iodev;
  struct open_dev *adev;

  SetupDevice(&iodev, CRAS_STREAM_OUTPUT);

  // Check the newly added device is open.
  thread_add_open_dev(thread_, &iodev);
  adev = thread_->open_devs[CRAS_STREAM_OUTPUT];
  EXPECT_EQ(adev->dev, &iodev);

  thread_rm_open_dev(thread_, &iodev);
  adev = thread_->open_devs[CRAS_STREAM_OUTPUT];
  EXPECT_EQ(NULL, adev);
}

TEST_F(StreamDeviceSuite, AddRemoveOpenInputDevice) {
  struct cras_iodev iodev;
  struct open_dev *adev;

  SetupDevice(&iodev, CRAS_STREAM_INPUT);

  // Check the newly added device is open.
  thread_add_open_dev(thread_, &iodev);
  adev = thread_->open_devs[CRAS_STREAM_INPUT];
  EXPECT_EQ(adev->dev, &iodev);

  thread_rm_open_dev(thread_, &iodev);
  adev = thread_->open_devs[CRAS_STREAM_INPUT];
  EXPECT_EQ(NULL, adev);
}

TEST_F(StreamDeviceSuite, AddRemoveMultipleOpenDevices) {
  struct cras_iodev odev;
  struct cras_iodev odev2;
  struct cras_iodev odev3;
  struct cras_iodev idev;
  struct cras_iodev idev2;
  struct cras_iodev idev3;
  struct open_dev *adev;

  SetupDevice(&odev, CRAS_STREAM_OUTPUT);
  SetupDevice(&odev2, CRAS_STREAM_OUTPUT);
  SetupDevice(&odev3, CRAS_STREAM_OUTPUT);
  SetupDevice(&idev, CRAS_STREAM_INPUT);
  SetupDevice(&idev2, CRAS_STREAM_INPUT);
  SetupDevice(&idev3, CRAS_STREAM_INPUT);

  // Add 2 open devices and check both are open.
  thread_add_open_dev(thread_, &odev);
  thread_add_open_dev(thread_, &odev2);
  adev = thread_->open_devs[CRAS_STREAM_OUTPUT];
  EXPECT_EQ(adev->dev, &odev);
  EXPECT_EQ(adev->next->dev, &odev2);

  // Remove first open device and check the second one is still open.
  thread_rm_open_dev(thread_, &odev);
  adev = thread_->open_devs[CRAS_STREAM_OUTPUT];
  EXPECT_EQ(adev->dev, &odev2);

  // Add another open device and check both are open.
  thread_add_open_dev(thread_, &odev3);
  adev = thread_->open_devs[CRAS_STREAM_OUTPUT];
  EXPECT_EQ(adev->dev, &odev2);
  EXPECT_EQ(adev->next->dev, &odev3);

  // Add 2 open devices and check both are open.
  thread_add_open_dev(thread_, &idev);
  thread_add_open_dev(thread_, &idev2);
  adev = thread_->open_devs[CRAS_STREAM_INPUT];
  EXPECT_EQ(adev->dev, &idev);
  EXPECT_EQ(adev->next->dev, &idev2);

  // Remove first open device and check the second one is still open.
  thread_rm_open_dev(thread_, &idev);
  adev = thread_->open_devs[CRAS_STREAM_INPUT];
  EXPECT_EQ(adev->dev, &idev2);

  // Add and remove another open device and check still open.
  thread_add_open_dev(thread_, &idev3);
  thread_rm_open_dev(thread_, &idev3);
  adev = thread_->open_devs[CRAS_STREAM_INPUT];
  EXPECT_EQ(adev->dev, &idev2);
}

TEST_F(StreamDeviceSuite, MultipleInputStreamsCopyFirstStreamOffset) {
  struct cras_iodev iodev;
  struct cras_iodev iodev2;
  struct cras_iodev *iodevs[] = {&iodev, &iodev2};
  struct cras_rstream rstream;
  struct cras_rstream rstream2;
  struct cras_rstream rstream3;

  SetupDevice(&iodev, CRAS_STREAM_INPUT);
  SetupDevice(&iodev2, CRAS_STREAM_INPUT);
  SetupRstream(&rstream, CRAS_STREAM_INPUT);
  SetupRstream(&rstream2, CRAS_STREAM_INPUT);
  SetupRstream(&rstream3, CRAS_STREAM_INPUT);

  thread_add_open_dev(thread_, &iodev);
  thread_add_open_dev(thread_, &iodev2);

  thread_add_stream(thread_, &rstream, iodevs, 2);
  EXPECT_NE((void *)NULL, iodev.streams);
  EXPECT_NE((void *)NULL, iodev2.streams);

  EXPECT_EQ(0, cras_rstream_dev_offset_called);
  EXPECT_EQ(0, cras_rstream_dev_offset_update_called);

  // Fake offset for rstream
  cras_rstream_dev_offset_ret[0] = 30;
  cras_rstream_dev_offset_ret[1] = 0;

  thread_add_stream(thread_, &rstream2, iodevs, 2);
  EXPECT_EQ(2, cras_rstream_dev_offset_called);
  EXPECT_EQ(&rstream, cras_rstream_dev_offset_rstream_val[0]);
  EXPECT_EQ(iodev.info.idx, cras_rstream_dev_offset_dev_id_val[0]);
  EXPECT_EQ(&rstream, cras_rstream_dev_offset_rstream_val[1]);
  EXPECT_EQ(iodev2.info.idx, cras_rstream_dev_offset_dev_id_val[1]);

  EXPECT_EQ(2, cras_rstream_dev_offset_update_called);
  EXPECT_EQ(&rstream2, cras_rstream_dev_offset_update_rstream_val[0]);
  EXPECT_EQ(30, cras_rstream_dev_offset_update_frames_val[0]);
  EXPECT_EQ(&rstream2, cras_rstream_dev_offset_update_rstream_val[1]);
  EXPECT_EQ(0, cras_rstream_dev_offset_update_frames_val[1]);

  TearDownRstream(&rstream);
  TearDownRstream(&rstream2);
  TearDownRstream(&rstream3);
}

TEST_F(StreamDeviceSuite, AddRemoveMultipleStreamsOnMultipleDevices) {
  struct cras_iodev iodev, *piodev = &iodev;
  struct cras_iodev iodev2, *piodev2 = &iodev2;
  struct cras_rstream rstream;
  struct cras_rstream rstream2;
  struct cras_rstream rstream3;
  struct dev_stream *dev_stream;

  SetupDevice(&iodev, CRAS_STREAM_OUTPUT);
  SetupDevice(&iodev2, CRAS_STREAM_OUTPUT);
  SetupRstream(&rstream, CRAS_STREAM_OUTPUT);
  SetupRstream(&rstream2, CRAS_STREAM_OUTPUT);
  SetupRstream(&rstream3, CRAS_STREAM_OUTPUT);

  // Add first device as open and check 2 streams can be added.
  thread_add_open_dev(thread_, &iodev);
  thread_add_stream(thread_, &rstream, &piodev, 1);
  dev_stream = iodev.streams;
  EXPECT_EQ(dev_stream->stream, &rstream);
  thread_add_stream(thread_, &rstream2, &piodev, 1);
  EXPECT_EQ(dev_stream->next->stream, &rstream2);

  // Add second device as open and check no streams are copied over.
  thread_add_open_dev(thread_, &iodev2);
  dev_stream = iodev2.streams;
  EXPECT_EQ(NULL, dev_stream);
  // Also check the 2 streams on first device remain intact.
  dev_stream = iodev.streams;
  EXPECT_EQ(dev_stream->stream, &rstream);
  EXPECT_EQ(dev_stream->next->stream, &rstream2);

  // Add a stream to the second dev and check it isn't also added to the first.
  thread_add_stream(thread_, &rstream3, &piodev2, 1);
  dev_stream = iodev.streams;
  EXPECT_EQ(dev_stream->stream, &rstream);
  EXPECT_EQ(dev_stream->next->stream, &rstream2);
  EXPECT_EQ(NULL, dev_stream->next->next);
  dev_stream = iodev2.streams;
  EXPECT_EQ(&rstream3, dev_stream->stream);
  EXPECT_EQ(NULL, dev_stream->next);

  // Remove first device from open and streams on second device remain
  // intact.
  thread_rm_open_dev(thread_, &iodev);
  dev_stream = iodev2.streams;
  EXPECT_EQ(&rstream3, dev_stream->stream);
  EXPECT_EQ(NULL, dev_stream->next);

  // Remove 2 streams, check the streams are removed from both open devices.
  thread_remove_stream(thread_, &rstream, &iodev);
  thread_remove_stream(thread_, &rstream3, &iodev2);
  dev_stream = iodev2.streams;
  EXPECT_EQ(NULL, dev_stream);

  // Remove open devices and check stream is on fallback device.
  thread_rm_open_dev(thread_, &iodev2);

  // Add open device, again check it is empty of streams.
  thread_add_open_dev(thread_, &iodev);
  dev_stream = iodev.streams;
  EXPECT_EQ(NULL, dev_stream);

  TearDownRstream(&rstream);
  TearDownRstream(&rstream2);
  TearDownRstream(&rstream3);
}

TEST_F(StreamDeviceSuite, WriteOutputSamplesNoStream) {
  struct cras_iodev iodev;
  struct open_dev *adev;

  ResetGlobalStubData();

  SetupDevice(&iodev, CRAS_STREAM_OUTPUT);

  // Setup the output buffer for device.
  cras_iodev_get_output_buffer_area = cras_audio_area_create(2);

  // Add the device.
  thread_add_open_dev(thread_, &iodev);
  adev = thread_->open_devs[CRAS_STREAM_OUTPUT];

  // Assume device is started.
  dev_running_ret = 1;

  // cras_iodev should handle no stream playback.
  write_output_samples(thread_, adev);
  EXPECT_EQ(1, cras_iodev_no_stream_playback_called);
  EXPECT_EQ(1, cras_iodev_no_stream_playback_enable);

  thread_rm_open_dev(thread_, &iodev);
}

TEST_F(StreamDeviceSuite, WriteOutputSamplesLeaveNoStream) {
  struct cras_iodev iodev, *piodev = &iodev;
  struct open_dev *adev;
  struct cras_rstream rstream;

  ResetGlobalStubData();

  SetupDevice(&iodev, CRAS_STREAM_OUTPUT);

  // Setup the output buffer for device.
  cras_iodev_get_output_buffer_area = cras_audio_area_create(2);

  // Add the device.
  thread_add_open_dev(thread_, &iodev);
  adev = thread_->open_devs[CRAS_STREAM_OUTPUT];

  // Assume device in no stream state.
  iodev.no_stream_state = 1;

  SetupRstream(&rstream, CRAS_STREAM_OUTPUT);
  thread_add_stream(thread_, &rstream, &piodev, 1);

  // Assume sample from stream is not ready.
  dev_stream_playback_frames_ret = 0;

  // cras_iodev should NOT leave no stream state;
  write_output_samples(thread_, adev);
  EXPECT_EQ(1, cras_iodev_no_stream_playback_called);
  EXPECT_EQ(1, cras_iodev_no_stream_playback_enable);

  // Assume sample from stream is ready.
  dev_stream_playback_frames_ret = 1;

  // cras_iodev should leave no stream state;
  write_output_samples(thread_, adev);
  EXPECT_EQ(2, cras_iodev_no_stream_playback_called);
  EXPECT_EQ(0, cras_iodev_no_stream_playback_enable);

  thread_rm_open_dev(thread_, &iodev);
}

TEST_F(StreamDeviceSuite, WriteOutputSamplesWithStream) {
  struct cras_iodev iodev, *piodev = &iodev;
  struct open_dev *adev;
  struct cras_rstream rstream;

  ResetGlobalStubData();

  SetupDevice(&iodev, CRAS_STREAM_OUTPUT);
  SetupRstream(&rstream, CRAS_STREAM_OUTPUT);

  // Setup the output buffer for device.
  cras_iodev_get_output_buffer_area = cras_audio_area_create(2);

  // Add the device and add the stream.
  thread_add_open_dev(thread_, &iodev);
  adev = thread_->open_devs[CRAS_STREAM_OUTPUT];
  thread_add_stream(thread_, &rstream, &piodev, 1);

  // Assume there is no sample written in this cycle.
  cras_iodev_all_streams_written_ret = 0;
  write_output_samples(thread_, adev);
  EXPECT_EQ(0, cras_iodev_start_called);

  // Assume sample from streams are ready.
  dev_stream_playback_frames_ret = 100;
  // Assume there are 100 samples from streams written in this cycle.
  cras_iodev_all_streams_written_ret = 100;
  write_output_samples(thread_, adev);
  EXPECT_EQ(1, cras_iodev_start_called);
  // Fill min_cb_level of zeros before samples from streams.
  EXPECT_EQ(iodev.min_cb_level, cras_iodev_fill_odev_zeros_frames);
  EXPECT_EQ(cras_iodev_all_streams_written_ret,
            cras_iodev_put_output_buffer_nframes);

  thread_rm_open_dev(thread_, &iodev);
  TearDownRstream(&rstream);
}

TEST_F(StreamDeviceSuite, WriteOutputSamplesUnderrun) {
  struct cras_iodev iodev, *piodev = &iodev;
  struct open_dev *adev;
  struct cras_rstream rstream;

  ResetGlobalStubData();

  SetupDevice(&iodev, CRAS_STREAM_OUTPUT);
  SetupRstream(&rstream, CRAS_STREAM_OUTPUT);

  // Setup the output buffer for device.
  cras_iodev_get_output_buffer_area = cras_audio_area_create(2);

  // Add the device and add the stream.
  thread_add_open_dev(thread_, &iodev);
  adev = thread_->open_devs[CRAS_STREAM_OUTPUT];
  thread_add_stream(thread_, &rstream, &piodev, 1);

  // Assume device is running and there is an underrun. There is no frame
  // queued and there is no sample written in this cycle.
  // Audio thread should fill one cb level of zeros.
  dev_running_ret = 1;
  frames_queued_ = 0;
  cras_iodev_all_streams_written_ret = 0;
  write_output_samples(thread_, adev);
  EXPECT_EQ(FIRST_CB_LEVEL, cras_iodev_fill_odev_zeros_frames);

  thread_rm_open_dev(thread_, &iodev);
  TearDownRstream(&rstream);
}

TEST(AUdioThreadStreams, DrainStream) {
  struct cras_rstream rstream;
  struct cras_audio_shm_area shm_area;
  struct audio_thread thread;

  memset(&rstream, 0, sizeof(rstream));
  memset(&shm_area, 0, sizeof(shm_area));
  rstream.shm.config.frame_bytes = 4;
  shm_area.config.frame_bytes = 4;
  shm_area.config.used_size = 4096 * 4;
  rstream.shm.config.used_size = 4096 * 4;
  rstream.shm.area = &shm_area;
  rstream.format.frame_rate = 48000;
  rstream.direction = CRAS_STREAM_OUTPUT;

  shm_area.write_offset[0] = 1 * 4;
  EXPECT_EQ(1, thread_drain_stream_ms_remaining(&thread, &rstream));

  shm_area.write_offset[0] = 479 * 4;
  EXPECT_EQ(10, thread_drain_stream_ms_remaining(&thread, &rstream));

  shm_area.write_offset[0] = 0;
  EXPECT_EQ(0, thread_drain_stream_ms_remaining(&thread, &rstream));

  rstream.direction = CRAS_STREAM_INPUT;
  shm_area.write_offset[0] = 479 * 4;
  EXPECT_EQ(0, thread_drain_stream_ms_remaining(&thread, &rstream));
}


extern "C" {

int cras_iodev_add_stream(struct cras_iodev *iodev, struct dev_stream *stream)
{
  DL_APPEND(iodev->streams, stream);
  return 0;
}

unsigned int cras_iodev_all_streams_written(struct cras_iodev *iodev)
{
  return cras_iodev_all_streams_written_ret;
}

int cras_iodev_close(struct cras_iodev *iodev)
{
  return 0;
}

void cras_iodev_free_format(struct cras_iodev *iodev)
{
  return;
}

double cras_iodev_get_est_rate_ratio(const struct cras_iodev *iodev)
{
  return 1.0;
}

unsigned int cras_iodev_max_stream_offset(const struct cras_iodev *iodev)
{
  return 0;
}

int cras_iodev_open(struct cras_iodev *iodev, unsigned int cb_level)
{
  return 0;
}

int cras_iodev_put_buffer(struct cras_iodev *iodev, unsigned int nframes)
{
  return 0;
}

struct dev_stream *cras_iodev_rm_stream(struct cras_iodev *iodev,
                                        const struct cras_rstream *stream)
{
  struct dev_stream *out;
  DL_FOREACH(iodev->streams, out) {
    if (out->stream == stream) {
      DL_DELETE(iodev->streams, out);
      return out;
    }
  }
  return NULL;
}

int cras_iodev_set_format(struct cras_iodev *iodev,
                          const struct cras_audio_format *fmt)
{
  return 0;
}

unsigned int cras_iodev_stream_offset(struct cras_iodev *iodev,
                                      struct dev_stream *stream)
{
  return 0;
}

int dev_stream_attached_devs(const struct dev_stream *dev_stream)
{
  return 1;
}

void cras_iodev_stream_written(struct cras_iodev *iodev,
                               struct dev_stream *stream,
                               unsigned int nwritten)
{
}

int cras_iodev_update_rate(struct cras_iodev *iodev, unsigned int level)
{
  return 0;
}

int cras_iodev_put_input_buffer(struct cras_iodev *iodev, unsigned int nframes)
{
  return 0;
}

int cras_iodev_put_output_buffer(struct cras_iodev *iodev, uint8_t *frames,
				 unsigned int nframes)
{
  cras_iodev_put_output_buffer_called++;
  cras_iodev_put_output_buffer_nframes = nframes;
  return 0;
}

int cras_iodev_get_input_buffer(struct cras_iodev *iodev,
				struct cras_audio_area **area,
				unsigned *frames)
{
  return 0;
}

int cras_iodev_get_output_buffer(struct cras_iodev *iodev,
				 struct cras_audio_area **area,
				 unsigned *frames)
{
  *area = cras_iodev_get_output_buffer_area;
  return 0;
}

int cras_iodev_get_dsp_delay(const struct cras_iodev *iodev)
{
  return 0;
}

void cras_fmt_conv_destroy(struct cras_fmt_conv *conv)
{
}

struct cras_fmt_conv *cras_channel_remix_conv_create(
    unsigned int num_channels,
    const float *coefficient)
{
  return NULL;
}

void cras_rstream_dev_attach(struct cras_rstream *rstream,
                             unsigned int dev_id,
                             void *dev_ptr)
{
}

void cras_rstream_dev_detach(struct cras_rstream *rstream, unsigned int dev_id)
{
}

void cras_rstream_destroy(struct cras_rstream *stream)
{
}

void cras_rstream_dev_offset_update(struct cras_rstream *rstream,
				    unsigned int frames,
				    unsigned int dev_id)
{
  int i = cras_rstream_dev_offset_update_called;
  if (i < MAX_CALLS)  {
    cras_rstream_dev_offset_update_rstream_val[i] = rstream;
    cras_rstream_dev_offset_update_frames_val[i] = frames;
    cras_rstream_dev_offset_update_dev_id_val[i] = dev_id;
    cras_rstream_dev_offset_update_called++;
  }
}

unsigned int cras_rstream_dev_offset(const struct cras_rstream *rstream,
				     unsigned int dev_id)
{
  int i = cras_rstream_dev_offset_called;
  if (i < MAX_CALLS) {
    cras_rstream_dev_offset_rstream_val[i] = rstream;
    cras_rstream_dev_offset_dev_id_val[i] = dev_id;
    cras_rstream_dev_offset_called++;
    return cras_rstream_dev_offset_ret[i];
  }
  return 0;
}

void cras_rstream_record_fetch_interval(struct cras_rstream *rstream,
    const struct timespec *now)
{
}

int cras_set_rt_scheduling(int rt_lim)
{
  return 0;
}

int cras_set_thread_priority(int priority)
{
  return 0;
}

void cras_system_rm_select_fd(int fd)
{
}

unsigned int dev_stream_capture(struct dev_stream *dev_stream,
                                const struct cras_audio_area *area,
                                unsigned int area_offset,
                                float software_gain_scaler)
{
  return 0;
}

unsigned int dev_stream_capture_avail(const struct dev_stream *dev_stream)
{
  return 0;

}
unsigned int dev_stream_cb_threshold(const struct dev_stream *dev_stream)
{
  return 0;
}

int dev_stream_capture_update_rstream(struct dev_stream *dev_stream)
{
  return 0;
}

struct dev_stream *dev_stream_create(struct cras_rstream *stream,
                                     unsigned int dev_id,
                                     const struct cras_audio_format *dev_fmt,
                                     void *dev_ptr, struct timespec *cb_ts)
{
  struct dev_stream *out = static_cast<dev_stream*>(calloc(1, sizeof(*out)));
  out->stream = stream;
  return out;
}

void dev_stream_destroy(struct dev_stream *dev_stream)
{
  free(dev_stream);
}

int dev_stream_mix(struct dev_stream *dev_stream,
		   const struct cras_audio_format *fmt,
                   uint8_t *dst,
                   unsigned int num_to_write)
{
  return num_to_write;
}

int dev_stream_playback_frames(const struct dev_stream *dev_stream)
{
  return dev_stream_playback_frames_ret;
}

int dev_stream_playback_update_rstream(struct dev_stream *dev_stream)
{
  return 0;
}

int dev_stream_poll_stream_fd(const struct dev_stream *dev_stream)
{
  return dev_stream->stream->fd;
}

int dev_stream_can_fetch(struct dev_stream *dev_stream)
{
  return 1;
}

int dev_stream_request_playback_samples(struct dev_stream *dev_stream,
                                        const struct timespec *now)
{
  return 0;
}

void dev_stream_set_delay(const struct dev_stream *dev_stream,
                          unsigned int delay_frames)
{
}

void dev_stream_set_dev_rate(struct dev_stream *dev_stream,
                             unsigned int dev_rate,
                             double dev_rate_ratio,
                             double master_rate_ratio,
                             int coarse_rate_adjust)
{
}

void dev_stream_update_frames(const struct dev_stream *dev_stream)
{
}

int cras_iodev_frames_queued(struct cras_iodev *iodev)
{
	return iodev->frames_queued(iodev);
}

int cras_iodev_buffer_avail(struct cras_iodev *iodev, unsigned hw_level)
{
  return iodev->buffer_size - iodev->frames_queued(iodev);
}

int cras_iodev_start(struct cras_iodev *iodev)
{
  cras_iodev_start_called = 1;
  return 0;
}

int cras_iodev_fill_odev_zeros(struct cras_iodev *odev, unsigned int frames)
{
  cras_iodev_fill_odev_zeros_frames = frames;
  return 0;
}

int cras_iodev_no_stream_playback(struct cras_iodev *odev, int enable)
{
  cras_iodev_no_stream_playback_called++;
  cras_iodev_no_stream_playback_enable = enable;
  return 0;
}

int cras_server_metrics_longest_fetch_delay(int delay_msec)
{
  return 0;
}

float cras_iodev_get_software_gain_scaler(const struct cras_iodev *iodev)
{
  return 1.0f;
}

unsigned int cras_iodev_frames_to_play_in_sleep(struct cras_iodev *odev,
                                                unsigned int *hw_level)
{
  return 0;
}

int cras_iodev_odev_should_wake(const struct cras_iodev *odev)
{
  return 1;
}

struct cras_audio_area *cras_audio_area_create(int num_channels)
{
  struct cras_audio_area *area;
  size_t sz;

  sz = sizeof(*area) + num_channels * sizeof(struct cras_channel_area);
  area = (cras_audio_area *)calloc(1, sz);
  area->num_channels = num_channels;
  area->channels[0].buf = (uint8_t*)calloc(1, BUFFER_SIZE * 2 * num_channels);

  return area;
}

}  // extern "C"

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
