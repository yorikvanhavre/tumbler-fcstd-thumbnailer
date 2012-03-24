/* vi:set et ai sw=2 sts=2 ts=2: */
/*-
 * Copyright (c) 2012 Yorik van Havre <yorik@uncreated.net>
 * Copyright (c) 2011 Jannis Pohlmann <jannis@xfce.org>
 * Copyright (c) 2011 Nick Schermer <nick@xfce.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Parts are based on the gsf-office-thumbnailer in libgsf, which is
 * written by Federico Mena-Quintero <federico@novell.com>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <math.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <gsf/gsf.h>
#include <gsf/gsf-input-memory.h>
#include <gsf/gsf-input-gio.h>
#include <gsf/gsf-infile.h>
#include <gsf/gsf-infile-msole.h>
#include <gsf/gsf-infile-zip.h>
#include <gsf/gsf-open-pkg-utils.h>
#include <gsf/gsf-clip-data.h>
#include <gsf/gsf-doc-meta-data.h>
#include <gsf/gsf-meta-names.h>
#include <gsf/gsf-msole-utils.h>

#include <tumbler/tumbler.h>

#include <fcstd-thumbnailer.h>


#define OPEN_XML_SCHEMA "http://schemas.openxmlformats.org/package/2006/relationships/metadata/thumbnail"



static void fcstd_thumbnailer_create   (TumblerAbstractThumbnailer *thumbnailer,
                                      GCancellable               *cancellable,
                                      TumblerFileInfo            *info);



struct _FcstdThumbnailerClass
{
  TumblerAbstractThumbnailerClass __parent__;
};

struct _FcstdThumbnailer
{
  TumblerAbstractThumbnailer __parent__;
};



G_DEFINE_DYNAMIC_TYPE (FcstdThumbnailer,
                       fcstd_thumbnailer,
                       TUMBLER_TYPE_ABSTRACT_THUMBNAILER);



void
fcstd_thumbnailer_register (TumblerProviderPlugin *plugin)
{
  fcstd_thumbnailer_register_type (G_TYPE_MODULE (plugin));
}



static void
fcstd_thumbnailer_class_init (FcstdThumbnailerClass *klass)
{
  TumblerAbstractThumbnailerClass *abstractthumbnailer_class;

  abstractthumbnailer_class = TUMBLER_ABSTRACT_THUMBNAILER_CLASS (klass);
  abstractthumbnailer_class->create = fcstd_thumbnailer_create;
}



static void
fcstd_thumbnailer_class_finalize (FcstdThumbnailerClass *klass)
{
}



static void
fcstd_thumbnailer_init (FcstdThumbnailer *thumbnailer)
{
}



static void
fcstd_thumbnailer_size_prepared (GdkPixbufLoader  *loader,
                               gint              source_width,
                               gint              source_height,
                               TumblerThumbnail *thumbnail)
{
  TumblerThumbnailFlavor *flavor;
  gint                    dest_width;
  gint                    dest_height;
  gdouble                 hratio;
  gdouble                 wratio;

  g_return_if_fail (GDK_IS_PIXBUF_LOADER (loader));
  g_return_if_fail (TUMBLER_IS_THUMBNAIL (thumbnail));

  flavor = tumbler_thumbnail_get_flavor (thumbnail);
  tumbler_thumbnail_flavor_get_size (flavor, &dest_width, &dest_height);
  g_object_unref (flavor);

  if (source_width <= dest_width && source_height <= dest_height)
    {
      /* do not scale the image */
      dest_width = source_width;
      dest_height = source_height;
    }
  else
    {
      /* determine which axis needs to be scaled down more */
      wratio = (gdouble) source_width / (gdouble) dest_width;
      hratio = (gdouble) source_height / (gdouble) dest_height;

      /* adjust the other axis */
      if (hratio > wratio)
        dest_width = rint (source_width / hratio);
     else
        dest_height = rint (source_height / wratio);
    }

  gdk_pixbuf_loader_set_size (loader, MAX (dest_width, 1), MAX (dest_height, 1));
}



