/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2015 - Daniel De Matteis
 *  Copyright (C) 2012-2015 - Michael Lelli
 *  Copyright (C) 2013-2014 - Steven Crowe
 *                2019 - Neil Fore
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

#include <android/keycodes.h>
#include <unistd.h>
#include <dlfcn.h>

#include <retro_inline.h>

#include "../../frontend/drivers/platform_android.h"
#include "../input_autodetect.h"
#include "../input_common.h"
#include "../input_joypad.h"
#include "../../performance.h"
#include "../../general.h"
#include "../../configuration.h"


#define MAX_TOUCH 16
#define MAX_PADS 8

#define AKEY_EVENT_NO_ACTION 255

#ifndef AKEYCODE_ASSIST
#define AKEYCODE_ASSIST 219
#endif

#define LAST_KEYCODE AKEYCODE_ASSIST

typedef struct
{
   float x;
   float y;
   float z;
} sensor_t;

struct input_pointer
{
   int16_t x, y;
   int16_t full_x, full_y;
   /* Ellipse values, for internal use on fullscreen overlays */
   float orientation, touch_major, touch_minor;
};

extern void set_ellipse(uint8_t, const float, const float, const float);
extern void reset_ellipse(uint8_t);

enum
{
   AXIS_X = 0,
   AXIS_Y = 1,
   AXIS_Z = 11,
   AXIS_RZ = 14,
   AXIS_HAT_X = 15,
   AXIS_HAT_Y = 16,
   AXIS_LTRIGGER = 17,
   AXIS_RTRIGGER = 18,
   AXIS_GAS = 22,
   AXIS_BRAKE = 23,
};

#define MAX_AXIS 10

typedef struct state_device
{
   int id;
   int port;
   char name[256];
} state_device_t;

typedef struct android_input
{
   bool blocked;
   unsigned pads_connected;
   state_device_t pad_states[MAX_PADS];
   uint8_t pad_state[MAX_PADS][(LAST_KEYCODE + 7) / 8];
   int8_t hat_state[MAX_PADS][2];
   
   int16_t analog_state[MAX_PADS][MAX_AXIS];
   sensor_t accelerometer_state;
   struct input_pointer pointer[MAX_TOUCH];
   unsigned pointer_count;
   ASensorManager *sensorManager;
   ASensorEventQueue *sensorEventQueue;
   const input_device_driver_t *joypad;
} android_input_t;

typedef struct android_input_poll_scratchpad
{
   int32_t down_id[MAX_TOUCH];
   int32_t last_known_action;  /* of any poll */
   uint8_t downs;  /* num downs or pointer downs */
   uint8_t taps;  /* quick down+up between frames */
   bool any_events;
} android_input_poll_scratchpad_t;

static android_input_poll_scratchpad_t frame;
static pthread_cond_t vibrate_flag;

static void frontend_android_get_version_sdk(int32_t *sdk);

bool (*engine_lookup_name)(char *buf,
      int *vendorId, int *productId, size_t size, int id);

void (*engine_handle_dpad)(android_input_t *android, AInputEvent*, int, int);
static bool android_input_set_sensor_state(void *data, unsigned port,
      enum retro_sensor_action action, unsigned event_rate);

extern float AMotionEvent_getAxisValue(const AInputEvent* motion_event,
      int32_t axis, size_t pointer_idx);

static typeof(AMotionEvent_getAxisValue) *p_AMotionEvent_getAxisValue;

#define AMotionEvent_getAxisValue (*p_AMotionEvent_getAxisValue)

static void engine_handle_dpad_default(android_input_t *android,
      AInputEvent *event, int port, int source)
{
   size_t motion_ptr = AMotionEvent_getAction(event) >>
      AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
   float x = AMotionEvent_getX(event, motion_ptr);
   float y = AMotionEvent_getY(event, motion_ptr);

   android->analog_state[port][0] = (int16_t)(x * 32767.0f);
   android->analog_state[port][1] = (int16_t)(y * 32767.0f);
}

static void engine_handle_dpad_getaxisvalue(android_input_t *android,
      AInputEvent *event, int port, int source)
{
   size_t motion_ptr = AMotionEvent_getAction(event) >>
      AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
   float x = AMotionEvent_getAxisValue(event, AXIS_X, motion_ptr);
   float y = AMotionEvent_getAxisValue(event, AXIS_Y, motion_ptr);
   float z = AMotionEvent_getAxisValue(event, AXIS_Z, motion_ptr);
   float rz = AMotionEvent_getAxisValue(event, AXIS_RZ, motion_ptr);
   float hatx = AMotionEvent_getAxisValue(event, AXIS_HAT_X, motion_ptr);
   float haty = AMotionEvent_getAxisValue(event, AXIS_HAT_Y, motion_ptr);
   float ltrig = AMotionEvent_getAxisValue(event, AXIS_LTRIGGER, motion_ptr);
   float rtrig = AMotionEvent_getAxisValue(event, AXIS_RTRIGGER, motion_ptr);
   float brake = AMotionEvent_getAxisValue(event, AXIS_BRAKE, motion_ptr);
   float gas = AMotionEvent_getAxisValue(event, AXIS_GAS, motion_ptr);

   android->hat_state[port][0] = (int)hatx;
   android->hat_state[port][1] = (int)haty;

   /* XXX: this could be a loop instead, but do we really want to 
    * loop through every axis?
    */
   android->analog_state[port][0] = (int16_t)(x * 32767.0f);
   android->analog_state[port][1] = (int16_t)(y * 32767.0f);
   android->analog_state[port][2] = (int16_t)(z * 32767.0f);
   android->analog_state[port][3] = (int16_t)(rz * 32767.0f);
   /*android->analog_state[port][4] = (int16_t)(hatx * 32767.0f);*/
   /*android->analog_state[port][5] = (int16_t)(haty * 32767.0f);*/
   android->analog_state[port][6] = (int16_t)(ltrig * 32767.0f);
   android->analog_state[port][7] = (int16_t)(rtrig * 32767.0f);
   android->analog_state[port][8] = (int16_t)(brake * 32767.0f);
   android->analog_state[port][9] = (int16_t)(gas * 32767.0f);
}

