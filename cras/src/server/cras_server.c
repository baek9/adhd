/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define _GNU_SOURCE /* Needed for Linux socket credential passing. */

#ifdef CRAS_DBUS
#include <dbus/dbus.h>
#endif
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <syslog.h>
#include <unistd.h>

#ifdef CRAS_DBUS
#include "cras_a2dp_endpoint.h"
#include "cras_bt_manager.h"
#include "cras_bt_device.h"
#include "cras_bt_player.h"
#include "cras_dbus.h"
#include "cras_dbus_control.h"
#include "cras_hfp_ag_profile.h"
#include "cras_telephony.h"
#endif
#include "cras_alert.h"
#include "cras_audio_thread_monitor.h"
#include "cras_config.h"
#include "cras_device_monitor.h"
#include "cras_hotword_handler.h"
#include "cras_iodev_list.h"
#include "cras_main_message.h"
#include "cras_messages.h"
#include "cras_metrics.h"
#include "cras_non_empty_audio_handler.h"
#include "cras_observer.h"
#include "cras_rclient.h"
#include "cras_control_rclient.h"
#include "cras_playback_rclient.h"
#include "cras_server.h"
#include "cras_server_metrics.h"
#include "cras_system_state.h"
#include "cras_tm.h"
#include "cras_udev.h"
#include "cras_util.h"
#include "cras_mix.h"
#include "utlist.h"

/* Store a list of clients that are attached to the server.
 * Members:
 *    id - Unique identifier for this client.
 *    fd - socket file descriptor used to communicate with client.
 *    ucred - Process, user, and group ID of the client.
 *    client - rclient to handle messages from this client.
 *    pollfd - Pointer to struct pollfd for this callback.
 */
struct attached_client {
	size_t id;
	int fd;
	struct ucred ucred;
	struct cras_rclient *client;
	struct pollfd *pollfd;
	struct attached_client *next, *prev;
};

/* Stores file descriptors to callback mappings for clients. Callback/fd/data
 * args are registered by clients.  When fd is ready, the callback will be
 * called on the main server thread and the callback data will be passed back to
 * it.  This allows the use of the main server loop instead of spawning a thread
 * to watch file descriptors.  The client can then read or write the fd.
 * Members:
 *    fd - The file descriptor passed to select.
 *    callack - The funciton to call when fd is ready.
 *    callback_data - Pointer passed to the callback.
 *    pollfd - Pointer to struct pollfd for this callback.
 */
struct client_callback {
	int select_fd;
	void (*callback)(void *);
	void *callback_data;
	struct pollfd *pollfd;
	int deleted;
	struct client_callback *prev, *next;
};

/* Stores callback function and argument data to be executed later. */
struct system_task {
	void (*callback)(void *);
	void *callback_data;
	struct system_task *next, *prev;
};

/* Local server data. */
struct server_data {
	struct attached_client *clients_head;
	size_t num_clients;
	struct client_callback *client_callbacks;
	struct system_task *system_tasks;
	size_t num_client_callbacks;
	size_t next_client_id;
} server_instance;

/* Remove a client from the list and destroy it.  Calling rclient_destroy will
 * also free all the streams owned by the client */
static void remove_client(struct attached_client *client)
{
	close(client->fd);
	DL_DELETE(server_instance.clients_head, client);
	server_instance.num_clients--;
	cras_rclient_destroy(client->client);
	free(client);
}

/* This is called when "select" indicates that the client has written data to
 * the socket.  Read out one message and pass it to the client message handler.
 */
static void handle_message_from_client(struct attached_client *client)
{
	uint8_t buf[CRAS_SERV_MAX_MSG_SIZE];
	int nread;
	unsigned int num_fds = 2;
	int fds[num_fds];

	nread = cras_recv_with_fds(client->fd, buf, sizeof(buf), fds, &num_fds);
	if (nread < 0)
		goto read_error;
	if (cras_rclient_buffer_from_client(client->client, buf, nread, fds,
					    num_fds) < 0)
		goto read_error;
	return;

read_error:
	for (int i = 0; i < num_fds; i++)
		if (fds[i] >= 0)
			close(fds[i]);
	switch (nread) {
	case 0:
		break;
	default:
		syslog(LOG_DEBUG, "read err [%d] '%s', removing client %zu",
		       -nread, strerror(-nread), client->id);
		break;
	}
	remove_client(client);
}

