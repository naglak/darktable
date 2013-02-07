/*
    This file is part of darktable,
    copyright (c) 2012 aldric renaudin.

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

#ifndef DT_DEVELOP_MASKS_H
#define DT_DEVELOP_MASKS_H

#include "dtgtk/button.h"
#include "dtgtk/icon.h"
#include "dtgtk/tristatebutton.h"
#include "dtgtk/slider.h"
#include "dtgtk/tristatebutton.h"
#include "dtgtk/gradientslider.h"
#include "develop/pixelpipe.h"
#include "common/opencl.h"

/**forms types */
typedef enum dt_masks_type_t
{
  DT_MASKS_NONE = 0, // keep first
  DT_MASKS_CIRCLE = 1,
  DT_MASKS_CURVE = 2,
  DT_MASKS_GROUP = 3

}
dt_masks_type_t;

typedef enum dt_masks_states_t
{
  DT_MASKS_STATE_NONE  = 0,
  DT_MASKS_STATE_USE   = 1,
  DT_MASKS_STATE_SHOW  = 2,
  DT_MASKS_STATE_INVERSE = 4
}
dt_masks_states_t;

typedef enum dt_masks_points_states_t
{
  DT_MASKS_POINT_STATE_NORMAL   = 1,
  DT_MASKS_POINT_STATE_USER  = 2
}
dt_masks_points_states_t;

/** structure used to store 1 point for a circle */
typedef struct dt_masks_point_circle_t
{
  float center[2];
  float radius;
  float border;
}
dt_masks_point_circle_t;

/** structure used to store 1 point for a curve form */
typedef struct dt_masks_point_curve_t
{
  float corner[2];
  float ctrl1[2];
  float ctrl2[2];
  float border[2];
  dt_masks_points_states_t state;
}
dt_masks_point_curve_t;

/** structure used to store all forms's id for a group */
typedef struct dt_masks_point_group_t
{
  int formid;
  dt_masks_states_t state;
}
dt_masks_point_group_t;

/** structure used to define a form */
typedef struct dt_masks_form_t
{
  GList *points;
  dt_masks_type_t type;
  
  //name of the form
  char name[128];
  //id used to store the form
  int formid;  
  //version of the form
  int version;
}
dt_masks_form_t;

typedef struct dt_masks_form_gui_points_t
{
  float *points;
  int points_count;
  float *border;
  int border_count;
  gboolean clockwise;
}
dt_masks_form_gui_points_t;

/** structure used to display a form */
typedef struct dt_masks_form_gui_t
{
  //points used to draw the form
  GList *points;  //list of dt_masks_form_gui_points_t
   
  //values for mouse positions, etc...
  float posx, posy, dx, dy;
  gboolean form_selected;
  gboolean border_selected;
  int point_selected;
  int feather_selected;
  int seg_selected;
  int point_border_selected;
  
  gboolean form_dragging;
  int point_dragging;
  int feather_dragging;
  int seg_dragging;
  int point_border_dragging;
  
  int group_edited;
  int group_selected;
  

  gboolean creation;
  gboolean creation_closing_form;
  dt_iop_module_t *creation_module;
  
  //ids
  int formid;
  uint64_t pipe_hash;
}
dt_masks_form_gui_t;

dt_masks_point_circle_t *dt_masks_get_circle(dt_masks_form_t *form);

/** get points in real space with respect of distortion dx and dy are used to eventually move the center of the circle */
int dt_masks_get_points(dt_develop_t *dev, dt_masks_form_t *form, float **points, int *points_count, float dx, float dy);
int dt_masks_get_border(dt_develop_t *dev, dt_masks_form_t *form, float **border, int *border_count, float dx, float dy);
int dt_masks_get_points_border(dt_develop_t *dev, dt_masks_form_t *form, float **points, int *points_count, float **border, int *border_count, float dx, float dy);

/** get the rectangle which include the form and his border */
int dt_masks_get_area(dt_iop_module_t *module, dt_dev_pixelpipe_iop_t *piece, dt_masks_form_t *form, int *width, int *height, int *posx, int *posy);
/** get the transparency mask of the form and his border */
int dt_masks_get_mask(dt_iop_module_t *module, dt_dev_pixelpipe_iop_t *piece, dt_masks_form_t *form, float **buffer, int *width, int *height, int *posx, int *posy);

/** we create a completly new form. */
dt_masks_form_t *dt_masks_create(dt_masks_type_t type);
/** retrieve a form with is id */
dt_masks_form_t *dt_masks_get_from_id(dt_develop_t *dev, int id);

/** read the forms from the db */
void dt_masks_read_forms(dt_develop_t *dev);
/** write the forms into the db */
void dt_masks_write_form(dt_masks_form_t *form, dt_develop_t *dev);
void dt_masks_write_forms(dt_develop_t *dev);
void dt_masks_free_form(dt_masks_form_t *form);

/** function used to manipulate forms for masks */
void dt_masks_init_formgui(dt_develop_t *dev);
void dt_masks_change_form_gui(dt_masks_form_t *newform);

int dt_masks_events_mouse_moved (struct dt_iop_module_t *module, double x, double y, int which);
int dt_masks_events_button_released (struct dt_iop_module_t *module, double x, double y, int which, uint32_t state);
int dt_masks_events_button_pressed (struct dt_iop_module_t *module, double x, double y, int which, int type, uint32_t state);
int dt_masks_events_mouse_scrolled (struct dt_iop_module_t *module, double x, double y, int up, uint32_t state);
void dt_masks_events_post_expose (struct dt_iop_module_t *module, cairo_t *cr, int32_t width, int32_t height, int32_t pointerx, int32_t pointery);

/** functions used to manipulate gui datas */
void dt_masks_gui_form_create (dt_masks_form_t *form, dt_masks_form_gui_t *gui,int index);
void dt_masks_gui_form_remove (dt_masks_form_t *form, dt_masks_form_gui_t *gui, int index);
void dt_masks_gui_form_update_border (dt_masks_form_t *form, dt_masks_form_gui_t *gui, int index);
void dt_masks_gui_form_test_create (dt_masks_form_t *form, dt_masks_form_gui_t *gui);
void dt_masks_gui_form_save_creation (struct dt_iop_module_t *module, dt_masks_form_t *form, dt_masks_form_gui_t *gui);

void dt_masks_iop_edit_toggle_callback(GtkWidget *widget, struct dt_iop_module_t *module);
void dt_masks_iop_dropdown_callback(GtkWidget *widget, struct dt_iop_module_t *module);
void dt_masks_set_edit_mode(struct dt_iop_module_t *module,gboolean value);

#endif
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;