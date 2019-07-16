/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2015 - Hans-Kristian Arntzen
 * 
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <boolean.h>

#include "../input_common.h"
#include "../input_joypad.h"
#include "../input_keymaps.h"

#include "../../configuration.h"
#include "../../gfx/video_viewport.h"
#include "../../general.h"

typedef struct x11_input
{
   bool blocked;
   const input_device_driver_t *joypad;

   Display *display;
   Window win;

   char state[32];
   bool mouse_l, mouse_r, mouse_m, mouse_wu, mouse_wd;
   int mouse_x, mouse_y;
   int mouse_last_x, mouse_last_y;

   bool grab_mouse;
} x11_input_t;

static void *x_input_init(void)
{
   driver_t *driver     = driver_get_ptr();
   settings_t *settings = config_get_ptr();

   if (driver->display_type != RARCH_DISPLAY_X11)
   {
      RARCH_ERR("Currently active window is not an X11 window. Cannot use this driver.\n");
      return NULL;
   }

   x11_input_t *x11 = (x11_input_t*)calloc(1, sizeof(*x11));
   if (!x11)
      return NULL;

   /* Borrow the active X window ... */
   x11->display = (Display*)driver->video_display;
   x11->win     = (Window)driver->video_window;

   x11->joypad = input_joypad_init_driver(settings->input.joypad_driver, x11);
   input_keymaps_init_keyboard_lut(rarch_key_map_x11);

   return x11;
}

static bool x_key_pressed(x11_input_t *x11, int key)
{
   unsigned sym;
   int keycode;
   bool ret;

   if (key >= RETROK_LAST)
      return false;

   sym     = input_keymaps_translate_rk_to_keysym((enum retro_key)key);
   keycode = XKeysymToKeycode(x11->display, sym);
   ret     = x11->state[keycode >> 3] & (1 << (keycode & 7));

   return ret;
}

static bool x_is_pressed(x11_input_t *x11,
      const struct retro_keybind *binds, unsigned id)
{
   if (id < RARCH_BIND_LIST_END)
   {
      const struct retro_keybind *bind = &binds[id];
      return bind->valid && x_key_pressed(x11, binds[id].key);
   }

   return false;
}

static int16_t x_pressed_analog(x11_input_t *x11,
      const struct retro_keybind *binds, unsigned idx, unsigned id)
{
   int16_t pressed_minus = 0, pressed_plus = 0;
   unsigned id_minus = 0;
   unsigned id_plus  = 0;

   input_conv_analog_id_to_bind_id(idx, id, &id_minus, &id_plus);

   if (x_is_pressed(x11, binds, id_minus))
      pressed_minus = -0x7fff;
   if (x_is_pressed(x11, binds, id_plus))
      pressed_plus  =  0x7fff;

   return pressed_plus + pressed_minus;
}

static bool x_bind_button_pressed(void *data, int key)
{
   x11_input_t *x11 = (x11_input_t*)data;
   settings_t *settings = config_get_ptr();
   if (!x11)
      return false;
   return x_is_pressed(x11, settings->input.binds[0], key) ||
      input_joypad_pressed(x11->joypad, 0, settings->input.binds[0], key);
}

static int16_t x_mouse_state(x11_input_t *x11, unsigned id)
{
   switch (id)
   {
      case RETRO_DEVICE_ID_MOUSE_X:
         return x11->mouse_x - x11->mouse_last_x;
      case RETRO_DEVICE_ID_MOUSE_Y:
         return x11->mouse_y - x11->mouse_last_y;
      case RETRO_DEVICE_ID_MOUSE_LEFT:
         return x11->mouse_l;
      case RETRO_DEVICE_ID_MOUSE_RIGHT:
         return x11->mouse_r;
      case RETRO_DEVICE_ID_MOUSE_WHEELUP:
         {
            int16_t ret = x11->mouse_wu;
            x11->mouse_wu = 0;
            return ret;
         }
      case RETRO_DEVICE_ID_MOUSE_WHEELDOWN:
         {
            int16_t ret = x11->mouse_wd;
            x11->mouse_wd = 0;
            return ret;
         }
      case RETRO_DEVICE_ID_MOUSE_MIDDLE:
         return x11->mouse_m;
   }

   return 0;
}

