/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * cras_iodev represents playback or capture devices on the system.  Each iodev
 * will attach to a thread to render or capture audio.  For playback, this
 * thread will gather audio from the streams that are attached to the device and
 * render the samples it gets to the iodev.  For capture the process is
 * reversed, the samples are pulled from the device and passed on to the
 * attached streams.
 */
#ifndef CRAS_IODEV_H_
#define CRAS_IODEV_H_

#include <stdbool.h>

#include "cras_dsp.h"
#include "cras_iodev_info.h"
#include "cras_messages.h"

struct buffer_share;
struct cras_fmt_conv;
struct cras_ramp;
struct cras_rstream;
struct cras_audio_area;
struct cras_audio_format;
struct audio_thread;
struct cras_iodev;
struct rate_estimator;

/*
 * Type of callback function to execute when loopback sender transfers audio
 * to the receiver. For example, this is called in audio thread when playback
 * samples are mixed and about to write to hardware.
 * Args:
 *    frames - Loopback audio data from sender.
 *    nframes - Number loopback audio data in frames.
 *    fmt - Format of the loopback audio data.
 *    cb_data - Pointer to the loopback receiver.
 */
typedef int (*loopback_hook_data_t)(const uint8_t *frames, unsigned int nframes,
				    const struct cras_audio_format *fmt,
				    void *cb_data);

/*
 * Type of callback function to notify loopback receiver that the loopback path
 * starts or stops.
 * Args:
 *    start - True to notify receiver that loopback starts. False to notify
 *        loopback stops.
 *    cb_data - Pointer to the loopback receiver.
 */
typedef int (*loopback_hook_control_t)(bool start, void *cb_data);

/* Callback type for an iodev event. */
typedef int (*iodev_hook_t)();

/*
 * Holds the information of a receiver of loopback audio, used to register
 * with the sender of loopback audio. A sender keeps a list of cras_loopback
 * objects representing all the receivers.
 * Members:
 *    type - Pre-dsp loopback can be used for system loopback. Post-dsp
 *        loopback can be used for echo reference.
 *    hook_data - Callback used for playback samples after mixing, before or
 *        after applying DSP depends on the value of |type|.
 *    hook_control - Callback to notify receiver that loopback starts or stops.
 *    cb_data - Pointer to the loopback receiver, will be passing to hook functions.
 */
struct cras_loopback {
	enum CRAS_LOOPBACK_TYPE type;
	loopback_hook_data_t hook_data;
	loopback_hook_control_t hook_control;
	void *cb_data;
	struct cras_loopback *prev, *next;
};

/* State of an iodev.
 * no_stream state is only supported on output device.
 * Open state is only supported for device supporting start ops.
 */
enum CRAS_IODEV_STATE {
	CRAS_IODEV_STATE_CLOSE = 0,
	CRAS_IODEV_STATE_OPEN = 1,
	CRAS_IODEV_STATE_NORMAL_RUN = 2,
	CRAS_IODEV_STATE_NO_STREAM_RUN = 3,
};

/* Holds an output/input node for this device.  An ionode is a control that
 * can be switched on and off such as headphones or speakers.
 * Members:
 *    dev - iodev which this node belongs to.
 *    idx - ionode index.
 *    plugged - true if the device is plugged.
 *    plugged_time - If plugged is true, this is the time it was attached.
 *    volume - per-node volume (0-100)
 *    capture_gain - per-node capture gain/attenuation (in 100*dBFS)
 *    left_right_swapped - If left and right output channels are swapped.
 *    type - Type displayed to the user.
 *    position - Specify where on the system this node locates.
 *    mic_positions - Whitespace-separated microphone positions using Cartesian
 *      coordinates in meters with ordering x, y, z. The string is formatted as:
 *      "x1 y1 z1 ... xn yn zn" for an n-microphone array.
 *    name - Name displayed to the user.
 *    dsp_name - The "DspName" variable specified in the ucm config.
 *    active_hotword_model - name of the currently selected hotword model.
 *    softvol_scalers - pointer to software volume scalers.
 *    software_volume_needed - For output: True if the volume range of the node
 *      is smaller than desired. For input: True if this node needs software
 *      gain.
 *    min_software_gain - The minimum software gain in 0.01 dB if needed.
 *    max_software_gain - The maximum software gain in 0.01 dB if needed.
 *    stable_id - id for node that doesn't change after unplug/plug.
 *    stable_id_new - New stable_id, it will be deprecated and be put on
 *      stable_id.
 *    is_sco_pcm - Bool to indicate whether the ionode is for SCO over PCM.
 */
