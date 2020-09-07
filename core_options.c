/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2015 - Daniel De Matteis
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

#include "core_options.h"
#include "general.h"
#include <string.h>
#include <file/config_file.h>
#include <file/dir_list.h>
#include "dir_list_special.h"
#include <compat/posix_string.h>
#include <compat/strl.h>
#include <retro_miscellaneous.h>
#include <string/stdstring.h>
#include "menu/menu.h"

bool options_touched = false;
unsigned *options_index_map;  /* from menu index to 'opts' index */

struct core_option
{
   char *desc;
   char *key;
   char *info;
   struct string_list *vals;
   struct string_list *labels;
   size_t index;
   size_t default_index;
   bool hide;
};

struct core_option_manager
{
   config_file_t *conf;
   char conf_path[PATH_MAX_LENGTH];

   struct core_option *opts;
   size_t size;
   bool updated;
};

/**
 * core_option_free:
 * @opt              : options manager handle
 *
 * Frees core option manager handle.
 **/
void core_option_free(core_option_manager_t *opt)
{
   size_t i;

   if (!opt)
      return;

   for (i = 0; i < opt->size; i++)
   {
      free(opt->opts[i].desc);
      free(opt->opts[i].key);
      free(opt->opts[i].info);
      string_list_free(opt->opts[i].vals);
      string_list_free(opt->opts[i].labels);
   }

   if (opt->conf)
      config_file_free(opt->conf);
   free(opt->opts);
   free(options_index_map);
   free(opt);
}

void core_option_get(core_option_manager_t *opt, struct retro_variable *var)
{
   size_t i;

   opt->updated = false;

   for (i = 0; i < opt->size; i++)
   {
      if (!strcmp(opt->opts[i].key, var->key))
      {
         var->value = core_option_get_val(opt, i);
         return;
      }
   }

   var->value = NULL;
}

/**
 * core_option_copy_info_as_messagebox:
 * @option              : Pointer to core option struct
 * @option_def          : Pointer to core option definition
 *
 * Creates messagebox suitable info text in @option from the sublabel info
 * text in @option_def.  (RA Lite doesn't have menu sublabels)
 */
static void core_option_copy_info_as_messagebox(struct core_option *option,
      const struct retro_core_option_definition *option_def)
{
   const unsigned max_line_len = 45;
   unsigned info_len = strlen(option_def->desc)
                       + strlen(option_def->info) + 3;  /* +3 for :\n and \0 */
   unsigned i, line_start, line_end;
   char* tmp;

   option->info = calloc(info_len, sizeof(char));

   /* Use desc for messagebox title */
   strlcpy(option->info, option_def->desc, info_len);
   strlcat(option->info, ":\n", info_len);
   strlcat(option->info, option_def->info, info_len);

   line_start = strlen(option_def->desc) + 3;
   /* Replace space with newline near each max_line_len interval */
   for (i = line_start + max_line_len; i < info_len;)
   {
      while (option->info[i] != ' ' && i > line_start + max_line_len/2)
         i--;

      if (option->info[i] == ' ')
         option->info[i] = '\n';
      else
      {  /* edge case: can't find a space */
         info_len += 4;  /* +4 to insert ...\n */
         line_end = line_start + (max_line_len - 3);

         tmp = option->info;
         option->info = calloc(info_len, sizeof(char));

         strncpy(option->info, tmp, line_end);
         strlcat(option->info, "...\n", info_len);
         strlcat(option->info, tmp + line_end, info_len);
         i = line_end + 4;  /* put index on '\n' */

         free(tmp);
      }

      line_start = i + 1;
      i = line_start + max_line_len;
   }
}

/* From RA v1.8.5 core_option_manager_parse_option */
static bool parse_option(
      core_option_manager_t *opt, size_t idx,
      const struct retro_core_option_definition *option_def)
{
   size_t i;
   union string_list_elem_attr attr;
   size_t num_vals                              = 0;
   char *config_val                             = NULL;
   struct core_option *option                   = (struct core_option*)&opt->opts[idx];
   const struct retro_core_option_value *values = option_def->values;

   if (!string_is_empty(option_def->key))
      option->key             = strdup(option_def->key);

   if (!string_is_empty(option_def->desc))
      option->desc            = strdup(option_def->desc);

   if (!string_is_empty(option_def->info))
      core_option_copy_info_as_messagebox(option, option_def);

   /* Get number of values */
   for (;;)
   {
      if (string_is_empty(values[num_vals].value))
         break;
      num_vals++;
   }

   if (num_vals < 1)
      return false;

   /* Initialize string lists */
   attr.i             = 0;
   option->vals       = string_list_new();
   option->labels     = string_list_new();

   if (!option->vals || !option->labels)
      return false;

   /* Initialize default value */
   option->index         = 0;
   option->default_index = 0;

   /* Extract value/label pairs */
   for (i = 0; i < num_vals; i++)
   {
      /* We know that 'value' is valid */
      string_list_append(option->vals, values[i].value, attr);

      /* Value 'label' may be NULL */
      if (!string_is_empty(values[i].label))
         string_list_append(option->labels, values[i].label, attr);
      else
         string_list_append(option->labels, values[i].value, attr);

      /* Check whether this value is the default setting */
      if (!string_is_empty(option_def->default_value))
      {
         if (!strcmp(option_def->default_value, values[i].value))
         {
            option->index         = i;
            option->default_index = i;
         }
      }
   }

   /* Set current config value */
   if (config_get_string(opt->conf, option->key, &config_val))
   {
      for (i = 0; i < option->vals->size; i++)
      {
         if (!strcmp(option->vals->elems[i].data, config_val))
         {
            option->index = i;
            break;
         }
      }

      free(config_val);
   }

   return true;
}