/* Discovers and fills in info about the client that can be obtained from the
 * socket. The pid of the attaching client identifies it in logs. */
static void fill_client_info(struct attached_client *client)
{
	socklen_t ucred_length = sizeof(client->ucred);

	if (getsockopt(client->fd, SOL_SOCKET, SO_PEERCRED, &client->ucred,
		       &ucred_length))
		syslog(LOG_INFO, "Failed to get client socket info\n");
}

/* Fills the server_state with the current list of attached clients. */
static void send_client_list_to_clients(struct server_data *serv)
{
	struct attached_client *c;
	struct cras_attached_client_info *info;
	struct cras_server_state *state;
	unsigned i;

	state = cras_system_state_update_begin();
	if (!state)
		return;

	state->num_attached_clients =
		MIN(CRAS_MAX_ATTACHED_CLIENTS, serv->num_clients);

	info = state->client_info;
	i = 0;
	DL_FOREACH (serv->clients_head, c) {
		info->id = c->id;
		info->pid = c->ucred.pid;
		info->uid = c->ucred.uid;
		info->gid = c->ucred.gid;
		info++;
		if (++i == CRAS_MAX_ATTACHED_CLIENTS)
			break;
	}

	cras_system_state_update_complete();
}

/* CRAS client connection types. */
enum CRAS_CONNECTION_TYPE {
	CRAS_CONTROL, // For legacy client.
	CRAS_PLAYBACK, // For playback client.
	CRAS_NUM_CONN_TYPE,
};

/* Handles requests from a client to attach to the server.  Create a local
 * structure to track the client, assign it a unique id and let it attach */
static void handle_new_connection(struct sockaddr_un *address, int fd,
				  enum CRAS_CONNECTION_TYPE type)
{
	int connection_fd;
	struct attached_client *poll_client;
	socklen_t address_length;

	poll_client = malloc(sizeof(struct attached_client));
	if (poll_client == NULL) {
		syslog(LOG_ERR, "Allocating poll_client");
		return;
	}

	memset(&address_length, 0, sizeof(address_length));
	connection_fd = accept(fd, (struct sockaddr *)address, &address_length);
	if (connection_fd < 0) {
		syslog(LOG_ERR, "connecting");
		free(poll_client);
		return;
	}

	/* find next available client id */
	while (1) {
		struct attached_client *out;
		DL_SEARCH_SCALAR(server_instance.clients_head, out, id,
				 server_instance.next_client_id);
		poll_client->id = server_instance.next_client_id;
		server_instance.next_client_id++;
		if (out == NULL)
			break;
	}

	/* When full, getting an error is preferable to blocking. */
	cras_make_fd_nonblocking(connection_fd);

	poll_client->fd = connection_fd;
	poll_client->next = NULL;
	poll_client->pollfd = NULL;
	fill_client_info(poll_client);
	switch (type) {
	case CRAS_CONTROL:
		poll_client->client = cras_control_rclient_create(
			connection_fd, poll_client->id);
		break;
	case CRAS_PLAYBACK:
		poll_client->client = cras_playback_rclient_create(
			connection_fd, poll_client->id);
		break;
	default:
		syslog(LOG_ERR, "unsupported connection type");
		goto error;
	}
	if (poll_client->client == NULL) {
		syslog(LOG_ERR, "failed to create client");
		goto error;
	}

	DL_APPEND(server_instance.clients_head, poll_client);
	server_instance.num_clients++;
	/* Send a current list of available inputs and outputs. */
	cras_iodev_list_update_device_list();
	send_client_list_to_clients(&server_instance);
	return;
error:
	close(connection_fd);
	free(poll_client);
	return;
}

/* Add a file descriptor to be passed to select in the main loop. This is
 * registered with system state so that it is called when any client asks to
 * have a callback triggered based on an fd being readable. */
static int add_select_fd(int fd, void (*cb)(void *data), void *callback_data,
			 void *server_data)
{
	struct client_callback *new_cb;
	struct client_callback *client_cb;
	struct server_data *serv;

	serv = (struct server_data *)server_data;
	if (serv == NULL)
		return -EINVAL;

	/* Check if fd already exists. */
	DL_FOREACH (serv->client_callbacks, client_cb)
		if (client_cb->select_fd == fd && !client_cb->deleted)
			return -EEXIST;

	new_cb = (struct client_callback *)calloc(1, sizeof(*new_cb));
	if (new_cb == NULL)
		return -ENOMEM;

	new_cb->select_fd = fd;
	new_cb->callback = cb;
	new_cb->callback_data = callback_data;
	new_cb->deleted = 0;
	new_cb->pollfd = NULL;

	DL_APPEND(serv->client_callbacks, new_cb);
	server_instance.num_client_callbacks++;
	return 0;
}