struct cras_ionode {
	struct cras_iodev *dev;
	uint32_t idx;
	int plugged;
	struct timeval plugged_time;
	unsigned int volume;
	long capture_gain;
	int left_right_swapped;
	enum CRAS_NODE_TYPE type;
	enum CRAS_NODE_POSITION position;
	char mic_positions[CRAS_NODE_MIC_POS_BUFFER_SIZE];
	char name[CRAS_NODE_NAME_BUFFER_SIZE];
	const char *dsp_name;
	char active_hotword_model[CRAS_NODE_HOTWORD_MODEL_BUFFER_SIZE];
	float *softvol_scalers;
	int software_volume_needed;
	long min_software_gain;
	long max_software_gain;
	unsigned int stable_id;
	unsigned int stable_id_new;
	int is_sco_pcm;
	struct cras_ionode *prev, *next;
};

/* An input or output device, that can have audio routed to/from it.
 * set_volume - Function to call if the system volume changes.
 * set_mute - Function to call if the system mute state changes.
 * set_capture_gain - Function to call if the system capture_gain changes.
 * set_capture_mute - Function to call if the system capture mute state changes.
 * set_swap_mode_for_node - Function to call to set swap mode for the node.
 * open_dev - Opens the device.
 * configure_dev - Configures the device.
 * close_dev - Closes the device if it is open.
 * update_supported_formats - Refresh supported frame rates and channel counts.
 * frames_queued - The number of frames in the audio buffer, and fills tstamp
 *                 with the associated timestamp. The timestamp is {0, 0} when
 *                 the device hasn't started processing data (and on error).
 * delay_frames - The delay of the next sample in frames.
 * get_buffer - Returns a buffer to read/write to/from.
 * put_buffer - Marks a buffer from get_buffer as read/written.
 * flush_buffer - Flushes the buffer and return the number of frames flushed.
 * start - Starts running device. This is optionally supported on output device.
 *         If device supports this ops, device can be in CRAS_IODEV_STATE_OPEN
 *         state after being opened.
 *         If device does not support this ops, then device will be in
 *         CRAS_IODEV_STATE_NO_STREAM_RUN.
 * no_stream - (Optional) When there is no stream, we let device keep running
 *             for some time to save the time to open device for the next
 *             stream. This is the no stream state of an output device.
 *             The default action of no stream state is to fill zeros
 *             periodically. Device can implement this function to define
 *             its own optimization of entering/exiting no stream state.
 * is_free_running - (Optional) Checks if the device is in free running state.
 * output_underrun - (Optional) Handle output device underrun.
 * update_active_node - Update the active node when the selected device/node has
 *     changed.
 * update_channel_layout - Update the channel layout base on set iodev->format,
 *     expect the best available layout be filled to iodev->format.
 * set_hotword_model - Sets the hotword model to this iodev.
 * get_hotword_models - Gets a comma separated string of the list of supported
 *     hotword models of this iodev.
 * get_num_underruns - Gets number of underrun recorded so far.
 * get_num_severe_underruns - Gets number of severe underrun recorded since
 *                            iodev was created.
 * get_valid_frames - Gets number of valid frames in device which have not
 *                    played yet. Valid frames does not include zero samples
 *                    we filled under no streams state.
 * format - The audio format being rendered or captured to hardware.
 * rate_est - Rate estimator to estimate the actual device rate.
 * area - Information about how the samples are stored.
 * info - Unique identifier for this device (index and name).
 * nodes - The output or input nodes available for this device.
 * active_node - The current node being used for playback or capture.
 * direction - Input or Output.
 * supported_rates - Array of sample rates supported by device 0-terminated.
 * supported_channel_counts - List of number of channels supported by device.
 * supported_formats - List of audio formats (s16le, s32le) supported by device.
 * buffer_size - Size of the audio buffer in frames.
 * min_buffer_level - Extra frames to keep queued in addition to requested.
 * dsp_context - The context used for dsp processing on the audio data.
 * dsp_name - The "dsp_name" dsp variable specified in the ucm config.
 * echo_reference_dev - Used only for playback iodev. Pointer to the input
 *        iodev, which can be used to record what is playing out from this
 *        iodev. This will be used as the echo reference for echo cancellation.
 * is_enabled - True if this iodev is enabled, false otherwise.
 * software_volume_needed - True if volume control is not supported by hardware.
 * software_gain_scaler - Scaler value to apply to captured data. This can
 *     be different when active node changes. Configured when there's no
 *     hardware gain control.
 * streams - List of audio streams serviced by dev.
 * state - Device is in one of close, open, normal, or no_stream state defined
 *         in enum CRAS_IODEV_STATE.
 * min_cb_level - min callback level of any stream attached.
 * max_cb_level - max callback level of any stream attached.
 * highest_hw_level - The highest hardware level of the device.
 * largest_cb_level - The largest callback level of streams attached to this
 *                    device. The difference with max_cb_level is it takes all
 *                    streams into account even if they have been removed.
 * buf_state - If multiple streams are writing to this device, then this
 *     keeps track of how much each stream has written.
 * idle_timeout - The timestamp when to close the dev after being idle.
 * open_ts - The time when the device opened.
 * loopbacks - List of registered cras_loopback objects representing the
 *    receivers who wants a copy of the audio sending through this iodev.
 * pre_open_iodev_hook - Optional callback to call before iodev open.
 * post_close_iodev_hook - Optional callback to call after iodev close.
 * ext_dsp_module - External dsp module to process audio data in stream level
 *        after dsp_context.
 * reset_request_pending - The flag for pending reset request.
 * ramp - The cras_ramp struct to control ramping up/down at mute/unmute and
 *        start of playback.
 * input_streaming - For capture only. Indicate if input has started.
 * input_frames_read - The number of frames read from the device, but that
 *                     haven't been "put" yet.
 * input_dsp_offset - The number of frames in the HW buffer that have already
 *                    been processed by the input DSP.
 * input_data - Used to pass audio input data to streams with or without
 *              stream side processing.
 */