static bool android_input_lookup_name_prekitkat(char *buf,
      int *vendorId, int *productId, size_t size, int id)
{
   RARCH_LOG("Using old lookup");

   jclass class;
   jmethodID method, getName;
   jobject device, name;
   const char *str = NULL;
   JNIEnv     *env = (JNIEnv*)jni_thread_getenv();

   if (!env)
      goto error;

   class = NULL;
   FIND_CLASS(env, class, "android/view/InputDevice");
   if (!class)
      goto error;

   method = NULL;
   GET_STATIC_METHOD_ID(env, method, class, "getDevice",
         "(I)Landroid/view/InputDevice;");
   if (!method)
      goto error;

   device = NULL;
   CALL_OBJ_STATIC_METHOD_PARAM(env, device, class, method, (jint)id);
   if (!device)
   {
      RARCH_ERR("Failed to find device for ID: %d\n", id);
      goto error;
   }

   getName = NULL;
   GET_METHOD_ID(env, getName, class, "getName", "()Ljava/lang/String;");
   if (!getName)
      goto error;

   name = NULL;
   CALL_OBJ_METHOD(env, name, device, getName);
   if (!name)
   {
      RARCH_ERR("Failed to find name for device ID: %d\n", id);
      goto error;
   }

   buf[0] = '\0';

   str = (*env)->GetStringUTFChars(env, name, 0);
   if (str)
      strlcpy(buf, str, size);
   (*env)->ReleaseStringUTFChars(env, name, str);

   RARCH_LOG("device name: %s\n", buf);

   return true;
error:
   return false;
}

static bool android_input_lookup_name(char *buf,
      int *vendorId, int *productId, size_t size, int id)
{
   RARCH_LOG("Using new lookup");

   jclass class;
   jmethodID method, getName, getVendorId, getProductId;
   jobject device, name;
   const char *str = NULL;
   JNIEnv     *env = (JNIEnv*)jni_thread_getenv();

   if (!env)
      goto error;

   class = NULL;
   FIND_CLASS(env, class, "android/view/InputDevice");
   if (!class)
      goto error;

   method = NULL;
   GET_STATIC_METHOD_ID(env, method, class, "getDevice",
         "(I)Landroid/view/InputDevice;");
   if (!method)
      goto error;

   device = NULL;
   CALL_OBJ_STATIC_METHOD_PARAM(env, device, class, method, (jint)id);
   if (!device)
   {
      RARCH_ERR("Failed to find device for ID: %d\n", id);
      goto error;
   }

   getName = NULL;
   GET_METHOD_ID(env, getName, class, "getName", "()Ljava/lang/String;");
   if (!getName)
      goto error;

   name = NULL;
   CALL_OBJ_METHOD(env, name, device, getName);
   if (!name)
   {
      RARCH_ERR("Failed to find name for device ID: %d\n", id);
      goto error;
   }

   buf[0] = '\0';

   str = (*env)->GetStringUTFChars(env, name, 0);
   if (str)
      strlcpy(buf, str, size);
   (*env)->ReleaseStringUTFChars(env, name, str);

   RARCH_LOG("device name: %s\n", buf);

   getVendorId = NULL;
   GET_METHOD_ID(env, getVendorId, class, "getVendorId", "()I");
   if (!getVendorId)
      goto error;

   CALL_INT_METHOD(env, *vendorId, device, getVendorId);
   if (!*vendorId)
   {
      RARCH_ERR("Failed to find vendor id for device ID: %d\n", id);
      goto error;
   }
   RARCH_LOG("device vendor id: %d\n", *vendorId);

   getProductId = NULL;
   GET_METHOD_ID(env, getProductId, class, "getProductId", "()I");
   if (!getProductId)
      goto error;

   *productId = 0;
   CALL_INT_METHOD(env, *productId, device, getProductId);
   if (!*productId)
   {
      RARCH_ERR("Failed to find product id for device ID: %d\n", id);
      goto error;
   }
   RARCH_LOG("device product id: %d\n", *productId);

   return true;
error:
   return false;
}

