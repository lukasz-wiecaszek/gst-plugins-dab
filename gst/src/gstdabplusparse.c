/* GStreamer DAB Plus (superframe) parser
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

/**
 * SECTION:element-dabplusparse
 * @short_description: DAB Plus parser
 * @see_also: #GstAacParse
 *
 * This is a DAB Plus parser which handles DAB Plus Audio Super Frames.
 * In fact DAB Plus Audio Super Frames carry MPEG4 HE AAC coded Access Units as their payload.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 filesrc location=subchannel02.raw ! dabplusparse ! avdec_aac ! audioresample ! audioconvert ! autoaudiosink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/base/gstbitreader.h>
#include <gst/pbutils/pbutils.h>
#include "gstdabplusparse.h"

#define RS_CODE_SIZE           10
#define SUPERFRAME_MIN_SIZE		120
#define N_MAX                 216
#define SUPERFRAME_MAX_SIZE		(SUPERFRAME_MIN_SIZE * N_MAX)
#define FIRECODE_LENGTH	       11
#define MPEGVERSION             4   /* Superframe carries audio coded by MPEG 4 HE AAC v2 */
#define DABPLUS_HEADER_LENGTH  12
#define ADTS_HEADER_LENGTH      7   /* Total byte-length of fixed and variable adts header
                                       prepended during raw to adts conversion */

G_DEFINE_TYPE (GstDabPlusParse, gst_dabplusparse, GST_TYPE_BASE_PARSE);

/* The polynomial is: x^16 + x^14 + x^13 + x^12 + x^11 + x^5 + x^3 + x^2 + x + 1 */
static const guint16 gst_dabplusparse_firecode_crc_table[256] = {
  0x0000, 0x782f, 0xf05e, 0x8871, 0x9893, 0xe0bc, 0x68cd, 0x10e2,
  0x4909, 0x3126, 0xb957, 0xc178, 0xd19a, 0xa9b5, 0x21c4, 0x59eb,
  0x9212, 0xea3d, 0x624c, 0x1a63, 0x0a81, 0x72ae, 0xfadf, 0x82f0,
  0xdb1b, 0xa334, 0x2b45, 0x536a, 0x4388, 0x3ba7, 0xb3d6, 0xcbf9,
  0x5c0b, 0x2424, 0xac55, 0xd47a, 0xc498, 0xbcb7, 0x34c6, 0x4ce9,
  0x1502, 0x6d2d, 0xe55c, 0x9d73, 0x8d91, 0xf5be, 0x7dcf, 0x05e0,
  0xce19, 0xb636, 0x3e47, 0x4668, 0x568a, 0x2ea5, 0xa6d4, 0xdefb,
  0x8710, 0xff3f, 0x774e, 0x0f61, 0x1f83, 0x67ac, 0xefdd, 0x97f2,
  0xb816, 0xc039, 0x4848, 0x3067, 0x2085, 0x58aa, 0xd0db, 0xa8f4,
  0xf11f, 0x8930, 0x0141, 0x796e, 0x698c, 0x11a3, 0x99d2, 0xe1fd,
  0x2a04, 0x522b, 0xda5a, 0xa275, 0xb297, 0xcab8, 0x42c9, 0x3ae6,
  0x630d, 0x1b22, 0x9353, 0xeb7c, 0xfb9e, 0x83b1, 0x0bc0, 0x73ef,
  0xe41d, 0x9c32, 0x1443, 0x6c6c, 0x7c8e, 0x04a1, 0x8cd0, 0xf4ff,
  0xad14, 0xd53b, 0x5d4a, 0x2565, 0x3587, 0x4da8, 0xc5d9, 0xbdf6,
  0x760f, 0x0e20, 0x8651, 0xfe7e, 0xee9c, 0x96b3, 0x1ec2, 0x66ed,
  0x3f06, 0x4729, 0xcf58, 0xb777, 0xa795, 0xdfba, 0x57cb, 0x2fe4,
  0x0803, 0x702c, 0xf85d, 0x8072, 0x9090, 0xe8bf, 0x60ce, 0x18e1,
  0x410a, 0x3925, 0xb154, 0xc97b, 0xd999, 0xa1b6, 0x29c7, 0x51e8,
  0x9a11, 0xe23e, 0x6a4f, 0x1260, 0x0282, 0x7aad, 0xf2dc, 0x8af3,
  0xd318, 0xab37, 0x2346, 0x5b69, 0x4b8b, 0x33a4, 0xbbd5, 0xc3fa,
  0x5408, 0x2c27, 0xa456, 0xdc79, 0xcc9b, 0xb4b4, 0x3cc5, 0x44ea,
  0x1d01, 0x652e, 0xed5f, 0x9570, 0x8592, 0xfdbd, 0x75cc, 0x0de3,
  0xc61a, 0xbe35, 0x3644, 0x4e6b, 0x5e89, 0x26a6, 0xaed7, 0xd6f8,
  0x8f13, 0xf73c, 0x7f4d, 0x0762, 0x1780, 0x6faf, 0xe7de, 0x9ff1,
  0xb015, 0xc83a, 0x404b, 0x3864, 0x2886, 0x50a9, 0xd8d8, 0xa0f7,
  0xf91c, 0x8133, 0x0942, 0x716d, 0x618f, 0x19a0, 0x91d1, 0xe9fe,
  0x2207, 0x5a28, 0xd259, 0xaa76, 0xba94, 0xc2bb, 0x4aca, 0x32e5,
  0x6b0e, 0x1321, 0x9b50, 0xe37f, 0xf39d, 0x8bb2, 0x03c3, 0x7bec,
  0xec1e, 0x9431, 0x1c40, 0x646f, 0x748d, 0x0ca2, 0x84d3, 0xfcfc,
  0xa517, 0xdd38, 0x5549, 0x2d66, 0x3d84, 0x45ab, 0xcdda, 0xb5f5,
  0x7e0c, 0x0623, 0x8e52, 0xf67d, 0xe69f, 0x9eb0, 0x16c1, 0x6eee,
  0x3705, 0x4f2a, 0xc75b, 0xbf74, 0xaf96, 0xd7b9, 0x5fc8, 0x27e7
};

