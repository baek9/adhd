/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _CRAS_ALSA_MIXER_H
#define _CRAS_ALSA_MIXER_H

#include <alsa/asoundlib.h>
#include <iniparser.h>

/* cras_alsa_mixer represents the alsa mixer interface for an alsa card.  It
 * houses the volume and mute controls as well as playback switches for
 * headphones and mic.
 */

struct mixer_control;
struct cras_alsa_mixer;
struct cras_volume_curve;
struct cras_card_config;

/* Creates a cras_alsa_mixer instance for the given alsa device.
 * Args:
 *    card_name - Name of the card to open a mixer for.  This is an alsa name of
 *      the form "hw:X" where X ranges from 0 to 31 inclusive.
 *    config - Config info for this card, can be NULL if none found.
 *    output_names_extra - An array of extra output mixer control names. The
 *      array may contain NULL entries which should be ignored.
 *    output_names_extra_size - The length of the output_names_extra array.
 *    extra_main_volume - Name of extra main volume if any.
 * Returns:
 *    A pointer to the newly created cras_alsa_mixer which must later be freed
 *    by calling cras_alsa_mixer_destroy.
 */
struct cras_alsa_mixer *cras_alsa_mixer_create(
		const char *card_name,
		const struct cras_card_config *config,
		const char *output_names_extra[],
		size_t output_names_extra_size,
		const char *extra_main_volume);

/* Destroys a cras_alsa_mixer that was returned from cras_alsa_mixer_create.
 * Args:
 *    cras_mixer - The cras_alsa_mixer pointer returned from
 *        cras_alsa_mixer_create.
 */
void cras_alsa_mixer_destroy(struct cras_alsa_mixer *cras_mixer);

/* Gets the default volume curve for this mixer.  This curve will be used if
 * there is not output-node specific curve to use.
 */
const struct cras_volume_curve *cras_alsa_mixer_default_volume_curve(
		const struct cras_alsa_mixer *mixer);

/* Sets the output volume for the device associated with this mixer.
 * Args:
 *    cras_mixer - The mixer to set the volume on.
 *    dBFS - The volume level as dB * 100.  dB is a normally a negative quantity
 *      specifying how much to attenuate.
 *    mixer_output - The mixer output to set if not all attenuation can be
 *      obtained from the main controls.  Can be null.
 */
void cras_alsa_mixer_set_dBFS(struct cras_alsa_mixer *cras_mixer,
			      long dBFS,
			      struct mixer_control *mixer_output);

/* Gets the volume range of the mixer in dB.
 * Args:
 *    cras_mixer - The mixer to get the volume range.
 */
long cras_alsa_mixer_get_dB_range(struct cras_alsa_mixer *cras_mixer);

/* Gets the volume range of the mixer output in dB.
 * Args:
 *    mixer_output - The mixer output to get the volume range.
 */
long cras_alsa_mixer_get_output_dB_range(
		struct mixer_control *mixer_output);

/* Sets the capture gain for the device associated with this mixer.
 * Args:
 *    cras_mixer - The mixer to set the volume on.
 *    dBFS - The capture gain level as dB * 100.  dB can be a positive or a
 *    negative quantity specifying how much gain or attenuation to apply.
 *    mixer_input - The specific mixer control for input node, can be null.
 */
void cras_alsa_mixer_set_capture_dBFS(struct cras_alsa_mixer *cras_mixer,
				      long dBFS,
				      struct mixer_control* mixer_input);

/* Gets the minimum allowed setting for capture gain.
 * Args:
 *    cmix - The mixer to set the capture gain on.
 *    mixer_input - The additional input mixer control, mainly specified
 *      in ucm config. Can be null.
 * Returns:
 *    The minimum allowed capture gain in dBFS * 100.
 */
long cras_alsa_mixer_get_minimum_capture_gain(
                struct cras_alsa_mixer *cmix,
		struct mixer_control *mixer_input);

/* Gets the maximum allowed setting for capture gain.
 * Args:
 *    cmix - The mixer to set the capture gain on.
 *    mixer_input - The additional input mixer control, mainly specified
 *      in ucm config. Can be null.
 * Returns:
 *    The maximum allowed capture gain in dBFS * 100.
 */
long cras_alsa_mixer_get_maximum_capture_gain(
		struct cras_alsa_mixer *cmix,
		struct mixer_control *mixer_input);

/* Sets the playback switch for the device.
 * Args:
 *    cras_mixer - Mixer to set the volume in.
 *    muted - 1 if muted, 0 if not.
 *    mixer_output - The mixer output to mute if no master mute.
 */
void cras_alsa_mixer_set_mute(struct cras_alsa_mixer *cras_mixer,
			      int muted,
			      struct mixer_control *mixer_output);

/* Sets the capture switch for the device.
 * Args:
 *    cras_mixer - Mixer to set the volume in.
 *    muted - 1 if muted, 0 if not.
 *    mixer_input - The mixer input to mute if no master mute.
 */
void cras_alsa_mixer_set_capture_mute(struct cras_alsa_mixer *cras_mixer,
				      int muted,
				      struct mixer_control *mixer_input);

/* Invokes the provided callback once for each output associated with the given
 * device number.  The callback will be provided with a reference to the control
 * that can be queried to see what the control supports.
 * Args:
 *    cras_mixer - Mixer to set the volume in.
 *    cb - Function to call for each output.
 *    cb_arg - Argument to pass to cb.
 */
typedef void (*cras_alsa_mixer_control_callback)(
		struct mixer_control *control, void *arg);
void cras_alsa_mixer_list_outputs(struct cras_alsa_mixer *cras_mixer,
				  cras_alsa_mixer_control_callback cb,
				  void *cb_arg);

/* Gets the name of a given control. */
const char *cras_alsa_mixer_get_control_name(
		const struct mixer_control *control);

/* Finds the output that matches the given string.  Used to match Jacks to Mixer
 * elements.
 * Args:
 *    cras_mixer - Mixer to search for a control.
 *    name - The name to match against the controls.
 * Returns:
 *    A pointer to the output with a mixer control that matches "name".
 */
struct mixer_control *cras_alsa_mixer_get_output_matching_name(
		const struct cras_alsa_mixer *cras_mixer,
		const char *name);

/* Finds the mixer control for that matches the control name of input control
 * name specified in ucm config.
 * Args:
 *    cras_mixer - Mixer to search for a control.
 *    control_name - Name of the control to search for.
 * Returns:
 *    A pointer the input mixer control that matches control_name.
 */
struct mixer_control *cras_alsa_mixer_get_input_matching_name(
		struct cras_alsa_mixer *cras_mixer,
		const char *control_name);

/* Sets the given output active or inactive. */
int cras_alsa_mixer_set_output_active_state(
		struct mixer_control *output,
		int active);

/* Returns a volume curve for the given output node name.  The name can be that
 * of a control or of a Jack.  Looks for an entry in the ini file (See README
 * for format), or falls back to the default volume curve if the ini file
 * doesn't specify a curve for this output. */
struct cras_volume_curve *cras_alsa_mixer_create_volume_curve_for_name(
		const struct cras_alsa_mixer *cmix,
		const char *name);

/* Returns a volume curve stored in the output control element, can be null. */
struct cras_volume_curve *cras_alsa_mixer_get_output_volume_curve(
		const struct mixer_control *control);

#endif /* _CRAS_ALSA_MIXER_H */