static int16_t x_pointer_state(x11_input_t *x11,
      unsigned idx, unsigned id, bool screen)
{
   int16_t res_x = 0, res_y = 0, res_screen_x = 0, res_screen_y = 0;
   bool valid, inside;

   if (idx != 0)
      return 0;

   valid = input_translate_coord_viewport(x11->mouse_x, x11->mouse_y,
         &res_x, &res_y, &res_screen_x, &res_screen_y);

   if (!valid)
      return 0;

   if (screen)
   {
      res_x = res_screen_x;
      res_y = res_screen_y;
   }

   inside = (res_x >= -0x7fff) && (res_y >= -0x7fff);

   if (!inside)
      return 0;

   switch (id)
   {
      case RETRO_DEVICE_ID_POINTER_X:
         return res_x;
      case RETRO_DEVICE_ID_POINTER_Y:
         return res_y;
      case RETRO_DEVICE_ID_POINTER_PRESSED:
         return x11->mouse_l;
   }

   return 0;
}

static int16_t x_lightgun_mouse_state(x11_input_t *x11, unsigned id)
{
   static int coord_x, coord_y;
   struct video_viewport vp = {0};
   
   switch (id)
   {
      case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X:
         if (!video_driver_viewport_info(&vp))
            break;
         coord_x = (2 * x11->mouse_x * 0x7fff) / (int)vp.width - 0x7fff;
         if (coord_x < -0x7fff) coord_x = -0x7fff;
         else if (coord_x > 0x7fff) coord_x = 0x7fff;
         return coord_x;
      case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y:
         if (!video_driver_viewport_info(&vp))
            break;
         coord_y = (2 * x11->mouse_y * 0x7fff) / (int)vp.height - 0x7fff;
         if (coord_y < -0x7fff) coord_y = -0x7fff;
         else if (coord_y > 0x7fff) coord_y = 0x7fff;
         return coord_y;
      case RETRO_DEVICE_ID_LIGHTGUN_TRIGGER:
         return x11->mouse_l && !x11->mouse_r;
      case RETRO_DEVICE_ID_LIGHTGUN_CURSOR:
         return x11->mouse_m && !x11->mouse_r;
      case RETRO_DEVICE_ID_LIGHTGUN_TURBO:
         return x11->mouse_m && x11->mouse_r;
      case RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN:
         return abs(coord_y) == 0x7fff;
      case RETRO_DEVICE_ID_LIGHTGUN_RELOAD:
         return x11->mouse_l && (abs(coord_y) == 0x7fff);
      case RETRO_DEVICE_ID_LIGHTGUN_START:
      case RETRO_DEVICE_ID_LIGHTGUN_PAUSE:
         return x11->mouse_l && x11->mouse_r;
   }

   return 0;
}

static int16_t x_lightgun_overlay_state(unsigned id)
{
   driver_t *driver         = driver_get_ptr();
   global_t *global         = global_get_ptr();
   
   switch(id)
   {
      case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X:
         return driver->overlay_state.lightgun_x;
      case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y:
         return driver->overlay_state.lightgun_y;
      case RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN:
      case RETRO_DEVICE_ID_LIGHTGUN_RELOAD:
         return (driver->overlay_state.lightgun_buttons
                 & (1<<RARCH_LIGHTGUN_BIT_RELOAD));
      case RETRO_DEVICE_ID_LIGHTGUN_TRIGGER:
         if (global->overlay_lightgun_autotrigger)
            return driver->overlay_state.lightgun_ptr_active;
      default:
         return (driver->overlay_state.lightgun_buttons & (1<<id)) != 0;
   }
   return 0;
}

static int16_t x_input_state(void *data,
      const struct retro_keybind **binds, unsigned port,
      unsigned device, unsigned idx, unsigned id)
{
   int16_t ret;
   x11_input_t *x11 = (x11_input_t*)data;

   switch (device)
   {
      case RETRO_DEVICE_JOYPAD:
         return x_is_pressed(x11, binds[port], id) ||
            input_joypad_pressed(x11->joypad, port, binds[port], id);

      case RETRO_DEVICE_KEYBOARD:
         return x_key_pressed(x11, id);

      case RETRO_DEVICE_ANALOG:
         ret = x_pressed_analog(x11, binds[port], idx, id);
         if (!ret)
            ret = input_joypad_analog(x11->joypad, port, idx,
                  id, binds[port]);
         return ret;

      case RETRO_DEVICE_MOUSE:
         return x_mouse_state(x11, id);

      case RETRO_DEVICE_POINTER:
      case RARCH_DEVICE_POINTER_SCREEN:
         return x_pointer_state(x11, idx, id,
               device == RARCH_DEVICE_POINTER_SCREEN);

      case RETRO_DEVICE_LIGHTGUN:
#ifdef HAVE_OVERLAY
            if (settings->input.overlay_enable)
               return x_lightgun_overlay_state(x11, id);
            else
#endif
         return x_lightgun_mouse_state(id);
   }

   return 0;
}

