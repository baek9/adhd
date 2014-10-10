/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdlib.h>
#include <sys/param.h>

#include "cras_audio_area.h"
#include "cras_audio_format.h"

struct cras_audio_area *cras_audio_area_create(int num_channels)
{
	struct cras_audio_area *area;
	size_t sz;

	sz = sizeof(*area) + num_channels * sizeof(struct cras_channel_area);
	area = calloc(1, sz);
	area->num_channels = num_channels;

	return area;
}

unsigned int cras_audio_area_copy(const struct cras_audio_area *dst,
				  unsigned int dst_offset,
				  unsigned int dst_format_bytes,
				  const struct cras_audio_area *src,
				  unsigned int src_offset,
				  unsigned int skip_zero)
{
	unsigned int src_idx, dst_idx;
	unsigned int i;
	unsigned int ncopy;
	uint8_t *schan, *dchan;

	ncopy = MIN(src->frames - src_offset, dst->frames - dst_offset);

	/* TODO(dgreid) - make it so this isn't needed, can copy first stream of
	 * each channel. */
	if (!skip_zero)
		memset(dst->channels[0].buf +
				dst_offset * dst->channels[0].step_bytes, 0,
		       ncopy * dst_format_bytes);

	/* TODO(dgreid) - this replaces a memcpy, it needs to be way faster. */
	for (src_idx = 0; src_idx < src->num_channels; src_idx++) {

		for (dst_idx = 0; dst_idx < dst->num_channels; dst_idx++) {
			if (!(src->channels[src_idx].ch_set &
			      dst->channels[dst_idx].ch_set))
				continue;

			/* TODO(dgreid) - this assumes s16le. */
			schan = src->channels[src_idx].buf +
				src_offset * src->channels[dst_idx].step_bytes;
			dchan = dst->channels[dst_idx].buf +
				dst_offset * dst->channels[dst_idx].step_bytes;

			for (i = 0; i < ncopy; i++) {
				int32_t sum;
				sum = *(int16_t *)dchan + *(int16_t *)schan;
				if (sum > 0x7fff)
					sum = 0x7fff;
				else if (sum < -0x8000)
					sum = -0x8000;
				*(int16_t*)dchan = sum;
				dchan += dst->channels[dst_idx].step_bytes;
				schan += src->channels[src_idx].step_bytes;
			}
		}
	}

	return ncopy;
}

void cras_audio_area_destroy(struct cras_audio_area *area)
{
	free(area);
}

void cras_audio_area_config_channels(struct cras_audio_area *area,
				     const struct cras_audio_format *fmt)
{
	unsigned int i, ch;

	/* For mono, config the channel type to match both front
	 * left and front right.
	 * TODO(hychao): add more mapping when we have like {FL, FC}
	 * for mono + kb mic.
	 */
	if ((fmt->num_channels == 1) &&
	    ((fmt->channel_layout[CRAS_CH_FC] == 0) ||
	     (fmt->channel_layout[CRAS_CH_FL] == 0))) {
		channel_area_set_channel(area->channels, CRAS_CH_FL);
		channel_area_set_channel(area->channels, CRAS_CH_FR);
		return;
	}

	for (i = 0; i < fmt->num_channels; i++) {
		area->channels[i].ch_set = 0;
		for (ch = 0; ch < CRAS_CH_MAX; ch++)
			if (fmt->channel_layout[ch] == i)
				channel_area_set_channel(&area->channels[i], ch);
	}

}

void cras_audio_area_config_buf_pointers(struct cras_audio_area *area,
					 const struct cras_audio_format *fmt,
					 uint8_t *base_buffer)
{
	int i;
	const int sample_size = snd_pcm_format_physical_width(fmt->format) / 8;

	/* TODO(dgreid) - assuming interleaved audio here for now. */
	for (i = 0 ; i < area->num_channels; i++) {
		area->channels[i].step_bytes = cras_get_format_bytes(fmt);
		area->channels[i].buf = base_buffer + i * sample_size;
	}
}