/* GstBaseParse methods */
static gboolean gst_dabplusparse_start               (GstBaseParse * baseparse);
static gboolean gst_dabplusparse_stop                (GstBaseParse * baseparse);
static GstCaps *gst_dabplusparse_sink_getcaps        (GstBaseParse * baseparse, GstCaps * filter);
static GstFlowReturn gst_dabplusparse_handle_frame   (GstBaseParse * baseparse, GstBaseParseFrame * frame, gint * skipsize);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 4, "
        "rate = (int) [ 8000, 48000 ], "
        "channels = (int) [ 1, 2 ], "
        "stream-format = (string) { raw, adts }, "
        "framed = (boolean) true;"));

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "stream-format = (string) superframe;"));

GST_DEBUG_CATEGORY_STATIC (dabplusparse_debug);
#define GST_CAT_DEFAULT dabplusparse_debug

/**
 * gst_dabplusparse_reset:
 * @dabplusparse: #GstDabPlusParse.
 *
 * Resets 'dabplusparse' instance to its defaut state.
 *
 * Returns: None.
 */
static void
gst_dabplusparse_reset (GstDabPlusParse * dabplusparse)
{
  GST_INFO_OBJECT (dabplusparse, "resetting");

  dabplusparse->object_type = -1;
  dabplusparse->sample_rate = -1;
  dabplusparse->channels = -1;

  dabplusparse->i_header_type = DABPLUS_HEADER_NOT_PARSED;
  dabplusparse->o_header_type = DABPLUS_HEADER_NOT_PARSED;

  dabplusparse->superframe_size = 0;
  memset (&dabplusparse->superframe_header, 0377,
    sizeof(dabplusparse->superframe_header));

  gst_base_parse_set_min_frame_size (GST_BASE_PARSE (dabplusparse),
      SUPERFRAME_MAX_SIZE + FIRECODE_LENGTH);
}

/**
 * gst_dabplusparse_class_init:
 * @klass: #GstDabPlusParseClass.
 *
 * Returns: None.
 */