static GdkPixbuf *
fcstd_thumbnailer_create_from_data (const guchar     *data,
                                  gsize             bytes,
                                  TumblerThumbnail *thumbnail,
                                  GError          **error)
{
  GdkPixbufLoader *loader;
  GdkPixbuf       *pixbuf = NULL;
  GError          *err = NULL;

  g_return_val_if_fail (TUMBLER_IS_THUMBNAIL (thumbnail), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  loader = gdk_pixbuf_loader_new ();
  g_signal_connect (loader, "size-prepared",
      G_CALLBACK (fcstd_thumbnailer_size_prepared), thumbnail);
  if (gdk_pixbuf_loader_write (loader, data, bytes, &err))
    {
      if (gdk_pixbuf_loader_close (loader, &err))
        {
          pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
          if (pixbuf != NULL)
            g_object_ref (G_OBJECT (pixbuf));
        }
    }
  else
    {
      gdk_pixbuf_loader_close (loader, NULL);
    }
  g_object_unref (loader);

  /* forward errors to the caller */
  if (err != NULL)
    g_propagate_error (error, err);

  return pixbuf;
}



static GdkPixbuf *
fcstd_thumbnailer_create_zip (GsfInfile        *infile,
                            TumblerThumbnail *thumbnail,
                            GError          **error)
{
  GsfInput     *thumb_file;
  gsize         bytes;
  const guint8 *data;
  GdkPixbuf    *pixbuf = NULL;

  g_return_val_if_fail (GSF_IS_INFILE_ZIP (infile), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  /* get freecad thumbnail */
  thumb_file = gsf_infile_child_by_vname (infile, "thumbnails", "Thumbnail.png", NULL);
  if (thumb_file == NULL)
    return NULL;

  /* read data and generate a pixbuf */
  bytes = gsf_input_remaining (thumb_file);
  data = gsf_input_read (thumb_file, bytes, NULL);
  if (data != NULL)
    pixbuf = fcstd_thumbnailer_create_from_data (data, bytes, thumbnail, error);

  g_object_unref (thumb_file);

  return pixbuf;
}



static void
fcstd_thumbnailer_create (TumblerAbstractThumbnailer *thumbnailer,
                        GCancellable               *cancellable,
                        TumblerFileInfo            *info)
{
  GsfInput         *input = NULL;
  TumblerThumbnail *thumbnail;
  const gchar      *uri;
  GFile            *file;
  GError           *error = NULL;
  gchar            *path;
  GsfInfile        *infile;
  GdkPixbuf        *pixbuf = NULL;
  TumblerImageData  data;

  g_return_if_fail (IS_FCSTD_THUMBNAILER (thumbnailer));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
  g_return_if_fail (TUMBLER_IS_FILE_INFO (info));

  /* do nothing if cancelled */
  if (g_cancellable_is_cancelled (cancellable))
    return;

  uri = tumbler_file_info_get_uri (info);
  file = g_file_new_for_uri (uri);

  if (g_file_is_native (file))
    {
      /* try to mmap the file */
      path = g_file_get_path (file);
      input = gsf_input_mmap_new (path, NULL);
      g_free (path);
    }

  if (input == NULL)
    {
      /* fall-back to normal file reading */
      input = gsf_input_gio_new (file, &error);
      if (G_UNLIKELY (input == NULL))
        {
          g_signal_emit_by_name (thumbnailer, "error", uri, error->code, error->message);
          g_error_free (error);
          return;
        }
    }

  thumbnail = tumbler_file_info_get_thumbnail (info);

  /* extract the file */
  input = gsf_input_uncompress (input);

  /* try to detect file format and run thumbnail extractor */
  infile = gsf_infile_zip_new (input, NULL);
  if (infile != NULL)
    {
      pixbuf = fcstd_thumbnailer_create_zip (infile, thumbnail, &error);
      g_object_unref (infile);
    }
  else
    {
      g_set_error (&error, TUMBLER_ERROR, TUMBLER_ERROR_NO_CONTENT,
                    _("Thumbnail could not be inferred from file contents"));
    }

  if (pixbuf != NULL)
    {
      data.data = gdk_pixbuf_get_pixels (pixbuf);
      data.has_alpha = gdk_pixbuf_get_has_alpha (pixbuf);
      data.bits_per_sample = gdk_pixbuf_get_bits_per_sample (pixbuf);
      data.width = gdk_pixbuf_get_width (pixbuf);
      data.height = gdk_pixbuf_get_height (pixbuf);
      data.rowstride = gdk_pixbuf_get_rowstride (pixbuf);
      data.colorspace = (TumblerColorspace) gdk_pixbuf_get_colorspace (pixbuf);

      tumbler_thumbnail_save_image_data (thumbnail, &data,
                                         tumbler_file_info_get_mtime (info),
                                         NULL, &error);

      g_object_unref (pixbuf);
    }

  if (error != NULL)
    {
      g_signal_emit_by_name (thumbnailer, "error", uri, error->code, error->message);
      g_error_free (error);
    }
  else
    {
      g_signal_emit_by_name (thumbnailer, "ready", uri);
    }

  g_object_unref (input);
  g_object_unref (thumbnail);
}
