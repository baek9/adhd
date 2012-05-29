/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <iniparser.h>
#include <stdio.h>
#include <syslog.h>

#include "cras_alsa_mixer.h"
#include "cras_util.h"
#include "cras_volume_curve.h"
#include "utlist.h"

/* Represents an ALSA volume control element. Each device can have several
 * volume controls in the path to the output, a list of these will be used to
 * represent that so we can used each volume control in sequence. */
struct mixer_volume_control {
	snd_mixer_elem_t *elem;
	struct mixer_volume_control *prev, *next;
};

/* Represents an ALSA control element related to a specific output such as
 * speakers or headphones.  A device can have several of these, each potentially
 * having independent volume and mute controls. */
struct mixer_output_control {
	struct cras_alsa_mixer_output properties;
	struct mixer_output_control *prev, *next;
};

/* Holds a reference to the opened mixer and the volume controls.
 * mixer - Pointer to the opened alsa mixer.
 * main_volume_controls - List of volume controls (normally 'Master' and 'PCM').
 * playback_switch - Switch used to mute the device.
 * main_capture_controls - List of capture gain controls (normally 'Capture').
 * capture_switch - Switch used to mute the capture stream.
 * volume_curve - Default volume curve that converts from an index to dBFS.
 * ini - Configuration dictionary from libiniparser.
 */
struct cras_alsa_mixer {
	snd_mixer_t *mixer;
	struct mixer_volume_control *main_volume_controls;
	struct mixer_output_control *output_controls;
	snd_mixer_elem_t *playback_switch;
	struct mixer_volume_control *main_capture_controls;
	snd_mixer_elem_t *capture_switch;
	struct cras_volume_curve *volume_curve;
	dictionary *ini;
};

/* Wrapper for snd_mixer_open and helpers.
 * Args:
 *    mixdev - Name of the device to open the mixer for.
 * Returns:
 *    pointer to opened mixer on success, NULL on failure.
 */
static snd_mixer_t *alsa_mixer_open(const char *mixdev)
{
	snd_mixer_t *mixer = NULL;
	int rc;
	rc = snd_mixer_open(&mixer, 0);
	if (rc < 0)
		return NULL;
	rc = snd_mixer_attach(mixer, mixdev);
	if (rc < 0)
		goto fail_after_open;
	rc = snd_mixer_selem_register(mixer, NULL, NULL);
	if (rc < 0)
		goto fail_after_open;
	rc = snd_mixer_load(mixer);
	if (rc < 0)
		goto fail_after_open;
	return mixer;

fail_after_open:
	snd_mixer_close(mixer);
	return NULL;
}

/* Checks if the given element's name is in the list. */
static int name_in_list(const char *name,
			const char * const list[],
			size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		if (strcmp(list[i], name) == 0)
			return 1;
	return 0;
}

/* Adds the main volume control to the list and grabs the first seen playback
 * switch to use for mute. */
static int add_main_volume_control(struct cras_alsa_mixer *cmix,
				   snd_mixer_elem_t *elem)
{
	if (snd_mixer_selem_has_playback_volume(elem)) {
		struct mixer_volume_control *c;

		c = calloc(1, sizeof(*c));
		if (c == NULL) {
			syslog(LOG_ERR, "No memory for mixer.");
			return -ENOMEM;
		}

		c->elem = elem;

		DL_APPEND(cmix->main_volume_controls, c);
	}

	/* If cmix doesn't yet have a playback switch and this is a playback
	 * switch, use it. */
	if (cmix->playback_switch == NULL &&
			snd_mixer_selem_has_playback_switch(elem))
		cmix->playback_switch = elem;

	return 0;
}

/* Adds the main capture control to the list and grabs the first seen capture
 * switch to mute input. */
static int add_main_capture_control(struct cras_alsa_mixer *cmix,
				    snd_mixer_elem_t *elem)
{
	/* TODO(dgreid) handle index != 0, map to correct input. */
	if (snd_mixer_selem_get_index(elem) > 0)
		return 0;

	if (snd_mixer_selem_has_capture_volume(elem)) {
		struct mixer_volume_control *c;

		c = calloc(1, sizeof(*c));
		if (c == NULL) {
			syslog(LOG_ERR, "No memory for control.");
			return -ENOMEM;
		}

		c->elem = elem;

		syslog(LOG_DEBUG,
		       "Add capture control %s\n",
		       snd_mixer_selem_get_name(elem));
		DL_APPEND(cmix->main_capture_controls, c);
	}

	/* If cmix doesn't yet have a capture switch and this is a capture
	 * switch, use it. */
	if (cmix->capture_switch == NULL &&
	    snd_mixer_selem_has_capture_switch(elem))
		cmix->capture_switch = elem;

	return 0;
}

/* Creates a volume curve for a new output. Consulting any card/output specific
 * configuration first, and falling back to the system default curve. See README
 * for a description of the ini file format. */