struct cras_iodev {
	void (*set_volume)(struct cras_iodev *iodev);
	void (*set_mute)(struct cras_iodev *iodev);
	void (*set_capture_gain)(struct cras_iodev *iodev);
	void (*set_capture_mute)(struct cras_iodev *iodev);
	int (*set_swap_mode_for_node)(struct cras_iodev *iodev,
				      struct cras_ionode *node, int enable);
	int (*open_dev)(struct cras_iodev *iodev);
	int (*configure_dev)(struct cras_iodev *iodev);
	int (*close_dev)(struct cras_iodev *iodev);
	int (*update_supported_formats)(struct cras_iodev *iodev);
	int (*frames_queued)(const struct cras_iodev *iodev,
			     struct timespec *tstamp);
	int (*delay_frames)(const struct cras_iodev *iodev);
	int (*get_buffer)(struct cras_iodev *iodev,
			  struct cras_audio_area **area, unsigned *frames);
	int (*put_buffer)(struct cras_iodev *iodev, unsigned nwritten);
	int (*flush_buffer)(struct cras_iodev *iodev);
	int (*start)(const struct cras_iodev *iodev);
	int (*is_free_running)(const struct cras_iodev *iodev);
	int (*output_underrun)(struct cras_iodev *iodev);
	int (*no_stream)(struct cras_iodev *iodev, int enable);
	void (*update_active_node)(struct cras_iodev *iodev, unsigned node_idx,
				   unsigned dev_enabled);
	int (*update_channel_layout)(struct cras_iodev *iodev);
	int (*set_hotword_model)(struct cras_iodev *iodev,
				 const char *model_name);
	char *(*get_hotword_models)(struct cras_iodev *iodev);
	unsigned int (*get_num_underruns)(const struct cras_iodev *iodev);
	unsigned int (*get_num_severe_underruns)(const struct cras_iodev *iodev);
	int (*get_valid_frames)(const struct cras_iodev *odev,
				struct timespec *tstamp);
	struct cras_audio_format *format;
	struct rate_estimator *rate_est;
	struct cras_audio_area *area;
	struct cras_iodev_info info;
	struct cras_ionode *nodes;
	struct cras_ionode *active_node;
	enum CRAS_STREAM_DIRECTION direction;
	size_t *supported_rates;
	size_t *supported_channel_counts;
	snd_pcm_format_t *supported_formats;
	snd_pcm_uframes_t buffer_size;
	unsigned int min_buffer_level;
	struct cras_dsp_context *dsp_context;
	const char *dsp_name;
	struct cras_iodev *echo_reference_dev;
	int is_enabled;
	int software_volume_needed;
	float software_gain_scaler;
	struct dev_stream *streams;
	enum CRAS_IODEV_STATE state;
	unsigned int min_cb_level;
	unsigned int max_cb_level;
	unsigned int highest_hw_level;
	unsigned int largest_cb_level;
	struct buffer_share *buf_state;
	struct timespec idle_timeout;
	struct timespec open_ts;
	struct cras_loopback *loopbacks;
	iodev_hook_t pre_open_iodev_hook;
	iodev_hook_t post_close_iodev_hook;
	struct ext_dsp_module *ext_dsp_module;
	int reset_request_pending;
	struct cras_ramp *ramp;
	int input_streaming;
	unsigned int input_frames_read;
	unsigned int input_dsp_offset;
	struct input_data *input_data;
	struct cras_iodev *prev, *next;
};