static void engine_handle_cmd(void)
{
   int8_t cmd;
   struct android_app *android_app = (struct android_app*)g_android;
   runloop_t *runloop = rarch_main_get_ptr();
   global_t  *global  = global_get_ptr();
   driver_t  *driver  = driver_get_ptr();

   if (read(android_app->msgread, &cmd, sizeof(cmd)) != sizeof(cmd))
      cmd = -1;

   switch (cmd)
   {
      case APP_CMD_INPUT_CHANGED:
         slock_lock(android_app->mutex);

         if (android_app->inputQueue)
            AInputQueue_detachLooper(android_app->inputQueue);

         android_app->inputQueue = android_app->pendingInputQueue;

         if (android_app->inputQueue)
         {
            RARCH_LOG("Attaching input queue to looper");
            AInputQueue_attachLooper(android_app->inputQueue,
                  android_app->looper, LOOPER_ID_INPUT, NULL,
                  NULL);
         }

         scond_broadcast(android_app->cond);
         slock_unlock(android_app->mutex);
         
         break;

      case APP_CMD_INIT_WINDOW:
         slock_lock(android_app->mutex);
         android_app->window = android_app->pendingWindow;
         scond_broadcast(android_app->cond);
         slock_unlock(android_app->mutex);

         if (runloop->is_paused)
            event_command(EVENT_CMD_REINIT);
         break;

      case APP_CMD_RESUME:
         slock_lock(android_app->mutex);
         android_app->activityState = cmd;
         scond_broadcast(android_app->cond);
         slock_unlock(android_app->mutex);
         break;

      case APP_CMD_START:
         slock_lock(android_app->mutex);
         android_app->activityState = cmd;
         scond_broadcast(android_app->cond);
         slock_unlock(android_app->mutex);
         break;

      case APP_CMD_PAUSE:
         slock_lock(android_app->mutex);
         android_app->activityState = cmd;
         scond_broadcast(android_app->cond);
         slock_unlock(android_app->mutex);

         if (!global->system.shutdown)
         {
            RARCH_LOG("Pausing RetroArch.\n");
            runloop->is_paused = true;
            runloop->is_idle   = true;
         }
         break;

      case APP_CMD_STOP:
         slock_lock(android_app->mutex);
         android_app->activityState = cmd;
         scond_broadcast(android_app->cond);
         slock_unlock(android_app->mutex);
         break;

      case APP_CMD_CONFIG_CHANGED:
         break;
      case APP_CMD_TERM_WINDOW:
         slock_lock(android_app->mutex);

         /* The window is being hidden or closed, clean it up. */
         /* terminate display/EGL context here */

#if 0
         RARCH_WARN("Window is terminated outside PAUSED state.\n");
#endif

         android_app->window = NULL;
         scond_broadcast(android_app->cond);
         slock_unlock(android_app->mutex);
         break;

      case APP_CMD_GAINED_FOCUS:
         runloop->is_paused = false;
         runloop->is_idle   = false;

         if ((android_app->sensor_state_mask 
                  & (1ULL << RETRO_SENSOR_ACCELEROMETER_ENABLE))
               && android_app->accelerometerSensor == NULL
               && driver->input_data)
            android_input_set_sensor_state(driver->input_data, 0,
                  RETRO_SENSOR_ACCELEROMETER_ENABLE,
                  android_app->accelerometer_event_rate);
         break;
      case APP_CMD_LOST_FOCUS:
         /* Avoid draining battery while app is not being used. */
         if ((android_app->sensor_state_mask
                  & (1ULL << RETRO_SENSOR_ACCELEROMETER_ENABLE))
               && android_app->accelerometerSensor != NULL
               && driver->input_data)
            android_input_set_sensor_state(driver->input_data, 0,
                  RETRO_SENSOR_ACCELEROMETER_DISABLE,
                  android_app->accelerometer_event_rate);
         break;

      case APP_CMD_DESTROY:
         global->system.shutdown = true;
         break;
   }
}

/* Used for vibration and setting media volume control */
static void *jni_thread_func(void* data)
{
   (void)data;
   
   settings_t* settings = config_get_ptr();
   jobject jobj = g_android->activity->clazz;
   JavaVM* p_jvm = g_android->activity->vm;
   JNIEnv* p_jenv;
   
   jobject vibrator_service;
   jclass vibrator_class;
   jclass activity_class;
   jmethodID getSystemService_id;
   jmethodID vibrate_method_id;
   jmethodID vol_control_id;
   pthread_mutex_t g_vibrate_mutex;
   
   jint status = (*p_jvm)->AttachCurrentThreadAsDaemon(p_jvm, &p_jenv, 0);
   if (status < 0)
   {
      RARCH_ERR("jni_thread_func: Failed to attach current thread.\n");
      return NULL;
   }
   GET_OBJECT_CLASS( p_jenv, activity_class, jobj )
   
  /*
   * set volume control to media
   */
   GET_METHOD_ID( p_jenv,
                  vol_control_id,
                  activity_class,
                  "setVolumeControlStream",
                  "(I)V" )
   CALL_VOID_METHOD_PARAM( p_jenv, jobj, vol_control_id, (jint)3 )
   
   /*
    * setup timed vibrate method
    */
   GET_METHOD_ID( p_jenv,
                  getSystemService_id,
                  activity_class,
                  "getSystemService",
                  "(Ljava/lang/String;)Ljava/lang/Object;" )
   CALL_OBJ_METHOD_PARAM( p_jenv,
                          vibrator_service,
                          jobj,
                          getSystemService_id,
                          (*p_jenv)->NewStringUTF( p_jenv, "vibrator" ) )
   GET_OBJECT_CLASS( p_jenv, vibrator_class, vibrator_service )
   GET_METHOD_ID( p_jenv,
                  vibrate_method_id,
                  vibrator_class,
                  "vibrate",
                  "(J)V" )
   
   pthread_mutex_init(&g_vibrate_mutex, NULL);
   
   /* Sit and wait for vibrate_flag.*/
   for (;;)
   {
      pthread_cond_wait(&vibrate_flag, &g_vibrate_mutex);
      CALL_VOID_METHOD_PARAM( p_jenv,
                              vibrator_service,
                              vibrate_method_id,
                              (jlong)(settings->input.vibrate_time) )
   }
}

