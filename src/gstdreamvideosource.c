/*
 * GStreamer dreamvideosource
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
#include <config.h>
#endif

#include <gst/gst.h>
#include "gstdreamvideosource.h"

GST_DEBUG_CATEGORY_STATIC (dreamvideosource_debug);
#define GST_CAT_DEFAULT dreamvideosource_debug

GType gst_dreamvideosource_input_mode_get_type (void)
{
	static volatile gsize input_mode_type = 0;
	static const GEnumValue input_mode[] = {
		{GST_DREAMVIDEOSOURCE_INPUT_MODE_LIVE, "GST_DREAMVIDEOSOURCE_INPUT_MODE_LIVE", "live"},
		{GST_DREAMVIDEOSOURCE_INPUT_MODE_HDMI_IN, "GST_DREAMVIDEOSOURCE_INPUT_MODE_HDMI_IN", "hdmi_in"},
		{GST_DREAMVIDEOSOURCE_INPUT_MODE_BACKGROUND, "GST_DREAMVIDEOSOURCE_INPUT_MODE_BACKGROUND", "background"},
		{0, NULL, NULL},
	};

	if (g_once_init_enter (&input_mode_type)) {
		GType tmp = g_enum_register_static ("GstDreamVideoSourceInputMode", input_mode);
		g_once_init_leave (&input_mode_type, tmp);
	}
	return (GType) input_mode_type;
}

enum
{
	SIGNAL_GET_DTS_OFFSET,
	LAST_SIGNAL
};

enum
{
	ARG_0,
	ARG_CAPS,
	ARG_BITRATE,
	ARG_INPUT_MODE,
	ARG_GOP_LENGTH,
	ARG_GOP_SCENE,
	ARG_OPEN_GOP,
	ARG_BFRAMES,
	ARG_PFRAMES,
	ARG_SLICES,
	ARG_LEVEL,
};

static guint gst_dreamvideosource_signals[LAST_SIGNAL] = { 0 };

#define DEFAULT_BITRATE     2048
#define DEFAULT_GOP_LENGTH  0
#define DEFAULT_GOP_SCENE   FALSE
#define DEFAULT_BFRAMES     2
#define DEFAULT_PFRAMES     1
#define DEFAULT_SLICES      0
#define DEFAULT_LEVEL       level_default
#define DEFAULT_FRAMERATE   25
#define DEFAULT_WIDTH       1280
#define DEFAULT_HEIGHT      720
#define DEFAULT_INPUT_MODE  GST_DREAMVIDEOSOURCE_INPUT_MODE_LIVE
#define DEFAULT_BUFFER_SIZE 50

static GstStaticPadTemplate srctemplate =
    GST_STATIC_PAD_TEMPLATE ("src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS	("video/x-h264, "
	"width = { 720, 1280, 1920 }, "
	"height = { 576, 720, 1080 }, "
	"framerate = { 25/1, 30/1, 50/1, 60/1 }, "
	"display-aspect-ratio = { 5/4, 16/9 }, "
	"stream-format = (string) byte-stream, "
	"profile = (string) { main, high }")
    );

#define gst_dreamvideosource_parent_class parent_class
G_DEFINE_TYPE (GstDreamVideoSource, gst_dreamvideosource, GST_TYPE_PUSH_SRC);

static GstCaps *gst_dreamvideosource_getcaps (GstBaseSrc * bsrc, GstCaps * filter);
static gboolean gst_dreamvideosource_setcaps (GstBaseSrc * bsrc, GstCaps * caps);
static GstCaps *gst_dreamvideosource_fixate (GstBaseSrc * bsrc, GstCaps * caps);
static gboolean gst_dreamvideosource_query (GstBaseSrc * bsrc, GstQuery * query);

static gboolean gst_dreamvideosource_unlock (GstBaseSrc * bsrc);
static gboolean gst_dreamvideosource_unlock_stop (GstBaseSrc * bsrc);
static void gst_dreamvideosource_dispose (GObject * gobject);
static GstFlowReturn gst_dreamvideosource_create (GstPushSrc * psrc, GstBuffer ** outbuf);

static void gst_dreamvideosource_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_dreamvideosource_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_dreamvideosource_change_state (GstElement * element, GstStateChange transition);
static gint64 gst_dreamvideosource_get_dts_offset (GstDreamVideoSource *self);

static gboolean gst_dreamvideosource_encoder_init (GstDreamVideoSource * self);
static void gst_dreamvideosource_encoder_release (GstDreamVideoSource * self);

static void gst_dreamvideosource_read_thread_func (GstDreamVideoSource * self);

#ifdef PROVIDE_CLOCK
static GstClock *gst_dreamvideosource_provide_clock (GstElement * elem);
// static GstClockTime gst_dreamvideosource_get_encoder_time_ (GstClock * clock, GstBaseSrc * bsrc);
#endif

static void
gst_dreamvideosource_class_init (GstDreamVideoSourceClass * klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;
	GstBaseSrcClass *gstbsrc_class;
	GstPushSrcClass *gstpush_src_class;

	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;
	gstbsrc_class = (GstBaseSrcClass *) klass;
	gstpush_src_class = (GstPushSrcClass *) klass;

	gobject_class->set_property = gst_dreamvideosource_set_property;
	gobject_class->get_property = gst_dreamvideosource_get_property;
	gobject_class->dispose = gst_dreamvideosource_dispose;

	gst_element_class_add_pad_template (gstelement_class,
					    gst_static_pad_template_get (&srctemplate));

	gst_element_class_set_static_metadata (gstelement_class,
	    "Dream Video source", "Source/Video",
	    "Provide an h.264 video elementary stream from Dreambox encoder device",
	    "Andreas Frisch <fraxinas@opendreambox.org>");

	gstelement_class->change_state = gst_dreamvideosource_change_state;

	gstbsrc_class->get_caps = gst_dreamvideosource_getcaps;
 	gstbsrc_class->set_caps = gst_dreamvideosource_setcaps;
	gstbsrc_class->query = gst_dreamvideosource_query;
 	gstbsrc_class->fixate = gst_dreamvideosource_fixate;
	gstbsrc_class->unlock = gst_dreamvideosource_unlock;
	gstbsrc_class->unlock_stop = gst_dreamvideosource_unlock_stop;

	gstpush_src_class->create = gst_dreamvideosource_create;

#ifdef PROVIDE_CLOCK
	gstelement_class->provide_clock = GST_DEBUG_FUNCPTR (gst_dreamvideosource_provide_clock);
//	g_type_class_ref (GST_TYPE_SYSTEM_CLOCK);
#endif

	g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BITRATE,
	  g_param_spec_int ("bitrate", "Bitrate (kb/s)",
	    "Bitrate in kbit/sec", bitrate_min, bitrate_max, DEFAULT_BITRATE,
	    G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_GOP_LENGTH,
	  g_param_spec_int ("gop-length", "GOP length (ms)",
	    "GOP length in ms", gop_length_auto, gop_length_max, DEFAULT_GOP_LENGTH,
	    G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_GOP_SCENE,
	  g_param_spec_boolean ("gop-scene", "GOP on scene change",
	    "New GOP on scene change", FALSE,
	    G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_OPEN_GOP,
	  g_param_spec_boolean ("open-gop", "Enable Open GOPs",
	    "Open GOPs", FALSE,
	    G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BFRAMES,
	  g_param_spec_int ("bframes", "Number of B-Frames",
	    "Number of B-Frames before P-Frame", bframes_min, bframes_max, DEFAULT_BFRAMES,
	    G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PFRAMES,
	  g_param_spec_int ("pframes", "Number of P-Frames",
	    "Number of P-Frames after B-Frames", pframes_min, pframes_max, DEFAULT_PFRAMES,
	    G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SLICES,
	  g_param_spec_int ("slices", "Number of slices",
	    "Number of slices per picture", slices_min, slices_max, DEFAULT_SLICES,
	    G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LEVEL,
	  g_param_spec_int ("level", "h.264 Level",
	    "h.264 Level", level_min, level_max, DEFAULT_LEVEL,
	    G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (gobject_class, ARG_CAPS,
	  g_param_spec_boxed ("caps", "Caps",
	    "The caps for the source stream", GST_TYPE_CAPS,
	    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (gobject_class, ARG_INPUT_MODE,
	  g_param_spec_enum ("input-mode", "Input Mode",
	    "Select the input source of the video stream",
	    GST_TYPE_DREAMVIDEOSOURCE_INPUT_MODE, DEFAULT_INPUT_MODE,
	    G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	gst_dreamvideosource_signals[SIGNAL_GET_DTS_OFFSET] =
		g_signal_new ("get-dts-offset",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		G_STRUCT_OFFSET (GstDreamVideoSourceClass, get_dts_offset),
		NULL, NULL, gst_dreamsource_marshal_INT64__VOID, G_TYPE_INT64, 0);

	klass->get_dts_offset = gst_dreamvideosource_get_dts_offset;
}

static gint64
gst_dreamvideosource_get_dts_offset (GstDreamVideoSource *self)
{
	GST_DEBUG_OBJECT (self, "gst_dreamvideosource_get_dts_offset %" GST_TIME_FORMAT"", GST_TIME_ARGS (self->dts_offset) );
	return self->dts_offset;
}

static void gst_dreamvideosource_set_bitrate (GstDreamVideoSource * self, uint32_t bitrate)
{
	g_mutex_lock (&self->mutex);
	uint32_t vbr = bitrate*1000;
	if (!self->encoder || !self->encoder->fd)
	{
		self->video_info.bitrate = bitrate;
		g_mutex_unlock (&self->mutex);
		return;
	}

	int ret = ioctl(self->encoder->fd, VENC_SET_BITRATE, &vbr);
	if (ret != 0)
	{
		GST_WARNING_OBJECT (self, "can't set video bitrate to %i bytes/s!", vbr);
		g_mutex_unlock (&self->mutex);
		return;
	}
	GST_INFO_OBJECT (self, "set video bitrate to %i kBytes/s", bitrate);
	self->video_info.bitrate = bitrate;
	g_mutex_unlock (&self->mutex);
}

static void gst_dreamvideosource_set_goplen (GstDreamVideoSource * self, uint32_t goplen)
{
	g_mutex_lock (&self->mutex);
	if (!self->encoder || !self->encoder->fd)
	{
		self->video_info.gop_length = goplen;
		g_mutex_unlock (&self->mutex);
		return;
	}

	int ret = ioctl(self->encoder->fd, VENC_SET_GOP_LENGTH, &goplen);
	if (ret != 0)
	{
		GST_WARNING_OBJECT (self, "can't set video gop length to %i ms!", goplen);
		g_mutex_unlock (&self->mutex);
		return;
	}
	GST_INFO_OBJECT (self, "set video gop length to %i ms", goplen);
	self->video_info.gop_length = goplen;
	g_mutex_unlock (&self->mutex);
}

static void gst_dreamvideosource_set_gop_on_scene_change (GstDreamVideoSource * self, gboolean enabled)
{
	g_mutex_lock (&self->mutex);
	uint32_t en = enabled;
	if (!self->encoder || !self->encoder->fd)
	{
		self->video_info.gop_scene = enabled;
		g_mutex_unlock (&self->mutex);
		return;
	}

	int ret = ioctl(self->encoder->fd, VENC_SET_NEW_GOP_ON_NEW_SCENE, &en);
	if (ret != 0)
	{
		GST_WARNING_OBJECT (self, "can't set video new gop on new scene to %i (unspported?)!", enabled);
		g_mutex_unlock (&self->mutex);
		return;
	}
	GST_INFO_OBJECT (self, "set video new gop on new scene to %i!", enabled);
	self->video_info.gop_scene = enabled;
	g_mutex_unlock (&self->mutex);
}

static void gst_dreamvideosource_set_open_gop (GstDreamVideoSource * self, gboolean enabled)
{
	g_mutex_lock (&self->mutex);
	if (!self->encoder || !self->encoder->fd)
	{
		self->video_info.open_gop = enabled;
		g_mutex_unlock (&self->mutex);
		return;
	}

	uint32_t en = enabled;
	int ret = ioctl(self->encoder->fd, VENC_SET_OPEN_GOP, &en);
	if (ret != 0)
	{
		GST_WARNING_OBJECT (self, "can't set video open gop to %i (unspported?)!", enabled);
		g_mutex_unlock (&self->mutex);
		return;
	}
	GST_INFO_OBJECT (self, "set video open gop to %i!", enabled);
	self->video_info.open_gop = enabled;
	g_mutex_unlock (&self->mutex);
}

static void gst_dreamvideosource_set_bframes (GstDreamVideoSource * self, uint32_t bframes)
{
	g_mutex_lock (&self->mutex);
	if (!self->encoder || !self->encoder->fd)
	{
		self->video_info.bframes = bframes;
		g_mutex_unlock (&self->mutex);
		return;
	}

	int ret = ioctl(self->encoder->fd, VENC_SET_B_FRAMES, &bframes);
	if (ret != 0)
	{
		GST_WARNING_OBJECT (self, "can't set video b-frames %i!", bframes);
		g_mutex_unlock (&self->mutex);
		return;
	}
	GST_INFO_OBJECT (self, "set video b-frames to %i", bframes);
	self->video_info.bframes= bframes;
	g_mutex_unlock (&self->mutex);
}

static void gst_dreamvideosource_set_pframes (GstDreamVideoSource * self, uint32_t pframes)
{
	g_mutex_lock (&self->mutex);
	if (!self->encoder || !self->encoder->fd)
	{
		self->video_info.pframes = pframes;
		g_mutex_unlock (&self->mutex);
		return;
	}

	int ret = ioctl(self->encoder->fd, VENC_SET_P_FRAMES, &pframes);
	if (ret != 0)
	{
		GST_WARNING_OBJECT (self, "can't set video p-frames %i!", pframes);
		g_mutex_unlock (&self->mutex);
		return;
	}
	GST_INFO_OBJECT (self, "set video p-frames to %i", pframes);
	self->video_info.pframes= pframes;
	g_mutex_unlock (&self->mutex);
}

static void gst_dreamvideosource_set_slices (GstDreamVideoSource * self, uint32_t slices)
{
	g_mutex_lock (&self->mutex);
	if (!self->encoder || !self->encoder->fd)
	{
		self->video_info.slices = slices;
		g_mutex_unlock (&self->mutex);
		return;
	}

	int ret = ioctl(self->encoder->fd, VENC_SET_SLICES_PER_PIC, &slices);
	if (ret != 0)
	{
		GST_WARNING_OBJECT (self, "can't set video slices to %i %i!", slices, ret);
		g_mutex_unlock (&self->mutex);
		return;
	}
	GST_INFO_OBJECT (self, "set video slices to %i", slices);
	self->video_info.slices = slices;
	g_mutex_unlock (&self->mutex);
}

static void gst_dreamvideosource_set_level (GstDreamVideoSource * self, uint32_t level)
{
	g_mutex_lock (&self->mutex);
	if (!self->encoder || !self->encoder->fd)
	{
		self->video_info.level = level;
		g_mutex_unlock (&self->mutex);
		return;
	}

	int ret = ioctl(self->encoder->fd, VENC_SET_LEVEL, &level);
	if (ret != 0)
	{
		GST_WARNING_OBJECT (self, "can't set h264 level to %i %i!", level, ret);
		g_mutex_unlock (&self->mutex);
		return;
	}
	GST_INFO_OBJECT (self, "set video h264 level to %i", level);
	self->video_info.level = level;
	g_mutex_unlock (&self->mutex);
}

static gboolean gst_dreamvideosource_set_format (GstDreamVideoSource * self, VideoFormatInfo * info)
{
	g_mutex_lock (&self->mutex);
	info->bitrate = self->video_info.bitrate;
	info->gop_length = self->video_info.gop_length;
	info->gop_scene = self->video_info.gop_scene;
	info->open_gop = self->video_info.open_gop;
	info->bframes = self->video_info.bframes;
	info->pframes = self->video_info.pframes;
	info->slices = self->video_info.slices;
	info->level = self->video_info.level;

	if (!self->encoder || !self->encoder->fd)
	{
		self->video_info = *info;
		g_mutex_unlock (&self->mutex);
		return TRUE;
	}

	if (info->fps_n > 0)
	{
		int venc_fps = 0;
		switch (info->fps_n) {
			case 25:
				venc_fps = rate_25;
				break;
			case 30:
				venc_fps = rate_30;
				break;
			case 50:
				venc_fps = rate_50;
				break;
			case 60:
				venc_fps = rate_60;
				break;
			case 23:
				venc_fps = rate_23_976;
				break;
			case 24:
				venc_fps = rate_24;
				break;
			case 29:
				venc_fps = rate_29_97;
				break;
			case 59:
				venc_fps = rate_59_94;
			default:
				GST_ERROR_OBJECT (self, "invalid framerate %d/%d", info->fps_n, info->fps_d);
				goto fail;
		}
		if (!ioctl(self->encoder->fd, VENC_SET_FRAMERATE, &venc_fps))
			GST_INFO_OBJECT (self, "set framerate to %d/%d -> ioctrl(%d, VENC_SET_FRAMERATE, &%d)", info->fps_n, info->fps_d, self->encoder->fd, venc_fps);
		else
		{
			GST_WARNING_OBJECT (self, "can't set framerate to %d/%d -> ioctrl(%d, VENC_SET_FRAMERATE, &%d)", info->fps_n, info->fps_d, self->encoder->fd, venc_fps);
			goto fail;
		}
	}

	if (info->width && info->height)
	{
		int venc_size = 0;
		if ( info->width == 720 && info->height == 576 )
			venc_size = fmt_720x576;
		else if ( info->width == 1280 && info->height == 720)
			venc_size = fmt_1280x720;
		else if ( info->width == 1920 && info->height == 1080)
			venc_size = fmt_1920x1080;
		else
		{
			GST_ERROR_OBJECT (self, "invalid resolution %dx%d", info->width, info->height);
			goto fail;
		}
		if (!ioctl(self->encoder->fd, VENC_SET_RESOLUTION, &venc_size))
			GST_INFO_OBJECT (self, "set resolution to %dx%d -> ioctrl(%d, VENC_SET_RESOLUTION, &%d)", info->width, info->height, self->encoder->fd, venc_size);
		else
		{
			GST_WARNING_OBJECT (self, "can't set resolution to %dx%d -> ioctrl(%d, VENC_SET_RESOLUTION, &%d)", info->width, info->height, self->encoder->fd, venc_size);
			goto fail;
		}
	}

	if(!ioctl(self->encoder->fd, VENC_SET_PROFILE, &info->profile))
		GST_INFO_OBJECT (self, "set profile to %d -> ioctl(%d, VENC_SET_PROFILE)", info->profile, self->encoder->fd);
	else
		GST_WARNING_OBJECT (self, "can't set profile to %d -> ioctl(%d, VENC_SET_PROFILE)", info->profile, self->encoder->fd);

	self->video_info = *info;
	g_mutex_unlock (&self->mutex);
	return TRUE;

fail:
	g_mutex_unlock (&self->mutex);
	return FALSE;
}

void gst_dreamvideosource_set_input_mode (GstDreamVideoSource *self, GstDreamVideoSourceInputMode mode)
{
	g_return_if_fail (GST_IS_DREAMVIDEOSOURCE (self));
	GEnumValue *val = g_enum_get_value (G_ENUM_CLASS (g_type_class_ref (GST_TYPE_DREAMVIDEOSOURCE_INPUT_MODE)), mode);
	if (!val)
	{
		GST_ERROR_OBJECT (self, "no such input_mode %i!", mode);
		return;
	}
	const gchar *value_nick = val->value_nick;

	g_mutex_lock (&self->mutex);
	if (!self->encoder || !self->encoder->fd)
	{
		self->input_mode = mode;
		goto out;
	}
	int int_mode = mode;
	int ret = ioctl(self->encoder->fd, VENC_SET_SOURCE, &int_mode);
	if (ret != 0)
	{
		GST_WARNING_OBJECT (self, "can't set input mode to %s (%i) error: %s", value_nick, mode, strerror(errno));
		goto out;
	}
	GST_INFO_OBJECT (self, "successfully set input mode to %s (%i)", value_nick, mode);
	self->input_mode = mode;

out:
	g_mutex_unlock (&self->mutex);
	return;
}

GstDreamVideoSourceInputMode gst_dreamvideosource_get_input_mode (GstDreamVideoSource *self)
{
	GstDreamVideoSourceInputMode result;
	g_return_val_if_fail (GST_IS_DREAMVIDEOSOURCE (self), -1);
	g_mutex_lock (&self->mutex);
	result =self->input_mode;
	g_mutex_unlock (&self->mutex);
	return result;
}

gboolean
gst_dreamvideosource_plugin_init (GstPlugin *plugin)
{
	GST_DEBUG_CATEGORY_INIT (dreamvideosource_debug, "dreamvideosource", 0, "dreamvideosource");
	return gst_element_register (plugin, "dreamvideosource", GST_RANK_PRIMARY, GST_TYPE_DREAMVIDEOSOURCE);
}

static void
gst_dreamvideosource_init (GstDreamVideoSource * self)
{
	self->current_caps = NULL;
	self->new_caps = NULL;

	self->dts_valid = FALSE;
	self->encoder = NULL;
	self->descriptors_available = 0;
	self->input_mode = DEFAULT_INPUT_MODE;

	self->buffer_size = DEFAULT_BUFFER_SIZE;
	g_queue_init (&self->current_frames);
	self->readthread = NULL;

	g_mutex_init (&self->mutex);
	g_cond_init (&self->cond);
	READ_SOCKET (self) = -1;
	WRITE_SOCKET (self) = -1;

	gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);
	gst_base_src_set_live (GST_BASE_SRC (self), TRUE);

	self->encoder = NULL;
	self->encoder_clock = NULL;

#ifdef dump
	self->dumpfd = open("/media/hdd/movie/dreamvideosource.dump", O_WRONLY | O_CREAT | O_TRUNC);
	GST_DEBUG_OBJECT (self, "dumpfd = %i (%s)", self->dumpfd, (self->dumpfd > 0) ? "OK" : strerror(errno));
#endif
}

static gboolean gst_dreamvideosource_encoder_init (GstDreamVideoSource * self)
{
	GST_LOG_OBJECT (self, "initializating encoder...");
	self->encoder = malloc (sizeof(EncoderInfo));

	if (!self->encoder) {
		GST_ERROR_OBJECT (self,"out of space");
		return FALSE;
	}

	char fn_buf[32];
	sprintf(fn_buf, "/dev/venc%d", 0);
	self->encoder->fd = open(fn_buf, O_RDWR | O_SYNC);
	if (self->encoder->fd <= 0) {
		GST_ERROR_OBJECT (self,"cannot open device %s (%s)", fn_buf, strerror(errno));
		free(self->encoder);
		self->encoder = NULL;
		return FALSE;
	}

	self->encoder->buffer = malloc(VBUFSIZE);
	if (!self->encoder->buffer) {
		GST_ERROR_OBJECT(self,"cannot alloc buffer");
		return FALSE;
	}

	self->encoder->cdb = (unsigned char *)mmap (0, VMMAPSIZE, PROT_READ, MAP_PRIVATE, self->encoder->fd, 0);

	if (!self->encoder->cdb || self->encoder->cdb == MAP_FAILED) {
		GST_ERROR_OBJECT(self, "cannot alloc buffer: %s (%i)", strerror(errno), errno);
		self->encoder->cdb = NULL;
		return FALSE;
	}

	int control_sock[2];
	if (socketpair (PF_UNIX, SOCK_STREAM, 0, control_sock) < 0)
	{
		GST_ERROR_OBJECT(self, "cannot create control sockets: %s (%i)", strerror(errno), errno);
		return FALSE;
	}
	READ_SOCKET (self) = control_sock[0];
	WRITE_SOCKET (self) = control_sock[1];
	fcntl (READ_SOCKET (self), F_SETFL, O_NONBLOCK);
	fcntl (WRITE_SOCKET (self), F_SETFL, O_NONBLOCK);

	gst_dreamvideosource_set_bitrate (self, self->video_info.bitrate);
	gst_dreamvideosource_set_goplen(self, self->video_info.gop_length);
	gst_dreamvideosource_set_bframes(self,  self->video_info.bframes);
	gst_dreamvideosource_set_pframes(self,  self->video_info.pframes);
	gst_dreamvideosource_set_gop_on_scene_change(self, self->video_info.gop_scene);
	gst_dreamvideosource_set_open_gop(self, self->video_info.open_gop);
	gst_dreamvideosource_set_slices(self, self->video_info.slices);
	gst_dreamvideosource_set_level(self, self->video_info.level);
	gst_dreamvideosource_set_format (self, &self->video_info);
	gst_dreamvideosource_set_input_mode (self, self->input_mode);

	GST_LOG_OBJECT (self, "encoder %s successfully initialized", fn_buf);
	return TRUE;
}

static void gst_dreamvideosource_encoder_release (GstDreamVideoSource * self)
{
	GST_LOG_OBJECT (self, "releasing encoder...");
	if (self->encoder) {
		if (self->encoder->buffer)
			free(self->encoder->buffer);
		if (self->encoder->cdb)
			munmap(self->encoder->cdb, VMMAPSIZE);
		if (self->encoder->fd)
			close(self->encoder->fd);
		free(self->encoder);
	}
	self->encoder = NULL;
	close (READ_SOCKET (self));
	close (WRITE_SOCKET (self));
	READ_SOCKET (self) = -1;
	WRITE_SOCKET (self) = -1;
	if (self->encoder_clock) {
		gst_object_unref (self->encoder_clock);
		self->encoder_clock = NULL;
	}
}

static void
gst_dreamvideosource_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
	GstDreamVideoSource *self = GST_DREAMVIDEOSOURCE (object);

	switch (prop_id) {
		case ARG_CAPS:
		{
			if (self->new_caps)
				gst_caps_unref (self->new_caps);
			self->new_caps = gst_caps_copy (gst_value_get_caps (value));
			gst_pad_mark_reconfigure (GST_BASE_SRC_PAD (GST_BASE_SRC(object)));
			break;
		}
		case ARG_BITRATE:
			gst_dreamvideosource_set_bitrate(self, g_value_get_int (value));
			break;
		case ARG_INPUT_MODE:
			gst_dreamvideosource_set_input_mode (self, g_value_get_enum (value));
			break;
		case ARG_GOP_LENGTH:
			gst_dreamvideosource_set_goplen(self, g_value_get_int (value));
			break;
		case ARG_GOP_SCENE:
			gst_dreamvideosource_set_gop_on_scene_change(self, g_value_get_boolean (value));
			break;
		case ARG_OPEN_GOP:
			gst_dreamvideosource_set_open_gop(self, g_value_get_boolean (value));
			break;
		case ARG_BFRAMES:
			gst_dreamvideosource_set_bframes(self, g_value_get_int (value));
			break;
		case ARG_PFRAMES:
			gst_dreamvideosource_set_pframes(self, g_value_get_int (value));
			break;
		case ARG_SLICES:
			gst_dreamvideosource_set_slices(self, g_value_get_int (value));
			break;
		case ARG_LEVEL:
			gst_dreamvideosource_set_level(self, g_value_get_int (value));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
gst_dreamvideosource_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
	GstDreamVideoSource *self = GST_DREAMVIDEOSOURCE (object);

	switch (prop_id) {
		case ARG_CAPS:
			g_value_take_boxed (value, gst_dreamvideosource_getcaps (GST_BASE_SRC(object), GST_CAPS_ANY));
			break;
		case ARG_BITRATE:
			g_value_set_int (value, self->video_info.bitrate);
			break;
		case ARG_INPUT_MODE:
			g_value_set_enum (value, gst_dreamvideosource_get_input_mode (self));
			break;
		case ARG_GOP_LENGTH:
			g_value_set_int(value, self->video_info.gop_length);
			break;
		case ARG_GOP_SCENE:
			g_value_set_boolean(value, self->video_info.gop_scene);
			break;
		case ARG_OPEN_GOP:
			g_value_set_boolean(value, self->video_info.open_gop);
			break;
		case ARG_BFRAMES:
			g_value_set_int(value, self->video_info.bframes);
			break;
		case ARG_PFRAMES:
			g_value_set_int(value, self->video_info.pframes);
			break;
		case ARG_SLICES:
			g_value_set_int(value, self->video_info.slices);
			break;
		case ARG_LEVEL:
			g_value_set_int(value, self->video_info.level);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static GstCaps *
gst_dreamvideosource_getcaps (GstBaseSrc * bsrc, GstCaps * filter)
{
	GstDreamVideoSource *self = GST_DREAMVIDEOSOURCE (bsrc);
	GstCaps *caps;

	g_mutex_lock (&self->mutex);
	if (self->new_caps)
	{
		GST_DEBUG_OBJECT (self, "gst_dreamvideosource_getcaps has new_caps: %" GST_PTR_FORMAT " / current_caps: %" GST_PTR_FORMAT "", self->new_caps, self->current_caps);
		if (self->current_caps)
		{
			caps = gst_caps_new_empty ();
			gst_caps_replace (&caps, self->new_caps);
			self->new_caps = NULL;
		}
		else
			caps = gst_caps_copy (self->new_caps);
	} else if (self->current_caps == NULL) {
		GstPadTemplate *pad_template;
		pad_template = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS(self), "src");
		if (pad_template)
			caps = gst_caps_copy (gst_pad_template_get_caps (pad_template));
		else
			caps = gst_caps_new_empty ();

		caps = gst_pad_template_get_caps (pad_template);
	} else
		caps = gst_caps_copy(self->current_caps);

	GST_LOG_OBJECT (self, "gst_dreamvideosource_getcaps %" GST_PTR_FORMAT " filter %" GST_PTR_FORMAT, caps, filter);

	if (filter) {
		GstCaps *intersection;
		intersection = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
		gst_caps_unref (caps);
		caps = intersection;
	}

	GST_LOG_OBJECT (self, "return caps %" GST_PTR_FORMAT, caps);
	g_mutex_unlock (&self->mutex);
	return caps;
}

static gboolean
gst_dreamvideosource_setcaps (GstBaseSrc * bsrc, GstCaps * caps)
{
	GstDreamVideoSource *self = GST_DREAMVIDEOSOURCE (bsrc);
	GstCaps *current_caps;
	const GstStructure *structure;
	VideoFormatInfo info;
	gboolean ret;
	const GValue *framerate;
	gint *profile;

	g_mutex_lock (&self->mutex);
	structure = gst_caps_get_structure (caps, 0);

	current_caps = gst_pad_get_current_caps (GST_BASE_SRC_PAD (bsrc));

	GST_LOG_OBJECT (self, "gst_dreamvideosource_setcaps parameter %" GST_PTR_FORMAT " self->current_caps=%" GST_PTR_FORMAT " pad's current_caps=%" GST_PTR_FORMAT "  new_caps=%" GST_PTR_FORMAT"", caps, current_caps, self->current_caps, self->new_caps);

	if (current_caps && gst_caps_is_equal (current_caps, caps)) {
		GST_LOG_OBJECT (self, "New caps equal to old ones: %" GST_PTR_FORMAT, caps);
		ret = TRUE;
	} else {
		GstState state;
		gst_element_get_state (GST_ELEMENT(self), &state, NULL, 1*GST_MSECOND);
		if (state == GST_STATE_PLAYING)
		{
			GST_WARNING_OBJECT (self, "can't change caps while in PLAYING state %" GST_PTR_FORMAT, caps);
			g_mutex_unlock (&self->mutex);
			return TRUE;
		}
		else if (gst_structure_has_name (structure, "video/x-h264"))
		{
			memset (&info, 0, sizeof(VideoFormatInfo));
			ret = gst_structure_get_int (structure, "width", &info.width);
			ret &= gst_structure_get_int (structure, "height", &info.height);
			framerate = gst_structure_get_value (structure, "framerate");
			if (GST_VALUE_HOLDS_FRACTION(framerate)) {
				info.fps_n = gst_value_get_fraction_numerator (framerate);
				info.fps_d = gst_value_get_fraction_denominator (framerate);
			}
			GST_DEBUG_OBJECT (self, "set caps %" GST_PTR_FORMAT, caps);

			const gchar* profile = gst_structure_get_string(structure, "profile");

			info.profile = profile_main;

			if (!profile)
				GST_WARNING_OBJECT (self, "profile missing in caps... set main progile");
			else if (!g_strcmp0 (profile, "high"))
				info.profile = profile_high;
			else if (!g_strcmp0 (profile, "main"))
				info.profile = profile_main;
			else
				GST_WARNING_OBJECT (self, "unknown profile '%s' in caps... set main profile");

			gst_caps_replace (&self->current_caps, caps);

			g_mutex_unlock (&self->mutex);
			if (gst_caps_is_fixed(caps) && gst_dreamvideosource_set_format(self, &info))
				ret = gst_pad_push_event (bsrc->srcpad, gst_event_new_caps (caps));
			g_mutex_lock (&self->mutex);
		}
		else {
			GST_WARNING_OBJECT (self, "unsupported caps: %" GST_PTR_FORMAT, caps);
			ret = FALSE;
		}
	}
	if (current_caps)
		gst_caps_unref (current_caps);

	g_mutex_unlock (&self->mutex);
	return ret;
}

static GstCaps *
gst_dreamvideosource_fixate (GstBaseSrc * bsrc, GstCaps * caps)
{
	GstDreamVideoSource *self = GST_DREAMVIDEOSOURCE (bsrc);
	GstStructure *structure;

	caps = gst_caps_make_writable (caps);
	structure = gst_caps_get_structure (caps, 0);

	if (gst_structure_has_field (structure, "width"))
		gst_structure_fixate_field_nearest_int (structure, "width", DEFAULT_WIDTH);
	if (gst_structure_has_field (structure, "height"))
		gst_structure_fixate_field_nearest_int (structure, "height", DEFAULT_HEIGHT);
	if (gst_structure_has_field (structure, "framerate"))
		gst_structure_fixate_field_nearest_fraction (structure, "framerate", DEFAULT_FRAMERATE, 1);
	if (gst_structure_has_field (structure, "display-aspect-ratio"))
		gst_structure_fixate_field_nearest_fraction (structure, "display-aspect-ratio", DEFAULT_WIDTH, DEFAULT_HEIGHT);

	caps = GST_BASE_SRC_CLASS (parent_class)->fixate (bsrc, caps);
	GST_DEBUG_OBJECT (self, "fixated caps: %" GST_PTR_FORMAT, caps);
	return caps;
}

static gboolean gst_dreamvideosource_query (GstBaseSrc * bsrc, GstQuery * query)
{
	GstDreamVideoSource *self = GST_DREAMVIDEOSOURCE (bsrc);
	gboolean ret = TRUE;

	switch (GST_QUERY_TYPE (query)) {
		case GST_QUERY_LATENCY:{
			if (self->video_info.fps_n) {
				GstClockTime min, max;

				g_mutex_lock (&self->mutex);
				min = gst_util_uint64_scale_ceil (GST_SECOND, self->video_info.fps_d, self->video_info.fps_n);
				g_mutex_unlock (&self->mutex);

				max = self->buffer_size * min;

				gst_query_set_latency (query, TRUE, min, max);
				GST_DEBUG_OBJECT (bsrc, "set LATENCY QUERY %" GST_PTR_FORMAT, query);
				ret = TRUE;
			} else {
				ret = FALSE;
			}
			break;
		}
		default:
			ret = GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
			break;
	}
	return ret;
}

static gboolean gst_dreamvideosource_unlock (GstBaseSrc * bsrc)
{
	GstDreamVideoSource *self = GST_DREAMVIDEOSOURCE (bsrc);
	GST_DEBUG_OBJECT (self, "stop creating buffers");
	g_mutex_lock (&self->mutex);
	self->flushing = TRUE;
	GST_DEBUG_OBJECT (self, "set flushing TRUE");
	g_cond_signal (&self->cond);
	GST_DEBUG_OBJECT (self, "post cond");
	g_mutex_unlock (&self->mutex);
	GST_DEBUG_OBJECT (self, "post unlock");
	return TRUE;
}

static gboolean gst_dreamvideosource_unlock_stop (GstBaseSrc * bsrc)
{
	GstDreamVideoSource *self = GST_DREAMVIDEOSOURCE (bsrc);
	GST_DEBUG_OBJECT (self, "stop flushing...");
	g_mutex_lock (&self->mutex);
	self->flushing = FALSE;
	g_queue_foreach (&self->current_frames, (GFunc) gst_buffer_unref, NULL);
	g_queue_clear (&self->current_frames);
	g_mutex_unlock (&self->mutex);
	return TRUE;
}

static void gst_dreamvideosource_read_thread_func (GstDreamVideoSource * self)
{
	EncoderInfo *enc = self->encoder;
	GstDreamSourceReadthreadState state = READTHREADSTATE_NONE;
	GstBuffer *readbuf;

	if (!enc) {
		GST_WARNING_OBJECT (self, "encoder device not opened!");
		return;
	}

	GST_DEBUG_OBJECT (self, "enter read thread");

	GstMessage *message;
	GValue val = { 0 };

	message = gst_message_new_stream_status (GST_OBJECT_CAST (self), GST_STREAM_STATUS_TYPE_ENTER, GST_ELEMENT_CAST (GST_OBJECT_PARENT(self)));
	g_value_init (&val, GST_TYPE_G_THREAD);
	g_value_set_boxed (&val, self->readthread);
	gst_message_set_stream_status_object (message, &val);
	g_value_unset (&val);
	GST_DEBUG_OBJECT (self, "posting ENTER stream status");
	gst_element_post_message (GST_ELEMENT_CAST (self), message);
	GstClockTime clock_time, base_time;
	gboolean discont = TRUE;

	while (TRUE) {
		readbuf = NULL;
		{
			if (state == READTHREADSTATE_STOP)
				goto stop_running;

			struct pollfd rfd[2];
			int timeout, nfds;

			rfd[0].fd = READ_SOCKET (self);
			rfd[0].events = POLLIN | POLLERR | POLLHUP | POLLPRI;
			rfd[1].revents = 0;
			rfd[1].events = POLLIN;
			nfds = 1;
			timeout = 0;

			if (state <= READTRREADSTATE_PAUSED)
				timeout = 200;
			else if (state == READTRREADSTATE_RUNNING && self->descriptors_available == 0)
			{
				rfd[1].fd = enc->fd;
				self->descriptors_count = 0;
				timeout = 200;
				nfds = 2;
			}

			int ret = poll(rfd, nfds, timeout);

			if (G_UNLIKELY (ret == -1))
			{
				GST_ERROR_OBJECT (self, "SELECT ERROR!");
				goto stop_running;
			}
			else if ( ret == 0 && self->descriptors_available == 0 )
			{
				g_mutex_lock (&self->mutex);
				gst_clock_get_internal_time(self->encoder_clock);
				g_mutex_unlock (&self->mutex);
				GST_DEBUG_OBJECT (self, "SELECT TIMEOUT");
				discont = TRUE;
// 				readbuf = gst_buffer_new();
			}
			else if ( rfd[0].revents )
			{
				char command;
				READ_COMMAND (self, command, ret);
				switch (command) {
					case CONTROL_STOP:
						GST_DEBUG_OBJECT (self, "CONTROL_STOP!");
						state = READTHREADSTATE_STOP;
						break;
					case CONTROL_PAUSE:
						GST_DEBUG_OBJECT (self, "CONTROL_PAUSE!");
						state = READTRREADSTATE_PAUSED;
						break;
					case CONTROL_RUN:
						GST_DEBUG_OBJECT (self, "CONTROL_RUN");
						state = READTRREADSTATE_RUNNING;
						break;
					default:
						GST_ERROR_OBJECT (self, "illegal control socket command %c received!", command);
				}
				continue;
			}
			else if ( G_LIKELY(rfd[1].revents & POLLIN) )
			{
				int rlen = read(enc->fd, enc->buffer, VBUFSIZE);
				if (G_UNLIKELY (!self->encoder_clock))
				{
					GST_DEBUG_OBJECT(self, "no encoder clock yet... continue");
					continue;
				}
				clock_time = gst_clock_get_internal_time (self->encoder_clock);
				base_time = gst_element_get_base_time(GST_ELEMENT(self));
				if (rlen <= 0 || rlen % VBDSIZE ) {
					if ( errno == 512 )
						goto stop_running;
					GST_WARNING_OBJECT (self, "read error %s (%i)", strerror(errno), errno);
					goto stop_running;
				}
				self->descriptors_available = rlen / VBDSIZE;
				GST_LOG_OBJECT (self, "encoder buffer was empty, %d descriptors available", self->descriptors_available);
			}
			if (self->flushing)
			{
				g_mutex_lock (&self->mutex);
				GST_DEBUG_OBJECT (self, "FLUSHING!");
				g_cond_signal (&self->cond);
				g_mutex_unlock (&self->mutex);
				continue;
			}
		}

		while (self->descriptors_count < self->descriptors_available)
		{
			GstClockTime encoder_dts = GST_CLOCK_TIME_NONE;
			GstClockTime encoder_pts = GST_CLOCK_TIME_NONE;
			GstClockTime result_dts = GST_CLOCK_TIME_NONE;
			GstClockTime result_pts = GST_CLOCK_TIME_NONE;
			gint64 dts_pts_offset;
			gboolean skip_frame = FALSE;

			off_t offset = self->descriptors_count * VBDSIZE;
			VideoBufferDescriptor *desc = (VideoBufferDescriptor*)(&enc->buffer[offset]);

			uint32_t f = desc->stCommon.uiFlags;

			GST_LOG_OBJECT (self, "descriptors_count=%d, descriptors_available=%d\tuiOffset=%d, uiLength=%d", self->descriptors_count, self->descriptors_available, desc->stCommon.uiOffset, desc->stCommon.uiLength);

			if (G_UNLIKELY (f & CDB_FLAG_METADATA))
			{
				GST_LOG_OBJECT (self, "CDB_FLAG_METADATA... skip outdated packet");
				self->descriptors_count = self->descriptors_available;
				continue;
			}

			// uiDTS since kernel driver booted
			if (f & VBD_FLAG_DTS_VALID && desc->uiDTS)
			{
				encoder_dts = MPEGTIME_TO_GSTTIME(desc->uiDTS);
				GST_LOG_OBJECT (self, "f & VBD_FLAG_DTS_VALID && encoder's uiDTS=%" GST_TIME_FORMAT"", GST_TIME_ARGS(encoder_dts));

				g_mutex_lock (&self->mutex);
				if (G_UNLIKELY (self->dts_offset == GST_CLOCK_TIME_NONE && self->flushing == FALSE))
				{
					if (self->dreamaudiosrc)
					{
						guint64 audiosource_dts_offset;
						g_signal_emit_by_name(self->dreamaudiosrc, "get-dts-offset", &audiosource_dts_offset);
						if (audiosource_dts_offset != GST_CLOCK_TIME_NONE)
						{
							GST_DEBUG_OBJECT (self, "use DREAMAUDIOSOURCE's dts_offset=%" GST_TIME_FORMAT "", GST_TIME_ARGS (audiosource_dts_offset) );
							self->dts_offset = audiosource_dts_offset;
						}
					}
					else if (self->dts_offset == GST_CLOCK_TIME_NONE)
					{
						self->dts_offset = encoder_dts - clock_time;
						GST_DEBUG_OBJECT (self, "use encoder_dts-clock_time as dts_offset (%" GST_TIME_FORMAT" = %" GST_TIME_FORMAT" - %" GST_TIME_FORMAT")", GST_TIME_ARGS (self->dts_offset), GST_TIME_ARGS (encoder_dts), GST_TIME_ARGS (clock_time));
					}
				}
				if (G_UNLIKELY (self->dts_valid == FALSE && self->dts_offset != GST_CLOCK_TIME_NONE))
					self->dts_valid = TRUE;
				g_mutex_unlock (&self->mutex);
			}

			g_mutex_lock (&self->mutex);
			if (G_UNLIKELY (self->dts_valid == FALSE))
			{
				GST_DEBUG_OBJECT (self, "dts_valid not set, skipping frame...");
				self->descriptors_count++;
				g_mutex_unlock (&self->mutex);
				break;
			}
			else
				g_mutex_unlock (&self->mutex);

			if (G_UNLIKELY (encoder_dts < self->dts_offset))
			{
				GST_DEBUG_OBJECT (self, "encoder_dts < dts_offset, skipping frame...");
				skip_frame = TRUE;
			}
// 			if (self->video_info.fps_d)
// 				GST_BUFFER_DURATION(readbuf) = gst_util_uint64_scale (GST_SECOND, self->video_info.fps_d, self->video_info.fps_n);

			if (!skip_frame && encoder_dts != GST_CLOCK_TIME_NONE)
			{
				if (f & CDB_FLAG_PTS_VALID)
					encoder_pts = MPEGTIME_TO_GSTTIME(desc->stCommon.uiPTS);
				else
					encoder_pts = encoder_dts;
				dts_pts_offset = encoder_pts - encoder_dts;

				GstClockTime orig_dts_clock_time = encoder_dts - self->dts_offset;
				GstClockTime orig_pts_clock_time = orig_dts_clock_time + dts_pts_offset;
				GstClockTime calib_dts_clock_time = orig_dts_clock_time;

				GstClockTime internal, external;
				GstClockTime rate_n, rate_d;
				GstClockTimeDiff diff;

				gst_clock_get_calibration (self->encoder_clock, &internal, &external, &rate_n, &rate_d);

				if (internal > orig_dts_clock_time) {
					diff = internal - orig_dts_clock_time;
					diff = gst_util_uint64_scale (diff, rate_n, rate_d);
					calib_dts_clock_time = external - diff;
				} else {
					diff = orig_dts_clock_time - internal;
					diff = gst_util_uint64_scale (diff, rate_n, rate_d);
					calib_dts_clock_time = external + diff;
				}

				if ( calib_dts_clock_time < base_time )
				{
					GST_DEBUG_OBJECT (self, "calib_dts_clock_time < base_time, skipping frame...");
					skip_frame = TRUE;
				}
				result_dts = calib_dts_clock_time - base_time;
				result_pts = result_dts + dts_pts_offset;

#define extra_timestamp_debug
#ifdef extra_timestamp_debug
				GstClockTime my_int_time = gst_clock_get_internal_time(self->encoder_clock);
				GstClockTime pipeline_int_time = GST_CLOCK_TIME_NONE;
				GstClock *elemclk = gst_element_get_clock (GST_ELEMENT (self));
				if (elemclk)
				{
					pipeline_int_time = gst_clock_get_internal_time(elemclk);
					gst_object_unref (elemclk);
				}

				GST_LOG_OBJECT (self, "post-calibration\n"
				"  %" GST_TIME_FORMAT " =base_time       %" GST_TIME_FORMAT " =clock_time            %" PRId64 " =dts_pts_offset\n"
				"  %" GST_TIME_FORMAT " =encoder_dts     %" GST_TIME_FORMAT " =orig_dts_clock_time   %" GST_TIME_FORMAT " =calib_dts_clock_time   %" GST_TIME_FORMAT " =result_dts\n"
				"  %" GST_TIME_FORMAT " =encoder_pts     %" GST_TIME_FORMAT " =orig_pts_clock_time                                            %" GST_TIME_FORMAT " =result_pts\n"
				"  %" GST_TIME_FORMAT " =internal        %" GST_TIME_FORMAT " =external              %" GST_TIME_FORMAT " =diff                   %" PRId64 "/%" PRId64 " =rate\n"
				"  %" GST_TIME_FORMAT " =my_int_time     %" GST_TIME_FORMAT " =pipeline_int_time"
				,
				GST_TIME_ARGS (base_time), GST_TIME_ARGS (clock_time), dts_pts_offset,
				GST_TIME_ARGS (encoder_dts), GST_TIME_ARGS (orig_dts_clock_time), GST_TIME_ARGS (calib_dts_clock_time), GST_TIME_ARGS (result_dts),
				GST_TIME_ARGS (encoder_pts), GST_TIME_ARGS (orig_pts_clock_time), GST_TIME_ARGS (result_pts),
				GST_TIME_ARGS (internal), GST_TIME_ARGS (external), GST_TIME_ARGS (diff), rate_n, rate_d,
				GST_TIME_ARGS (my_int_time), GST_TIME_ARGS (pipeline_int_time)
				);
#endif
			}

			if (!skip_frame)
			{
				readbuf = gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, enc->cdb, VMMAPSIZE, desc->stCommon.uiOffset, desc->stCommon.uiLength, self, NULL);
				if (result_dts != GST_CLOCK_TIME_NONE)
				{
					GST_BUFFER_DTS(readbuf) = result_dts;
					GST_BUFFER_PTS(readbuf) = result_pts;
				}
			}

#ifdef dump
			int wret = write(self->dumpfd, (unsigned char*)(enc->cdb + desc->stCommon.uiOffset), desc->stCommon.uiLength);
			GST_LOG_OBJECT (self, "read %i dumped %i total %" G_GSIZE_FORMAT " ", desc->stCommon.uiLength, wret, gst_buffer_get_size (*outbuf) );
#endif
			self->descriptors_count++;
			break;
		}

		if (self->descriptors_count == self->descriptors_available)
		{
			GST_LOG_OBJECT (self, "self->descriptors_count == self->descriptors_available -> release %i consumed descriptors", self->descriptors_count);
			if (state == READTHREADSTATE_STOP)
				GST_DEBUG_OBJECT (self, "readthread stopping, don't write to fd anymore!");
			/* release consumed descs */
			else if (write(enc->fd, &self->descriptors_count, sizeof(self->descriptors_count)) != sizeof(self->descriptors_count)) {
				GST_WARNING_OBJECT (self, "release consumed descs write error!");
				goto stop_running;
			}
			self->descriptors_available = 0;
		}

		if (readbuf)
		{
			g_mutex_lock (&self->mutex);
			if (!self->flushing)
			{
				while (g_queue_get_length (&self->current_frames) >= self->buffer_size)
				{
					GstBuffer * oldbuf = g_queue_pop_head (&self->current_frames);
					GST_WARNING_OBJECT (self, "dropping %" GST_PTR_FORMAT " because of queue overflow! buffers count=%i", oldbuf, g_queue_get_length (&self->current_frames));
					gst_buffer_unref(oldbuf);
					GST_BUFFER_FLAG_SET ((GstBuffer *) g_queue_peek_head (&self->current_frames), GST_BUFFER_FLAG_DISCONT);
				}
				if (discont)
				{
					GST_BUFFER_FLAG_SET (readbuf, GST_BUFFER_FLAG_DISCONT);
					discont = FALSE;
				}
				g_queue_push_tail (&self->current_frames, readbuf);
				GST_INFO_OBJECT (self, "read %" GST_PTR_FORMAT " to queue... buffers count=%i", readbuf, g_queue_get_length (&self->current_frames));
				g_cond_signal (&self->cond);
			}
			else
				gst_buffer_unref(readbuf);
// 			g_cond_signal (&self->cond);
			g_mutex_unlock (&self->mutex);
		}
	}

	g_assert_not_reached ();
	return;

	stop_running:
	{
//		g_mutex_unlock (&self->mutex);
		g_cond_signal (&self->cond);
		GST_DEBUG ("stop running, exit thread");
		message = gst_message_new_stream_status (GST_OBJECT_CAST (self), GST_STREAM_STATUS_TYPE_LEAVE, GST_ELEMENT_CAST (GST_OBJECT_PARENT(self)));
		g_value_init (&val, GST_TYPE_G_THREAD);
		g_value_set_boxed (&val, self->readthread);
		gst_message_set_stream_status_object (message, &val);
		g_value_unset (&val);
		GST_DEBUG_OBJECT (self, "posting LEAVE stream status");
		gst_element_post_message (GST_ELEMENT_CAST (self), message);
		return;
	}
}