/*
 * Ramp request used in cras_iodev_start_ramp.
 *
 * - CRAS_IODEV_RAMP_REQUEST_UP_UNMUTE: Mute->unmute.
 *   Change device to unmute state after ramping is stared,
 *                 that is, (a) in the plot.
 *
 *                                  ____
 *                            .... /
 *                      _____/
 *                          (a)
 *
 * - CRAS_IODEV_RAMP_REQUEST_DOWN_MUTE: Unmute->mute.
 *   Change device to mute state after ramping is done, that is,
 *                 (b) in the plot.
 *
 *                      _____
 *                           \....
 *                                \____
 *                                (b)
 *
 * - CRAS_IODEV_RAMP_REQUEST_UP_START_PLAYBACK: Ramping is requested because
 *   first sample of new stream is ready, there is no need to change mute/unmute
 *   state.
 */

enum CRAS_IODEV_RAMP_REQUEST {
	CRAS_IODEV_RAMP_REQUEST_UP_UNMUTE = 0,
	CRAS_IODEV_RAMP_REQUEST_DOWN_MUTE = 1,
	CRAS_IODEV_RAMP_REQUEST_UP_START_PLAYBACK = 2,
};

/*
 * Utility functions to be used by iodev implementations.
 */

/* Sets up the iodev for the given format if possible.  If the iodev can't
 * handle the requested format, format conversion will happen in dev_stream.
 * It also allocates a dsp context for the iodev.
 * Args:
 *    iodev - the iodev you want the format for.
 *    fmt - the desired format.
 */
int cras_iodev_set_format(struct cras_iodev *iodev,
			  const struct cras_audio_format *fmt);

/* Clear the format previously set for this iodev.
 *
 * Args:
 *    iodev - the iodev you want to free the format.
 */
void cras_iodev_free_format(struct cras_iodev *iodev);

/* Initializes the audio area for this iodev.
 * Args:
 *    iodev - the iodev to init audio area
 *    num_channels - the total number of channels
 */
void cras_iodev_init_audio_area(struct cras_iodev *iodev, int num_channels);

/* Frees the audio area for this iodev.
 * Args:
 *    iodev - the iodev to free audio area
 */