void android_input_vibrate()
{
   pthread_cond_signal(&vibrate_flag);
}

static void *android_input_init(void)
{  
   int32_t sdk;
   settings_t *settings = config_get_ptr();
   android_input_t *android = (android_input_t*)calloc(1, sizeof(*android));

   if (!android)
      return NULL;

   android->pads_connected = 0;
   android->joypad = input_joypad_init_driver(settings->input.joypad_driver, android);

   frontend_android_get_version_sdk(&sdk);

   RARCH_LOG("sdk version: %d\n", sdk);
   
   if (sdk >= 19)
      engine_lookup_name = android_input_lookup_name;
   else
      engine_lookup_name = android_input_lookup_name_prekitkat;

   /* Start the JNI thread if it isn't running. */
   static pthread_t jni_thread_id = 0;
   if (!jni_thread_id)
      pthread_create(&jni_thread_id, NULL, jni_thread_func, NULL);
	  
   return android;
}

static INLINE int android_input_poll_event_type_motion(
      android_input_t *android, AInputEvent *event, int source)
{
   int getaction, action;
   size_t motion_ptr;
   size_t ignore_ptr = MAX_TOUCH;
   size_t event_ptr_count;
   bool keyup, keydown;
   uint8_t idx;
   float x, y;
   struct input_pointer* p;

   if (source & ~(AINPUT_SOURCE_TOUCHSCREEN | AINPUT_SOURCE_MOUSE | AINPUT_SOURCE_STYLUS))
      return 1;

   frame.any_events = true;
   getaction = AMotionEvent_getAction(event);
   action = getaction & AMOTION_EVENT_ACTION_MASK;
   motion_ptr = getaction >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
   
   frame.any_events = true;
   frame.last_known_action = action;
   
   if (action == AMOTION_EVENT_ACTION_MOVE)
   {
      keydown = false;
      keyup = false;
   }
   else
   {
      keydown = ( action == AMOTION_EVENT_ACTION_DOWN
                  || action == AMOTION_EVENT_ACTION_POINTER_DOWN );
      keyup = ( action == AMOTION_EVENT_ACTION_UP
                || action == AMOTION_EVENT_ACTION_POINTER_UP
                || action == AMOTION_EVENT_ACTION_CANCEL
                || (source == AINPUT_SOURCE_MOUSE
                    && (action != AMOTION_EVENT_ACTION_DOWN)) );
      
      if ( action == AMOTION_EVENT_ACTION_HOVER_MOVE
           || action == AMOTION_EVENT_ACTION_HOVER_ENTER
           || action == AMOTION_EVENT_ACTION_HOVER_EXIT )
         ignore_ptr = motion_ptr;
   }

   if (keydown && frame.downs < MAX_TOUCH)
   {
      /* record all downs since last poll */
      frame.down_id[frame.downs++] = AMotionEvent_getPointerId(event, motion_ptr);
   }
   else if (keyup)
   {
      ignore_ptr = motion_ptr;
      int32_t keyup_id = AMotionEvent_getPointerId(event, motion_ptr);
      
      /* capture quick taps */
      for (idx = 0; idx < frame.downs; idx++)
      {
         if (frame.down_id[idx] == keyup_id)
         {
            x = AMotionEvent_getX(event, motion_ptr);
            y = AMotionEvent_getY(event, motion_ptr);
            
            p = &android->pointer[frame.taps];
            input_translate_coord_viewport(x, y, &p->x, &p->y,
                                                 &p->full_x, &p->full_y);
            
            /* Ignore ellipse data for quick taps. */
            reset_ellipse(frame.taps);
            
            frame.taps++;
            frame.down_id[idx] = -1;
            break;
         }
      }
   }
   
   frame.last_known_action = action;
   android->pointer_count = frame.taps;
   
   event_ptr_count = min(AMotionEvent_getPointerCount(event), MAX_TOUCH);
   for (motion_ptr = 0; motion_ptr < event_ptr_count; motion_ptr++)
   {
      if ( motion_ptr == ignore_ptr )
         continue;
      idx = android->pointer_count;
      
      x = AMotionEvent_getX(event, motion_ptr);
      y = AMotionEvent_getY(event, motion_ptr);
      
      p = &android->pointer[idx];
      input_translate_coord_viewport(x, y, &p->x, &p->y,
                                           &p->full_x, &p->full_y);
      
      set_ellipse(idx, AMotionEvent_getOrientation(event, motion_ptr),
                       AMotionEvent_getTouchMajor(event, motion_ptr),
                       AMotionEvent_getTouchMinor(event, motion_ptr));
      
      android->pointer_count++;
   }

   return 0;
}

