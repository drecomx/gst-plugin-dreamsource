/*
 * GStreamer dreamsource
 * Copyright 2014-2015 Andreas Frisch <fraxinas@opendreambox.org>
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
#include "config.h"
#endif
#include <gst/gst.h>

#include "gstdreamsource.h"
#include "gstdreamaudiosource.h"
#include "gstdreamvideosource.h"
#include "gstdreamtssource.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean res = TRUE;
  res &= gst_dreamaudiosource_plugin_init (plugin);
  res &= gst_dreamvideosource_plugin_init (plugin);
  res &= gst_dreamtssource_plugin_init (plugin);

  return res;
}

GST_PLUGIN_DEFINE (
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	dreamsource,
	"Dreambox Audio/Video Source",
	plugin_init,
	VERSION,
	"Proprietary",
	"dreamsource",
	"https://schwerkraft.elitedvb.net/scm/browser.php?group_id=10"
)

GST_DEBUG_CATEGORY_STATIC (dreamsourceclock_debug);
#define GST_CAT_DEFAULT dreamsourceclock_debug

G_DEFINE_TYPE (GstDreamSourceClock, gst_dreamsource_clock, GST_TYPE_SYSTEM_CLOCK);

static GstClockTime gst_dreamsource_clock_get_internal_time (GstClock * clock);

static void
gst_dreamsource_clock_class_init (GstDreamSourceClockClass * klass)
{
	GstClockClass *clock_class = (GstClockClass *) klass;
	GST_DEBUG_CATEGORY_INIT (dreamsourceclock_debug, "dreamsourceclock", 0, "dreamsourceclock");
	clock_class->get_internal_time = gst_dreamsource_clock_get_internal_time;
}

static void
gst_dreamsource_clock_init (GstDreamSourceClock * self)
{
	self->fd = 0;
	self->stc_offset = 0;
	self->first_stc = 0;
	self->prev_stc = 0;
	GST_OBJECT_FLAG_SET (self, GST_CLOCK_FLAG_CAN_SET_MASTER);
}

GstClock *
gst_dreamsource_clock_new (const gchar * name, int fd)
{
	GstDreamSourceClock *self = GST_DREAMSOURCE_CLOCK (g_object_new (GST_TYPE_DREAMSOURCE_CLOCK, "name", name, "clock-type", GST_CLOCK_TYPE_OTHER, NULL));
	self->fd = fd;
	GST_DEBUG_OBJECT (self, "gst_dreamsource_clock_new fd=%i", self->fd);
	return GST_CLOCK_CAST (self);
}

static GstClockTime gst_dreamsource_clock_get_internal_time (GstClock * clock)
{
	GstDreamSourceClock *self = GST_DREAMSOURCE_CLOCK (clock);

	uint32_t stc = 0;
	GstClockTime encoder_time = 0;

	GST_OBJECT_LOCK(self);
	if (self->fd > 0) {
		int ret = ioctl(self->fd, ENC_GET_STC, &stc);
		if (ret == 0)
		{
			GST_TRACE_OBJECT (self, "current stc=%" GST_TIME_FORMAT "", GST_TIME_ARGS(ENCTIME_TO_GSTTIME(stc)));
			if (G_UNLIKELY(self->first_stc == 0))
				self->first_stc = stc;

			stc -= self->first_stc;

			if (stc < self->prev_stc)
			{
				self->stc_offset += UINT32_MAX;
				GST_LOG_OBJECT (self, "clock_wrap! new offset=%" PRIu64 " ", self->stc_offset);
			}

			self->prev_stc = stc;

			uint64_t total_stc = stc + self->stc_offset;
// 			GST_WARNING_OBJECT (self, "after subtract stc_offset=%" PRIu64 "   new total_stc=%" PRIu64 "", self->stc_offset, total_stc);
			encoder_time = ENCTIME_TO_GSTTIME(total_stc);
			GST_TRACE_OBJECT (self, "result %" GST_TIME_FORMAT "", GST_TIME_ARGS(encoder_time));
		}
		else
			GST_WARNING_OBJECT (self, "can't ENC_GET_STC error: %s, fd=%i, ret=%i", strerror(errno), self->fd, ret);
	}
	else
		GST_ERROR_OBJECT (self, "timebase not available because encoder device is not opened");

	GST_OBJECT_UNLOCK(self);
	return encoder_time;
}
