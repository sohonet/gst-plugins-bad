/*
 * Copyright (C) 2014 Stefan Ringel
 *
 * Authors:
 *   Stefan Ringel <linuxtv@stefanringel.de>
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

#include <string.h>
#include <stdlib.h>

#include "mpegts.h"
#include "gstmpegts-private.h"

/**
 * SECTION:gst-atsc-section
 * @title: ATSC variants of MPEG-TS sections
 * @short_description: Sections for the various ATSC specifications
 * @include: gst/mpegts/mpegts.h
 *
 */

/* Terrestrial/Cable Virtual Channel Table TVCT/CVCT */
static GstMpegTsAtscVCTSource *
_gst_mpegts_atsc_vct_source_copy (GstMpegTsAtscVCTSource * source)
{
  GstMpegTsAtscVCTSource *copy;

  copy = g_slice_dup (GstMpegTsAtscVCTSource, source);
  copy->descriptors = g_ptr_array_ref (source->descriptors);

  return copy;
}

static void
_gst_mpegts_atsc_vct_source_free (GstMpegTsAtscVCTSource * source)
{
  if (source->descriptors)
    g_ptr_array_unref (source->descriptors);
  g_slice_free (GstMpegTsAtscVCTSource, source);
}

G_DEFINE_BOXED_TYPE (GstMpegTsAtscVCTSource, gst_mpegts_atsc_vct_source,
    (GBoxedCopyFunc) _gst_mpegts_atsc_vct_source_copy,
    (GFreeFunc) _gst_mpegts_atsc_vct_source_free);

static GstMpegTsAtscVCT *
_gst_mpegts_atsc_vct_copy (GstMpegTsAtscVCT * vct)
{
  GstMpegTsAtscVCT *copy;

  copy = g_slice_dup (GstMpegTsAtscVCT, vct);
  copy->sources = g_ptr_array_ref (vct->sources);
  copy->descriptors = g_ptr_array_ref (vct->descriptors);

  return copy;
}

static void
_gst_mpegts_atsc_vct_free (GstMpegTsAtscVCT * vct)
{
  if (vct->sources)
    g_ptr_array_unref (vct->sources);
  if (vct->descriptors)
    g_ptr_array_unref (vct->descriptors);
  g_slice_free (GstMpegTsAtscVCT, vct);
}

G_DEFINE_BOXED_TYPE (GstMpegTsAtscVCT, gst_mpegts_atsc_vct,
    (GBoxedCopyFunc) _gst_mpegts_atsc_vct_copy,
    (GFreeFunc) _gst_mpegts_atsc_vct_free);