void cras_iodev_free_audio_area(struct cras_iodev *iodev);

/* Free resources allocated for this iodev.
 *
 * Args:
 *    iodev - the iodev you want to free the resources for.
 */
void cras_iodev_free_resources(struct cras_iodev *iodev);

/* Fill timespec ts with the time to sleep based on the number of frames and
 * frame rate.
 * Args:
 *    frames - Number of frames in buffer..
 *    frame_rate - 44100, 48000, etc.
 *    ts - Filled with the time to sleep for.
 */
void cras_iodev_fill_time_from_frames(size_t frames, size_t frame_rate,
				      struct timespec *ts);

/* Update the "dsp_name" dsp variable. This may cause the dsp pipeline to be
 * reloaded.
 * Args:
 *    iodev - device which the state changes.
 */
void cras_iodev_update_dsp(struct cras_iodev *iodev);

/* Sets swap mode on a node using dsp. This function can be called when
 * dsp pipline is not created yet. It will take effect when dsp pipeline
 * is created later. If there is dsp pipeline, this function causes the dsp
 * pipeline to be reloaded and swap mode takes effect right away.
 * Args:
 *    iodev - device to be changed for swap mode.
 *    node - the node to be changed for swap mode.
 *    enable - 1 to enable swap mode, 0 otherwise.
 * Returns:
 *    0 on success, error code on failure.
 */
int cras_iodev_dsp_set_swap_mode_for_node(struct cras_iodev *iodev,
					  struct cras_ionode *node, int enable);

/* Handles a plug event happening on this node.
 * Args:
 *    node - ionode on which a plug event was detected.
 *    plugged - true if the device was plugged, false for unplugged.
 */
void cras_ionode_plug_event(struct cras_ionode *node, int plugged);

/* Returns true if node a is preferred over node b. */
int cras_ionode_better(struct cras_ionode *a, struct cras_ionode *b);

/* Sets the plugged state of a node. */
void cras_iodev_set_node_plugged(struct cras_ionode *node, int plugged);

/* Adds a node to the iodev's node list. */
void cras_iodev_add_node(struct cras_iodev *iodev, struct cras_ionode *node);

/* Removes a node from iodev's node list. */
void cras_iodev_rm_node(struct cras_iodev *iodev, struct cras_ionode *node);

/* Assign a node to be the active node of the device */
void cras_iodev_set_active_node(struct cras_iodev *iodev,
				struct cras_ionode *node);

/* Adjust the system volume based on the volume of the given node. */
static inline unsigned int
cras_iodev_adjust_node_volume(const struct cras_ionode *node,
			      unsigned int system_volume)
{
	unsigned int node_vol_offset = 100 - node->volume;

	if (system_volume > node_vol_offset)
		return system_volume - node_vol_offset;
	else
		return 0;
}

/* Get the volume scaler for the active node. */
static inline unsigned int
cras_iodev_adjust_active_node_volume(struct cras_iodev *iodev,
				     unsigned int system_volume)
{
	if (!iodev->active_node)
		return system_volume;

	return cras_iodev_adjust_node_volume(iodev->active_node, system_volume);
}

/* Get the gain adjusted based on system for the active node. */
static inline long
cras_iodev_adjust_active_node_gain(const struct cras_iodev *iodev,
				   long system_gain)
{
	if (!iodev->active_node)
		return system_gain;

	return iodev->active_node->capture_gain + system_gain;
}

/* Returns true if the active node of the iodev needs software volume. */
static inline int
cras_iodev_software_volume_needed(const struct cras_iodev *iodev)
{
	if (iodev->software_volume_needed)
		return 1;

	if (!iodev->active_node)
		return 0;

	return iodev->active_node->software_volume_needed;
}

/* Returns minimum software gain for the iodev.
 * Args:
 *    iodev - The device.
 * Returs:
 *    0 if software gain is not needed, or if there is no active node.
 *    Returns min_software_gain on active node if there is one. */