static bool parse_variable(core_option_manager_t *opt, size_t idx,
      const struct retro_variable *var)
{
   size_t i;
   const char *val_start      = NULL;
   char *value                = NULL;
   char *desc_end             = NULL;
   char *config_val           = NULL;
   struct core_option *option = (struct core_option*)&opt->opts[idx];

   if (!option)
      return false;

   option->key = strdup(var->key);
   value       = strdup(var->value);
   desc_end    = strstr(value, "; ");

   if (!desc_end)
   {
      free(value);
      return false;
   }

   *desc_end    = '\0';
   option->desc = strdup(value);

   val_start    = desc_end + 2;
   option->vals = string_split(val_start, "|");

   if (!option->vals)
   {
      free(value);
      return false;
   }

   option->default_index = 0;

   if (config_get_string(opt->conf, option->key, &config_val))
   {
      for (i = 0; i < option->vals->size; i++)
      {
         if (!strcmp(option->vals->elems[i].data, config_val))
         {
            option->index = i;
            break;
         }
      }

      free(config_val);
   }

   free(value);

   return true;
}

/**
 * core_option_new:
 * @conf_path        : Filesystem path to write core option config file to.
 * @option_defs      : Pointer to option definition array handle (v1)
 * @vars             : Pointer to variable array handle (legacy)
 *
 * Creates and initializes a core manager handle. @vars only used if
 * @option_defs is NULL.
 *
 * Returns: handle to new core manager handle, otherwise NULL.
 **/
static core_option_manager_t *core_option_new(const char *conf_path,
      const struct retro_core_option_definition *option_defs,
      const struct retro_variable *vars)
{
   const struct retro_variable *var;
   const struct retro_core_option_definition *option_def;
   size_t size                      = 0;
   core_option_manager_t *opt       = (core_option_manager_t*)
      calloc(1, sizeof(*opt));

   if (!opt)
      return NULL;

   if (*conf_path)
      opt->conf = config_file_new(conf_path);
   if (!opt->conf)
      opt->conf = config_file_new(NULL);

   strlcpy(opt->conf_path, conf_path, sizeof(opt->conf_path));

   if (!opt->conf)
      goto error;

   if (option_defs)
   {
      for (option_def = option_defs;
         option_def->key && option_def->desc && option_def->values[0].value;
         option_def++)
      size++;
   }
   else if (vars) /* legacy */
   {
      for (var = vars; var->key && var->value; var++)
         size++;
   }

   opt->opts = (struct core_option*)calloc(size, sizeof(*opt->opts));
   options_index_map = (unsigned*)calloc(size, sizeof(unsigned));
   if (!opt->opts || !options_index_map)
      goto error;

   opt->size = size;
   size      = 0;

   if (option_defs)
   {
      for (option_def = option_defs;
           option_def->key && option_def->desc && option_def->values[0].value;
           size++, option_def++)
      {
         if (!parse_option(opt, size, option_def))
            goto error;
      }
   }
   else if (vars) /* legacy */
   {
      for (var = vars; var->key && var->value; size++, var++)
      {
         if (!parse_variable(opt, size, var))
            goto error;
      }
   }

   return opt;

error:
   core_option_free(opt);
   free(options_index_map);
   return NULL;
}

/**
 * core_options_init:
 * @option_defs     : Pointer to option definition array handle (version 1)
 * @vars            : Pointer to variable array handle (legacy)
 *
 * Creates and initializes a core manager handle. @vars is only used if
 * @option_defs is NULL.
 *
 **/
void core_options_init(const struct retro_core_option_definition *option_defs,
      const struct retro_variable *vars)
{
   global_t *global                = global_get_ptr();
   char conf_path[PATH_MAX_LENGTH] = {0};

   if (global->system.core_options)
   {
      core_option_free(global->system.core_options);
      global->system.core_options = NULL;
   }

   if (!core_option_get_game_conf_path(conf_path))
      core_option_get_core_conf_path(conf_path);

   global->system.core_options = core_option_new(conf_path, option_defs, vars);
}