static gpointer
_parse_atsc_vct (GstMpegTsSection * section)
{
  GstMpegTsAtscVCT *vct = NULL;
  guint8 *data, *end, source_nb;
  guint32 tmp32;
  guint16 descriptors_loop_length, tmp16;
  guint i;
  GError *err = NULL;

  vct = g_slice_new0 (GstMpegTsAtscVCT);

  data = section->data;
  end = data + section->section_length;

  vct->transport_stream_id = section->subtable_extension;

  /* Skip already parsed data */
  data += 8;

  /* minimum size */
  if (end - data < 2 + 2 + 4)
    goto error;

  vct->protocol_version = *data;
  data += 1;

  source_nb = *data;
  data += 1;

  vct->sources = g_ptr_array_new_full (source_nb,
      (GDestroyNotify) _gst_mpegts_atsc_vct_source_free);

  for (i = 0; i < source_nb; i++) {
    GstMpegTsAtscVCTSource *source;

    /* minimum 32 bytes for a entry, 2 bytes second descriptor
       loop-length, 4 bytes crc */
    if (end - data < 32 + 2 + 4)
      goto error;

    source = g_slice_new0 (GstMpegTsAtscVCTSource);
    g_ptr_array_add (vct->sources, source);

    source->short_name =
        g_convert ((gchar *) data, 14, "utf-8", "utf-16be", NULL, NULL, &err);
    if (err) {
      GST_WARNING ("Failed to convert VCT Source short_name to utf-8: %d %s",
          err->code, err->message);
      GST_MEMDUMP ("UTF-16 string", data, 14);
      g_error_free (err);
    }
    data += 14;

    tmp32 = GST_READ_UINT32_BE (data);
    source->major_channel_number = (tmp32 >> 18) & 0x03FF;
    source->minor_channel_number = (tmp32 >> 8) & 0x03FF;
    source->modulation_mode = tmp32 & 0xF;
    data += 4;

    source->carrier_frequency = GST_READ_UINT32_BE (data);
    data += 4;

    source->channel_TSID = GST_READ_UINT16_BE (data);
    data += 2;

    source->program_number = GST_READ_UINT16_BE (data);
    data += 2;

    tmp16 = GST_READ_UINT16_BE (data);
    source->ETM_location = (tmp16 >> 14) & 0x3;
    source->access_controlled = (tmp16 >> 13) & 0x1;
    source->hidden = (tmp16 >> 12) & 0x1;

    /* only used in CVCT */
    source->path_select = (tmp16 >> 11) & 0x1;
    source->out_of_band = (tmp16 >> 10) & 0x1;

    source->hide_guide = (tmp16 >> 9) & 0x1;
    source->service_type = tmp16 & 0x3f;
    data += 2;

    source->source_id = GST_READ_UINT16_BE (data);
    data += 2;

    descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x03FF;
    data += 2;

    if (end - data < descriptors_loop_length + 6)
      goto error;

    source->descriptors =
        gst_mpegts_parse_descriptors (data, descriptors_loop_length);
    if (source->descriptors == NULL)
      goto error;
    data += descriptors_loop_length;
  }

  descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x03FF;
  data += 2;

  if (end - data < descriptors_loop_length + 4)
    goto error;

  vct->descriptors =
      gst_mpegts_parse_descriptors (data, descriptors_loop_length);
  if (vct->descriptors == NULL)
    goto error;
  data += descriptors_loop_length;

  return (gpointer) vct;

error:
  if (vct)
    _gst_mpegts_atsc_vct_free (vct);

  return NULL;
}

/**
 * gst_mpegts_section_get_atsc_tvct:
 * @section: a #GstMpegTsSection of type %GST_MPEGTS_SECTION_ATSC_TVCT
 *
 * Returns the #GstMpegTsAtscVCT contained in the @section
 *
 * Returns: The #GstMpegTsAtscVCT contained in the section, or %NULL if an error
 * happened.
 */
const GstMpegTsAtscVCT *
gst_mpegts_section_get_atsc_tvct (GstMpegTsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_ATSC_TVCT,
      NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed)
    section->cached_parsed =
        __common_section_checks (section, 16, _parse_atsc_vct,
        (GDestroyNotify) _gst_mpegts_atsc_vct_free);

  return (const GstMpegTsAtscVCT *) section->cached_parsed;
}

/**
 * gst_mpegts_section_get_atsc_cvct:
 * @section: a #GstMpegTsSection of type %GST_MPEGTS_SECTION_ATSC_CVCT
 *
 * Returns the #GstMpegTsAtscVCT contained in the @section
 *
 * Returns: The #GstMpegTsAtscVCT contained in the section, or %NULL if an error
 * happened.
 */
const GstMpegTsAtscVCT *
gst_mpegts_section_get_atsc_cvct (GstMpegTsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_ATSC_CVCT,
      NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed)
    section->cached_parsed =
        __common_section_checks (section, 16, _parse_atsc_vct,
        (GDestroyNotify) _gst_mpegts_atsc_vct_free);

  return (const GstMpegTsAtscVCT *) section->cached_parsed;
}

/* MGT */

static GstMpegTsAtscMGTTable *
_gst_mpegts_atsc_mgt_table_copy (GstMpegTsAtscMGTTable * mgt_table)
{
  GstMpegTsAtscMGTTable *copy;

  copy = g_slice_dup (GstMpegTsAtscMGTTable, mgt_table);
  copy->descriptors = g_ptr_array_ref (mgt_table->descriptors);

  return copy;
}

