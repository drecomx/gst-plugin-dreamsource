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

#ifndef __GST_DREAMVIDEOSOURCE_H__
#define __GST_DREAMVIDEOSOURCE_H__

#include "gstdreamsource.h"
#include <gst/video/video.h>

G_BEGIN_DECLS

/* VIDEO field validity flags */
#define VBD_FLAG_DTS_VALID                 0x00000001
/* VIDEO indicator flags */
#define VBD_FLAG_RAP                       0x00010000
/* indicates a video data unit (NALU, EBDU, etc) starts at the beginning of
   this descriptor  - if this is set, then the uiDataUnitID field is valid also */
#define VBD_FLAG_DATA_UNIT_START           0x00020000
#define VBD_FLAG_EXTENDED                  0x80000000

#define VENC_START          _IO('v', 128)
#define VENC_STOP           _IO('v', 129)
#define VENC_SET_BITRATE    _IOW('v', 130, unsigned int)
#define VENC_SET_RESOLUTION _IOW('v', 131, unsigned int) /* need restart */
#define VENC_SET_FRAMERATE  _IOW('v', 132, unsigned int)
#define VENC_SET_PROFILE    _IOW('v', 133, unsigned int) /* need restart */
#define VENC_SET_LEVEL      _IOW('v', 134, unsigned int) /* need restart */
#define VENC_SET_GOP_LENGTH _IOW('v', 135, unsigned int) /* 0 = use specified P-/B-Frames, > 0 use IBBPBBPBBPBBP... for specified amount of ms */
#define VENC_SET_OPEN_GOP   _IOW('v', 136, unsigned int)
#define VENC_SET_B_FRAMES   _IOW('v', 137, unsigned int) /* 0..2 ... forced to 1 when gop length is set */
#define VENC_SET_P_FRAMES   _IOW('v', 138, unsigned int) /* 0..14 ... forced to 2 when gop length is set */
#define VENC_SET_SOURCE     _IOW('v', 140, unsigned int)
#define VENC_GET_STC        _IOR('v', 141, uint32_t)
#define VENC_SET_SLICES_PER_PIC _IOW('v', 142, unsigned int) /* 0 = encode default, max 16 */
#define VENC_SET_NEW_GOP_ON_NEW_SCENE _IOW('v', 143, unsigned int) /* currently not on mipsel */

enum venc_framerate {
        rate_custom = 0,
        rate_25,
        rate_30,
        rate_50,
        rate_60,
        rate_23_976,
        rate_24,
        rate_29_97,
        rate_59_94
};

enum venc_videoformat {
        fmt_custom = 0,
        fmt_720x576,
        fmt_1280x720,
        fmt_1920x1080,
};

enum venc_profile {
        profile_main = 0,
        profile_high,
};

enum venc_bitrate {
        bitrate_min = 16,
        bitrate_max = 200000,
};

enum venc_gop_length {
        gop_length_auto = 0,
        gop_length_max = 15000
};

enum venc_bframes {
        bframes_min = 0,
        bframes_max = 2,
};

enum venc_pframes {
        pframes_min = 0,
        pframes_max = 14,
};

enum venc_slices {
        slices_min = 0,
        slices_max = 16,
};

enum level {
        level1_1,
        level1_2,
        level1_3,
        level2_0,
        level2_1,
        level2_2,
        level3_0,
        level3_1, /* default */
        level3_2,
        level4_0,
        level4_1,
        level4_2,
        level_min = level1_1,
        level_default = level3_1,
        level_max = level4_2
};

typedef enum venc_source {
        GST_DREAMVIDEOSOURCE_INPUT_MODE_LIVE = 0,
        GST_DREAMVIDEOSOURCE_INPUT_MODE_HDMI_IN,
        GST_DREAMVIDEOSOURCE_INPUT_MODE_BACKGROUND
} GstDreamVideoSourceInputMode;

#define GST_TYPE_DREAMVIDEOSOURCE_INPUT_MODE (gst_dreamvideosource_input_mode_get_type ())

struct _VideoBufferDescriptor
{
	CompressedBufferDescriptor stCommon;
	uint32_t uiVideoFlags;
	uint64_t uiDTS;		/* 33-bit DTS value (in 90 Kh or 27Mhz?) */
	uint8_t uiDataUnitType;
};

struct _VideoFormatInfo {
	gint width;
	gint height;

	gint fps_n;	/* framerate numerator */
	gint fps_d;	/* framerate demnominator */

	gint bitrate;

	gint profile;

	gint gop_length;
	gboolean gop_scene;
	gboolean open_gop;

	gint bframes;
	gint pframes;
	gint slices;
	gint level;
};

#define VBDSIZE 	sizeof(VideoBufferDescriptor)
#define VBUFSIZE	(1024*16)
#define VMMAPSIZE	(1024*1024*6)

#define GST_TYPE_DREAMVIDEOSOURCE \
  (gst_dreamvideosource_get_type())
#define GST_DREAMVIDEOSOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DREAMVIDEOSOURCE,GstDreamVideoSource))
#define GST_DREAMVIDEOSOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DREAMVIDEOSOURCE,GstDreamVideoSourceClass))
#define GST_IS_DREAMVIDEOSOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DREAMVIDEOSOURCE))
#define GST_IS_DREAMVIDEOSOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DREAMVIDEOSOURCE))

typedef struct _GstDreamVideoSource        GstDreamVideoSource;
typedef struct _GstDreamVideoSourceClass   GstDreamVideoSourceClass;

typedef struct _VideoFormatInfo            VideoFormatInfo;
typedef struct _VideoBufferDescriptor      VideoBufferDescriptor;

// #define dump 1
#define PROVIDE_CLOCK

struct _GstDreamVideoSource
{
	GstPushSrc element;

	EncoderInfo *encoder;

	GstDreamVideoSourceInputMode input_mode;

	VideoFormatInfo video_info;
	GstCaps *current_caps, *new_caps;

	unsigned int descriptors_available;
	unsigned int descriptors_count;

	int dumpfd;

	GstElement *dreamaudiosrc;
	gint64 dts_offset;

	GMutex mutex;
	GCond cond;
	int control_sock[2];

	gboolean flushing;
	gboolean dts_valid;

	GThread *readthread;
	GQueue current_frames;
	guint buffer_size;

	GstClock *encoder_clock;
};

struct _GstDreamVideoSourceClass
{
	GstPushSrcClass parent_class;
	gint64 (*get_dts_offset) (GstDreamVideoSource *self);
};

GType gst_dreamvideosource_get_type (void);
GType gst_dreamvideosource_input_mode_get_type (void);
gboolean gst_dreamvideosource_plugin_init (GstPlugin * plugin);

void gst_dreamvideosource_set_input_mode (GstDreamVideoSource *self, GstDreamVideoSourceInputMode mode);
GstDreamVideoSourceInputMode gst_dreamvideosource_get_input_mode (GstDreamVideoSource *self);

G_END_DECLS

#endif /* __GST_DREAMVIDEOSOURCE_H__ */