static void
gst_dabplusparse_class_init (GstDabPlusParseClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (dabplusparse_debug, "dabplusparse", 0, "dab+ audio stream parser");

  gst_element_class_set_static_metadata (element_class,
      "DAB+ audio stream parser", "Codec/Parser/Audio",
      "Parses DAB+ audio super frames giving raw aac or adts access units as the result",
      "Lukasz Wiecaszek <lukasz.wiecaszek@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  parse_class->start = GST_DEBUG_FUNCPTR (gst_dabplusparse_start);
  parse_class->stop = GST_DEBUG_FUNCPTR (gst_dabplusparse_stop);
  parse_class->get_sink_caps = GST_DEBUG_FUNCPTR (gst_dabplusparse_sink_getcaps);
  parse_class->handle_frame = GST_DEBUG_FUNCPTR (gst_dabplusparse_handle_frame);
}

/**
 * gst_dabplusparse_init:
 * @dabplusparse: #GstDabPlusParse.
 *
 * Returns: None.
 */
static void
gst_dabplusparse_init (GstDabPlusParse * dabplusparse)
{
  gst_dabplusparse_reset(dabplusparse);
  GST_PAD_SET_ACCEPT_INTERSECT (GST_BASE_PARSE_SINK_PAD (dabplusparse));
  GST_INFO_OBJECT (dabplusparse, "init done");
}

/**
 * gst_dabplusparse_set_src_caps:
 * @dabplusparse: #GstDabPlusParse.
 *
 * Set source pad caps according to current knowledge about the
 * audio stream.
 *
 * Returns: TRUE if caps were successfully set.
 */
static gboolean
gst_dabplusparse_set_src_caps (GstDabPlusParse * dabplusparse)
{
  GstStructure *s;
  GstCaps *src_caps = NULL, *allowed;
  gboolean res = FALSE;
  guint8 codec_data_table[2];
  guint16 codec_data;
  gint sample_rate_idx;

  GST_DEBUG_OBJECT (dabplusparse, "setting src caps ...");

  sample_rate_idx =
      gst_codec_utils_aac_get_index_from_sample_rate (dabplusparse->sample_rate);
  if (G_UNLIKELY (sample_rate_idx < 0)) {
    GST_ERROR_OBJECT (dabplusparse, "not a known sample rate: %d",
      dabplusparse->sample_rate);
    return FALSE;
  }

  src_caps = gst_caps_new_empty_simple ("audio/mpeg");
  s = gst_caps_get_structure (src_caps, 0);

  gst_structure_set (s, "mpegversion", G_TYPE_INT, MPEGVERSION, NULL);
  gst_structure_set (s, "framed", G_TYPE_BOOLEAN, TRUE, NULL);

  /* Generate codec data to be able to set profile/level on the caps */
  codec_data =
      (dabplusparse->object_type << 11) |
      (sample_rate_idx << 7) | (dabplusparse->channels << 3);
  GST_WRITE_UINT16_BE (codec_data_table, codec_data);
  if (!gst_codec_utils_aac_caps_set_level_and_profile (src_caps,
    codec_data_table, G_N_ELEMENTS(codec_data_table)))
      GST_WARNING_OBJECT (dabplusparse,
        "cannot set caps for object_type: %d, sample rate index: %d, channels: %d",
          dabplusparse->object_type, sample_rate_idx, dabplusparse->channels);

  if (dabplusparse->channels > 0)
    gst_structure_set (s, "channels", G_TYPE_INT, dabplusparse->channels, NULL);

  GST_INFO_OBJECT (dabplusparse, "trying adts format first");

  gst_structure_set (s, "stream-format", G_TYPE_STRING, "adts", NULL);
  dabplusparse->o_header_type = DABPLUS_HEADER_ADTS;

  allowed = gst_pad_get_allowed_caps (GST_BASE_PARSE (dabplusparse)->srcpad);
  GST_DEBUG_OBJECT (dabplusparse, "allowed caps: %" GST_PTR_FORMAT, allowed);

  do {
    if (gst_caps_can_intersect (src_caps, allowed))
      break;

    GST_INFO_OBJECT (GST_BASE_PARSE (dabplusparse)->srcpad,
      "caps can not intersect, trying raw format");

    gst_structure_set (s, "stream-format", G_TYPE_STRING, "raw", NULL);
    dabplusparse->o_header_type = DABPLUS_HEADER_RAW;

    if (gst_caps_can_intersect (src_caps, allowed)) {
      /* The codec_data data is according to AudioSpecificConfig,
         ISO/IEC 14496-3, 1.6.2.1 */
      GstBuffer *codec_data_buffer =
        gst_buffer_new_and_alloc (G_N_ELEMENTS(codec_data_table));
      gst_buffer_fill (codec_data_buffer, 0,
        codec_data_table, G_N_ELEMENTS(codec_data_table));
      gst_caps_set_simple (src_caps, "codec_data", GST_TYPE_BUFFER,
          codec_data_buffer, NULL);
      gst_buffer_unref (codec_data_buffer);

      break;
    }

    GST_INFO_OBJECT (GST_BASE_PARSE (dabplusparse)->srcpad,
      "Caps can not intersect, giving up");

    gst_structure_remove_field(s, "stream-format");
    dabplusparse->o_header_type = DABPLUS_HEADER_UNKNOWN;

  } while (0);

  if (allowed)
    gst_caps_unref (allowed);

  GST_DEBUG_OBJECT (dabplusparse, "src caps: %" GST_PTR_FORMAT, src_caps);

  res = gst_pad_set_caps (GST_BASE_PARSE (dabplusparse)->srcpad, src_caps);
  gst_caps_unref (src_caps);
  return res;
}

/**
 * gst_dabplusparse_superframe_header_compare_audio_params:
 * @hdr1: #GstDabPlusSuperframeHeader.
 * @hdr2: #GstDabPlusSuperframeHeader.
 *
 * Resets 'dabplusparse' instance to its defaut state.
 *
 * Returns: TRUE if audio params are the same, FALSE otherwise.
 */
static gboolean
gst_dabplusparse_superframe_header_compare_audio_params(
  const GstDabPlusSuperframeHeader *hdr1,
  const GstDabPlusSuperframeHeader *hdr2)
{
  return (hdr1->dac_rate             == hdr2->dac_rate) &&
         (hdr1->sbr_flag             == hdr2->sbr_flag) &&
         (hdr1->aac_channel_mode     == hdr2->aac_channel_mode) &&
         (hdr1->ps_flag              == hdr2->ps_flag) &&
         (hdr1->mpeg_surround_config == hdr2->mpeg_surround_config);
}

/* caller ensure sufficient data */
static gboolean
gst_dabplusparse_check_firecode (const guint8 * data)
{
  gboolean retval = FALSE;

  do {
    guint16 firecode;
    guint16 header_firecode;

    firecode = 0;
    header_firecode = (data[0] << 8) | (data[1] << 0);

    for (gint i = 2; i < FIRECODE_LENGTH; ++i) {
      /* XOR-in next input byte into MSB of 'firecode', that's our new intermediate divident */
      guint8 pos = ((firecode >> 8) ^ data[i]);
      /* Shift out the MSB used for division per lookuptable and XOR with the firecode */
      firecode = (guint16)((firecode << 8) ^ gst_dabplusparse_firecode_crc_table[pos]);
    }

    if (header_firecode != firecode)
      break;

    /* all zeros will also generate zero firecode, hmmm */
    if (firecode == 0)
      break;

    retval = TRUE;
  } while (0);

  return retval;
}

/* caller ensure sufficient data */
static inline gboolean
gst_dabplusparse_parse_superframe_header (GstDabPlusSuperframeHeader *hdr,
  const guint8 *data, guint framesize)
{
  guint i;
  guint aus_end;

  hdr->header_firecode = (data[0] << 8) | (data[1] << 0);
  hdr->rfa = !!(data[2] & 0x80);
  hdr->dac_rate = !!(data[2] & 0x40);
  hdr->sbr_flag = !!(data[2] & 0x20);
  hdr->aac_channel_mode = !!(data[2] & 0x10);
  hdr->ps_flag = !!(data[2] & 0x08);
  hdr->mpeg_surround_config = data[2] & 0x07;

  if (hdr->sbr_flag) {
    if (!hdr->dac_rate) {
      hdr->num_aus = 2;
      hdr->au[0].start = 5;
    } else {
      hdr->num_aus = 3;
      hdr->au[0].start = 6;
    }
  } else {
    if (!hdr->dac_rate) {
      hdr->num_aus = 4;
      hdr->au[0].start = 8;
    } else {
      hdr->num_aus = 6;
      hdr->au[0].start = 11;
    }
  }

  switch (hdr->num_aus) {
    case 6:
      hdr->au[5].start = (data[9] << 4) | (data[10] >> 4);
      hdr->au[4].start = ((data[7] << 8) & 0xf00) | data[8];
    case 4:
      hdr->au[3].start = (data[6] << 4) | (data[7] >> 4);
    case 3:
      hdr->au[2].start = ((data[4] << 8) & 0xf00) | data[5];
    case 2:
      hdr->au[1].start = (data[3] << 4) | (data[4] >> 4);
      break;
    default:
      return FALSE;
  }

  for(i = 0; i < hdr->num_aus - 1; ++i)
    hdr->au[i].size = hdr->au[i + 1].start - hdr->au[i].start - 2;

  aus_end = framesize - (framesize / SUPERFRAME_MIN_SIZE) * RS_CODE_SIZE;
  hdr->au[i].size = aus_end - hdr->au[i].start - 2;

  return TRUE;
}

/**
 * gst_dabplusparse_detect_stream:
 * @dabplusparse: #GstDabPlusParse.
 * @data: A block of data that needs to be examined for stream characteristics.
 * @avail: Size of the given datablock.
 * @skipsize: If valid stream was found, this will be set to tell the first
 *            audio frame position within the given data.
 *
 * If the stream is detected, TRUE will be returned and #framesize
 * is set to indicate the found frame size. Additionally, #skipsize might
 * be set to indicate the number of bytes that need to be skipped, a.k.a. the
 * position of the frame inside given data chunk.
 *
 * Returns: TRUE on success.
 */
static gboolean
gst_dabplusparse_detect_stream (GstDabPlusParse * dabplusparse,
    const guint8 * data, const guint avail, gint * skipsize)
{
  gboolean found;
  guint i;
  guint offsets[2];
  guint superframe_size;

  GST_DEBUG_OBJECT (dabplusparse, "parsing header data (%u bytes)", avail);

  if (avail < SUPERFRAME_MAX_SIZE + FIRECODE_LENGTH) {
    GST_DEBUG_OBJECT (dabplusparse, "not enough data to check");
    gst_base_parse_set_min_frame_size (
      GST_BASE_PARSE (dabplusparse), SUPERFRAME_MAX_SIZE + FIRECODE_LENGTH);
    return FALSE;
  }

  for (found = FALSE, i = 0; i < avail - FIRECODE_LENGTH; i++) {
    if (gst_dabplusparse_check_firecode (data + i) == TRUE) {
      GST_DEBUG_OBJECT (dabplusparse, "found first superframe at offset %u", i);
      offsets[0] = i;
      found = TRUE;
      break;
    }
  }

  if (!found)
    GST_DEBUG_OBJECT (dabplusparse, "cannot find superframe header");

  if (!found || i) {
    /* Trick: tell the parent class that we didn't find the frame yet,
        but make it skip 'i' amount of bytes. Next time we arrive
        here we have full frame in the beginning of the data. */
    *skipsize = i;
    return FALSE;
  }

  for (found = FALSE, i = SUPERFRAME_MIN_SIZE; i < avail - FIRECODE_LENGTH; i++) {
    if (gst_dabplusparse_check_firecode (data + i) == TRUE) {
      GST_DEBUG_OBJECT (dabplusparse, "found second superframe at offset %u", i);
      offsets[1] = i;
      found = TRUE;
      break;
    }
  }

  if (!found) {
    *skipsize = i;
    return FALSE;
  }

  superframe_size = offsets[1] - offsets[0];

  if (superframe_size % SUPERFRAME_MIN_SIZE) {
    GST_DEBUG_OBJECT (dabplusparse, "superframe size is not multiple of %u", SUPERFRAME_MIN_SIZE);
    *skipsize = i;
    return FALSE;
  }

  GST_INFO_OBJECT (dabplusparse, "superframe size: %u (%u x %u)",
    superframe_size, superframe_size / SUPERFRAME_MIN_SIZE, SUPERFRAME_MIN_SIZE);

  dabplusparse->superframe_size = superframe_size;

  gst_base_parse_set_min_frame_size (GST_BASE_PARSE (dabplusparse), superframe_size);

  return TRUE;
}

/**
 * gst_dabplusparse_get_audio_profile_object_type
 * @dabplusparse: #GstDabPlusParse.
 *
 * Gets the MPEG-2 profile or the MPEG-4 object type value corresponding to the
 * mpegversion and profile of @dabplusparse's src pad caps, according to the
 * values defined by table 1.A.11 in ISO/IEC 14496-3.
 *
 * Returns: the profile or object type value corresponding to @dabplusparse's src
 * pad caps, if such a value exists; otherwise G_MAXUINT8.
 */
static guint8
gst_dabplusparse_get_audio_profile_object_type (GstDabPlusParse * dabplusparse)
{
  GstCaps *srccaps;
  GstStructure *srcstruct;
  const gchar *profile;
  guint8 ret;

  srccaps = gst_pad_get_current_caps (GST_BASE_PARSE_SRC_PAD (dabplusparse));
  srcstruct = gst_caps_get_structure (srccaps, 0);
  profile = gst_structure_get_string (srcstruct, "profile");
  if (G_UNLIKELY (profile == NULL)) {
    gst_caps_unref (srccaps);
    return G_MAXUINT8;
  }

  if (g_strcmp0 (profile, "main") == 0) {
    ret = (guint8) 0U;
  } else if (g_strcmp0 (profile, "lc") == 0) {
    ret = (guint8) 1U;
  } else if (g_strcmp0 (profile, "ssr") == 0) {
    ret = (guint8) 2U;
  } else if (g_strcmp0 (profile, "ltp") == 0) {
    ret = (guint8) 3U;
  } else {
    ret = G_MAXUINT8;
  }

  gst_caps_unref (srccaps);
  return ret;
}

/**
 * gst_dabplusparse_get_audio_channel_configuration
 * @num_channels: number of audio channels.
 *
 * Gets the Channel Configuration value, as defined by table 1.19 in ISO/IEC
 * 14496-3, for a given number of audio channels.
 *
 * Returns: the Channel Configuration value corresponding to @num_channels, if
 * such a value exists; otherwise G_MAXUINT8.
 */
static guint8
gst_dabplusparse_get_audio_channel_configuration (gint num_channels)
{
  if (num_channels >= 1 && num_channels <= 6)   /* Mono up to & including 5.1 */
    return (guint8) num_channels;
  else if (num_channels == 8)   /* 7.1 */
    return (guint8) 7U;
  else
    return G_MAXUINT8;
}

/**
 * gst_dabplusparse_get_audio_sampling_frequency_index:
 * @sample_rate: audio sampling rate.
 *
 * Gets the Sampling Frequency Index value, as defined by table 1.18 in ISO/IEC
 * 14496-3, for a given sampling rate.
 *
 * Returns: the Sampling Frequency Index value corresponding to @sample_rate,
 * if such a value exists; otherwise G_MAXUINT8.
 */
static guint8
gst_dabplusparse_get_audio_sampling_frequency_index (gint sample_rate)
{
  switch (sample_rate) {
    case 96000:
      return 0x0U;
    case 88200:
      return 0x1U;
    case 64000:
      return 0x2U;
    case 48000:
      return 0x3U;
    case 44100:
      return 0x4U;
    case 32000:
      return 0x5U;
    case 24000:
      return 0x6U;
    case 22050:
      return 0x7U;
    case 16000:
      return 0x8U;
    case 12000:
      return 0x9U;
    case 11025:
      return 0xAU;
    case 8000:
      return 0xBU;
    case 7350:
      return 0xCU;
    default:
      return G_MAXUINT8;
  }
}

/**
 * gst_dabplusparse_prepend_adts_headers:
 * @dabplusparse: #GstDabPlusParse.
 * @frame: raw AAC frame to which ADTS headers shall be prepended.
 *
 * Prepends ADTS headers to a raw AAC audio frame.
 *
 * Returns: TRUE if ADTS headers were successfully prepended; FALSE otherwise.
 */
static gboolean
gst_dabplusparse_prepend_adts_headers (GstDabPlusParse * dabplusparse,
    GstBaseParseFrame * frame)
{
  GstMemory *mem;
  guint8 *adts_headers;
  gsize buf_size;
  gsize frame_size;
  guint8 id, profile, channel_configuration, sampling_frequency_index;

  id = 0x0U; /* MPEG4 */
  profile = gst_dabplusparse_get_audio_profile_object_type (dabplusparse);
  if (profile == G_MAXUINT8) {
    GST_ERROR_OBJECT (dabplusparse, "unsupported audio profile or object type");
    return FALSE;
  }
  channel_configuration =
      gst_dabplusparse_get_audio_channel_configuration (dabplusparse->channels);
  if (channel_configuration == G_MAXUINT8) {
    GST_ERROR_OBJECT (dabplusparse, "unsupported number of channels");
    return FALSE;
  }
  sampling_frequency_index =
      gst_dabplusparse_get_audio_sampling_frequency_index (dabplusparse->sample_rate);
  if (sampling_frequency_index == G_MAXUINT8) {
    GST_ERROR_OBJECT (dabplusparse, "unsupported sampling frequency");
    return FALSE;
  }

  frame->out_buffer = gst_buffer_copy (frame->buffer);
  buf_size = gst_buffer_get_size (frame->out_buffer);
  frame_size = buf_size + ADTS_HEADER_LENGTH;

  if (G_UNLIKELY (frame_size >= 0x4000)) {
    GST_ERROR_OBJECT (dabplusparse, "frame size is too big for adts");
    return FALSE;
  }

  adts_headers = (guint8 *) g_malloc0 (ADTS_HEADER_LENGTH);

  /* Note: no error correction bits are added to the resulting ADTS frames */
  adts_headers[0] = 0xFFU;
  adts_headers[1] = 0xF0U | (id << 3) | 0x1U;
  adts_headers[2] = (profile << 6) | (sampling_frequency_index << 2) | 0x2U |
      (channel_configuration & 0x4U);
  adts_headers[3] = ((channel_configuration & 0x3U) << 6) | 0x30U |
      (guint8) (frame_size >> 11);
  adts_headers[4] = (guint8) ((frame_size >> 3) & 0x00FF);
  adts_headers[5] = (guint8) (((frame_size & 0x0007) << 5) + 0x1FU);
  adts_headers[6] = 0xFCU;

  mem = gst_memory_new_wrapped (0, adts_headers, ADTS_HEADER_LENGTH, 0,
      ADTS_HEADER_LENGTH, NULL, NULL);
  gst_buffer_prepend_memory (frame->out_buffer, mem);

  return TRUE;
}

static void
gst_dabplusparse_remove_fields (GstCaps * caps)
{
  guint i, n;

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);

    gst_structure_remove_field (s, "framed");
  }
}

