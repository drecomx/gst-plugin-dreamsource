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

#ifndef __GST_DREAMSOURCE_H__
#define __GST_DREAMSOURCE_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>

#include "gstdreamsource-marshal.h"

#define CONTROL_RUN            'R'     /* start producing frames */
#define CONTROL_PAUSE          'P'     /* pause producing frames */
#define CONTROL_STOP           'S'     /* stop the select call */
#define CONTROL_SOCKETS(src)   src->control_sock
#define WRITE_SOCKET(src)      src->control_sock[1]
#define READ_SOCKET(src)       src->control_sock[0]

#define CLEAR_COMMAND(src)                  \
G_STMT_START {                              \
  char c;                                   \
  read(READ_SOCKET(src), &c, 1);            \
} G_STMT_END

#define SEND_COMMAND(src, command)          \
G_STMT_START {                              \
  int G_GNUC_UNUSED _res; unsigned char c; c = command;   \
  _res = write (WRITE_SOCKET(src), &c, 1);  \
} G_STMT_END

#define READ_COMMAND(src, command, res)        \
G_STMT_START {                                 \
  res = read(READ_SOCKET(src), &command, 1);   \
} G_STMT_END

typedef enum
{
	READTHREADSTATE_NONE = 0,
	READTRREADSTATE_PAUSED,
	READTRREADSTATE_RUNNING,
	READTHREADSTATE_STOP
} GstDreamSourceReadthreadState;

G_BEGIN_DECLS

typedef struct _CompressedBufferDescriptor CompressedBufferDescriptor;
typedef struct _EncoderInfo                EncoderInfo;

#define ENCTIME_TO_GSTTIME(time)           (gst_util_uint64_scale ((time), GST_USECOND, 27LL))
#define MPEGTIME_TO_GSTTIME(time)          (gst_util_uint64_scale ((time), GST_MSECOND/10, 9LL))

/* validity flags */
#define CDB_FLAG_ORIGINALPTS_VALID         0x00000001
#define CDB_FLAG_PTS_VALID                 0x00000002
#define CDB_FLAG_ESCR_VALID                0x00000004
#define CDB_FLAG_TICKSPERBIT_VALID         0x00000008
#define CDB_FLAG_SHR_VALID                 0x00000010
#define CDB_FLAG_STCSNAPSHOT_VALID         0x00000020

/* indicator flags */
#define CDB_FLAG_FRAME_START               0x00010000
#define CDB_FLAG_EOS                       0x00020000
#define CDB_FLAG_EMPTY_FRAME               0x00040000
#define CDB_FLAG_FRAME_END                 0x00080000
#define CDB_FLAG_EOC                       0x00100000

#define CDB_FLAG_METADATA                  0x40000000
#define CDB_FLAG_EXTENDED                  0x80000000

struct _CompressedBufferDescriptor
{
	uint32_t uiFlags;

	/* Timestamp Parameters */
	uint32_t uiOriginalPTS;      /* 32-bit original PTS value (in 45 Khz or 27Mhz?) */
	uint64_t uiPTS;              /* 33-bit PTS value (in 90 Khz) */
	uint64_t uiSTCSnapshot;      /* 42-bit STC snapshot when frame received by the encoder (in 27Mhz) */

	/* Transmission Parameters */
	uint32_t uiESCR;             /* Expected mux transmission start time for the first bit of the data (in 27 Mhz) */

	uint16_t uiTicksPerBit;
	int16_t iSHR;

	/* Buffer Parameters */
	unsigned uiOffset;           /* REQUIRED: offset of frame data from frame buffer base address (in bytes) */
	size_t uiLength;             /* REQUIRED: 0 if fragment is empty, e.g. for EOS entry (in bytes) */
	unsigned uiReserved;         /* Unused field */
};

struct _EncoderInfo {
	int fd;

	/* descriptor space */
	unsigned char *buffer;

	/* mmapp'ed data buffer */
	unsigned char *cdb;

	guint         used_range_min;
	guint         used_range_max;
};

#define ENC_GET_STC      _IOR('v', 141, uint32_t)

#define GST_TYPE_DREAMSOURCE_CLOCK \
  (gst_dreamsource_clock_get_type())
#define GST_DREAMSOURCE_CLOCK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DREAMSOURCE_CLOCK,GstDreamSourceClock))
#define GST_DREAMSOURCE_CLOCK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DREAMSOURCE_CLOCK,GstDreamSourceClockClass))
#define GST_IS_DreamSource_CLOCK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DREAMSOURCE_CLOCK))
#define GST_IS_DreamSource_CLOCK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DREAMSOURCE_CLOCK))
#define GST_DREAMSOURCE_CLOCK_CAST(obj) \
  ((GstDreamSourceClock*)(obj))

typedef struct _GstDreamSourceClock GstDreamSourceClock;
typedef struct _GstDreamSourceClockClass GstDreamSourceClockClass;

struct _GstDreamSourceClock
{
	GstSystemClock clock;

	uint32_t prev_stc;
	uint32_t first_stc;
	uint64_t stc_offset;
	int fd;
};

struct _GstDreamSourceClockClass
{
	GstSystemClockClass parent_class;
};

GType gst_dreamsource_clock_get_type (void);
GstClock *gst_dreamsource_clock_new (const gchar * name, int fd);

G_END_DECLS

#endif /* __GST_DREAMSOURCE_H__ */
