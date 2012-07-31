// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <iniparser.h>
#include <stdio.h>

extern "C" {
#include "cras_alsa_card.h"
#include "cras_alsa_io.h"
#include "cras_alsa_mixer.h"
#include "cras_types.h"
#include "cras_util.h"
}

namespace {

static size_t cras_alsa_mixer_create_called;
static struct cras_alsa_mixer *cras_alsa_mixer_create_return;
static size_t cras_alsa_mixer_destroy_called;
static size_t cras_alsa_iodev_create_called;
static struct cras_iodev *cras_alsa_iodev_create_return;
static size_t iodev_create_auto_route_index;
static size_t iodev_create_auto_route_size;
static int *iodev_create_auto_route;
static size_t iodev_create_priority_index;
static size_t iodev_create_priority_size;
static int *iodev_create_priority;
static size_t cras_alsa_iodev_destroy_called;
static struct cras_iodev *cras_alsa_iodev_destroy_arg;
static size_t snd_ctl_open_called;
static size_t snd_ctl_open_return;
static size_t snd_ctl_close_called;
static size_t snd_ctl_close_return;
static size_t snd_ctl_pcm_next_device_called;
static int *snd_ctl_pcm_next_device_set_devs;
static size_t snd_ctl_pcm_next_device_set_devs_size;
static size_t snd_ctl_pcm_next_device_set_devs_index;
static size_t snd_ctl_pcm_info_called;
static int *snd_ctl_pcm_info_rets;
static size_t snd_ctl_pcm_info_rets_size;
static size_t snd_ctl_pcm_info_rets_index;
static size_t snd_ctl_card_info_called;
static int snd_ctl_card_info_ret;
static size_t iniparser_freedict_called;
static size_t iniparser_load_called;
static size_t fake_priority;

static void ResetStubData() {
  cras_alsa_mixer_create_called = 0;
  cras_alsa_mixer_create_return = reinterpret_cast<struct cras_alsa_mixer *>(1);
  cras_alsa_mixer_destroy_called = 0;
  cras_alsa_iodev_create_called = 0;
  iodev_create_auto_route_index = 0;
  iodev_create_priority_index = 0;
  cras_alsa_iodev_create_return = reinterpret_cast<struct cras_iodev *>(2);
  cras_alsa_iodev_destroy_called = 0;
  snd_ctl_open_called = 0;
  snd_ctl_open_return = 0;
  snd_ctl_close_called = 0;
  snd_ctl_close_return = 0;
  snd_ctl_pcm_next_device_called = 0;
  snd_ctl_pcm_next_device_set_devs_size = 0;
  snd_ctl_pcm_next_device_set_devs_index = 0;
  snd_ctl_pcm_info_called = 0;
  snd_ctl_pcm_info_rets_size = 0;
  snd_ctl_pcm_info_rets_index = 0;
  snd_ctl_card_info_called = 0;
  snd_ctl_card_info_ret = 0;
  iniparser_freedict_called = 0;
  iniparser_load_called = 0;
  fake_priority = 200;
}

TEST(AlsaCard, CreateFailInvalidCard) {
  struct cras_alsa_card *c;
  cras_alsa_card_info card_info;

  ResetStubData();
  card_info.card_type = ALSA_CARD_TYPE_INTERNAL;
  card_info.card_index = 55;
  card_info.priority = fake_priority;
  c = cras_alsa_card_create(&card_info);
  EXPECT_EQ(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(snd_ctl_close_called, snd_ctl_open_called);
  EXPECT_EQ(cras_alsa_mixer_create_called, cras_alsa_mixer_destroy_called);
}

TEST(AlsaCard, CreateFailMixerInit) {
  struct cras_alsa_card *c;
  cras_alsa_card_info card_info;

  ResetStubData();
  card_info.card_type = ALSA_CARD_TYPE_INTERNAL;
  card_info.card_index = 0;
  card_info.priority = fake_priority;
  cras_alsa_mixer_create_return = static_cast<struct cras_alsa_mixer *>(NULL);
  c = cras_alsa_card_create(&card_info);
  EXPECT_EQ(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(snd_ctl_close_called, snd_ctl_open_called);
  EXPECT_EQ(1, cras_alsa_mixer_create_called);
  EXPECT_EQ(0, cras_alsa_mixer_destroy_called);
}

TEST(AlsaCard, CreateFailCtlOpen) {
  struct cras_alsa_card *c;
  cras_alsa_card_info card_info;

  ResetStubData();
  card_info.card_type = ALSA_CARD_TYPE_INTERNAL;
  card_info.card_index = 0;
  card_info.priority = fake_priority;
  snd_ctl_open_return = -1;
  c = cras_alsa_card_create(&card_info);
  EXPECT_EQ(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(1, snd_ctl_open_called);
  EXPECT_EQ(0, snd_ctl_close_called);
  EXPECT_EQ(cras_alsa_mixer_create_called, cras_alsa_mixer_destroy_called);
  EXPECT_EQ(iniparser_load_called, iniparser_freedict_called);
}

TEST(AlsaCard, CreateFailCtlCardInfo) {
  struct cras_alsa_card *c;
  cras_alsa_card_info card_info;

  ResetStubData();
  card_info.card_type = ALSA_CARD_TYPE_INTERNAL;
  card_info.card_index = 0;
  card_info.priority = fake_priority;
  snd_ctl_card_info_ret = -1;
  c = cras_alsa_card_create(&card_info);
  EXPECT_EQ(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(1, snd_ctl_open_called);
  EXPECT_EQ(1, snd_ctl_close_called);
  EXPECT_EQ(cras_alsa_mixer_create_called, cras_alsa_mixer_destroy_called);
  EXPECT_EQ(iniparser_load_called, iniparser_freedict_called);
}

TEST(AlsaCard, CreateNoDevices) {
  struct cras_alsa_card *c;
  cras_alsa_card_info card_info;

  ResetStubData();
  card_info.card_type = ALSA_CARD_TYPE_INTERNAL;
  card_info.card_index = 1;
  card_info.priority = fake_priority;
  c = cras_alsa_card_create(&card_info);
  EXPECT_NE(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(snd_ctl_close_called, snd_ctl_open_called);
  EXPECT_EQ(1, snd_ctl_pcm_next_device_called);
  EXPECT_EQ(0, cras_alsa_iodev_create_called);
  EXPECT_EQ(1, cras_alsa_card_get_index(c));

  cras_alsa_card_destroy(c);
  EXPECT_EQ(0, cras_alsa_iodev_destroy_called);
  EXPECT_EQ(cras_alsa_mixer_create_called, cras_alsa_mixer_destroy_called);
  EXPECT_EQ(iniparser_load_called, iniparser_freedict_called);
}

TEST(AlsaCard, CreateOneOutput) {
  struct cras_alsa_card *c;
  int dev_nums[] = {0};
  int info_rets[] = {0, -1};
  cras_alsa_card_info card_info;

  ResetStubData();
  snd_ctl_pcm_next_device_set_devs_size = ARRAY_SIZE(dev_nums);
  snd_ctl_pcm_next_device_set_devs = dev_nums;
  snd_ctl_pcm_info_rets_size = ARRAY_SIZE(info_rets);
  snd_ctl_pcm_info_rets = info_rets;
  card_info.card_type = ALSA_CARD_TYPE_INTERNAL;
  card_info.card_index = 0;
  card_info.priority = fake_priority;
  c = cras_alsa_card_create(&card_info);
  EXPECT_NE(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(snd_ctl_close_called, snd_ctl_open_called);
  EXPECT_EQ(2, snd_ctl_pcm_next_device_called);
  EXPECT_EQ(1, cras_alsa_iodev_create_called);
  EXPECT_EQ(1, snd_ctl_card_info_called);

  cras_alsa_card_destroy(c);
  EXPECT_EQ(1, cras_alsa_iodev_destroy_called);
  EXPECT_EQ(cras_alsa_iodev_create_return, cras_alsa_iodev_destroy_arg);
  EXPECT_EQ(cras_alsa_mixer_create_called, cras_alsa_mixer_destroy_called);
  EXPECT_EQ(iniparser_load_called, iniparser_freedict_called);
}

TEST(AlsaCard, CreateTwoOutputs) {
  struct cras_alsa_card *c;
  int dev_nums[] = {0, 3};
  int info_rets[] = {0, -1, 0};
  int auto_route_vals[2];
  int priority_vals[2];
  cras_alsa_card_info card_info;

  ResetStubData();
  snd_ctl_pcm_next_device_set_devs_size = ARRAY_SIZE(dev_nums);
  snd_ctl_pcm_next_device_set_devs = dev_nums;
  snd_ctl_pcm_info_rets_size = ARRAY_SIZE(info_rets);
  snd_ctl_pcm_info_rets = info_rets;
  iodev_create_auto_route = auto_route_vals;
  iodev_create_auto_route_size = ARRAY_SIZE(auto_route_vals);
  iodev_create_priority = priority_vals;
  iodev_create_priority_size = ARRAY_SIZE(priority_vals);
  card_info.card_type = ALSA_CARD_TYPE_INTERNAL;
  card_info.card_index = 0;
  card_info.priority = fake_priority;
  c = cras_alsa_card_create(&card_info);
  EXPECT_NE(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(snd_ctl_close_called, snd_ctl_open_called);
  EXPECT_EQ(3, snd_ctl_pcm_next_device_called);
  EXPECT_EQ(2, cras_alsa_iodev_create_called);
  EXPECT_EQ(1, snd_ctl_card_info_called);
  EXPECT_EQ(1, auto_route_vals[0]);
  EXPECT_EQ(0, auto_route_vals[1]);
  EXPECT_EQ(fake_priority, priority_vals[0]);
  EXPECT_EQ(fake_priority - 1, priority_vals[1]);

  cras_alsa_card_destroy(c);
  EXPECT_EQ(2, cras_alsa_iodev_destroy_called);
  EXPECT_EQ(cras_alsa_iodev_create_return, cras_alsa_iodev_destroy_arg);
  EXPECT_EQ(cras_alsa_mixer_create_called, cras_alsa_mixer_destroy_called);
  EXPECT_EQ(iniparser_load_called, iniparser_freedict_called);
}

TEST(AlsaCard, CreateOneInput) {
  struct cras_alsa_card *c;
  int dev_nums[] = {0};
  int info_rets[] = {-1, 0};
  cras_alsa_card_info card_info;

  ResetStubData();
  snd_ctl_pcm_next_device_set_devs_size = ARRAY_SIZE(dev_nums);
  snd_ctl_pcm_next_device_set_devs = dev_nums;
  snd_ctl_pcm_info_rets_size = ARRAY_SIZE(info_rets);
  snd_ctl_pcm_info_rets = info_rets;
  card_info.card_type = ALSA_CARD_TYPE_INTERNAL;
  card_info.card_index = 0;
  card_info.priority = fake_priority;
  c = cras_alsa_card_create(&card_info);
  EXPECT_NE(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(snd_ctl_close_called, snd_ctl_open_called);
  EXPECT_EQ(2, snd_ctl_pcm_next_device_called);
  EXPECT_EQ(1, cras_alsa_iodev_create_called);

  cras_alsa_card_destroy(c);
  EXPECT_EQ(1, cras_alsa_iodev_destroy_called);
  EXPECT_EQ(cras_alsa_iodev_create_return, cras_alsa_iodev_destroy_arg);
  EXPECT_EQ(cras_alsa_mixer_create_called, cras_alsa_mixer_destroy_called);
  EXPECT_EQ(iniparser_load_called, iniparser_freedict_called);
}

TEST(AlsaCard, CreateOneInputAndOneOutput) {
  struct cras_alsa_card *c;
  int dev_nums[] = {0};
  int info_rets[] = {0, 0};
  cras_alsa_card_info card_info;

  ResetStubData();
  snd_ctl_pcm_next_device_set_devs_size = ARRAY_SIZE(dev_nums);
  snd_ctl_pcm_next_device_set_devs = dev_nums;
  snd_ctl_pcm_info_rets_size = ARRAY_SIZE(info_rets);
  snd_ctl_pcm_info_rets = info_rets;
  card_info.card_type = ALSA_CARD_TYPE_INTERNAL;
  card_info.card_index = 0;
  card_info.priority = fake_priority;
  c = cras_alsa_card_create(&card_info);
  EXPECT_NE(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(snd_ctl_close_called, snd_ctl_open_called);
  EXPECT_EQ(2, snd_ctl_pcm_next_device_called);
  EXPECT_EQ(2, cras_alsa_iodev_create_called);

  cras_alsa_card_destroy(c);
  EXPECT_EQ(2, cras_alsa_iodev_destroy_called);
  EXPECT_EQ(cras_alsa_iodev_create_return, cras_alsa_iodev_destroy_arg);
  EXPECT_EQ(cras_alsa_mixer_create_called, cras_alsa_mixer_destroy_called);
  EXPECT_EQ(iniparser_load_called, iniparser_freedict_called);
}

TEST(AlsaCard, CreateOneInputAndOneOutputTwoDevices) {
  struct cras_alsa_card *c;
  int dev_nums[] = {0, 3};
  int info_rets[] = {0, -1, -1, 0};
  int auto_route_vals[2];
  int priority_vals[2];
  cras_alsa_card_info card_info;

  ResetStubData();
  snd_ctl_pcm_next_device_set_devs_size = ARRAY_SIZE(dev_nums);
  snd_ctl_pcm_next_device_set_devs = dev_nums;
  snd_ctl_pcm_info_rets_size = ARRAY_SIZE(info_rets);
  snd_ctl_pcm_info_rets = info_rets;
  iodev_create_auto_route = auto_route_vals;
  iodev_create_auto_route_size = ARRAY_SIZE(auto_route_vals);
  iodev_create_priority = priority_vals;
  iodev_create_priority_size = ARRAY_SIZE(priority_vals);
  card_info.card_type = ALSA_CARD_TYPE_INTERNAL;
  card_info.card_index = 0;
  card_info.priority = fake_priority;
  c = cras_alsa_card_create(&card_info);
  EXPECT_NE(static_cast<struct cras_alsa_card *>(NULL), c);
  EXPECT_EQ(snd_ctl_close_called, snd_ctl_open_called);
  EXPECT_EQ(3, snd_ctl_pcm_next_device_called);
  EXPECT_EQ(2, cras_alsa_iodev_create_called);
  EXPECT_EQ(1, auto_route_vals[0]);
  EXPECT_EQ(1, auto_route_vals[1]);
  EXPECT_EQ(fake_priority, priority_vals[0]);
  EXPECT_EQ(fake_priority, priority_vals[1]);

  cras_alsa_card_destroy(c);
  EXPECT_EQ(2, cras_alsa_iodev_destroy_called);
  EXPECT_EQ(cras_alsa_iodev_create_return, cras_alsa_iodev_destroy_arg);
  EXPECT_EQ(cras_alsa_mixer_create_called, cras_alsa_mixer_destroy_called);
  EXPECT_EQ(iniparser_load_called, iniparser_freedict_called);
}

/* Stubs */

extern "C" {
struct cras_alsa_mixer *cras_alsa_mixer_create(const char *card_name) {
  cras_alsa_mixer_create_called++;
  return cras_alsa_mixer_create_return;
}
void cras_alsa_mixer_destroy(struct cras_alsa_mixer *cras_mixer) {
  cras_alsa_mixer_destroy_called++;
}

struct cras_iodev *alsa_iodev_create(size_t card_index,
				     const char *card_name,
				     size_t device_index,
				     struct cras_alsa_mixer *mixer,
				     int auto_route,
				     size_t priority,
				     enum CRAS_STREAM_DIRECTION direction) {
  cras_alsa_iodev_create_called++;
  if (iodev_create_auto_route_index < iodev_create_auto_route_size)
    iodev_create_auto_route[iodev_create_auto_route_index++] = auto_route;
  if (iodev_create_priority_index < iodev_create_priority_size)
    iodev_create_priority[iodev_create_priority_index++] = priority;
  return cras_alsa_iodev_create_return;
}
void alsa_iodev_destroy(struct cras_iodev *iodev) {
  cras_alsa_iodev_destroy_called++;
  cras_alsa_iodev_destroy_arg = iodev;
}

size_t snd_pcm_info_sizeof() {
  return 10;
}
size_t snd_ctl_card_info_sizeof() {
  return 10;
}
int snd_ctl_open(snd_ctl_t **handle, const char *name, int card) {
  snd_ctl_open_called++;
  if (snd_ctl_open_return == 0)
    *handle = reinterpret_cast<snd_ctl_t*>(0xff);
  else
    *handle = NULL;
  return snd_ctl_open_return;
}
int snd_ctl_close(snd_ctl_t *handle) {
  snd_ctl_close_called++;
  return snd_ctl_close_return;
}
int snd_ctl_pcm_next_device(snd_ctl_t *ctl, int *device) {
  snd_ctl_pcm_next_device_called++;
  if (snd_ctl_pcm_next_device_set_devs_index >=
      snd_ctl_pcm_next_device_set_devs_size) {
    *device = -1;
    return -1;
  }
  *device =
      snd_ctl_pcm_next_device_set_devs[snd_ctl_pcm_next_device_set_devs_index];
  snd_ctl_pcm_next_device_set_devs_index++;
  return 0;
}
void snd_pcm_info_set_device(snd_pcm_info_t *obj, unsigned int val) {
}
void snd_pcm_info_set_subdevice(snd_pcm_info_t *obj, unsigned int val) {
}
void snd_pcm_info_set_stream(snd_pcm_info_t *obj, snd_pcm_stream_t val) {
}
int snd_ctl_pcm_info(snd_ctl_t *ctl, snd_pcm_info_t *info) {
  int ret;
  snd_ctl_pcm_info_called++;
  if (snd_ctl_pcm_info_rets_index >=
      snd_ctl_pcm_info_rets_size) {
    return -1;
  }
  ret = snd_ctl_pcm_info_rets[snd_ctl_pcm_info_rets_index];
  snd_ctl_pcm_info_rets_index++;
  return ret;
}
int snd_ctl_card_info(snd_ctl_t *ctl, snd_ctl_card_info_t *info) {
  snd_ctl_card_info_called++;
  return snd_ctl_card_info_ret;
}
const char *snd_ctl_card_info_get_name(const snd_ctl_card_info_t *obj) {
  return "TestName";
}
const char *snd_ctl_card_info_get_id(const snd_ctl_card_info_t *obj) {
  return "TestId";
}

struct cras_card_config *cras_card_config_create(const char *config_path,
						 const char *card_name)
{
  return NULL;
}

void cras_card_config_destroy(struct cras_card_config *card_config)
{
}

struct cras_volume_curve *cras_card_config_get_volume_curve_for_control(
		const struct cras_card_config *card_config,
		const char *control_name)
{
  return NULL;
}

} /* extern "C" */

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