static void
gst_dabplusparse_add_conversion_fields (GstCaps * caps)
{
  guint i, n;

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);

    if (gst_structure_has_field (s, "stream-format")) {
      const GValue *v = gst_structure_get_value (s, "stream-format");

      if (G_VALUE_HOLDS_STRING (v)) {
        const gchar *str = g_value_get_string (v);

        if (strcmp (str, "adts") == 0 || strcmp (str, "raw") == 0) {
          GValue va = G_VALUE_INIT;
          GValue vs = G_VALUE_INIT;

          g_value_init (&va, GST_TYPE_LIST);
          g_value_init (&vs, G_TYPE_STRING);
          g_value_set_string (&vs, "adts");
          gst_value_list_append_value (&va, &vs);
          g_value_set_string (&vs, "raw");
          gst_value_list_append_value (&va, &vs);
          gst_structure_set_value (s, "stream-format", &va);
          g_value_unset (&va);
          g_value_unset (&vs);
        }
      } else if (GST_VALUE_HOLDS_LIST (v)) {
        gboolean contains_raw = FALSE;
        gboolean contains_adts = FALSE;
        guint m = gst_value_list_get_size (v), j;

        for (j = 0; j < m; j++) {
          const GValue *ve = gst_value_list_get_value (v, j);
          const gchar *str;

          if (G_VALUE_HOLDS_STRING (ve) && (str = g_value_get_string (ve))) {
            if (strcmp (str, "adts") == 0)
              contains_adts = TRUE;
            else if (strcmp (str, "raw") == 0)
              contains_raw = TRUE;
          }
        }

        if (contains_adts || contains_raw) {
          GValue va = G_VALUE_INIT;
          GValue vs = G_VALUE_INIT;

          g_value_init (&va, GST_TYPE_LIST);
          g_value_init (&vs, G_TYPE_STRING);
          g_value_copy (v, &va);

          if (!contains_raw) {
            g_value_set_string (&vs, "raw");
            gst_value_list_append_value (&va, &vs);
          }
          if (!contains_adts) {
            g_value_set_string (&vs, "adts");
            gst_value_list_append_value (&va, &vs);
          }

          gst_structure_set_value (s, "stream-format", &va);

          g_value_unset (&vs);
          g_value_unset (&va);
        }
      }
    }
  }
}