/* Removes a file descriptor to be passed to select in the main loop. This is
 * registered with system state so that it is called when any client asks to
 * remove a callback added with add_select_fd. */
static void rm_select_fd(int fd, void *server_data)
{
	struct server_data *serv;
	struct client_callback *client_cb;

	serv = (struct server_data *)server_data;
	if (serv == NULL)
		return;

	DL_FOREACH (serv->client_callbacks, client_cb)
		if (client_cb->select_fd == fd)
			client_cb->deleted = 1;
}

/* Creates a new task entry and append to system_tasks list, which will be
 * executed in main loop later without wait time.
 */
static int add_task(void (*cb)(void *data), void *callback_data,
		    void *server_data)
{
	struct server_data *serv;
	struct system_task *new_task;

	serv = (struct server_data *)server_data;
	if (serv == NULL)
		return -EINVAL;

	new_task = (struct system_task *)calloc(1, sizeof(*new_task));
	if (new_task == NULL)
		return -ENOMEM;

	new_task->callback = cb;
	new_task->callback_data = callback_data;

	DL_APPEND(serv->system_tasks, new_task);
	return 0;
}

/* Cleans up the file descriptor list removing items deleted during the main
 * loop iteration. */
static void cleanup_select_fds(void *server_data)
{
	struct server_data *serv;
	struct client_callback *client_cb;

	serv = (struct server_data *)server_data;
	if (serv == NULL)
		return;

	DL_FOREACH (serv->client_callbacks, client_cb)
		if (client_cb->deleted) {
			DL_DELETE(serv->client_callbacks, client_cb);
			server_instance.num_client_callbacks--;
			free(client_cb);
		}
}

/* Checks that at least two outputs are present (one will be the "empty"
 * default device. */
void check_output_exists(struct cras_timer *t, void *data)
{
	if (cras_iodev_list_get_outputs(NULL) < 2)
		cras_metrics_log_event(kNoCodecsFoundMetric);
}

#if defined(__amd64__)
/* CPU detection - probaby best to move this elsewhere */
static void cpuid(unsigned int *eax, unsigned int *ebx, unsigned int *ecx,
		  unsigned int *edx, unsigned int op)
{
	// clang-format off
	__asm__ __volatile__ (
		"cpuid"
		: "=a" (*eax),
		  "=b" (*ebx),
		  "=c" (*ecx),
		  "=d" (*edx)
		: "a" (op), "c" (0)
	);
	// clang-format on
}

static unsigned int cpu_x86_flags(void)
{
	unsigned int eax, ebx, ecx, edx, id;
	unsigned int cpu_flags = 0;

	cpuid(&id, &ebx, &ecx, &edx, 0);

	if (id >= 1) {
		cpuid(&eax, &ebx, &ecx, &edx, 1);

		if (ecx & (1 << 20))
			cpu_flags |= CPU_X86_SSE4_2;

		if (ecx & (1 << 28))
			cpu_flags |= CPU_X86_AVX;

		if (ecx & (1 << 12))
			cpu_flags |= CPU_X86_FMA;
	}

	if (id >= 7) {
		cpuid(&eax, &ebx, &ecx, &edx, 7);

		if (ebx & (1 << 5))
			cpu_flags |= CPU_X86_AVX2;
	}

	return cpu_flags;
}
#endif

int cpu_get_flags(void)
{
#if defined(__amd64__)
	return cpu_x86_flags();
#endif
	return 0;
}

/*
 * Exported Interface.
 */

int cras_server_init()
{
	/* Log to syslog. */
	openlog("cras_server", LOG_PID, LOG_USER);

	server_instance.next_client_id = RESERVED_CLIENT_IDS;

	/* Initialize global observer. */
	cras_observer_server_init();

	/* init mixer with CPU capabilities */
	cras_mix_init(cpu_get_flags());

	/* Allow clients to register callbacks for file descriptors.
	 * add_select_fd and rm_select_fd will add and remove file descriptors
	 * from the list that are passed to select in the main loop below. */
	cras_system_set_select_handler(add_select_fd, rm_select_fd,
				       &server_instance);
	cras_system_set_add_task_handler(add_task, &server_instance);
	cras_main_message_init();

	return 0;
}