static struct cras_volume_curve *create_volume_curve_for_output(
		const struct cras_alsa_mixer *cmix,
		struct cras_alsa_mixer_output *output)
{
#define INI_KEY_LEN 31 /* 31 chars + 1 in declaration for null. */
	const char *curve_type;
	const char *output_name;
	char ini_key[INI_KEY_LEN + 1];

	output_name = snd_mixer_selem_get_name(output->elem);
	if (cmix->ini == NULL || output_name == NULL)
		return cras_volume_curve_create_default();

	snprintf(ini_key, INI_KEY_LEN, "%s:volume_curve", output_name);
	ini_key[INI_KEY_LEN] = 0;
	curve_type = iniparser_getstring(cmix->ini, ini_key, NULL);

	if (strcmp(curve_type, "simple_step") == 0) {
		int max_volume;
		int volume_step;

		snprintf(ini_key, INI_KEY_LEN, "%s:max_volume", output_name);
		ini_key[INI_KEY_LEN] = 0;
		max_volume = iniparser_getint(cmix->ini, ini_key, 0);
		snprintf(ini_key, INI_KEY_LEN, "%s:volume_step", output_name);
		ini_key[INI_KEY_LEN] = 0;
		volume_step = iniparser_getint(cmix->ini, ini_key, 300);
		return cras_volume_curve_create_simple_step(max_volume,
							    volume_step);
	} else {
		syslog(LOG_ERR, "Invalid volume curve type %s.", curve_type);
		return cras_volume_curve_create_default();
	}
}

/* Adds an output control to the list for the specified device. */
static int add_output_control(struct cras_alsa_mixer *cmix,
			      snd_mixer_elem_t *elem,
			      size_t device_index)
{
	int index; /* Index part of mixer simple element */
	struct mixer_output_control *c;

	index = snd_mixer_selem_get_index(elem);
	syslog(LOG_DEBUG, "Add output control for dev %zu: %s,%d\n",
	       device_index, snd_mixer_selem_get_name(elem), index);

	c = calloc(1, sizeof(*c));
	if (c == NULL) {
		syslog(LOG_ERR, "No memory for output control.");
		return -ENOMEM;
	}

	c->properties.elem = elem;
	c->properties.has_volume = snd_mixer_selem_has_playback_volume(elem);
	c->properties.has_mute = snd_mixer_selem_has_playback_switch(elem);
	c->properties.device_index = device_index;
	c->properties.volume_curve =
		create_volume_curve_for_output(cmix, &c->properties);
	DL_APPEND(cmix->output_controls, c);

	return 0;
}

/*
 * Exported interface.
 */

struct cras_alsa_mixer *cras_alsa_mixer_create(const char *card_name,
					       dictionary *ini)
{
	/* Names of controls for main system volume. */
	static const char * const main_volume_names[] = {
		"Master",
		"Digital",
		"PCM",
	};
	/* Names of controls for individual outputs. */
	static const char * const output_names[] = {
		"Headphone",
		"Speaker",
	};
	/* Names of controls for capture gain/attenuation and mute. */
	static const char * const main_capture_names[] = {
		"Capture",
		"Digital Capture",
	};
	snd_mixer_elem_t *elem;
	struct cras_alsa_mixer *cmix;

	cmix = calloc(1, sizeof(*cmix));
	if (cmix == NULL)
		return NULL;

	syslog(LOG_DEBUG, "Add mixer for device %s", card_name);

	cmix->mixer = alsa_mixer_open(card_name);
	if (cmix->mixer == NULL) {
		syslog(LOG_DEBUG, "Couldn't open mixer.");
		return NULL;
	}

	cmix->ini = ini;
	cmix->volume_curve = cras_volume_curve_create_default();

	/* Find volume and mute controls. */
	for(elem = snd_mixer_first_elem(cmix->mixer);
			elem != NULL; elem = snd_mixer_elem_next(elem)) {
		const char *name;

		name = snd_mixer_selem_get_name(elem);
		if (name == NULL)
			continue;

		if (name_in_list(name, main_volume_names,
				 ARRAY_SIZE(main_volume_names))) {
			if (add_main_volume_control(cmix, elem) != 0) {
				cras_alsa_mixer_destroy(cmix);
				return NULL;
			}
		} else if (name_in_list(name, main_capture_names,
					ARRAY_SIZE(main_capture_names))) {
			if (add_main_capture_control(cmix, elem) != 0) {
				cras_alsa_mixer_destroy(cmix);
				return NULL;
			}
		} else if (name_in_list(name, output_names,
					ARRAY_SIZE(output_names))) {
			/* TODO(dgreid) - determine device index. */
			if (add_output_control(cmix, elem, 0) != 0) {
				cras_alsa_mixer_destroy(cmix);
				return NULL;
			}
		}
	}

	return cmix;
}