/**
 * gst_dabplusparse_start:
 * @baseparse: #GstBaseParse.
 *
 * Implementation of "start" vmethod in #GstBaseParse class.
 *
 * Returns: TRUE if startup succeeded.
 */
static gboolean
gst_dabplusparse_start (GstBaseParse * baseparse)
{
  GstDabPlusParse *dabplusparse;

  dabplusparse = GST_DABPLUSPARSE (baseparse);

  GST_INFO_OBJECT (dabplusparse, "starting");

  gst_dabplusparse_reset (dabplusparse);

  return TRUE;
}

/**
 * gst_dabplusparse_stop:
 * @baseparse: #GstBaseParse.
 *
 * Implementation of "stop" vmethod in #GstBaseParse class.
 *
 * Returns: TRUE if stopping succeeded.
 */
static gboolean
gst_dabplusparse_stop (GstBaseParse * baseparse)
{
  GstDabPlusParse *dabplusparse;

  dabplusparse = GST_DABPLUSPARSE (baseparse);

  GST_INFO_OBJECT (dabplusparse, "stopping");

  return TRUE;
}

/**
 * gst_dabplusparse_sink_getcaps:
 * @baseparse: #GstBaseParse.
 * @filter: #GstCaps
 *
 * Implementation of "get_sink_caps" vmethod in #GstBaseParse class.
 *
 * Returns: Our resulting #GstCaps.
 */