static void
_gst_mpegts_atsc_mgt_table_free (GstMpegTsAtscMGTTable * mgt_table)
{
  g_ptr_array_unref (mgt_table->descriptors);
  g_slice_free (GstMpegTsAtscMGTTable, mgt_table);
}

G_DEFINE_BOXED_TYPE (GstMpegTsAtscMGTTable, gst_mpegts_atsc_mgt_table,
    (GBoxedCopyFunc) _gst_mpegts_atsc_mgt_table_copy,
    (GFreeFunc) _gst_mpegts_atsc_mgt_table_free);

static GstMpegTsAtscMGT *
_gst_mpegts_atsc_mgt_copy (GstMpegTsAtscMGT * mgt)
{
  GstMpegTsAtscMGT *copy;

  copy = g_slice_dup (GstMpegTsAtscMGT, mgt);
  copy->tables = g_ptr_array_ref (mgt->tables);
  copy->descriptors = g_ptr_array_ref (mgt->descriptors);

  return copy;
}

static void
_gst_mpegts_atsc_mgt_free (GstMpegTsAtscMGT * mgt)
{
  g_ptr_array_unref (mgt->tables);
  g_ptr_array_unref (mgt->descriptors);
  g_slice_free (GstMpegTsAtscMGT, mgt);
}

G_DEFINE_BOXED_TYPE (GstMpegTsAtscMGT, gst_mpegts_atsc_mgt,
    (GBoxedCopyFunc) _gst_mpegts_atsc_mgt_copy,
    (GFreeFunc) _gst_mpegts_atsc_mgt_free);

static gpointer
_parse_atsc_mgt (GstMpegTsSection * section)
{
  GstMpegTsAtscMGT *mgt = NULL;
  guint i = 0;
  guint8 *data, *end;
  guint16 descriptors_loop_length;

  mgt = g_slice_new0 (GstMpegTsAtscMGT);

  data = section->data;
  end = data + section->section_length;

  /* Skip already parsed data */
  data += 8;

  mgt->protocol_version = GST_READ_UINT8 (data);
  data += 1;
  mgt->tables_defined = GST_READ_UINT16_BE (data);
  data += 2;
  mgt->tables = g_ptr_array_new_full (mgt->tables_defined,
      (GDestroyNotify) _gst_mpegts_atsc_mgt_table_free);
  for (i = 0; i < mgt->tables_defined && data + 11 < end; i++) {
    GstMpegTsAtscMGTTable *mgt_table;

    if (data + 11 >= end) {
      GST_WARNING ("MGT data too short to parse inner table num %d", i);
      goto error;
    }

    mgt_table = g_slice_new0 (GstMpegTsAtscMGTTable);
    g_ptr_array_add (mgt->tables, mgt_table);

    mgt_table->table_type = GST_READ_UINT16_BE (data);
    data += 2;
    mgt_table->pid = GST_READ_UINT16_BE (data) & 0x1FFF;
    data += 2;
    mgt_table->version_number = GST_READ_UINT8 (data) & 0x1F;
    data += 1;
    mgt_table->number_bytes = GST_READ_UINT32_BE (data);
    data += 4;
    descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x0FFF;
    data += 2;

    if (data + descriptors_loop_length >= end) {
      GST_WARNING ("MGT data too short to parse inner table descriptors (table "
          "num %d", i);
      goto error;
    }
    mgt_table->descriptors =
        gst_mpegts_parse_descriptors (data, descriptors_loop_length);
    data += descriptors_loop_length;
  }

  descriptors_loop_length = GST_READ_UINT16_BE (data) & 0xFFF;
  data += 2;
  if (data + descriptors_loop_length >= end) {
    GST_WARNING ("MGT data too short to parse descriptors");
    goto error;
  }
  mgt->descriptors =
      gst_mpegts_parse_descriptors (data, descriptors_loop_length);
  data += descriptors_loop_length;

  return (gpointer) mgt;

error:
  if (mgt)
    _gst_mpegts_atsc_mgt_free (mgt);

  return NULL;
}