static void x_input_free(void *data)
{
   x11_input_t *x11 = (x11_input_t*)data;

   if (!x11)
      return;

   if (x11->joypad)
      x11->joypad->destroy();

   free(x11);
}

static void x_input_poll_mouse(x11_input_t *x11)
{
   unsigned mask;
   int root_x, root_y, win_x, win_y;
   Window root_win, child_win;

   x11->mouse_last_x = x11->mouse_x;
   x11->mouse_last_y = x11->mouse_y;

   XQueryPointer(x11->display,
            x11->win,
            &root_win, &child_win,
            &root_x, &root_y,
            &win_x, &win_y,
            &mask);

   x11->mouse_x  = win_x;
   x11->mouse_y  = win_y;
   x11->mouse_l  = mask & Button1Mask; 
   x11->mouse_m  = mask & Button2Mask; 
   x11->mouse_r  = mask & Button3Mask; 

   /* Somewhat hacky, but seem to do the job. */
   if (x11->grab_mouse && video_driver_focus())
   {
      int mid_w, mid_h;
      struct video_viewport vp = {0};

      video_driver_viewport_info(&vp);

      mid_w = vp.full_width >> 1;
      mid_h = vp.full_height >> 1;

      if (x11->mouse_x != mid_w || x11->mouse_y != mid_h)
      {
         XWarpPointer(x11->display, None,
               x11->win, 0, 0, 0, 0,
               mid_w, mid_h);
      }
      x11->mouse_last_x = mid_w;
      x11->mouse_last_y = mid_h;
   }
}

void x_input_poll_wheel(void *data, XButtonEvent *event, bool latch)
{
   x11_input_t *x11 = (x11_input_t*)data;

   if (!x11)
      return;

   switch (event->button)
   {
      case 4:
         x11->mouse_wu = 1;
         break;
      case 5:
         x11->mouse_wd = 1;
         break;
   }
}

static void x_input_poll(void *data)
{
   x11_input_t *x11 = (x11_input_t*)data;

   if (video_driver_focus())
      XQueryKeymap(x11->display, x11->state);
   else
      memset(x11->state, 0, sizeof(x11->state));

   x_input_poll_mouse(x11);

   if (x11->joypad)
      x11->joypad->poll();
}

static void x_grab_mouse(void *data, bool state)
{
   x11_input_t *x11 = (x11_input_t*)data;
   if (x11)
      x11->grab_mouse = state;
}

static bool x_set_rumble(void *data, unsigned port,
      enum retro_rumble_effect effect, uint16_t strength)
{
   x11_input_t *x11 = (x11_input_t*)data;
   if (!x11)
      return false;
   return input_joypad_set_rumble(x11->joypad, port, effect, strength);
}

static const input_device_driver_t *x_get_joypad_driver(void *data)
{
   x11_input_t *x11 = (x11_input_t*)data;

   if (!x11)
      return NULL;
   return x11->joypad;
}

static uint64_t x_input_get_capabilities(void *data)
{
   uint64_t caps = 0;

   caps |= (1 << RETRO_DEVICE_JOYPAD);
   caps |= (1 << RETRO_DEVICE_MOUSE);
   caps |= (1 << RETRO_DEVICE_KEYBOARD);
   caps |= (1 << RETRO_DEVICE_LIGHTGUN);
   caps |= (1 << RETRO_DEVICE_POINTER);
   caps |= (1 << RETRO_DEVICE_ANALOG);

   return caps;
}

static bool x_keyboard_mapping_is_blocked(void *data)
{
   x11_input_t *x11 = (x11_input_t*)data;
   if (!x11)
      return false;
   return x11->blocked;
}

static void x_keyboard_mapping_set_block(void *data, bool value)
{
   x11_input_t *x11 = (x11_input_t*)data;
   if (!x11)
      return;
   x11->blocked = value;
}

input_driver_t input_x = {
   x_input_init,
   x_input_poll,
   x_input_state,
   x_bind_button_pressed,
   x_input_free,
   NULL,
   NULL,
   x_input_get_capabilities,
   "x",
   x_grab_mouse,
   NULL,
   x_set_rumble,
   x_get_joypad_driver,
   x_keyboard_mapping_is_blocked,
   x_keyboard_mapping_set_block,
   NULL /* overlay_haptic_feedback */
};