static INLINE void android_input_poll_event_type_key(
      android_input_t *android, struct android_app *android_app,
      AInputEvent *event, int port, int keycode, int source,
      int type_event, int *handled)
{
   uint8_t *buf = android->pad_state[port];
   int action  = AKeyEvent_getAction(event);
   global_t* global = global_get_ptr();

   /* some controllers send both the up and down events at once
    * when the button is released for "special" buttons, like menu buttons
    * work around that by only using down events for meta keys (which get
    * cleared every poll anyway)
    */
   if (action == AKEY_EVENT_ACTION_UP)
      BIT_CLEAR(buf, keycode);
   else if (action == AKEY_EVENT_ACTION_DOWN)
      BIT_SET(buf, keycode);

   if ((keycode == AKEYCODE_VOLUME_UP || keycode == AKEYCODE_VOLUME_DOWN))
      *handled = 0;
   else if (keycode == AKEYCODE_BACK)
   {
      if (action == AKEY_EVENT_ACTION_DOWN)
         global->lifecycle_state |= (1ULL << RARCH_MENU_TOGGLE);
      else if (action == AKEY_EVENT_ACTION_UP)
         global->lifecycle_state &= ~(1ULL << RARCH_MENU_TOGGLE);
   }
}

static int android_input_get_id_port(android_input_t *android, int id,
      int source)
{
   unsigned i;
   if (source & (AINPUT_SOURCE_TOUCHSCREEN | AINPUT_SOURCE_MOUSE | 
            AINPUT_SOURCE_TOUCHPAD | AINPUT_SOURCE_STYLUS ))
      return 0; /* touch overlay is always user 1 */

   for (i = 0; i < android->pads_connected; i++)
      if (android->pad_states[i].id == id)
         return i;

   return -1;
}



/* Returns the index inside android->pad_state */
static int android_input_get_id_index_from_name(android_input_t *android,
      const char *name)
{
   int i;
   for (i = 0; i < android->pads_connected; i++)
   {
      if (!strcmp(name, android->pad_states[i].name))
         return i;
   }

   return -1;
}

static int zeus_id = -1;
static int zeus_second_id = -1;