/**
 * gst_mpegts_section_get_atsc_mgt:
 * @section: a #GstMpegTsSection of type %GST_MPEGTS_SECTION_ATSC_MGT
 *
 * Returns the #GstMpegTsAtscMGT contained in the @section.
 *
 * Returns: The #GstMpegTsAtscMGT contained in the section, or %NULL if an error
 * happened.
 */
const GstMpegTsAtscMGT *
gst_mpegts_section_get_atsc_mgt (GstMpegTsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_ATSC_MGT,
      NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed)
    section->cached_parsed =
        __common_section_checks (section, 17, _parse_atsc_mgt,
        (GDestroyNotify) _gst_mpegts_atsc_mgt_free);

  return (const GstMpegTsAtscMGT *) section->cached_parsed;
}

static GstMpegTsAtscStringSegment *
_gst_mpegts_atsc_string_segment_copy (GstMpegTsAtscStringSegment * seg)
{
  GstMpegTsAtscStringSegment *copy;

  copy = g_slice_dup (GstMpegTsAtscStringSegment, seg);

  return copy;
}

static void
_gst_mpegts_atsc_string_segment_free (GstMpegTsAtscStringSegment * seg)
{
  if (seg->cached_string)
    g_free (seg->cached_string);
  g_slice_free (GstMpegTsAtscStringSegment, seg);
}

static void
_gst_mpegts_atsc_string_segment_decode_string (GstMpegTsAtscStringSegment * seg)
{
  g_return_if_fail (seg->cached_string == NULL);

  if (seg->compression_type != 0) {
    GST_FIXME ("Compressed strings not yet supported");
    return;
  }
  /* FIXME check encoding */

  seg->cached_string =
      g_strndup ((gchar *) seg->compressed_data, seg->compressed_data_size);
}

const gchar *
gst_mpegts_atsc_string_segment_get_string (GstMpegTsAtscStringSegment * seg)
{
  if (!seg->cached_string)
    _gst_mpegts_atsc_string_segment_decode_string (seg);

  return seg->cached_string;
}

G_DEFINE_BOXED_TYPE (GstMpegTsAtscStringSegment, gst_mpegts_atsc_string_segment,
    (GBoxedCopyFunc) _gst_mpegts_atsc_string_segment_copy,
    (GFreeFunc) _gst_mpegts_atsc_string_segment_free);

static GstMpegTsAtscMultString *
_gst_mpegts_atsc_mult_string_copy (GstMpegTsAtscMultString * mstring)
{
  GstMpegTsAtscMultString *copy;

  copy = g_slice_dup (GstMpegTsAtscMultString, mstring);
  copy->segments = g_ptr_array_ref (mstring->segments);

  return copy;
}

static void
_gst_mpegts_atsc_mult_string_free (GstMpegTsAtscMultString * mstring)
{
  g_ptr_array_unref (mstring->segments);
  g_slice_free (GstMpegTsAtscMultString, mstring);
}

G_DEFINE_BOXED_TYPE (GstMpegTsAtscMultString, gst_mpegts_atsc_mult_string,
    (GBoxedCopyFunc) _gst_mpegts_atsc_mult_string_copy,
    (GFreeFunc) _gst_mpegts_atsc_mult_string_free);

static GstMpegTsAtscETT *
_gst_mpegts_atsc_ett_copy (GstMpegTsAtscETT * ett)
{
  GstMpegTsAtscETT *copy;

  copy = g_slice_dup (GstMpegTsAtscETT, ett);
  copy->messages = g_ptr_array_ref (ett->messages);

  return copy;
}

static void
_gst_mpegts_atsc_ett_free (GstMpegTsAtscETT * ett)
{
  g_ptr_array_unref (ett->messages);
  g_slice_free (GstMpegTsAtscETT, ett);
}

G_DEFINE_BOXED_TYPE (GstMpegTsAtscETT, gst_mpegts_atsc_ett,
    (GBoxedCopyFunc) _gst_mpegts_atsc_ett_copy,
    (GFreeFunc) _gst_mpegts_atsc_ett_free);