static inline long
cras_iodev_minimum_software_gain(const struct cras_iodev *iodev)
{
	if (!cras_iodev_software_volume_needed(iodev))
		return 0;
	if (!iodev->active_node)
		return 0;
	return iodev->active_node->min_software_gain;
}

/* Returns maximum software gain for the iodev.
 * Args:
 *    iodev - The device.
 * Returs:
 *    0 if software gain is not needed, or if there is no active node.
 *    Returns max_software_gain on active node if there is one. */
static inline long
cras_iodev_maximum_software_gain(const struct cras_iodev *iodev)
{
	if (!cras_iodev_software_volume_needed(iodev))
		return 0;
	if (!iodev->active_node)
		return 0;
	return iodev->active_node->max_software_gain;
}

/* Gets the software gain scaler should be applied on the deivce.
 * Args:
 *    iodev - The device.
 * Returns:
 *    A scaler translated from system gain and active node gain.
 *    Returns 1.0 if software gain is not needed. */
float cras_iodev_get_software_gain_scaler(const struct cras_iodev *iodev);

/* Gets the software volume scaler of the iodev. The scaler should only be
 * applied if the device needs software volume. */
float cras_iodev_get_software_volume_scaler(struct cras_iodev *iodev);

/* Indicate that a stream has been added from the device. */
int cras_iodev_add_stream(struct cras_iodev *iodev, struct dev_stream *stream);

/* Indicate that a stream is taken into consideration of device's I/O. This
 * function is for output stream only. For input stream, it is already included
 * by add_stream function. */
void cras_iodev_start_stream(struct cras_iodev *iodev,
			     struct dev_stream *stream);

/* Indicate that a stream has been removed from the device. */
struct dev_stream *cras_iodev_rm_stream(struct cras_iodev *iodev,
					const struct cras_rstream *stream);

/* Get the offset of this stream into the dev's buffer. */
unsigned int cras_iodev_stream_offset(struct cras_iodev *iodev,
				      struct dev_stream *stream);

/* Get the maximum offset of any stream into the dev's buffer. */
unsigned int cras_iodev_max_stream_offset(const struct cras_iodev *iodev);

/* Tell the device how many frames the given stream wrote. */
void cras_iodev_stream_written(struct cras_iodev *iodev,
			       struct dev_stream *stream,
			       unsigned int nwritten);

/* All streams have written what they can, update the write pointers and return
 * the amount that has been filled by all streams and can be comitted to the
 * device.
 */
unsigned int cras_iodev_all_streams_written(struct cras_iodev *iodev);

/* Return the state of an iodev. */
enum CRAS_IODEV_STATE cras_iodev_state(const struct cras_iodev *iodev);

/* Open an iodev, does setup and invokes the open_dev callback. */
int cras_iodev_open(struct cras_iodev *iodev, unsigned int cb_level,
		    const struct cras_audio_format *fmt);

/* Open an iodev, does teardown and invokes the close_dev callback. */
int cras_iodev_close(struct cras_iodev *iodev);

/* Gets the available buffer to write/read audio.*/
int cras_iodev_buffer_avail(struct cras_iodev *iodev, unsigned hw_level);

/* Marks a buffer from get_buffer as read.
 * Args:
 *    iodev - The input device.
 * Returns:
 *    Number of frames to put sucessfully. Negative error code on failure.
 */
int cras_iodev_put_input_buffer(struct cras_iodev *iodev);

/* Marks a buffer from get_buffer as written. */
int cras_iodev_put_output_buffer(struct cras_iodev *iodev, uint8_t *frames,
				 unsigned int nframes, int *is_non_empty,
				 struct cras_fmt_conv *remix_converter);

/* Returns a buffer to read from.
 * Args:
 *    iodev - The device.
 *    frames - Filled with the number of frames that can be read/written.
 */
int cras_iodev_get_input_buffer(struct cras_iodev *iodev, unsigned *frames);

/* Returns a buffer to read from.
 * Args:
 *    iodev - The device.
 *    area - Filled with a pointer to the audio to read/write.
 *    frames - Filled with the number of frames that can be read/written.
 */