static void handle_hotplug(android_input_t *android,
      struct android_app *android_app, unsigned *port, unsigned id,
      int source)
{
   char device_name[256]        = {0};
   char name_buf[256]           = {0};
   autoconfig_params_t params   = {{0}};
   name_buf[0] = device_name[0] = 0;
   int vendorId = 0, productId  = 0;
   settings_t         *settings = config_get_ptr();

   if (!settings->input.autodetect_enable)
      return;

   if (*port > MAX_PADS)
   {
      RARCH_ERR("Max number of pads reached.\n");
      return;
   }

   if (!engine_lookup_name(device_name, &vendorId, &productId, sizeof(device_name), id))
   {
      RARCH_ERR("Could not look up device name or IDs.\n");
      return;
   }

   /* FIXME: Ugly hack, see other FIXME note below. */
   if (strstr(device_name, "keypad-game-zeus") ||
         strstr(device_name, "keypad-zeus"))
   {
      if (zeus_id < 0)
      {
         RARCH_LOG("zeus_pad 1 detected: %u\n", id);
         zeus_id = id;
      }
      else
      {
         RARCH_LOG("zeus_pad 2 detected: %u\n", id);
         zeus_second_id = id;
      }
      strlcpy(name_buf, "Xperia Play", sizeof(name_buf));
   }
   /* followed by a 4 (hex) char HW id */
   else if (strstr(device_name, "iControlPad-"))
      strlcpy(name_buf, "iControlPad HID Joystick profile", sizeof(name_buf));
   else if (strstr(device_name, "TTT THT Arcade console 2P USB Play"))
   {
      /* FIXME - need to do a similar thing here as we did for nVidia Shield
       * and Xperia Play. We need to keep 'count' of the amount of similar (grouped)
       * devices.
       *
       * For Xperia Play - count similar devices and bind them to the same 'user'
       * port
       *
       * For nVidia Shield - see above
       *
       * For TTT HT - keep track of how many of these 'pads' are already
       * connected, and based on that, assign one of them to be User 1 and
       * the other to be User 2.
       *
       * If this is finally implemented right, then these port conditionals can go. */
      if (*port == 0)
         strlcpy(name_buf, "TTT THT Arcade (User 1)", sizeof(name_buf));
      else if (*port == 1)
         strlcpy(name_buf, "TTT THT Arcade (User 2)", sizeof(name_buf));
   }      
   else if (strstr(device_name, "Sun4i-keypad"))
      strlcpy(name_buf, "iDroid x360", sizeof(name_buf));
   else if (strstr(device_name, "mtk-kpd"))
      strlcpy(name_buf, "MUCH iReadyGo i5", sizeof(name_buf));
   else if (strstr(device_name, "360 Wireless"))
      strlcpy(name_buf, "XBox 360 Wireless", sizeof(name_buf));
   else if (strstr(device_name, "Microsoft"))
   {
      if (strstr(device_name, "Dual Strike"))
         strlcpy(device_name, "SideWinder Dual Strike", sizeof(device_name));
      else if (strstr(device_name, "SideWinder"))
         strlcpy(name_buf, "SideWinder Classic", sizeof(name_buf));
      else if (strstr(device_name, "X-Box 360")
            || strstr(device_name, "X-Box"))
         strlcpy(name_buf, "XBox 360", sizeof(name_buf));
   }
   else if (strstr(device_name, "WiseGroup"))
   {
      if (
            strstr(device_name, "TigerGame") ||
            strstr(device_name, "Game Controller Adapter") ||
            strstr(device_name, "JC-PS102U") ||
            strstr(device_name, "Dual USB Joypad"))
      {
         if (strstr(device_name, "WiseGroup"))
            strlcpy(name_buf, "PlayStation2 WiseGroup", sizeof(name_buf));
         else if (strstr(device_name, "JC-PS102U"))
            strlcpy(name_buf, "PlayStation2 JCPS102", sizeof(name_buf));
         else
            strlcpy(name_buf, "PlayStation2 Generic", sizeof(name_buf));
      }
   }
   else if (
         strstr(device_name, "PLAYSTATION(R)3") ||
         strstr(device_name, "Dualshock3") ||
         strstr(device_name, "Sixaxis") ||
         strstr(device_name, "Gasia,Co") ||
         (strstr(device_name, "Gamepad 0") ||
          strstr(device_name, "Gamepad 1") || 
          strstr(device_name, "Gamepad 2") ||
          strstr(device_name, "Gamepad 3"))
         )
      strlcpy(name_buf, "PlayStation3", sizeof(name_buf));
   else if (strstr(device_name, "MOGA"))
      strlcpy(name_buf, "Moga IME", sizeof(name_buf));
   else if (strstr(device_name, "adc joystick"))
      strlcpy(name_buf, "JXD S7300B", sizeof(name_buf));
   else if (strstr(device_name, "2-Axis, 8-Button"))
      strlcpy(name_buf, "Genius Maxfire G08XU", sizeof(name_buf));
   else if (strstr(device_name, "USB,2-axis 8-button gamepad"))
      strlcpy(name_buf, "USB 2 Axis 8 button", sizeof(name_buf));
   else if (strstr(device_name, "joy_key"))
      strlcpy(name_buf, "Archos Gamepad", sizeof(name_buf));
   else if (strstr(device_name, "matrix_keyboard"))
      strlcpy(name_buf, "JXD S5110B", sizeof(name_buf));
   else if (strstr(device_name, "tincore_adc_joystick"))
      strlcpy(name_buf, "JXD S5110B (Skelrom)", sizeof(name_buf));
   else if (strstr(device_name, "keypad-zeus") ||
         (strstr(device_name, "keypad-game-zeus"))
         )
      strlcpy(name_buf, "Xperia Play", sizeof(name_buf));
   else if (strstr(device_name, "USB Gamepad"))
      strlcpy(name_buf, "Thrust Predator", sizeof(name_buf));
   else if (strstr(device_name, "ADC joystick"))
      strlcpy(name_buf, "JXD S7800B", sizeof(name_buf));
   else if (strstr(device_name, "2Axes 11Keys Game  Pad"))
      strlcpy(name_buf, "Tomee NES USB", sizeof(name_buf));
   else if (
         strstr(device_name, "rk29-keypad") ||
         strstr(device_name, "GAMEMID")
         )
      strlcpy(name_buf, "GameMID", sizeof(name_buf));
   else if (strstr(device_name, "USB Gamepad"))
      strlcpy(name_buf, "Defender Game Racer Classic", sizeof(name_buf));
   else if (strstr(device_name, "NVIDIA Controller"))
   {
      /* Shield is always user 1. FIXME: This is kinda ugly.
       * We really need to find a way to detect useless input devices
       * like gpio-keys in a general way.
       */
      *port = 0;
      strlcpy(name_buf, "NVIDIA Shield", sizeof(name_buf));
   }
   else if (device_name[0] != '\0')
      strlcpy(name_buf, device_name, sizeof(name_buf));

   if (strstr(android_app->current_ime, "net.obsidianx.android.mogaime"))
      strlcpy(name_buf, android_app->current_ime, sizeof(name_buf));
   else if (strstr(android_app->current_ime, "com.ccpcreations.android.WiiUseAndroid"))
      strlcpy(name_buf, android_app->current_ime, sizeof(name_buf));
   else if (strstr(android_app->current_ime, "com.hexad.bluezime"))
      strlcpy(name_buf, android_app->current_ime, sizeof(name_buf));

   if (source == AINPUT_SOURCE_KEYBOARD && strcmp(name_buf, "Xperia Play"))
      strlcpy(name_buf, "RetroKeyboard", sizeof(name_buf));

   if (name_buf[0] != '\0')
   {
      strlcpy(settings->input.device_names[*port],
            name_buf, sizeof(settings->input.device_names[*port]));

      RARCH_LOG("Port %d: %s.\n", *port, name_buf);
      params.idx = *port;
      strlcpy(params.name, name_buf, sizeof(params.name));
      params.vid = vendorId;
      params.pid = productId;
      strlcpy(params.driver, android_joypad.ident, sizeof(params.driver));
      input_config_autoconfigure_joypad(&params);
   }

   *port = android->pads_connected;
   android->pad_states[android->pads_connected].id = id;
   android->pad_states[android->pads_connected].port = *port;
   strlcpy(android->pad_states[*port].name, name_buf,
         sizeof(android->pad_states[*port].name));

   android->pads_connected++;
}

static int android_input_get_id(android_input_t *android, AInputEvent *event)
{
   int id = AInputEvent_getDeviceId(event);

   /* Needs to be cleaned up */
   if (id == zeus_second_id)
      id = zeus_id;

   return id;
}