static gpointer
_parse_ett (GstMpegTsSection * section)
{
  GstMpegTsAtscETT *ett = NULL;
  guint i = 0;
  guint8 *data, *end;
  guint8 num_strings;

  ett = g_slice_new0 (GstMpegTsAtscETT);

  data = section->data;
  end = data + section->section_length;

  /* Skip already parsed data */
  data += 8;

  ett->protocol_version = GST_READ_UINT8 (data);
  data += 1;
  ett->etm_id = GST_READ_UINT32_BE (data);
  data += 4;

  ett->messages = g_ptr_array_new_with_free_func ((GDestroyNotify)
      _gst_mpegts_atsc_mult_string_free);

  if (end - data > 4) {
    /* 1 is the minimum entry size, so no need to check here */
    num_strings = GST_READ_UINT8 (data);
    data += 1;

    for (i = 0; i < num_strings; i++) {
      GstMpegTsAtscMultString *mstring;
      guint8 num_segments;
      gint j;

      mstring = g_slice_new0 (GstMpegTsAtscMultString);
      g_ptr_array_add (ett->messages, mstring);
      mstring->segments =
          g_ptr_array_new_full (num_strings,
          (GDestroyNotify) _gst_mpegts_atsc_string_segment_free);

      /* each entry needs at least 4 bytes (lang code and segments number */
      if (end - data < 4 + 4) {
        GST_WARNING ("PID %d invalid ETT entry length %d",
            section->pid, (gint) (end - 4 - data));
        goto error;
      }

      mstring->iso_639_langcode[0] = GST_READ_UINT8 (data);
      data += 1;
      mstring->iso_639_langcode[1] = GST_READ_UINT8 (data);
      data += 1;
      mstring->iso_639_langcode[2] = GST_READ_UINT8 (data);
      data += 1;
      num_segments = GST_READ_UINT8 (data);
      data += 1;

      for (j = 0; j < num_segments; j++) {
        GstMpegTsAtscStringSegment *seg;

        seg = g_slice_new0 (GstMpegTsAtscStringSegment);
        g_ptr_array_add (mstring->segments, seg);

        /* each entry needs at least 4 bytes (lang code and segments number */
        if (end - data < 3 + 4) {
          GST_WARNING ("PID %d invalid ETT entry length %d",
              section->pid, (gint) (end - 4 - data));
          goto error;
        }

        seg->compression_type = GST_READ_UINT8 (data);
        data += 1;
        seg->mode = GST_READ_UINT8 (data);
        data += 1;
        seg->compressed_data_size = GST_READ_UINT8 (data);
        data += 1;

        if (end - data < seg->compressed_data_size + 4) {
          GST_WARNING ("PID %d invalid ETT entry length %d",
              section->pid, (gint) (end - 4 - data));
          goto error;
        }

        if (seg->compressed_data_size)
          seg->compressed_data = data;
        data += seg->compressed_data_size;
      }

    }
  }

  if (data != end - 4) {
    GST_WARNING ("PID %d invalid ETT parsed %d length %d",
        section->pid, (gint) (data - section->data), section->section_length);
    goto error;
  }

  return (gpointer) ett;

error:
  if (ett)
    _gst_mpegts_atsc_ett_free (ett);

  return NULL;

}

/**
 * gst_mpegts_section_get_atsc_ett:
 * @section: a #GstMpegTsSection of type %GST_MPEGTS_SECTION_ATSC_ETT
 *
 * Returns the #GstMpegTsAtscETT contained in the @section.
 *
 * Returns: The #GstMpegTsAtscETT contained in the section, or %NULL if an error
 * happened.
 */
const GstMpegTsAtscETT *
gst_mpegts_section_get_atsc_ett (GstMpegTsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_ATSC_ETT,
      NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed)
    section->cached_parsed = __common_section_checks (section, 17, _parse_ett,
        (GDestroyNotify) _gst_mpegts_atsc_ett_free);

  return (const GstMpegTsAtscETT *) section->cached_parsed;
}