int cras_iodev_get_output_buffer(struct cras_iodev *iodev,
				 struct cras_audio_area **area,
				 unsigned *frames);

/* Update the estimated sample rate of the device. */
int cras_iodev_update_rate(struct cras_iodev *iodev, unsigned int level,
			   struct timespec *level_tstamp);

/* Resets the rate estimator of the device. */
int cras_iodev_reset_rate_estimator(const struct cras_iodev *iodev);

/* Returns the rate of estimated frame rate and the claimed frame rate of
 * the device. */
double cras_iodev_get_est_rate_ratio(const struct cras_iodev *iodev);

/* Get the delay from DSP processing in frames. */
int cras_iodev_get_dsp_delay(const struct cras_iodev *iodev);

/* Returns the number of frames in the hardware buffer.
 * Args:
 *    iodev - The device.
 *    tstamp - The associated hardware time stamp.
 * Returns:
 *    Number of frames in the hardware buffer.
 *    Returns -EPIPE if there is severe underrun.
 */
int cras_iodev_frames_queued(struct cras_iodev *iodev, struct timespec *tstamp);

/* Get the delay for input/output in frames. */
static inline int cras_iodev_delay_frames(const struct cras_iodev *iodev)
{
	return iodev->delay_frames(iodev) + cras_iodev_get_dsp_delay(iodev);
}

/* Returns if input iodev has started streaming. */
static inline int cras_iodev_input_streaming(const struct cras_iodev *iodev)
{
	return iodev->input_streaming;
}

/* Returns true if the device is open. */
static inline int cras_iodev_is_open(const struct cras_iodev *iodev)
{
	if (iodev && iodev->state != CRAS_IODEV_STATE_CLOSE)
		return 1;
	return 0;
}

/* Configure iodev to exit idle mode. */
static inline void cras_iodev_exit_idle(struct cras_iodev *iodev)
{
	iodev->idle_timeout.tv_sec = 0;
}

/*
 * Sets the external dsp module for |iodev| and configures the module
 * accordingly if iodev is already open. This function should be called
 * in main thread.
 * Args:
 *    iodev - The iodev to hold the dsp module.
 *    ext - External dsp module to set to iodev. Pass NULL to release
 *        the ext_dsp_module already added to dsp pipeline.
 */
void cras_iodev_set_ext_dsp_module(struct cras_iodev *iodev,
				   struct ext_dsp_module *ext);

/* Put 'frames' worth of zero samples into odev. */
int cras_iodev_fill_odev_zeros(struct cras_iodev *odev, unsigned int frames);

/* Gets the number of frames to play when audio thread sleeps.
 * Args:
 *    iodev[in] - The device.
 *    hw_level[out] - Pointer to number of frames in hardware.
 *    hw_tstamp[out] - Pointer to the timestamp for hw_level.
 * Returns:
 *    Number of frames to play in sleep for this output device.
 */
unsigned int cras_iodev_frames_to_play_in_sleep(struct cras_iodev *odev,
						unsigned int *hw_level,
						struct timespec *hw_tstamp);

/* Checks if audio thread should wake for this output device.
 * Args:
 *    iodev[in] - The output device.
 * Returns:
 *    1 if audio thread should wake for this output device. 0 otherwise.
 */
int cras_iodev_odev_should_wake(const struct cras_iodev *odev);

/* The default implementation of no_stream ops.
 * The default behavior is to fill some zeros when entering no stream state.
 * Note that when a device in no stream state enters into no stream state again,
 * device needs to fill some zeros again.
 * Do nothing to leave no stream state.
 * Args:
 *    iodev[in] - The output device.
 *    enable[in] - 1 to enter no stream playback, 0 to leave.
 * Returns:
 *    0 on success. Negative error code on failure.
 * */
int cras_iodev_default_no_stream_playback(struct cras_iodev *odev, int enable);

/* Get current state of iodev.
 * Args:
 *    iodev[in] - The device.
 * Returns:
 *    One of states defined in CRAS_IODEV_STATE.
 */
enum CRAS_IODEV_STATE cras_iodev_state(const struct cras_iodev *iodev);