/*
 * Creates a server socket with a given socket_file name and listens on it.
 * The socket_file will be created under cras_config_get_system_socket_file_dir
 * with permission=0770. The socket_fd will be listened with parameter
 * backlog=5.
 *
 * Returns the socket_fd and leaves the address information in addr.
 * When error occurs, the created fd will be closed and the file path will be
 * unlinked.
 */
static int create_and_listen_server_socket(const char *socket_file,
					   struct sockaddr_un *addr)
{
	int socket_fd = -1;
	int rc = 0;
	const char *sockdir;

	socket_fd = socket(PF_UNIX, SOCK_SEQPACKET, 0);
	if (socket_fd < 0) {
		syslog(LOG_ERR, "Main server socket failed.");
		rc = socket_fd;
		goto error;
	}

	sockdir = cras_config_get_system_socket_file_dir();
	if (sockdir == NULL) {
		rc = -ENOTDIR;
		goto error;
	}

	memset(addr, 0, sizeof(*addr));
	addr->sun_family = AF_UNIX;
	snprintf(addr->sun_path, sizeof(addr->sun_path), "%s/%s", sockdir,
		 socket_file);
	unlink(addr->sun_path);

	/* Linux quirk: calling fchmod before bind, sets the permissions of the
	 * file created by bind, leaving no window for it to be modified. Start
	 * with very restricted permissions. */
	rc = fchmod(socket_fd, 0700);
	if (rc < 0)
		goto error;

	rc = bind(socket_fd, (struct sockaddr *)addr,
		  sizeof(struct sockaddr_un));
	if (rc < 0) {
		syslog(LOG_ERR, "Bind to server socket failed.");
		rc = errno;
		goto error;
	}

	/* Let other members in our group play audio through this socket. */
	rc = chmod(addr->sun_path, 0770);
	if (rc < 0)
		goto error;

	if (listen(socket_fd, 5) != 0) {
		syslog(LOG_ERR, "Listen on server socket failed.");
		rc = errno;
		goto error;
	}

	return socket_fd;
error:
	if (socket_fd >= 0) {
		close(socket_fd);
		unlink(addr->sun_path);
	}
	return rc;
}

