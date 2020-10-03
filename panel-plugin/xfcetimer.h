/*  
 *
 *  Copyright (C) 2005 Kemal Ilgar Eroglu <kieroglu@math.washington.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

typedef struct alarm
{
  gchar *name, *info;
  gchar *command; /* Command when countdown ends */
  struct alarm *next_alarm; /* Next alarm to start when current countdown ends */
  gint next_alarm_groupnum; /* Next alarm groupnum, used only in load_\save_settings */
  gint time;
  gboolean is_auto_start, timer_on;

  gboolean is_repeating; /* True while alarm repeats */
  gboolean is_paused; /* True if the countdown is paused */
  gboolean is_countdown; /* True if the alarm type is contdown */
  gpointer pd;
  gint timeout_period_in_sec,    /* Active countdown period */
          rem_repetitions;      /* Remaining repeats */
  guint timeout,repeat_timeout;	/* The timeout IDs */
  GTimer *timer; /* Keeps track of the time elapsed */
} alarm_t;

typedef struct
{
  GtkWidget *box; /* v/hbox that holds pbar */
  GtkWidget *pbar; /* Progress bar */
  GtkWidget *tree; /* Treeview */
  GtkWidget *buttonadd, *buttonedit, *buttonremove; /* options window buttons */
  GtkWidget *buttonup, *buttondown;
  GtkWidget *spin_repeat, *spin_interval; /* spinbuttons for alarm repeat */
  GtkWidget *menu;
  GtkWidget *glob_command_entry; /* Text entry widget for the default alarm command */
  GtkWidget *global_command_box;/* Box holding the default command settings */
  GtkWidget *repeat_alarm_box; /* Box holding the repeat alarm settings */
  XfcePanelPlugin *base; /* The plugin widget */
  GtkListStore *liststore; /* The alarms list */
  gint count;
  gint repetitions; /* Number of alarm repeats */
  gint repeat_interval; /* Time interval between repeats (in secs) */
  gboolean nowin_if_alarm; /* Show warning window when alarm command is set */
  gboolean repeat_alarm_command; /* Repeat alarm command*/
  gboolean use_global_command; /* Use a default alarm command if no alarm command is set */
  gchar *global_command; /* The global (default) command to be run when countdown ends */
  gchar *configfile; /* Full address of the permanent config file -- this is not the plugin rc file. */
  GList *alarm_list; /* List of alarms */
  GList *selected; /* Selected alarm */
  guint num_active_timers;
} plugin_data;

typedef struct
{
  GtkSpinButton *timeh, *times, *timem; /* Spinbuttons for h-m-s format */
  GtkSpinButton *time_h, *time_m; /* Spinbuttons for 24h format */
  GtkEntry *name, *command; /* Name, and command entries */
  GtkRadioButton *rb1; /* Radio button for the h-m-s format */
  GtkWidget *next_alarm; /* Combobox for next alarm selection */
  GtkWidget *autostart_cb; /* Check button for autostart */
  GtkWidget *dialog; /* Add/Edit dialog */
  plugin_data *pd; /* Plugin data */
} alarm_data;