/* Possibly transit state for output device.
 * Check if this output device needs to transit from open state/no_stream state
 * into normal run state. If device does not need transition and is still in
 * no stream state, call no_stream ops to do its work for one cycle.
 * Args:
 *    odev[in] - The output device.
 * Returns:
 *    0 on success. Negative error code on failure.
 */
int cras_iodev_prepare_output_before_write_samples(struct cras_iodev *odev);

/* Get number of underruns recorded so far.
 * Args:
 *    iodev[in] - The device.
 * Returns:
 *    An unsigned int for number of underruns recorded.
 */
unsigned int cras_iodev_get_num_underruns(const struct cras_iodev *iodev);

/* Get number of severe underruns recorded so far.
 * Args:
 *    iodev[in] - The device.
 * Returns:
 *    An unsigned int for number of severe underruns recorded since iodev
 *    was created.
 */
unsigned int
cras_iodev_get_num_severe_underruns(const struct cras_iodev *iodev);

/* Get number of valid frames in the hardware buffer. The valid frames does
 * not include zero samples we filled with before.
 * Args:
 *    iodev[in] - The device.
 *    hw_tstamp[out] - Pointer to the timestamp for hw_level.
 * Returns:
 *    Number of valid frames in the hardware buffer.
 *    Negative error code on failure.
 */
int cras_iodev_get_valid_frames(struct cras_iodev *iodev,
				struct timespec *hw_tstamp);

/* Request main thread to re-open device. This should be used in audio thread
 * when it finds device is in a bad state. The request will be ignored if
 * there is still a pending request.
 * Args:
 *    iodev[in] - The device.
 * Returns:
 *    0 on success. Negative error code on failure.
 */
int cras_iodev_reset_request(struct cras_iodev *iodev);

/* Handle output underrun.
 * Args:
 *    odev[in] - The output device.
 * Returns:
 *    0 on success. Negative error code on failure.
 */
int cras_iodev_output_underrun(struct cras_iodev *odev);

/* Start ramping samples up/down on a device.
 * Args:
 *    iodev[in] - The device.
 *    request[in] - The request type. Check the docstrings of
 *                  CRAS_IODEV_RAMP_REQUEST.
 * Returns:
 *    0 on success. Negative error code on failure.
 */
int cras_iodev_start_ramp(struct cras_iodev *odev,
			  enum CRAS_IODEV_RAMP_REQUEST request);

/* Start ramping samples up/down on a device after a volume change.
 * Args:
 *    iodev[in] - The device.
 *    old_volume[in] - The previous volume percentage of the device.
 *    new_volume[in] - The new volume percentage of the device.
 * Returns:
 *    0 on success. Negative error code on failure.
 */
int cras_iodev_start_volume_ramp(struct cras_iodev *odev,
				 unsigned int old_volume,
				 unsigned int new_volume);

/* Set iodev to mute/unmute state.
 * Args:
 *    iodev[in] - The device.
 * Returns:
 *    0 on success. Negative error code on failure.
 */
int cras_iodev_set_mute(struct cras_iodev *iodev);

/*
 * Checks if an output iodev's volume is zero.
 * If there is an active node, check the adjusted node volume.
 * If there is no active node, check system volume.
 * Args:
 *    odev[in] - The device.
 * Returns:
 *    1 if device's volume is 0. 0 otherwise.
 */
int cras_iodev_is_zero_volume(const struct cras_iodev *odev);

/*
 * Updates the highest hardware level of the device.
 * Args:
 *    iodev - The device.
 */
void cras_iodev_update_highest_hw_level(struct cras_iodev *iodev,
					unsigned int hw_level);

/*
 * Makes an input device drop the specific number of frames by given time.
 * Args:
 *    iodev - The device.
 *    ts - The time indicates how many frames will be dropped in a device.
 * Returns:
 *    The number of frames have been dropped. Negative error code on failure.
 */
int cras_iodev_drop_frames_by_time(struct cras_iodev *iodev,
				   struct timespec ts);

#endif /* CRAS_IODEV_H_ */
