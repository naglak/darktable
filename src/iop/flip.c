/*
    This file is part of darktable,
    copyright (c) 2009--2011 johannes hanika.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <gtk/gtk.h>
#include <inttypes.h>
#include <gdk/gdkkeysyms.h>
#include <assert.h>

#include "develop/develop.h"
#include "develop/imageop.h"
#include "control/control.h"
#include "control/conf.h"
#include "common/debug.h"
#include "common/imageio.h"
#include "common/opencl.h"
#include "dtgtk/label.h"
#include "dtgtk/resetlabel.h"
#include "dtgtk/button.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"
#include "gui/draw.h"
#include "gui/presets.h"

DT_MODULE(1)

typedef struct dt_iop_flip_params_t
{
  int32_t orientation;
}
dt_iop_flip_params_t;

typedef struct dt_iop_flip_data_t
{
  int32_t orientation;
}
dt_iop_flip_data_t;

typedef struct dt_iop_flip_global_data_t
{
  int kernel_flip;
}
dt_iop_flip_global_data_t;


// helper to count corners in for loops:
static void get_corner(const int32_t *aabb, const int i, int32_t *p)
{
  for(int k=0; k<2; k++) p[k] = aabb[2*((i>>k)&1) + k];
}

static void adjust_aabb(const int32_t *p, int32_t *aabb)
{
  aabb[0] = MIN(aabb[0], p[0]);
  aabb[1] = MIN(aabb[1], p[1]);
  aabb[2] = MAX(aabb[2], p[0]);
  aabb[3] = MAX(aabb[3], p[1]);
}

const char *name()
{
  return _("orientation");
}

int
groups ()
{
  return IOP_GROUP_BASIC;
}

int
operation_tags ()
{
  return IOP_TAG_DISTORT;
}

int flags()
{
  return IOP_FLAGS_ALLOW_TILING | IOP_FLAGS_TILING_FULL_ROI | IOP_FLAGS_ONE_INSTANCE;
}


static void
backtransform(const int32_t *x, int32_t *o, const int32_t orientation, int32_t iw, int32_t ih)
{
  if(orientation & 4)
  {
    o[1] = x[0];
    o[0] = x[1];
    int32_t tmp = iw;
    iw = ih;
    ih = tmp;
  }
  else
  {
    o[0] = x[0];
    o[1] = x[1];
  }
  if(orientation & 2)
  {
    o[1] = ih - o[1] - 1;
  }
  if(orientation & 1)
  {
    o[0] = iw - o[0] - 1;
  }
}

int distort_transform(dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, float *points, int points_count)
{
  //if (!self->enabled) return 2;
  dt_iop_flip_data_t *d = (dt_iop_flip_data_t *)piece->data;

  float x,y;

  for (int i=0; i<points_count*2; i+=2)
  {
    x = points[i];
    y = points[i+1];
    if(d->orientation & 2) y = piece->buf_in.height - points[i+1];
    if(d->orientation & 1) x = piece->buf_in.width - points[i];
    if(d->orientation & 4)
    {
      float yy = y;
      y = x;
      x = yy;
    }
    points[i] = x;
    points[i+1] = y;
  }

  return 1;
}
int distort_backtransform(dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, float *points, int points_count)
{
  //if (!self->enabled) return 2;
  dt_iop_flip_data_t *d = (dt_iop_flip_data_t *)piece->data;

  float x,y;

  for (int i=0; i<points_count*2; i+=2)
  {
    if(d->orientation & 4)
    {
      y = points[i];
      x = points[i+1];
    }
    else
    {
      x = points[i];
      y = points[i+1];
    }
    if(d->orientation & 2) y = piece->buf_in.height - y;
    if(d->orientation & 1) x = piece->buf_in.width - x;
    points[i] = x;
    points[i+1] = y;
  }

  return 1;
}

// 1st pass: how large would the output be, given this input roi?
// this is always called with the full buffer before processing.
void modify_roi_out(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece, dt_iop_roi_t *roi_out, const dt_iop_roi_t *roi_in)
{
  *roi_out = *roi_in;
  dt_iop_flip_data_t *d = (dt_iop_flip_data_t *)piece->data;

  // transform whole buffer roi
  if(d->orientation & 4)
  {
    roi_out->width  = roi_in->height;
    roi_out->height = roi_in->width;
  }

  piece->pipe->iflipped = d->orientation & 4;
}

// 2nd pass: which roi would this operation need as input to fill the given output region?
void modify_roi_in(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece, const dt_iop_roi_t *roi_out, dt_iop_roi_t *roi_in)
{
  dt_iop_flip_data_t *d = (dt_iop_flip_data_t *)piece->data;
  *roi_in = *roi_out;
  // transform aabb back to roi_in

  // this aabb contains all valid points (thus the -1)
  int32_t p[2], o[2], aabb[4] = {roi_out->x, roi_out->y, roi_out->x+roi_out->width-1, roi_out->y+roi_out->height-1};
  int32_t aabb_in[4] = {INT_MAX, INT_MAX, INT_MIN, INT_MIN};
  for(int c=0; c<4; c++)
  {
    // get corner points of roi_out
    get_corner(aabb, c, p);
    // backtransform aabb
    backtransform(p, o, d->orientation, piece->buf_out.width*roi_out->scale, piece->buf_out.height*roi_out->scale);
    // transform to roi_in space, get aabb.
    adjust_aabb(o, aabb_in);
  }

  // adjust roi_in to minimally needed region
  roi_in->x      = aabb_in[0];
  roi_in->y      = aabb_in[1];
  // to convert valid points to widths, we need to add one
  roi_in->width  = aabb_in[2]-aabb_in[0]+1;
  roi_in->height = aabb_in[3]-aabb_in[1]+1;

  // sanity check.
  roi_in->x = CLAMP(roi_in->x, 0, piece->pipe->iwidth);
  roi_in->y = CLAMP(roi_in->y, 0, piece->pipe->iheight);
  roi_in->width = CLAMP(roi_in->width, 1, piece->pipe->iwidth - roi_in->x);
  roi_in->height = CLAMP(roi_in->height, 1, piece->pipe->iheight - roi_in->y);
}

// 3rd (final) pass: you get this input region (may be different from what was requested above),
// do your best to fill the output region!
void process (struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, void *ivoid, void *ovoid, const dt_iop_roi_t *roi_in, const dt_iop_roi_t *roi_out)
{
  dt_iop_flip_data_t *d = (dt_iop_flip_data_t *)piece->data;

  const int bpp = sizeof(float)*piece->colors;
  const int stride = bpp*roi_in->width;

  dt_imageio_flip_buffers((char *)ovoid, (const char *)ivoid, bpp,
                          roi_in->width, roi_in->height, roi_in->width, roi_in->height, stride, d->orientation);
}



#ifdef HAVE_OPENCL
int
process_cl (struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, cl_mem dev_in, cl_mem dev_out, const dt_iop_roi_t *roi_in, const dt_iop_roi_t *roi_out)
{
  dt_iop_flip_data_t *data = (dt_iop_flip_data_t *)piece->data;
  dt_iop_flip_global_data_t *gd = (dt_iop_flip_global_data_t *)self->data;
  cl_int err = -999;

  const int devid = piece->pipe->devid;
  const int width = roi_in->width;
  const int height = roi_in->height;
  const int orientation = data->orientation;

  size_t sizes[] = { ROUNDUPWD(width), ROUNDUPWD(height), 1};

  dt_opencl_set_kernel_arg(devid, gd->kernel_flip, 0, sizeof(cl_mem), (void *)&dev_in);
  dt_opencl_set_kernel_arg(devid, gd->kernel_flip, 1, sizeof(cl_mem), (void *)&dev_out);
  dt_opencl_set_kernel_arg(devid, gd->kernel_flip, 2, sizeof(int), (void *)&width);
  dt_opencl_set_kernel_arg(devid, gd->kernel_flip, 3, sizeof(int), (void *)&height);
  dt_opencl_set_kernel_arg(devid, gd->kernel_flip, 4, sizeof(int), (void *)&orientation);
  err = dt_opencl_enqueue_kernel_2d(devid, gd->kernel_flip, sizes);

  if(err != CL_SUCCESS) goto error;
  return TRUE;

error:
  dt_print(DT_DEBUG_OPENCL, "[opencl_flip] couldn't enqueue kernel! %d\n", err);
  return FALSE;
}
#endif

void init_global(dt_iop_module_so_t *module)
{
  const int program = 2; // basic.cl, from programs.conf
  dt_iop_flip_global_data_t *gd = (dt_iop_flip_global_data_t *)malloc(sizeof(dt_iop_flip_global_data_t));
  module->data = gd;
  gd->kernel_flip = dt_opencl_create_kernel(program, "flip");
}

void cleanup_global(dt_iop_module_so_t *module)
{
  dt_iop_flip_global_data_t *gd = (dt_iop_flip_global_data_t *)module->data;
  dt_opencl_free_kernel(gd->kernel_flip);
  free(module->data);
  module->data = NULL;
}

void commit_params (struct dt_iop_module_t *self, dt_iop_params_t *p1, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_flip_params_t *p = (dt_iop_flip_params_t *)p1;
  dt_iop_flip_data_t *d = (dt_iop_flip_data_t *)piece->data;
  d->orientation = p->orientation;
}

void init_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  piece->data = malloc(sizeof(dt_iop_flip_data_t));
  self->commit_params(self, self->default_params, pipe, piece);
}

void cleanup_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  free(piece->data);
}

void init_presets (dt_iop_module_so_t *self)
{
  dt_iop_flip_params_t p = (dt_iop_flip_params_t)
  {
    0
  };
  DT_DEBUG_SQLITE3_EXEC(dt_database_get(darktable.db), "begin", NULL, NULL, NULL);
  p.orientation = 1;
  dt_gui_presets_add_generic(_("flip horizontally"), self->op, self->version(), &p, sizeof(p), 1);
  p.orientation = 2;
  dt_gui_presets_add_generic(_("flip vertically"), self->op, self->version(), &p, sizeof(p), 1);
  p.orientation = 6;
  dt_gui_presets_add_generic(_("rotate by -90"), self->op, self->version(), &p, sizeof(p), 1);
  p.orientation = 5;
  dt_gui_presets_add_generic(_("rotate by  90"), self->op, self->version(), &p, sizeof(p), 1);
  p.orientation = 3;
  dt_gui_presets_add_generic(_("rotate by 180"), self->op, self->version(), &p, sizeof(p), 1);
  DT_DEBUG_SQLITE3_EXEC(dt_database_get(darktable.db), "commit", NULL, NULL, NULL);
}

void reload_defaults(dt_iop_module_t *self)
{
  dt_iop_flip_params_t tmp = (dt_iop_flip_params_t)
  {
    0
  };
  self->default_enabled = 0;
  if(self->dev->image_storage.legacy_flip.user_flip != 0 &&
      self->dev->image_storage.legacy_flip.user_flip != 0xff)
  {
    sqlite3_stmt *stmt;
    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select * from history where imgid = ?1 and operation = 'flip'", -1, &stmt, NULL);
    DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, self->dev->image_storage.id);
    if(sqlite3_step(stmt) != SQLITE_ROW)
    {
      // convert the old legacy flip bits to a proper parameter set:
      self->default_enabled = 1;
      tmp.orientation = self->dev->image_storage.legacy_flip.user_flip;
    }
    sqlite3_finalize(stmt);
  }
  memcpy(self->params, &tmp, sizeof(dt_iop_flip_params_t));
  memcpy(self->default_params, &tmp, sizeof(dt_iop_flip_params_t));
}

void gui_update(struct dt_iop_module_t *self)
{
  // nothing to do
}

void init(dt_iop_module_t *module)
{
  // module->data = malloc(sizeof(dt_iop_flip_data_t));
  module->params = malloc(sizeof(dt_iop_flip_params_t));
  module->default_params = malloc(sizeof(dt_iop_flip_params_t));
  module->default_enabled = 0;
  module->params_size = sizeof(dt_iop_flip_params_t);
  module->gui_data = NULL;
  module->priority = 228; // module order created by iop_dependencies.py, do not edit!
}

void cleanup(dt_iop_module_t *module)
{
  free(module->gui_data);
  module->gui_data = NULL;
  free(module->params);
  module->params = NULL;
}

static void
do_rotate(dt_iop_module_t *self, uint32_t cw)
{
  dt_iop_flip_params_t *p = (dt_iop_flip_params_t *)self->params;
  int32_t orientation = p->orientation;

  if(cw == 1)
  {
    if(orientation & 4) orientation ^= 1;
    else                orientation ^= 2; // flip x
  }
  else
  {
    if(orientation & 4) orientation ^= 2;
    else                orientation ^= 1; // flip y
  }
  orientation ^= 4;             // flip axes
  p->orientation = orientation;
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}
static void
rotate_cw(GtkWidget *widget, dt_iop_module_t *self)
{
  do_rotate(self, 1);
}
static void
rotate_ccw(GtkWidget *widget, dt_iop_module_t *self)
{
  do_rotate(self, 0);
}
static gboolean
rotate_cw_key(GtkAccelGroup *accel_group, GObject *acceleratable, guint keyval, GdkModifierType modifier, dt_iop_module_t *self)
{
  do_rotate(self, 1);
  return TRUE;
}
static gboolean
rotate_ccw_key(GtkAccelGroup *accel_group, GObject *acceleratable, guint keyval, GdkModifierType modifier, dt_iop_module_t *self)
{
  do_rotate(self, 0);
  return TRUE;
}

void gui_init(struct dt_iop_module_t *self)
{
  self->gui_data = NULL;
  dt_iop_flip_params_t *p = (dt_iop_flip_params_t *)self->params;

  self->widget = gtk_hbox_new(TRUE, 5);

  GtkWidget *label = dtgtk_reset_label_new (_("rotate"), self, &p->orientation, sizeof(int32_t));
  gtk_box_pack_start(GTK_BOX(self->widget), label, TRUE, TRUE, 0);

  GtkWidget *button = dtgtk_button_new(dtgtk_cairo_paint_refresh, CPF_STYLE_FLAT|CPF_DO_NOT_USE_BORDER);
  gtk_widget_set_size_request(button, -1, 24);
  g_object_set(G_OBJECT(button), "tooltip-text", _("rotate 90 degrees CCW"), (char *)NULL);
  gtk_box_pack_start(GTK_BOX(self->widget), button, TRUE, TRUE, 0);
  g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(rotate_ccw), (gpointer)self);

  button = dtgtk_button_new(dtgtk_cairo_paint_refresh, CPF_STYLE_FLAT|CPF_DO_NOT_USE_BORDER|1);
  gtk_widget_set_size_request(button, -1, 24);
  g_object_set(G_OBJECT(button), "tooltip-text", _("rotate 90 degrees CW"), (char *)NULL);
  gtk_box_pack_start(GTK_BOX(self->widget), button, TRUE, TRUE, 0);
  g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(rotate_cw), (gpointer)self);
}

void gui_cleanup(struct dt_iop_module_t *self)
{
  self->gui_data = NULL;
}

void init_key_accels(dt_iop_module_so_t *self)
{
  dt_accel_register_iop(self, TRUE, NC_("accel", "rotate 90 degrees CCW"),
                        GDK_KEY_bracketleft, 0);
  dt_accel_register_iop(self, TRUE, NC_("accel", "rotate 90 degrees CW"),
                        GDK_KEY_bracketright, 0);
}

void connect_key_accels(dt_iop_module_t *self)
{
  GClosure *closure;
  closure = g_cclosure_new(G_CALLBACK(rotate_cw_key),
                           (gpointer)self, NULL);
  dt_accel_connect_iop(self, "rotate 90 degrees CW", closure);
  closure = g_cclosure_new(G_CALLBACK(rotate_ccw_key),
                           (gpointer)self, NULL);
  dt_accel_connect_iop(self, "rotate 90 degrees CCW", closure);
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