void cras_alsa_mixer_destroy(struct cras_alsa_mixer *cras_mixer)
{
	struct mixer_volume_control *c, *tmp;
	struct mixer_output_control *output, *output_tmp;

	assert(cras_mixer);

	DL_FOREACH_SAFE(cras_mixer->main_volume_controls, c, tmp) {
		DL_DELETE(cras_mixer->main_volume_controls, c);
		free(c);
	}
	DL_FOREACH_SAFE(cras_mixer->main_capture_controls, c, tmp) {
		DL_DELETE(cras_mixer->main_capture_controls, c);
		free(c);
	}
	DL_FOREACH_SAFE(cras_mixer->output_controls, output, output_tmp) {
		DL_DELETE(cras_mixer->output_controls, output);
		free(output);
	}
	snd_mixer_close(cras_mixer->mixer);
	free(cras_mixer);
}

const struct cras_volume_curve *cras_alsa_mixer_default_volume_curve(
		const struct cras_alsa_mixer *cras_mixer)
{
	assert(cras_mixer);
	assert(cras_mixer->volume_curve);
	return cras_mixer->volume_curve;
}

void cras_alsa_mixer_set_dBFS(struct cras_alsa_mixer *cras_mixer,
			      long dBFS)
{
	struct mixer_volume_control *c;
	long to_set;

	assert(cras_mixer);

	/* dBFS is normally < 0 to specify the attenuation. */
	to_set = dBFS;
	/* Go through all the controls, set the volume level for each,
	 * taking the value closest but greater than the desired volume.  If the
	 * entire volume can't be set on the current control, move on to the
	 * next one until we have the exact volume, or gotten as close as we
	 * can. Once all of the volume is set the rest of the controls should be
	 * set to 0dB. */
	DL_FOREACH(cras_mixer->main_volume_controls, c) {
		long actual_dB;
		snd_mixer_selem_set_playback_dB_all(c->elem, to_set, 1);
		snd_mixer_selem_get_playback_dB(c->elem,
						SND_MIXER_SCHN_FRONT_LEFT,
						&actual_dB);
		to_set -= actual_dB;
	}
}

void cras_alsa_mixer_set_capture_dBFS(struct cras_alsa_mixer *cras_mixer,
				      long dBFS)
{
	struct mixer_volume_control *c;
	long to_set;

	assert(cras_mixer);
	to_set = dBFS;
	/* Go through all the controls, set the gain for each, taking the value
	 * closest but greater than the desired gain.  If the entire gain can't
	 * be set on the current control, move on to the next one until we have
	 * the exact gain, or gotten as close as we can. Once all of the gain is
	 * set the rest of the controls should be set to 0dB. */
	DL_FOREACH(cras_mixer->main_capture_controls, c) {
		long actual_dB;
		snd_mixer_selem_set_capture_dB_all(c->elem, to_set, 1);
		snd_mixer_selem_get_capture_dB(c->elem,
					       SND_MIXER_SCHN_FRONT_LEFT,
					       &actual_dB);
		to_set -= actual_dB;
	}
	assert(cras_mixer);
}

long cras_alsa_mixer_get_minimum_capture_gain(struct cras_alsa_mixer *cmix)
{
	struct mixer_volume_control *c;
	long min, max, total_min;

	assert(cmix);
	total_min = 0;
	DL_FOREACH(cmix->main_capture_controls, c)
		if (snd_mixer_selem_get_capture_dB_range(c->elem,
							 &min, &max) == 0)
			total_min += min;

	return total_min;
}

long cras_alsa_mixer_get_maximum_capture_gain(struct cras_alsa_mixer *cmix)
{
	struct mixer_volume_control *c;
	long min, max, total_max;

	assert(cmix);
	total_max = 0;
	DL_FOREACH(cmix->main_capture_controls, c)
		if (snd_mixer_selem_get_capture_dB_range(c->elem,
							 &min, &max) == 0)
			total_max += max;

	return total_max;
}

void cras_alsa_mixer_set_mute(struct cras_alsa_mixer *cras_mixer, int muted)
{
	assert(cras_mixer);
	if (cras_mixer->playback_switch == NULL)
		return;
	snd_mixer_selem_set_playback_switch_all(cras_mixer->playback_switch,
						!muted);
}

void cras_alsa_mixer_set_capture_mute(struct cras_alsa_mixer *cras_mixer,
				      int muted)
{
	assert(cras_mixer);
	if (cras_mixer->capture_switch == NULL)
		return;
	snd_mixer_selem_set_capture_switch_all(cras_mixer->capture_switch,
					       !muted);
}

void cras_alsa_mixer_list_outputs(struct cras_alsa_mixer *cras_mixer,
				  size_t device_index,
				  cras_alsa_mixer_output_callback cb,
				  void *cb_arg)
{
	assert(cras_mixer);
	struct mixer_output_control *output;

	DL_FOREACH(cras_mixer->output_controls, output)
		if (output->properties.device_index == device_index)
			cb(&output->properties, cb_arg);
}

int cras_alsa_mixer_set_output_active_state(
		struct cras_alsa_mixer_output *output,
		int active)
{
	assert(output);
	if (!output->has_mute)
		return -1;
	return snd_mixer_selem_set_playback_switch_all(output->elem, active);
}
