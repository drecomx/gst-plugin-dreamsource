/*
 * GStreamer dreamtssource
 * Copyright 2015 Andreas Frisch <fraxinas@opendreambox.org>
 *
 * This program is licensed under the Creative Commons
 * Attribution-NonCommercial-ShareAlike 3.0 Unported
 * License. To view a copy of this license, visit
 * http://creativecommons.org/licenses/by-nc-sa/3.0/ or send a letter to
 * Creative Commons,559 Nathan Abbott Way,Stanford,California 94305,USA.
 *
 * Alternatively, this program may be distributed and executed on
 * hardware which is licensed by Dream Property GmbH.
 *
 * This program is NOT free software. It is open source, you are allowed
 * to modify it (if you keep the license), but it may not be commercially
 * distributed other than under the conditions noted above.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include "gstdreamtssource.h"

GST_DEBUG_CATEGORY_STATIC (dreamtssource_debug);
#define GST_CAT_DEFAULT dreamtssource_debug

enum
{
	SIGNAL_GET_BASE_PTS,
	LAST_SIGNAL
};

enum
{
	ARG_0,
	ARG_SREF,
};

#define safe_write write

static guint gst_dreamtssource_signals[LAST_SIGNAL] = { 0 };

static GstStaticPadTemplate srctemplate =
GST_STATIC_PAD_TEMPLATE ("src",
			GST_PAD_SRC,
			GST_PAD_ALWAYS,
			GST_STATIC_CAPS ("video/mpegts, "
			"systemstream = true, "
			"packetsize = 188" )
);

#define gst_dreamtssource_parent_class parent_class
G_DEFINE_TYPE (GstDreamTsSource, gst_dreamtssource, GST_TYPE_PUSH_SRC);

static gboolean gst_dreamtssource_start (GstBaseSrc * bsrc);
static gboolean gst_dreamtssource_stop (GstBaseSrc * bsrc);
static gboolean gst_dreamtssource_unlock (GstBaseSrc * bsrc);
static void gst_dreamtssource_dispose (GObject * gobject);
static GstFlowReturn gst_dreamtssource_create (GstPushSrc * psrc, GstBuffer ** outbuf);

static void gst_dreamtssource_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_dreamtssource_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_dreamtssource_change_state (GstElement * element, GstStateChange transition);
static gint64 gst_dreamtssource_get_base_pts (GstDreamTsSource *self);
static void gst_dreamtssource_set_sref (GstDreamTsSource *self, const gchar * sref);

static int handle_upstream(GstDreamTsSource * self);
static int handle_upstream_line(GstDreamTsSource * self);

static void
gst_dreamtssource_class_init (GstDreamTsSourceClass * klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;
	GstBaseSrcClass *gstbsrc_class;
	GstPushSrcClass *gstpush_src_class;
	
	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;
	gstbsrc_class = (GstBaseSrcClass *) klass;
	gstpush_src_class = (GstPushSrcClass *) klass;
	
	gobject_class->set_property = gst_dreamtssource_set_property;
	gobject_class->get_property = gst_dreamtssource_get_property;
	gobject_class->dispose = gst_dreamtssource_dispose;
	
	gst_element_class_add_pad_template (gstelement_class,
					    gst_static_pad_template_get (&srctemplate));
	
	gst_element_class_set_static_metadata (gstelement_class,
						"Dream TS source", "Source/Video",
						"Provide an mpeg transport stream from Dreambox demux",
						"Andreas Frisch <fraxinas@opendreambox.org>");
	
	gstelement_class->change_state = gst_dreamtssource_change_state;
	
	gstbsrc_class->start = gst_dreamtssource_start;
	gstbsrc_class->stop = gst_dreamtssource_stop;
	gstbsrc_class->unlock = gst_dreamtssource_unlock;
	
	gstpush_src_class->create = gst_dreamtssource_create;
	
	g_object_class_install_property (gobject_class, ARG_SREF,
		g_param_spec_string ("sref", "serviceref",
		"Enigma2 Service Reference", NULL,
		G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  
	gst_dreamtssource_signals[SIGNAL_GET_BASE_PTS] =
	g_signal_new ("get-base-pts",
		      G_TYPE_FROM_CLASS (klass),
		      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
	       G_STRUCT_OFFSET (GstDreamTsSourceClass, get_base_pts),
		      NULL, NULL, gst_dreamsource_marshal_INT64__VOID, G_TYPE_INT64, 0);
	
	klass->get_base_pts = gst_dreamtssource_get_base_pts;
}

static gint64
gst_dreamtssource_get_base_pts (GstDreamTsSource *self)
{
	GST_DEBUG_OBJECT (self, "gst_dreamtssource_get_base_pts %" GST_TIME_FORMAT"", GST_TIME_ARGS (self->base_pts) );
	return self->base_pts;
}

gboolean
gst_dreamtssource_plugin_init (GstPlugin *plugin)
{
	GST_DEBUG_CATEGORY_INIT (dreamtssource_debug, "dreamtssource", 0, "dreamtssource");
	return gst_element_register (plugin, "dreamtssource", GST_RANK_PRIMARY, GST_TYPE_DREAMTSSOURCE);
}

static void
gst_dreamtssource_init (GstDreamTsSource * self)
{
	g_mutex_init (&self->mutex);
	
	self->reason = "";
	self->demux_fd = -1;
	
	gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);
	gst_base_src_set_live (GST_BASE_SRC (self), TRUE);
}

static void gst_dreamtssource_set_sref (GstDreamTsSource *self, const gchar * sref)
{
	self->service_ref = g_strdup(sref);
	GST_INFO_OBJECT (self, "service reference set: %s", sref);
}

static void
gst_dreamtssource_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
	GstDreamTsSource *self = GST_DREAMTSSOURCE (object);
	
	switch (prop_id) {
		case ARG_SREF:
			gst_dreamtssource_set_sref(self, g_value_get_string (value));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
gst_dreamtssource_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
	GstDreamTsSource *self = GST_DREAMTSSOURCE (object);
	
	switch (prop_id) {
		case ARG_SREF:
			g_value_set_string (value, self->service_ref);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static gboolean gst_dreamtssource_unlock (GstBaseSrc * bsrc)
{
	GstDreamTsSource *self = GST_DREAMTSSOURCE (bsrc);
	GST_LOG_OBJECT (self, "stop creating buffers");
	SEND_COMMAND (self, CONTROL_STOP);
	return TRUE;
}

static void
gst_dreamtssource_free_buffer (GstDreamTsSource * self)
{
	GST_TRACE_OBJECT (self, "gst_dreamtssource_free_buffer");
}

static int handle_upstream(GstDreamTsSource * self)
{
	char buffer[MAX_LINE_LENGTH];
	int n = read(self->upstream, buffer, MAX_LINE_LENGTH);
	GST_LOG_OBJECT (self, "handle_upstream read %i", n);
	if (n == 0)
		return 1;

	if (n < 0)
	{
		perror("read");
		return 1;
	}
	
	char *c = buffer;
	
	while (n)
	{
		char *next_line;
		int valid;

		next_line = memchr(c, '\n', n);
		if (!next_line)
			next_line = c + n;
		else
			next_line++;
		
		valid = next_line - c;
		if (valid > sizeof(self->response_line)-self->response_p)
			return 1;
		
		memcpy(self->response_line + self->response_p, c, valid);
		c += valid;
		self->response_p += valid;
		n -= valid;
		
				/* line received? */
		if (self->response_line[self->response_p - 1] == '\n')
		{
			self->response_line[self->response_p-1] = 0;
			
			if (self->response_p >= 2 && self->response_line[self->response_p - 2] == '\r')
				self->response_line[self->response_p-2] = 0;
			self->response_p = 0;
		
			if (handle_upstream_line(self))
				return 1;
		}
	}
	return 0;
}