int cras_server_run(unsigned int profile_disable_mask)
{
	static const unsigned int OUTPUT_CHECK_MS = 5 * 1000;
#ifdef CRAS_DBUS
	DBusConnection *dbus_conn;
#endif
	int control_fd = -1;
	int playback_fd = -1;
	int rc = 0;
	struct sockaddr_un control_addr;
	struct sockaddr_un playback_addr;
	struct attached_client *elm;
	struct client_callback *client_cb;
	struct system_task *tasks;
	struct system_task *system_task;
	struct cras_tm *tm;
	struct timespec ts, *poll_timeout;
	int timers_active;
	struct pollfd *pollfds;
	unsigned int pollfds_size = 32;
	unsigned int num_pollfds, poll_size_needed;

	pollfds = malloc(sizeof(*pollfds) * pollfds_size);

	cras_udev_start_sound_subsystem_monitor();
#ifdef CRAS_DBUS
	cras_bt_device_start_monitor();
#endif

	cras_server_metrics_init();

	cras_device_monitor_init();

	cras_hotword_handler_init();

	cras_non_empty_audio_handler_init();

	cras_audio_thread_monitor_init();

#ifdef CRAS_DBUS
	dbus_threads_init_default();
	dbus_conn = cras_dbus_connect_system_bus();
	if (dbus_conn) {
		cras_bt_start(dbus_conn);
		if (!(profile_disable_mask & CRAS_SERVER_PROFILE_MASK_HFP))
			cras_hfp_ag_profile_create(dbus_conn);
		if (!(profile_disable_mask & CRAS_SERVER_PROFILE_MASK_HSP))
			cras_hsp_ag_profile_create(dbus_conn);
		cras_telephony_start(dbus_conn);
		if (!(profile_disable_mask & CRAS_SERVER_PROFILE_MASK_A2DP))
			cras_a2dp_endpoint_create(dbus_conn);
		cras_bt_player_create(dbus_conn);
		cras_dbus_control_start(dbus_conn);
	}
#endif

	control_fd = create_and_listen_server_socket(CRAS_SOCKET_FILE,
						     &control_addr);
	if (control_fd < 0)
		goto bail;

	playback_fd = create_and_listen_server_socket(CRAS_PLAYBACK_SOCKET_FILE,
						      &playback_addr);
	if (playback_fd < 0)
		goto bail;

	tm = cras_system_state_get_tm();
	if (!tm) {
		syslog(LOG_ERR, "Getting timer manager.");
		rc = -ENOMEM;
		goto bail;
	}

	/* After a delay, make sure there is at least one real output device. */
	cras_tm_create_timer(tm, OUTPUT_CHECK_MS, check_output_exists, 0);

	/* Main server loop - client callbacks are run from this context. */
	while (1) {
		poll_size_needed = CRAS_NUM_CONN_TYPE +
				   server_instance.num_clients +
				   server_instance.num_client_callbacks;
		if (poll_size_needed > pollfds_size) {
			pollfds_size = 2 * poll_size_needed;
			pollfds = realloc(pollfds,
					  sizeof(*pollfds) * pollfds_size);
		}

		pollfds[CRAS_CONTROL].fd = control_fd;
		pollfds[CRAS_CONTROL].events = POLLIN;

		pollfds[CRAS_PLAYBACK].fd = playback_fd;
		pollfds[CRAS_PLAYBACK].events = POLLIN;
		num_pollfds = CRAS_NUM_CONN_TYPE;

		DL_FOREACH (server_instance.clients_head, elm) {
			pollfds[num_pollfds].fd = elm->fd;
			pollfds[num_pollfds].events = POLLIN;
			elm->pollfd = &pollfds[num_pollfds];
			num_pollfds++;
		}
		DL_FOREACH (server_instance.client_callbacks, client_cb) {
			if (client_cb->deleted)
				continue;
			pollfds[num_pollfds].fd = client_cb->select_fd;
			pollfds[num_pollfds].events = POLLIN;
			client_cb->pollfd = &pollfds[num_pollfds];
			num_pollfds++;
		}

		tasks = server_instance.system_tasks;
		server_instance.system_tasks = NULL;
		DL_FOREACH (tasks, system_task) {
			system_task->callback(system_task->callback_data);
			DL_DELETE(tasks, system_task);
			free(system_task);
		}

		timers_active = cras_tm_get_next_timeout(tm, &ts);

		/*
		 * If new client task has been scheduled, no need to wait
		 * for timeout, just do another loop to execute them.
		 */
		if (server_instance.system_tasks)
			poll_timeout = NULL;
		else
			poll_timeout = timers_active ? &ts : NULL;

		rc = ppoll(pollfds, num_pollfds, poll_timeout, NULL);
		if (rc < 0)
			continue;

		cras_tm_call_callbacks(tm);

		/* Check for new connections. */
		if (pollfds[CRAS_CONTROL].revents & POLLIN)
			handle_new_connection(&control_addr, control_fd,
					      CRAS_CONTROL);
		if (pollfds[CRAS_PLAYBACK].revents & POLLIN)
			handle_new_connection(&playback_addr, playback_fd,
					      CRAS_PLAYBACK);
		/* Check if there are messages pending for any clients. */
		DL_FOREACH (server_instance.clients_head, elm)
			if (elm->pollfd && elm->pollfd->revents & POLLIN)
				handle_message_from_client(elm);
		/* Check any client-registered fd/callback pairs. */
		DL_FOREACH (server_instance.client_callbacks, client_cb)
			if (!client_cb->deleted && client_cb->pollfd &&
			    (client_cb->pollfd->revents & POLLIN))
				client_cb->callback(client_cb->callback_data);

		cleanup_select_fds(&server_instance);

#ifdef CRAS_DBUS
		if (dbus_conn)
			cras_dbus_dispatch(dbus_conn);
#endif

		cras_alert_process_all_pending_alerts();
	}

bail:
	if (control_fd >= 0) {
		close(control_fd);
		unlink(control_addr.sun_path);
	}
	if (playback_fd >= 0) {
		close(playback_fd);
		unlink(playback_addr.sun_path);
	}
	free(pollfds);
	cras_observer_server_free();
	return rc;
}

void cras_server_send_to_all_clients(const struct cras_client_message *msg)
{
	struct attached_client *client;

	DL_FOREACH (server_instance.clients_head, client)
		cras_rclient_send_message(client->client, msg, NULL, 0);
}