static void android_input_handle_input(void *data)
{
   AInputEvent *event = NULL;
   android_input_t    *android     = (android_input_t*)data;
   struct android_app *android_app = (struct android_app*)g_android;
   
   /* Read all pending events. */
   while (AInputQueue_hasEvents(android_app->inputQueue))
   {
      while (AInputQueue_getEvent(android_app->inputQueue, &event) >= 0)
      {
         int32_t handled = 1;
         int predispatched = AInputQueue_preDispatchEvent(android_app->inputQueue, event);
         int source = AInputEvent_getSource(event);
         int type_event = AInputEvent_getType(event);
         int id = android_input_get_id(android, event);
         int port = android_input_get_id_port(android, id, source);

         if (port < 0)
            handle_hotplug(android, android_app,
                  &android->pads_connected, id, source);

         switch (type_event)
         {
            case AINPUT_EVENT_TYPE_MOTION:
               if (android_input_poll_event_type_motion(android, event, source))
                  engine_handle_dpad(android, event, port, source);
               break;
            case AINPUT_EVENT_TYPE_KEY:
               {
                  int keycode = AKeyEvent_getKeyCode(event);
                  android_input_poll_event_type_key(android, android_app,
                        event, port, keycode, source, type_event, &handled);
               }
               break;
         }

         if (!predispatched)
            AInputQueue_finishEvent(android_app->inputQueue, event,
                  handled);
      }
   }
}

static void android_input_handle_user(void *data)
{
   android_input_t    *android     = (android_input_t*)data;
   struct android_app *android_app = (struct android_app*)g_android;

   if ((android_app->sensor_state_mask & (1ULL <<
               RETRO_SENSOR_ACCELEROMETER_ENABLE))
         && android_app->accelerometerSensor)
   {
      ASensorEvent event;
      while (ASensorEventQueue_getEvents(android->sensorEventQueue, &event, 1) > 0)
      {
         android->accelerometer_state.x = event.acceleration.x;
         android->accelerometer_state.y = event.acceleration.y;
         android->accelerometer_state.z = event.acceleration.z;
      }
   }
}

/* Handle all events. If our activity is in pause state,
 * block until we're unpaused.
 */
static void android_input_poll(void *data)
{
   int ident;
   android_input_t *android = (android_input_t*)data;
   runloop_t *runloop = rarch_main_get_ptr();
   frame.taps = 0;
   frame.downs = 0;
   frame.any_events = false;
   
   while ((ident = ALooper_pollAll( runloop->is_paused ? -1 : 0,
                                    NULL, NULL, NULL )) >= 0)
   {
      switch (ident)
      {
         case LOOPER_ID_INPUT:
            android_input_handle_input(data);
            break;
         case LOOPER_ID_USER:
            android_input_handle_user(data);
            break;
         case LOOPER_ID_MAIN:
            engine_handle_cmd();
            break;
      }
   }
   
   /* reset pointer_count if no active pointers */
   if (!frame.any_events && frame.last_known_action == AMOTION_EVENT_ACTION_UP)
      android->pointer_count = 0;
}

bool android_run_events(void *data)
{
   global_t *global = global_get_ptr();
   int id = ALooper_pollOnce(-1, NULL, NULL, NULL);

   if (id == LOOPER_ID_MAIN)
      engine_handle_cmd();

   /* Check if we are exiting. */
   if (global->system.shutdown)
      return false;

   return true;
}

static int16_t android_input_state(void *data,
      const struct retro_keybind **binds, unsigned port, unsigned device,
      unsigned idx, unsigned id)
{
   android_input_t *android = (android_input_t*)data;
   driver_t *driver         = driver_get_ptr();
   global_t *global         = global_get_ptr();
   
   switch (device)
   {
      case RETRO_DEVICE_JOYPAD:
         return input_joypad_pressed(android->joypad, port, binds[port], id);
      case RETRO_DEVICE_ANALOG:
         return input_joypad_analog(android->joypad, port, idx, id,
               binds[port]);
      case RETRO_DEVICE_POINTER:
         switch (id)
         {
            case RETRO_DEVICE_ID_POINTER_X:
               return android->pointer[idx].x;
            case RETRO_DEVICE_ID_POINTER_Y:
               return android->pointer[idx].y;
            case RETRO_DEVICE_ID_POINTER_PRESSED:
               return (idx < android->pointer_count);
            case RETRO_DEVICE_ID_POINTER_COUNT:
               return android->pointer_count;
         }
         break;
      case RARCH_DEVICE_POINTER_SCREEN:
         switch (id)
         {
            case RETRO_DEVICE_ID_POINTER_X:
               return android->pointer[idx].full_x;
            case RETRO_DEVICE_ID_POINTER_Y:
               return android->pointer[idx].full_y;
            case RETRO_DEVICE_ID_POINTER_PRESSED:
               return (idx < android->pointer_count);
            case RETRO_DEVICE_ID_POINTER_COUNT:
               return android->pointer_count;
         }
         break;
      case RETRO_DEVICE_LIGHTGUN:
         switch(id)
         {
            case RETRO_DEVICE_ID_LIGHTGUN_X:
               /* todo: should be relative! (should also be obsolete) */
            case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X:
               return driver->overlay_state.lightgun_x;
            case RETRO_DEVICE_ID_LIGHTGUN_Y:
            case RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y:
               return driver->overlay_state.lightgun_y;
            case RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN:
               if (!driver->overlay_state.lightgun_onscreen)
                  return 1; /* else, fall through to reload (offscreen shot) */
            case RETRO_DEVICE_ID_LIGHTGUN_RELOAD:
               return (driver->overlay_state.lightgun_buttons
                       & (1<<RARCH_LIGHTGUN_BIT_RELOAD));
            case RETRO_DEVICE_ID_LIGHTGUN_TRIGGER:
               if (global->overlay_lightgun_autotrigger)
                  return driver->overlay_state.lightgun_onscreen;
            default:
               return (driver->overlay_state.lightgun_buttons & (1<<id)) != 0;
         }
         break;
   }

   return 0;
}