/**
 * core_option_set_visible:
 * @opt                   : options manager handle
 * @key                   : String id of core_option
 * @visible               : False if option should be hidden in menu
 */
void core_option_set_visible(core_option_manager_t *opt,
                             const char* key, bool visible)
{
   unsigned i;

   if (!opt || !key)
      return;

   for (i = 0; i < opt->size; i++)
   {
      if (!strcmp(opt->opts[i].key, key))
      {
         opt->opts[i].hide = !visible;
         menu_entries_set_refresh();
         return;
      }
   }
}

/**
 * core_option_index:
 * @type            : menu entry type
 *
 * Returns: Index of core option
 */
static INLINE unsigned core_option_index(unsigned type)
{
   if (type >= MENU_SETTINGS_CORE_OPTION_START)
      return options_index_map[type - MENU_SETTINGS_CORE_OPTION_START];
   return type;
}

/**
 * core_option_updated:
 * @opt              : options manager handle
 *
 * Has a core option been updated?
 *
 * Returns: true (1) if a core option has been updated,
 * otherwise false (0).
 **/
bool core_option_updated(core_option_manager_t *opt)
{
   if (!opt)
      return false;
   return opt->updated;
}

/**
 * core_option_flush:
 * @opt              : options manager handle
 *
 * Writes core option key-pair values to file.
 *
 * Returns: true (1) if core option values could be
 * successfully saved to disk, otherwise false (0).
 **/
bool core_option_flush(core_option_manager_t *opt)
{
   size_t i;

   for (i = 0; i < opt->size; i++)
   {
      struct core_option *option = (struct core_option*)&opt->opts[i];

      if (option)
         config_set_string(opt->conf, option->key, core_option_get_val(opt, i));
   }

   return config_file_write(opt->conf, opt->conf_path);
}

/**
 * core_option_size:
 * @opt              : options manager handle
 *
 * Gets total number of options.
 *
 * Returns: Total number of options.
 **/
size_t core_option_size(core_option_manager_t *opt)
{
   if (!opt)
      return 0;
   return opt->size;
}

/**
 * core_option_set_menu_offset:
 * @opt                       : options manager handle
 * @idx                       : index of option in @opt
 * @menu_offset               : offset from first option in menu display list
 *
 * Maps @idx to a menu entry. Required to index core option by menu entry type.
 */
void core_option_set_menu_offset(core_option_manager_t *opt,
                                 unsigned idx, unsigned menu_offset)
{
   if (!opt || idx >= opt->size || menu_offset >= opt->size)
      return;
   options_index_map[menu_offset] = idx;
}

/**
 * core_option_get_desc:
 * @opt                : options manager handle
 * @idx                : option index or menu entry type
 *
 * Gets description for an option.
 *
 * Returns: Description for an option.
 **/
const char *core_option_get_desc(core_option_manager_t *opt, size_t idx)
{
   if (!opt)
      return NULL;

   idx = core_option_index(idx);
   return opt->opts[idx].desc;
}

/**
 * core_option_get_val:
 * @opt               : options manager handle
 * @idx               : option index or menu entry type
 *
 * Gets value for an option.
 *
 * Returns: Value for an option.
 **/
const char *core_option_get_val(core_option_manager_t *opt, size_t idx)
{
   struct core_option *option;
   if (!opt)
      return NULL;

   idx = core_option_index(idx);
   option = (struct core_option*)&opt->opts[idx];

   if (!option)
      return NULL;
   return option->vals->elems[option->index].data;
}

/**
 * core_option_get_label:
 * @opt              : options manager handle
 * @idx              : option index or menu entry type
 *
 * Gets label for an option value.
 *
 * Returns: Label for an option value.
 **/
const char *core_option_get_label(core_option_manager_t *opt, size_t idx)
{
   struct core_option *option;

   idx = core_option_index(idx);
   option = (struct core_option*)&opt->opts[idx];

   if (!option)
      return NULL;
   if (option->labels)
      return option->labels->elems[option->index].data;
   else
      return option->vals->elems[option->index].data;
}

/**
 * core_option_is_hidden:
 * @opt                 : options manager handle
 * @idx                 : index identifier of the option
 *
 * Returns: True if option should be hidden in menu, false if not
 */
bool core_option_is_hidden(core_option_manager_t *opt, size_t idx)
{
   if (!opt || idx >= opt->size)
      return true;
   return opt->opts[idx].hide;
}

/**
 * core_option_get_info:
 * @opt                : options manager handle
 * @s                  : output message
 * @len                : size of @s
 * @idx                : option index or menu entry type
 *
 * Gets info message text describing an option.
 */
