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

#ifndef __GST_DREAMTSSOURCE_H__
#define __GST_DREAMTSSOURCE_H__

#include "gstdreamsource.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/version.h>

#define MAX_PIDS 32
#define MAX_LINE_LENGTH 512

#define BSIZE                    32712

#if DVB_API_VERSION < 5
#define DMX_ADD_PID              _IO('o', 51)
#define DMX_REMOVE_PID           _IO('o', 52)

typedef enum {
	DMX_TAP_TS = 0,
	DMX_TAP_PES = DMX_PES_OTHER, /* for backward binary compat. */
} dmx_tap_type_t;
#endif

G_BEGIN_DECLS

#define GST_TYPE_DREAMTSSOURCE \
  (gst_dreamtssource_get_type())
#define GST_DREAMTSSOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DREAMTSSOURCE,GstDreamTsSource))
#define GST_DREAMTSSOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DREAMTSSOURCE,GstDreamTsSourceClass))
#define GST_IS_DREAMTSSOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DREAMTSSOURCE))
#define GST_IS_DREAMTSSOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DREAMTSSOURCE))

typedef struct _GstDreamTsSource        GstDreamTsSource;
typedef struct _GstDreamTsSourceClass   GstDreamTsSourceClass;

struct _GstDreamTsSource
{
	GstPushSrc element;
	
	gchar *service_ref;
	GstClockTime base_pts;
	
	int active_pids[MAX_PIDS];
	int upstream;
	int upstream_state, upstream_response_code;
	char *reason;
	char response_line[MAX_LINE_LENGTH];
	int response_p;
	int demux_fd;

	int control_sock[2];
	GMutex mutex;
};

struct _GstDreamTsSourceClass
{
	GstPushSrcClass parent_class;
	gint64 (*get_base_pts) (GstDreamTsSource *self);
};

GType gst_dreamtssource_get_type (void);
gboolean gst_dreamtssource_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_DREAMTSSOURCE_H__ */

