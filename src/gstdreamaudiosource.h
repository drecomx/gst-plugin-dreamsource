/*
 * GStreamer dreamaudiosource
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

#ifndef __GST_DREAMAUDIOSOURCE_H__
#define __GST_DREAMAUDIOSOURCE_H__

#include "gstdreamsource.h"

G_BEGIN_DECLS

struct _AudioBufferDescriptor
{
	CompressedBufferDescriptor stCommon;
	uint32_t uiRawDataOffset;
	size_t   uiRawDataLength;
	uint8_t  uiDataUnitType;
};

struct _AudioFormatInfo {
	gint bitrate;
	gint samplerate;
};

#define ABDSIZE		sizeof(AudioBufferDescriptor)
#define ABUFSIZE	(1024*16)
#define AMMAPSIZE	(256*1024)

#define AENC_START        _IO('v', 128)
#define AENC_STOP         _IO('v', 129)
#define AENC_SET_BITRATE  _IOW('v', 130, unsigned int)
#define AENC_SET_SOURCE   _IOW('v', 140, unsigned int)
#define AENC_GET_STC      _IOR('v', 141, uint32_t)

typedef enum aenc_source {
        GST_DREAMAUDIOSOURCE_INPUT_MODE_LIVE = 0,
        GST_DREAMAUDIOSOURCE_INPUT_MODE_HDMI_IN,
        GST_DREAMAUDIOSOURCE_INPUT_MODE_BACKGROUND
} GstDreamAudioSourceInputMode;

#define GST_TYPE_DREAMAUDIOSOURCE_INPUT_MODE (gst_dreamaudiosource_input_mode_get_type ())

#define GST_TYPE_DREAMAUDIOSOURCE \
  (gst_dreamaudiosource_get_type())
#define GST_DREAMAUDIOSOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DREAMAUDIOSOURCE,GstDreamAudioSource))
#define GST_DREAMAUDIOSOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DREAMAUDIOSOURCE,GstDreamAudioSourceClass))
#define GST_IS_DREAMAUDIOSOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DREAMAUDIOSOURCE))
#define GST_IS_DREAMAUDIOSOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DREAMAUDIOSOURCE))

typedef struct _GstDreamAudioSource        GstDreamAudioSource;
typedef struct _GstDreamAudioSourceClass   GstDreamAudioSourceClass;

typedef struct _AudioFormatInfo            AudioFormatInfo;
typedef struct _AudioBufferDescriptor      AudioBufferDescriptor;

// #define dump 1
#define PROVIDE_CLOCK

struct _buffer_memorytracker
{
	GstDreamAudioSource *self;
	GstBuffer *buffer;
	guint uiOffset;
	guint uiLength;
};

struct _GstDreamAudioSource
{
	GstPushSrc element;

	EncoderInfo *encoder;

	GstDreamAudioSourceInputMode input_mode;

	AudioFormatInfo audio_info;

	unsigned int descriptors_available;
	unsigned int descriptors_count;

	int dumpfd;
	goffset dumpsize;

	GstElement *dreamvideosrc;
	gint64 dts_offset;

	GMutex mutex;
	GCond cond;
	int control_sock[2];

	gboolean flushing;

	GThread *readthread;
	GQueue current_frames;
	guint buffer_size;
	GList *memtrack_list;

	GstClock *encoder_clock;
	GstClockTime last_ts;
};

struct _GstDreamAudioSourceClass
{
	GstPushSrcClass parent_class;
	/* signals */
	void (*signal_lost)  (GstDreamAudioSource *self);
	/* actions */
	gint64 (*get_dts_offset) (GstDreamAudioSource *self);
};

GType gst_dreamaudiosource_get_type (void);
GType gst_dreamaudiosource_input_mode_get_type (void);
gboolean gst_dreamaudiosource_plugin_init (GstPlugin * plugin);

void gst_dreamaudiosource_set_input_mode (GstDreamAudioSource *self, GstDreamAudioSourceInputMode mode);
GstDreamAudioSourceInputMode gst_dreamaudiosource_get_input_mode (GstDreamAudioSource *self);

G_END_DECLS

#endif /* __GST_DREAMAUDIOSOURCE_H__ */