void core_option_get_info(core_option_manager_t *opt,
                          char *s, size_t len, size_t idx)
{
   char *info;
   if (!opt)
      return;

   idx = core_option_index(idx);
   info = opt->opts[idx].info;

   if (!info || !*info)
      strlcpy(s, "-- No info on this item is available. --\n", len);
   else
      strlcpy(s, info, len);
}

/**
 * core_option_get_vals:
 * @opt                : pointer to core option manager object.
 * @idx                : option index or menu entry type
 *
 * Gets list of core option values from core option specified by @idx.
 *
 * Returns: string list of core option values if successful, otherwise
 * NULL.
 **/
struct string_list *core_option_get_vals(core_option_manager_t *opt, size_t idx)
{
   if (!opt)
      return NULL;
   idx = core_option_index(idx);
   return opt->opts[idx].vals;
}

void core_option_set_val(core_option_manager_t *opt,
      size_t idx, size_t val_idx)
{
   if (!opt)
      return;

   idx = core_option_index(idx);
   opt->opts[idx].index = val_idx % opt->opts[idx].vals->size;
   opt->updated         = true;
   options_touched      = true;
}

/**
 * core_option_next:
 * @opt            : pointer to core option manager object.
 * @idx            : option index or menu entry type
 *
 * Get next value for core option specified by @idx.
 * Options wrap around.
 **/
void core_option_next(core_option_manager_t *opt, size_t idx)
{
   struct core_option *option;
   if (!opt)
      return;

   idx = core_option_index(idx);
   option = (struct core_option*)&opt->opts[idx];

   option->index   = (option->index + 1) % option->vals->size;
   opt->updated    = true;
   options_touched = true;
}

/**
 * core_option_prev:
 * @opt            : pointer to core option manager object.
 * @idx            : option index or menu entry type
 * Options wrap around.
 *
 * Get previous value for core option specified by @idx.
 * Options wrap around.
 **/
void core_option_prev(core_option_manager_t *opt, size_t idx)
{
   struct core_option *option;
   if (!opt)
      return;

   idx = core_option_index(idx);
   option = (struct core_option*)&opt->opts[idx];

   option->index   = (option->index + option->vals->size - 1) %
      option->vals->size;
   opt->updated    = true;
   options_touched = true;
}

/**
 * core_option_set_default:
 * @opt                   : pointer to core option manager object.
 * @idx                   : option index or menu entry type
 *
 * Reset core option specified by @idx and sets default value for option.
 **/
void core_option_set_default(core_option_manager_t *opt, size_t idx)
{
   if (!opt)
      return;

   idx                  = core_option_index(idx);
   opt->opts[idx].index = opt->opts[idx].default_index;
   opt->updated         = true;
   options_touched      = true;
}

void core_options_conf_reload(core_option_manager_t *opt)
{
   config_file_free(opt->conf);
   if (path_file_exists(opt->conf_path))
      opt->conf = config_file_new(opt->conf_path);
   else
      opt->conf = config_file_new(NULL);
}

char* core_option_conf_path(core_option_manager_t *opt)
{
   return opt->conf_path;
}

void core_option_get_core_conf_path(char *path)
{
   settings_t *settings            = config_get_ptr();
   global_t *global                = global_get_ptr();  
   const char *core_name           = global ? global->libretro_name
                                              : NULL;
   char directory[PATH_MAX_LENGTH] = {0};
   
   if (!settings || !global || !*core_name)
      return;
   
   fill_pathname_join(directory, settings->menu_config_directory,
                      core_name, PATH_MAX_LENGTH);
   fill_pathname_join(path, directory,
                      core_name, PATH_MAX_LENGTH);
   strlcat(path, ".opt", PATH_MAX_LENGTH);

   if(!path_file_exists(directory))
      path_mkdir(directory);
}

bool core_option_get_game_conf_path(char *path)
{
   char directory[PATH_MAX_LENGTH] = {0};
   char abs_path[PATH_MAX_LENGTH]  = {0};
   global_t *global                = global_get_ptr();
   settings_t *settings            = config_get_ptr();
   const char *core_name           = global ? global->libretro_name
                                              : NULL;
   const char *game_name           = global ? path_basename(global->basename)
                                              : NULL;
   
   if (!settings || !global || !*core_name || !*game_name)
      return false;

   fill_pathname_join(directory, settings->menu_config_directory,
                      core_name, PATH_MAX_LENGTH);
   fill_pathname_join(abs_path, directory, game_name, PATH_MAX_LENGTH);
   strlcat(abs_path, ".opt", PATH_MAX_LENGTH);
   
   if (path_file_exists(abs_path))
   {
      strlcpy(path, abs_path, PATH_MAX_LENGTH);
      return true;
   }
   
   return false;
}