static GstCaps *
gst_dabplusparse_sink_getcaps (GstBaseParse *baseparse, GstCaps *filter)
{
  GstCaps *peercaps, *templ;
  GstCaps *res;
  GstDabPlusParse *dabplusparse;

  dabplusparse = GST_DABPLUSPARSE (baseparse);

  GST_INFO_OBJECT (dabplusparse, "filter caps: %" GST_PTR_FORMAT, filter);

  templ = gst_pad_get_pad_template_caps (GST_BASE_PARSE_SINK_PAD (baseparse));

  if (filter) {
    GstCaps *fcopy = gst_caps_copy (filter);
    /* Remove the fields we convert */
    gst_dabplusparse_remove_fields (fcopy);
    gst_dabplusparse_add_conversion_fields (fcopy);
    peercaps = gst_pad_peer_query_caps (GST_BASE_PARSE_SRC_PAD (baseparse), fcopy);
    gst_caps_unref (fcopy);
  } else
    peercaps = gst_pad_peer_query_caps (GST_BASE_PARSE_SRC_PAD (baseparse), NULL);

  if (peercaps) {
    /* Remove the fields we convert */
    peercaps = gst_caps_make_writable (peercaps);
    gst_dabplusparse_remove_fields (peercaps);
    gst_dabplusparse_add_conversion_fields (peercaps);

    res = gst_caps_intersect_full (peercaps, templ, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (peercaps);
    gst_caps_unref (templ);
  } else {
    res = templ;
  }

  if (filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full (filter, res, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (res);
    res = intersection;
  }

  GST_INFO_OBJECT (dabplusparse, "res caps: %" GST_PTR_FORMAT, res);

  return res;
}

/**
 * gst_dabplusparse_handle_frame:
 * @baseparse: #GstBaseParse.
 * @frame: #GstBaseParseFrame.
 * @skipsize: How much data parent class should skip in order to find the
 *            frame header.
 *
 * Implementation of "handle_frame" vmethod in #GstBaseParse class.
 *
 * Returns: a #GstFlowReturn.
 */
static GstFlowReturn
gst_dabplusparse_handle_frame (GstBaseParse *baseparse,
    GstBaseParseFrame *frame, gint *skipsize)
{
  GstMapInfo map;
  GstDabPlusParse *dabplusparse;
  GstDabPlusSuperframeHeader superframe_header;
  gboolean status;
  GstBuffer *buffer;
  guint i;

  dabplusparse = GST_DABPLUSPARSE (baseparse);
  *skipsize = 0;

  /* need to save buffer from invalidation upon _finish_frame */
  buffer = frame->buffer;
  gst_buffer_map (buffer, &map, GST_MAP_READ);

  do {
    if (dabplusparse->i_header_type != DABPLUS_HEADER_SUPERFRAME) {

      status = gst_dabplusparse_detect_stream (
        dabplusparse, map.data, map.size, skipsize);
      if (!status)
        break;

      dabplusparse->i_header_type = DABPLUS_HEADER_SUPERFRAME;
      dabplusparse->o_header_type = DABPLUS_HEADER_ADTS;
    }

    status = (map.size >= dabplusparse->superframe_size);
    if (G_UNLIKELY (!status)) {
      GST_INFO_OBJECT (dabplusparse, "buffer doesn't contain enough data");
      if (!GST_BASE_PARSE_DRAINING (baseparse))
        return GST_FLOW_ERROR;
      break;
    }

    status = gst_dabplusparse_check_firecode (map.data);
    if (G_UNLIKELY (!status)) {
      GST_INFO_OBJECT (dabplusparse, "buffer doesn't contain valid frame");
      gst_dabplusparse_reset (dabplusparse);
      break;
    }

    status = gst_dabplusparse_parse_superframe_header (
      &superframe_header, map.data, dabplusparse->superframe_size);
    if (G_UNLIKELY (!status)) {
      GST_INFO_OBJECT (dabplusparse, "cannot parse superframe header");
      gst_dabplusparse_reset (dabplusparse);
      break;
    }

  } while (0);

  gst_buffer_unmap (buffer, &map);

  if (G_UNLIKELY (!status))
    return GST_FLOW_OK;

  status = gst_dabplusparse_superframe_header_compare_audio_params(
    &superframe_header, &dabplusparse->superframe_header);

  dabplusparse->superframe_header = superframe_header;

  if (G_UNLIKELY (!status)) { /* if caps has changed */
    const GstDabPlusSuperframeHeader *hdr = &superframe_header;

    GST_INFO_OBJECT (dabplusparse, "caps has changed");
    GST_INFO_OBJECT (dabplusparse,
      "superframe: dac rate: '%s', sbr '%s', aac channel mode: '%s', ps: '%s', surround cfg: %u",
      hdr->dac_rate ? "48 kHz" : "32 kHz",
      hdr->sbr_flag ? "on" : "off",
      hdr->aac_channel_mode ? "stereo" : "mono",
      hdr->ps_flag ? "on" : "off",
      hdr->mpeg_surround_config);

      if (hdr->sbr_flag)
        dabplusparse->object_type = 1 /*5*/;
      else
        dabplusparse->object_type = 1;

      if (hdr->dac_rate) {
        dabplusparse->sample_rate = hdr->sbr_flag ? 24000 : 48000;
      } else {
        dabplusparse->sample_rate = hdr->sbr_flag ? 16000 : 32000;
      }

      if (hdr->mpeg_surround_config > 0) {
        switch (hdr->mpeg_surround_config)
        {
          case 1:
            /* MPEG Surround with 5.1 output channels is used */
            dabplusparse->channels = 6;
            break;
          case 2:
            /* MPEG Surround with 7.1 output channels is used */
            dabplusparse->channels = 8;
            break;
          default:
            dabplusparse->channels = 0;
            break;
        }
      } else
        dabplusparse->channels = hdr->aac_channel_mode + 1;

      if (!gst_dabplusparse_set_src_caps (dabplusparse)) {
        /* If linking fails, we need to return appropriate error */
        return GST_FLOW_NOT_LINKED;
      }

      //gst_base_parse_set_frame_rate (baseparse, dabplusparse->sample_rate, 1024, 2, 2);
  }

  if ((dabplusparse->o_header_type != DABPLUS_HEADER_ADTS) &&
      (dabplusparse->o_header_type != DABPLUS_HEADER_RAW)) {
    GST_ERROR_OBJECT (dabplusparse, "output type not negotiated");
    return GST_FLOW_NOT_LINKED;
  }

  for(i = 0; i < superframe_header.num_aus; ++i) {
    GstBaseParseFrame au_frame;
    GstFlowReturn ret;

    gst_base_parse_frame_init (&au_frame);
    au_frame.flags |= frame->flags;
    au_frame.buffer = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL,
        superframe_header.au[i].start, superframe_header.au[i].size);
    GST_BUFFER_FLAG_UNSET(au_frame.buffer, GST_BUFFER_FLAG_DISCONT);

    if (dabplusparse->o_header_type == DABPLUS_HEADER_ADTS) {
      if (!gst_dabplusparse_prepend_adts_headers (dabplusparse, &au_frame)) {
        GST_ERROR_OBJECT (dabplusparse, "failed to prepend adts headers to frame");
        return GST_FLOW_ERROR;
      }
    } else
      au_frame.out_buffer = gst_buffer_copy (au_frame.buffer);

    ret = gst_base_parse_finish_frame (baseparse, &au_frame, 0);
    if (ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (dabplusparse,
        "gst_base_parse_finish_frame() failed with code %d", ret);
      return ret;
    }
  }

  frame->flags |= GST_BASE_PARSE_FRAME_FLAG_DROP;
  return gst_base_parse_finish_frame (baseparse, frame, dabplusparse->superframe_size);
}