static GstFlowReturn
gst_dreamvideosource_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
	GstDreamVideoSource *self = GST_DREAMVIDEOSOURCE (psrc);

	GST_LOG_OBJECT (self, "new buffer requested. queue has %i buffers", g_queue_get_length (&self->current_frames));

	g_mutex_lock (&self->mutex);
	while (g_queue_is_empty (&self->current_frames) && !self->flushing)
	{
		GST_INFO_OBJECT (self, "waiting for buffer from encoder");
		g_cond_wait (&self->cond, &self->mutex);
	}

	*outbuf = g_queue_pop_head (&self->current_frames);
	g_mutex_unlock (&self->mutex);

	if (*outbuf)
	{
		GST_INFO_OBJECT (self, "pushing %" GST_PTR_FORMAT ". queue has %i buffers", *outbuf, g_queue_get_length (&self->current_frames));
		return GST_FLOW_OK;
	}
	GST_INFO_OBJECT (self, "FLUSHING");
	return GST_FLOW_FLUSHING;
}


static GstStateChangeReturn gst_dreamvideosource_change_state (GstElement * element, GstStateChange transition)
{
	GstDreamVideoSource *self = GST_DREAMVIDEOSOURCE (element);
	GstStateChangeReturn sret = GST_STATE_CHANGE_SUCCESS;
	int ret;

	switch (transition) {
		case GST_STATE_CHANGE_NULL_TO_READY:
		{
			if (!gst_dreamvideosource_encoder_init (self))
			{
				GError *err = g_error_new (GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_READ, "Can't initialize encoder device");
				GstMessage *msg = gst_message_new_error (GST_OBJECT (self), err, NULL);
				gst_element_post_message (element, msg);
				g_error_free (err);
				return GST_STATE_CHANGE_FAILURE;
			}
			GST_DEBUG_OBJECT (self, "GST_STATE_CHANGE_NULL_TO_READY");
			break;
		}
		case GST_STATE_CHANGE_READY_TO_PAUSED:
			GST_LOG_OBJECT (self, "GST_STATE_CHANGE_READY_TO_PAUSED");
			self->dreamaudiosrc = gst_bin_get_by_name_recurse_up(GST_BIN(GST_ELEMENT_PARENT(self)), "dreamaudiosource0");
		#ifdef PROVIDE_CLOCK
			if (self->dreamaudiosrc)
			{
				self->encoder_clock = gst_element_provide_clock (self->dreamaudiosrc);
				GST_DEBUG_OBJECT (self, "using dreamaudiosrc's encoder_clock = %" GST_PTR_FORMAT, self->encoder_clock);
			} else {
				self->encoder_clock = gst_dreamsource_clock_new ("GstDreamVideoSourceClock", self->encoder->fd);
				GST_OBJECT_FLAG_SET (self, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
				GstMessage* msg;
				msg = gst_message_new_clock_provide (GST_OBJECT_CAST (element), self->encoder_clock, TRUE);
				GST_DEBUG_OBJECT (self, "new clock: %" GST_PTR_FORMAT " %" GST_PTR_FORMAT " typename=%s", self->encoder_clock, msg, GST_MESSAGE_TYPE_NAME(msg));
				gst_element_post_message (element, msg);
			}
		#endif
			self->dts_offset = GST_CLOCK_TIME_NONE;
			self->flushing = TRUE;
			self->readthread = g_thread_try_new ("dreamvideosrc-read", (GThreadFunc) gst_dreamvideosource_read_thread_func, self, NULL);
			GST_DEBUG_OBJECT (self, "started readthread @%p", self->readthread );
			break;
		case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
			g_mutex_lock (&self->mutex);
			GST_LOG_OBJECT (self, "GST_STATE_CHANGE_PAUSED_TO_PLAYING");
			self->dts_valid = FALSE;
			GstClock *pipeline_clock = gst_element_get_clock (GST_ELEMENT (self));
			if (pipeline_clock)
			{
				if (self->dreamaudiosrc)
					GST_DEBUG_OBJECT (self, "dreamaudiosource owns the clock and is responsible for slaving it");
				else if (pipeline_clock != self->encoder_clock)
				{
					gst_clock_set_master (self->encoder_clock, pipeline_clock);
					GST_DEBUG_OBJECT (self, "slaved %" GST_PTR_FORMAT "to pipeline_clock %" GST_PTR_FORMAT "", self->encoder_clock, pipeline_clock);
				}
				else
					GST_DEBUG_OBJECT (self, "encoder_clock is master clock");
			}
				else
					GST_WARNING_OBJECT (self, "no pipeline clock!");
			ret = ioctl(self->encoder->fd, VENC_START);
			if ( ret != 0 )
				goto fail;
			self->descriptors_available = 0;
			CLEAR_COMMAND (self);
			g_mutex_unlock (&self->mutex);
			break;
		default:
			break;
	}

	if (GST_ELEMENT_CLASS (parent_class)->change_state)
		sret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

	switch (transition) {
		case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
			g_mutex_lock (&self->mutex);
			SEND_COMMAND (self, CONTROL_RUN);
			GST_INFO_OBJECT (self, "started encoder!");
			g_mutex_unlock (&self->mutex);
			break;
		case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
			g_mutex_lock (&self->mutex);
			GST_DEBUG_OBJECT (self, "GST_STATE_CHANGE_PLAYING_TO_PAUSED self->descriptors_count=%i self->descriptors_available=%i", self->descriptors_count, self->descriptors_available);
			SEND_COMMAND (self, CONTROL_PAUSE);
			if (self->descriptors_count < self->descriptors_available)
				self->descriptors_count = self->descriptors_available;
			if (self->descriptors_count)
				write(self->encoder->fd, &self->descriptors_count, sizeof(self->descriptors_count));
			ret = ioctl(self->encoder->fd, VENC_STOP);
			if ( ret != 0 )
				goto fail;
#ifdef PROVIDE_CLOCK
			gst_clock_set_master (self->encoder_clock, NULL);
#endif
			GST_INFO_OBJECT (self, "stopped encoder!");
			g_mutex_unlock (&self->mutex);
			break;
		case GST_STATE_CHANGE_PAUSED_TO_READY:
			GST_DEBUG_OBJECT (self,"GST_STATE_CHANGE_PAUSED_TO_READY");
#ifdef PROVIDE_CLOCK
			gst_element_post_message (element, gst_message_new_clock_lost (GST_OBJECT_CAST (element), self->encoder_clock));
			if (!self->dreamaudiosrc)
				gst_clock_set_calibration (self->encoder_clock, 0, 0, 1, 1);
#endif
			GST_DEBUG_OBJECT (self, "stopping readthread @%p...", self->readthread);
			SEND_COMMAND (self, CONTROL_STOP);
			g_thread_join (self->readthread);
			if (self->dreamaudiosrc)
				gst_object_unref(self->dreamaudiosrc);
			self->dreamaudiosrc = NULL;
			break;
		case GST_STATE_CHANGE_READY_TO_NULL:
			gst_dreamvideosource_encoder_release (self);
			GST_DEBUG_OBJECT (self,"GST_STATE_CHANGE_READY_TO_NULL");
			break;
		default:
			break;
	}

	return sret;
fail:
	GST_ERROR_OBJECT(self,"can't perform encoder ioctl! error: %s (%i)", strerror(errno), errno);
	g_mutex_unlock (&self->mutex);
	return GST_STATE_CHANGE_FAILURE;
}

static void
gst_dreamvideosource_dispose (GObject * gobject)
{
	GstDreamVideoSource *self = GST_DREAMVIDEOSOURCE (gobject);
#ifdef PROVIDE_CLOCK
	if (self->encoder_clock) {
		gst_object_unref (self->encoder_clock);
		self->encoder_clock = NULL;
	}
#endif
#ifdef dump
	close(self->dumpfd);
#endif
	if (self->current_caps)
		gst_caps_unref(self->current_caps);
	if (self->new_caps)
		gst_caps_unref(self->new_caps);
	g_mutex_clear (&self->mutex);
	g_cond_clear (&self->cond);
	GST_DEBUG_OBJECT (self, "disposed");
	G_OBJECT_CLASS (parent_class)->dispose (gobject);
}

#ifdef PROVIDE_CLOCK
static GstClock *gst_dreamvideosource_provide_clock (GstElement * element)
{
	GstDreamVideoSource *self = GST_DREAMVIDEOSOURCE (element);

	if (!self->encoder || self->encoder->fd < 0)
	{
		GST_DEBUG_OBJECT (self, "encoder device not started, can't provide clock!");
		return NULL;
	}

	if (!self->encoder_clock)
	{
		GST_DEBUG_OBJECT (self, "this element is not the clock provider!");
		return NULL;
	}

	return GST_CLOCK_CAST (gst_object_ref (self->encoder_clock));
}
#endif