static int handle_upstream_line(GstDreamTsSource * self)
{
	GST_LOG_OBJECT (self, "handle_upstream_line upstream_state %i response_line=%s", self->upstream_state, self->response_line);
	switch (self->upstream_state)
	{
	case 0:
		if (strncmp(self->response_line, "HTTP/1.", 7) || strlen(self->response_line) < 9) {
			self->reason = "Invalid upstream response.";
			return 1;
		}
		self->upstream_response_code = atoi(self->response_line + 9);
		self->reason = strdup(self->response_line + 9);
		self->upstream_state++;
		break;
	case 1:
		if (!*self->response_line)
		{
			if (self->upstream_response_code == 200)
				self->upstream_state = 2;
			else
				return 1; /* reason was already set in state 0, but we need all header lines for potential WWW-Authenticate */
		}/* else if (!strncasecmp(self->response_line, "WWW-Authenticate: ", 18))
			snprintf(wwwauthenticate, MAX_LINE_LENGTH, "%s\r\n", self->response_line);*/
		break;
	case 2:
	case 3:
		if (self->response_line[0] == '+') {
					/* parse (and possibly open) demux */
			int demux = atoi(self->response_line + 1);

					/* parse new pids */
			const char *p = strchr(self->response_line, ':');
			int old_active_pids[MAX_PIDS];
			
			memcpy(old_active_pids, self->active_pids, sizeof(self->active_pids));
			
			int nr_pids = 0, i, j;
			while (p)
			{
				++p;
				int pid = strtoul(p, 0, 0x10);
				p = strchr(p, ',');
				
					/* do not add pids twice */
				for (i = 0; i < nr_pids; ++i)
					if (self->active_pids[i] == pid)
						break;

				if (i != nr_pids)
					continue;

				self->active_pids[nr_pids++] = pid;
				GST_DEBUG_OBJECT (self, "added pid %i: %i", nr_pids, self->active_pids[nr_pids-1]);
				
				if (nr_pids == MAX_PIDS)
					break;
			}
			
			for (i = nr_pids; i < MAX_PIDS; ++i)
				self->active_pids[i] = -1;
				
					/* check for added pids */
			for (i = 0; i < nr_pids; ++i)
			{
				for (j = 0; j < MAX_PIDS; ++j)
					if (self->active_pids[i] == old_active_pids[j])
						break;
				if (j == MAX_PIDS) {
					if (self->demux_fd < 0) {
						struct dmx_pes_filter_params flt; 
						char demuxfn[32];
						sprintf(demuxfn, "/dev/dvb/adapter0/demux%d", demux);
						self->demux_fd = open(demuxfn, O_RDWR | O_NONBLOCK);
						if (self->demux_fd < 0) {
							self->reason = "DEMUX OPEN FAILED";
							return 2;
						}
						GST_DEBUG_OBJECT (self, "opened demux fd=%i",self->demux_fd);

						ioctl(self->demux_fd, DMX_SET_BUFFER_SIZE, 1024*1024);

						flt.pid = self->active_pids[i];
						flt.input = DMX_IN_FRONTEND;
#if DVB_API_VERSION > 3
						flt.output = DMX_OUT_TSDEMUX_TAP;
						flt.pes_type = DMX_PES_OTHER;
#else
						flt.output = DMX_OUT_TAP;
						flt.pes_type = DMX_TAP_TS;
#endif
						flt.flags = DMX_IMMEDIATE_START;

						if (ioctl(self->demux_fd, DMX_SET_PES_FILTER, &flt) < 0) {
							self->reason = "DEMUX PES FILTER SET FAILED";
							return 2;
						}
					}
					else {
						uint16_t pid = self->active_pids[i];
						int ret;
#if DVB_API_VERSION > 3
						ret = ioctl(self->demux_fd, DMX_ADD_PID, &pid);
#else
						ret = ioctl(self->demux_fd, DMX_ADD_PID, pid);
#endif
						GST_DEBUG_OBJECT (self, "i=%i ioctl(%i, DMX_ADD_PID, %i)=%i", i, self->demux_fd, pid, ret);
						
						if (ret < 0) {
							self->reason = "DMX_ADD_PID FAILED";
							return 2;
						}
					}
				}
			}
			
					/* check for removed pids */
			for (i = 0; i < MAX_PIDS; ++i)
			{
				if (old_active_pids[i] == -1)
					continue;
				for (j = 0; j < nr_pids; ++j)
					if (old_active_pids[i] == self->active_pids[j])
						break;
				if (j == nr_pids) {
#if DVB_API_VERSION > 3
					uint16_t pid = old_active_pids[i];
					ioctl(self->demux_fd, DMX_REMOVE_PID, &pid);
#else
					ioctl(self->demux_fd, DMX_REMOVE_PID, old_active_pids[i]);
#endif
				}
			}
			if (self->upstream_state == 2) {
// 				char *c = "HTTP/1.0 200 OK\r\nConnection: Close\r\nContent-Type: video/mpeg\r\nServer: stream_enigma2\r\n\r\n";
// 				safe_write(1, c, strlen(c));
				self->upstream_state = 3; /* HTTP response sent */
			}
		}
		else if (self->response_line[0] == '-') {
			self->reason = strdup(self->response_line + 1);
			return 1;
		}
				/* ignore everything not starting with + or - */
		break;
	}
	return 0;
}