static bool android_input_key_pressed(void *data, int key)
{
   android_input_t *android = (android_input_t*)data;
   driver_t *driver         = driver_get_ptr();
   global_t *global         = global_get_ptr();
   settings_t *settings     = config_get_ptr();

   if (!android)
      return false;

   return ((global->lifecycle_state | driver->overlay_state.buttons)
         & (1ULL << key)) || input_joypad_pressed(android->joypad,
         0, settings->input.binds[0], key);
}

static void android_input_free_input(void *data)
{
   android_input_t *android = (android_input_t*)data;
   if (!android)
      return;

   if (android->sensorManager)
      ASensorManager_destroyEventQueue(android->sensorManager,
            android->sensorEventQueue);

   free(data);
}

static uint64_t android_input_get_capabilities(void *data)
{
   (void)data;

   return 
      (1 << RETRO_DEVICE_JOYPAD)  |
      (1 << RETRO_DEVICE_POINTER) |
      (1 << RETRO_DEVICE_ANALOG);
}

static void android_input_enable_sensor_manager(void *data)
{
   android_input_t *android = (android_input_t*)data;
   struct android_app *android_app = (struct android_app*)g_android;

   android->sensorManager = ASensorManager_getInstance();
   android_app->accelerometerSensor =
      ASensorManager_getDefaultSensor(android->sensorManager,
         ASENSOR_TYPE_ACCELEROMETER);
   android->sensorEventQueue =
      ASensorManager_createEventQueue(android->sensorManager,
         android_app->looper, LOOPER_ID_USER, NULL, NULL);
}

static bool android_input_set_sensor_state(void *data, unsigned port,
      enum retro_sensor_action action, unsigned event_rate)
{
   android_input_t *android = (android_input_t*)data;
   struct android_app *android_app = (struct android_app*)g_android;

   if (event_rate == 0)
      event_rate = 60;

   switch (action)
   {
      case RETRO_SENSOR_ACCELEROMETER_ENABLE:
         if (!android_app->accelerometerSensor)
            android_input_enable_sensor_manager(android);

         if (android_app->accelerometerSensor)
            ASensorEventQueue_enableSensor(android->sensorEventQueue,
                  android_app->accelerometerSensor);

         /* events per second (in us). */
         if (android_app->accelerometerSensor)
            ASensorEventQueue_setEventRate(android->sensorEventQueue,
                  android_app->accelerometerSensor, (1000L / event_rate)
                  * 1000);

         android_app->sensor_state_mask &= ~(1ULL << RETRO_SENSOR_ACCELEROMETER_DISABLE);
         android_app->sensor_state_mask |= (1ULL  << RETRO_SENSOR_ACCELEROMETER_ENABLE);
         return true;

      case RETRO_SENSOR_ACCELEROMETER_DISABLE:
         if (android_app->accelerometerSensor)
            ASensorEventQueue_disableSensor(android->sensorEventQueue,
                  android_app->accelerometerSensor);
         
         android_app->sensor_state_mask &= ~(1ULL << RETRO_SENSOR_ACCELEROMETER_ENABLE);
         android_app->sensor_state_mask |= (1ULL  << RETRO_SENSOR_ACCELEROMETER_DISABLE);
         return true;
      default:
         return false;
   }

   return false;
}

static float android_input_get_sensor_input(void *data,
      unsigned port,unsigned id)
{
   android_input_t *android = (android_input_t*)data;

   switch (id)
   {
      case RETRO_SENSOR_ACCELEROMETER_X:
         return android->accelerometer_state.x;
      case RETRO_SENSOR_ACCELEROMETER_Y:
         return android->accelerometer_state.y;
      case RETRO_SENSOR_ACCELEROMETER_Z:
         return android->accelerometer_state.z;
   }

   return 0;
}

static const input_device_driver_t *android_input_get_joypad_driver(void *data)
{
   android_input_t *android = (android_input_t*)data;
   if (!android)
      return NULL;
   return android->joypad;
}

static bool android_input_keyboard_mapping_is_blocked(void *data)
{
   android_input_t *android = (android_input_t*)data;
   if (!android)
      return false;
   return android->blocked;
}

static void android_input_keyboard_mapping_set_block(void *data, bool value)
{
   android_input_t *android = (android_input_t*)data;
   if (!android)
      return;
   android->blocked = value;
}

static void android_input_grab_mouse(void *data, bool state)
{
   (void)data;
   (void)state;
}

static bool android_input_set_rumble(void *data, unsigned port,
      enum retro_rumble_effect effect, uint16_t strength)
{
   (void)data;
   (void)port;
   (void)effect;
   (void)strength;

   return false;
}

input_driver_t input_android = {
   android_input_init,
   android_input_poll,
   android_input_state,
   android_input_key_pressed,
   android_input_free_input,
   android_input_set_sensor_state,
   android_input_get_sensor_input,
   android_input_get_capabilities,
   "android",

   android_input_grab_mouse,
   NULL,
   android_input_set_rumble,
   android_input_get_joypad_driver,
   android_input_keyboard_mapping_is_blocked,
   android_input_keyboard_mapping_set_block,
   android_input_vibrate
};
