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

typedef struct
{
  gchar *name, *command, *info;
  gint time;
  gboolean iscountdown;
  gpointer pd;
} alarm_t;

typedef struct
{
  GtkWidget *eventbox; /* Main container widget in ctrl->base */
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
  gint timeout_period_in_sec; /* Active countdown period */
  gint repetitions; /* Number of alarm repeats */
  gint rem_repetitions; /* Remaining repeats */
  gint repeat_interval; /* Time interval between repeats (in secs) */
  guint timeout, repeat_timeout; /* The timeout IDs */
  gboolean timer_on; /* TRUE if countdown is in progress */
  gboolean nowin_if_alarm; /* Show warning window when alarm command is set */
  gboolean selecting_starts; /* selecting a timer also starts it */
  gboolean repeat_alarm; /* Repeat alarm */
  gboolean use_global_command; /* Use a default alarm command if no alarm command is set */
  gboolean alarm_repeating; /* True while alarm repeats */
  gboolean is_paused; /* True if the countdown is paused */
  gboolean is_countdown; /* True if the alarm type is contdown */
  gchar *timeout_command; /* Command when countdown ends */
  gchar *global_command; /* The global (default) command to be run when countdown ends */
  gchar *active_timer_name; /* Name of the timer running */
  gchar *configfile; /* Full address of the permanent config file -- this is not the plugin rc file. */
  GTimer *timer; /* Keeps track of the time elapsed */
  GList *alarm_list; /* List of alarms */
  GList *selected; /* Selected alarm */
} plugin_data;

typedef struct
{
  GtkSpinButton *timeh, *times, *timem; /* Spinbuttons for h-m-s format */
  GtkSpinButton *time_h, *time_m; /* Spinbuttons for 24h format */
  GtkEntry *name, *command; /* Name, and command entries */
  GtkRadioButton *rb1; /* Radio button for the h-m-s format */
  GtkWidget *dialog; /* Add/Edit dialog */
  plugin_data *pd; /* Plugin data */
} alarm_data;