static GstFlowReturn
gst_dreamtssource_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
	GstDreamTsSource *self = GST_DREAMTSSOURCE (psrc);

	GST_DEBUG_OBJECT (self, "create");

	while (1)
	{
		*outbuf = NULL;

		struct pollfd rfd[3];

		rfd[0].fd = self->upstream;
		rfd[0].events = POLLIN | POLLERR | POLLHUP | POLLPRI;
		rfd[1].fd = READ_SOCKET (self);
		rfd[1].events = POLLIN | POLLERR | POLLHUP | POLLPRI;
		rfd[2].fd = self->demux_fd;
		rfd[2].events = POLLIN | POLLERR | POLLHUP | POLLPRI;
		
		int ret = poll(rfd, 3, 1000);

		if (G_UNLIKELY (ret == -1))
		{
			GST_ERROR_OBJECT (self, "SELECT ERROR!");
			break;
		}
		else if ( ret == 0 )
		{
			GST_LOG_OBJECT (self, "SELECT TIMEOUT");
		}
		else if ( rfd[0].revents )
		{
			if (handle_upstream(self))
				break;
		}
		if ( G_LIKELY(rfd[1].revents & POLLIN) )
		{
			char command;
			READ_COMMAND (self, command, ret);
			GST_LOG_OBJECT (self, "CONTROL_STOP!");
			return GST_FLOW_FLUSHING;
		}
		if (self->demux_fd > 0 && rfd[2].revents)
		{
			unsigned char buffer[BSIZE];
			int r = read(self->demux_fd, buffer, BSIZE);
			if (r < 0) {
				if (errno == EINTR || errno == EAGAIN || errno == EBUSY || errno == EOVERFLOW)
					continue;
				break;
			}
			*outbuf = gst_buffer_ref(gst_buffer_new_wrapped (buffer, r));
			return GST_FLOW_OK;
		}
	}
	GST_ERROR_OBJECT("reason: %s", self->reason);
	*outbuf = gst_buffer_new();
	return GST_FLOW_ERROR;
}

