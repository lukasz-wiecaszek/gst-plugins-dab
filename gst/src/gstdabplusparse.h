/* GStreamer DAB Plus parser
 *
 * Copyright (C) 2020 Lukasz Wiecaszek <lukasz.wiecaszek@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_DABPLUSPARSE_H__
#define __GST_DABPLUSPARSE_H__

#include <gst/gst.h>
#include <gst/base/gstbaseparse.h>

G_BEGIN_DECLS

#define GST_TYPE_DABPLUSPARSE            (gst_dabplusparse_get_type())
#define GST_DABPLUSPARSE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DABPLUSPARSE, GstDabPlusParse))
#define GST_DABPLUSPARSE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),  GST_TYPE_DABPLUSPARSE, GstDabPlusParseClass))
#define GST_DABPLUSPARSE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),  GST_TYPE_DABPLUSPARSE, GstDabPlusParseClass))
#define GST_IS_DABPLUSPARSE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DABPLUSPARSE))
#define GST_IS_DABPLUSPARSE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),  GST_TYPE_DABPLUSPARSE))
#define GST_DABPLUSPARSE_CAST(obj)       ((GstDabPlusParse *)(obj))

typedef enum {
  DABPLUS_HEADER_NOT_PARSED,
  DABPLUS_HEADER_UNKNOWN,
  DABPLUS_HEADER_SUPERFRAME,
  DABPLUS_HEADER_RAW,
  DABPLUS_HEADER_ADTS
} GstDabPlusHeaderType;

typedef struct {
  guint header_firecode;
  guint rfa;
  guint dac_rate;
  guint sbr_flag;
  guint aac_channel_mode;
  guint ps_flag;
  guint mpeg_surround_config;
  guint num_aus;
  struct {
    guint start;
    guint size;
  } au[6];
} GstDabPlusSuperframeHeader;

typedef struct _GstDabPlusParse      GstDabPlusParse;
typedef struct _GstDabPlusParseClass GstDabPlusParseClass;

/**
 * GstDabPlusParse:
 *
 * The opaque GstDabPlusParse data structure.
 */
struct _GstDabPlusParse {
  GstBaseParse base_parse;

  /* Stream type related info */
  gint object_type;
  gint sample_rate;
  gint channels;

  GstDabPlusHeaderType i_header_type;
  GstDabPlusHeaderType o_header_type;

  guint superframe_size;
  GstDabPlusSuperframeHeader superframe_header;
};

/**
 * GstDabPlusParseClass:
 * @parent_class: Element's parent class.
 *
 * The opaque GstDabPlusParseClass data structure.
 */
struct _GstDabPlusParseClass {
  GstBaseParseClass parent_class;
};

GType gst_dabplusparse_get_type (void);

G_END_DECLS

#endif /* __GST_DABPLUSPARSE_H__ */