static GstStateChangeReturn gst_dreamtssource_change_state (GstElement * element, GstStateChange transition)
{
	GstDreamTsSource *self = GST_DREAMTSSOURCE (element);
	int ret;
	/*
	 *  switch (transition) {
	 *    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
	 *      GST_LOG_OBJECT (self, "GST_STATE_CHANGE_PAUSED_TO_PLAYING");
	 *      self->base_pts = GST_CLOCK_TIME_NONE;
	 *      ret = ioctl(self->encoder->fd, VENC_START);
	 *      if ( ret != 0 )
	 *      {
	 *        GST_ERROR_OBJECT(self,"can't start encoder ioctl!");
	 *        return GST_STATE_CHANGE_FAILURE;
}
self->descriptors_available = 0;
GST_INFO_OBJECT (self, "started encoder!");
break;
case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
	GST_LOG_OBJECT (self, "GST_STATE_CHANGE_PLAYING_TO_PAUSED self->descriptors_count=%i self->descriptors_available=%i", self->descriptors_count, self->descriptors_available);
	GST_OBJECT_LOCK (self);
	while (self->descriptors_count < self->descriptors_available)
		GST_INFO_OBJECT (self, "flushing self->descriptors_count=%i", self->descriptors_count++);
	if (self->descriptors_count)
		write(self->encoder->fd, &self->descriptors_count, 4);
	ret = ioctl(self->encoder->fd, VENC_STOP);
	GST_OBJECT_UNLOCK (self);
	if ( ret != 0 )
	{
	GST_ERROR_OBJECT(self,"can't stop encoder ioctl!");
	return GST_STATE_CHANGE_FAILURE;
}
GST_INFO_OBJECT (self, "stopped encoder!");
break;
default:
	break;
}*/
	if (GST_ELEMENT_CLASS (parent_class)->change_state)
		return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
	return GST_STATE_CHANGE_SUCCESS;
}

static gboolean
gst_dreamtssource_start (GstBaseSrc * bsrc)
{
	GstDreamTsSource *self = GST_DREAMTSSOURCE (bsrc);
	
	GST_DEBUG_OBJECT (self, "start");
	
	int control_sock[2];
	if (socketpair (PF_UNIX, SOCK_STREAM, 0, control_sock) < 0)
	{
		GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ_WRITE, (NULL), GST_ERROR_SYSTEM);
		return FALSE;
	}
	READ_SOCKET (self) = control_sock[0];
	WRITE_SOCKET (self) = control_sock[1];
	fcntl (READ_SOCKET (self), F_SETFL, O_NONBLOCK);
	fcntl (WRITE_SOCKET (self), F_SETFL, O_NONBLOCK);
	
	static gchar *xff_header = "";
	static gchar *authorization = "";
	gchar upstream_request[256];
	
	self->upstream = socket(PF_INET, SOCK_STREAM, 0);
	if (self->upstream < 0) {
		self->reason = "Failed to create socket.";
		goto bad_gateway;
	}
	
	struct sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(80);
	sin.sin_addr.s_addr = inet_addr("127.0.0.1");
	if (connect(self->upstream, (struct sockaddr*)&sin, sizeof(struct sockaddr_in)))
	{
		self->reason = "Upstream connect failed.";
		goto bad_gateway;
	}

	g_snprintf(upstream_request, sizeof(upstream_request), "GET /web/stream?StreamService=%s HTTP/1.0\r\n%s%s\r\n", self->service_ref, xff_header, authorization);

	if (safe_write(self->upstream, upstream_request, strlen(upstream_request)) != strlen(upstream_request)) {
		self->reason = "Failed to issue upstream request.";
		goto bad_gateway;
	}
	
	GST_DEBUG_OBJECT (self, "started! upstream request=%s", upstream_request);
	
	return TRUE;

bad_gateway:
	GST_ERROR_OBJECT("Bad Gateway: %s", self->reason);
	return FALSE;
}

static gboolean
gst_dreamtssource_stop (GstBaseSrc * bsrc)
{
	GstDreamTsSource *self = GST_DREAMTSSOURCE (bsrc);
	GST_DEBUG_OBJECT (self, "stop");
	return TRUE;
}

static void
gst_dreamtssource_dispose (GObject * gobject)
{
	GstDreamTsSource *self = GST_DREAMTSSOURCE (gobject);
	g_mutex_clear (&self->mutex);
	GST_DEBUG_OBJECT (self, "disposed");
	G_OBJECT_CLASS (parent_class)->dispose (gobject);
}
