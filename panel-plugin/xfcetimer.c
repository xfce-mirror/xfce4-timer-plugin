/*
 *
 *  Copyright (C) 2005-2014 Kemal Ilgar Eroglu <ilgar_eroglu@yahoo.com>
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



/* Countdown update period in milliseconds */
#define UPDATE_INTERVAL 2000
#define PBAR_THICKNESS  10
#define BORDER 4
#define WIDGET_SPACING 2



#ifdef HAVE_XFCE_REVISION_H
#include "xfce-revision.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include <gtk/gtk.h>
#include <glib/gprintf.h>  // for gcc's warning: implicit declaration of function 'g_sprintf'
#include <libxfce4util/libxfce4util.h>
#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4panel/libxfce4panel.h>

#include "xfcetimer.h"



static void
create_plugin_control (XfcePanelPlugin *plugin);

static void
start_timer (plugin_data *pd, alarm_t* alrm);

static void
dialog_response (GtkWidget *dlg, int response, alarm_t* alrm);

static void
start_stop_callback (GtkWidget* menuitem, gpointer data);
XFCE_PANEL_PLUGIN_REGISTER ( create_plugin_control);

void
make_menu (plugin_data *pd);

static void
add_edit_clicked (GtkButton *buttonn, gpointer data);


/* This is the timeout function that repeats the alarm */
static gboolean
repeat_alarm (gpointer data)
{
  alarm_t *alrm;
  gchar *command;
  plugin_data *pd;

  alrm = (alarm_t *) data;
  pd = alrm->pd;

  /* Don't repeat anymore */
  if (alrm->rem_repetitions == 0)
    {
      alrm->is_repeating=FALSE;
      return FALSE;
    }

  if (strlen(alrm->command)>0)
    command = g_strdup(alrm->command);
  else if (pd->use_global_command)
    command = g_strdup (pd->global_command);
  else
    command = g_strdup("");

  g_spawn_command_line_async (command, NULL);
  g_free (command);
  alrm->rem_repetitions = alrm->rem_repetitions - 1;
  return TRUE;
}



/**
 * Fills in the pd->liststore to create the treeview
 * in the options window. The second arguments indicates
 * the alarm whose row will be highlighted in the
 * treeview. If it is NULL, no action is taken.
 **/
static void
fill_liststore (plugin_data *pd, GList *selected)
{
  GtkTreeIter iter;
  GList *list;
  alarm_t *alrm;

  if (pd->liststore)
    gtk_list_store_clear (pd->liststore);

  list = pd->alarm_list;

  while (list)
    {

      alrm = (alarm_t *) list->data;

      gtk_list_store_append (pd->liststore, &iter);

      gtk_list_store_set (pd->liststore, &iter, 0, list, 1, alrm->name, 2,
                          alrm->info, 3, alrm->command, -1);

      /* We select the given row */
      if (selected && list == selected)
        gtk_tree_selection_select_iter (
            gtk_tree_view_get_selection (GTK_TREE_VIEW (pd->tree)), &iter);

      list = g_list_next (list);
    }
}



/**
 * This is the update function that updates the
 * tooltip, pbar and keeps track of elapsed time
 **/
static gboolean
update_function (gpointer data)
{
  plugin_data *pd = (plugin_data *) data;
  gint elapsed_sec, remaining;
  //for showing the progress of the first to finish
  gint min_remaining_time = G_MAXINT;
  gchar *tiptext = NULL, *temp, *dialog_title, *dialog_message;
  gchar *finalTipText = g_strdup("");
  GtkWidget *dialog;
  GList *list = NULL;
  alarm_t *alrm;
  gboolean callAgain = FALSE;
  gboolean firstActiveTimer = TRUE;

  list = pd->alarm_list;

  while (list){
      alrm = (alarm_t *) list->data;
      if(alrm->timer_on){

      elapsed_sec = (gint) g_timer_elapsed(alrm->timer, NULL);
  /* If countdown is not over, update tooltip */
      if (elapsed_sec < alrm->timeout_period_in_sec) {
          remaining = alrm->timeout_period_in_sec - elapsed_sec;

          if (remaining >= 3600)
            tiptext = g_strdup_printf (_("%dh %dm %ds left"), remaining / 3600,
                                       (remaining % 3600) / 60, remaining % 60);
          else if (remaining >= 60)
            tiptext = g_strdup_printf (_("%dm %ds left"), remaining / 60,
                                       remaining % 60);
          else
            tiptext = g_strdup_printf (_("%ds left"), remaining);

          if (alrm->is_paused)
            {
              temp = g_strconcat (tiptext, _(" (Paused)"), NULL);
              g_free (tiptext);
              tiptext = temp;
            }
          if(alrm->timeout_period_in_sec < min_remaining_time){
              min_remaining_time = alrm->timeout_period_in_sec;
              gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pd->pbar),
                      1.0 - ((gdouble) elapsed_sec) / alrm->timeout_period_in_sec);
          }
      callAgain =  TRUE;
      }else{
              gchar *command;

              /* Countdown is over, stop timer and free resources */
              if (alrm->timer)
                  g_timer_destroy(alrm->timer);
              alrm->timer = NULL;
              /* Disable tooltips, reset pbar */
              gtk_widget_set_tooltip_text (GTK_WIDGET (pd->base), "");
              gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (pd->pbar), 0);

              alrm->timeout = 0;
              alrm->timer_on = FALSE;

              /* If an alarm command is set, it overrides the default (if any) */
              if (strlen(alrm->command)>0)
                command = g_strdup(alrm->command);
              else if (pd->use_global_command)
                command = g_strdup (pd->global_command);
              else
                command = g_strdup("");

              if ((strlen(command) == 0) || !pd->nowin_if_alarm) {
                gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (pd->pbar), 0);

                /* Display the name of the alarm when the countdown ends */
                dialog_message = g_strdup_printf(_("Beeep! :) \nTime is up for the alarm %s."), alrm->name);
                dialog_title = g_strdup_printf(_("Timer %s"), alrm->name);

                dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
                        GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, "%s", dialog_message);

                gtk_window_set_keep_above((GtkWindow *) dialog, TRUE);

                gtk_window_set_title((GtkWindow *) dialog, dialog_title);

                gtk_dialog_add_button((GtkDialog *) dialog, _("Close"), 0);
                gtk_dialog_add_button((GtkDialog *) dialog, _("Rerun the timer"), 1);

                g_signal_connect(dialog, "response",
                        G_CALLBACK(dialog_response),
                        alrm);

                g_free(dialog_title);
                g_free(dialog_message);

                gtk_widget_show(dialog);
            }

            if (strlen(command) > 0) {

                g_spawn_command_line_async(command, NULL);

                if (pd->repeat_alarm_command) {
                    alrm->is_repeating = TRUE;
                    alrm->rem_repetitions = pd->repetitions;
                    if (alrm->repeat_timeout != 0)
                        g_source_remove(alrm->repeat_timeout);
                    alrm->repeat_timeout = g_timeout_add(pd->repeat_interval * 1000, repeat_alarm, alrm);
                } else {
                    g_free(command);
                    command = NULL;
                }
            }

          // Start next alarm if configured
          if (alrm->next_alarm)
            start_timer(pd, alrm->next_alarm);

          }
          //tiptext = g_strconcat("\t", tiptext, NULL);
          temp = g_strconcat(alrm->name ,"\t", tiptext, NULL);
          g_free(tiptext);
          tiptext = temp;
          //if not first
          if(firstActiveTimer){
            firstActiveTimer = FALSE;
          }else{
            temp = g_strconcat("\n", tiptext, NULL);
            g_free(tiptext);
            tiptext = temp;
          }
          temp = g_strconcat(finalTipText, tiptext, NULL);
          g_free(finalTipText);
          finalTipText = temp;
        }
        list = g_list_next (list);
      }
  gtk_widget_set_tooltip_text (GTK_WIDGET(pd->base), finalTipText);
  g_free(tiptext);
  g_free(finalTipText);

  return callAgain;
}



/**
 * This is the callback function called when a timer
 * is selected in the popup menu
 **/
static void
timer_selected (GtkWidget* menuitem, gpointer data)
{
  GList *list = (GList *) data;
  alarm_t *alrm;
  plugin_data *pd;

  alrm = (alarm_t *) list->data;
  pd = (plugin_data *) alrm->pd;

  pd->selected = list;

  start_stop_callback (menuitem, list);
}



/**
 * Used for starting/rerunning the timer
 * Assumes that the timer is already stopped
 **/
static void
start_timer (plugin_data *pd, alarm_t* alrm)
{

  gint cur_h, cur_m, cur_s;
  gint timeout_period;
  GDateTime *current;

  /* Empty timer list-> Nothing to do. alrm=0, though */
  if (alrm == NULL)
    return;

  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (pd->pbar), 1);

  /**
   *  If it's a 24h type alarm, we find the difference with current time
   *  Here 'time' is in minutes
   **/
  if (!alrm->is_countdown)
    {

      if (alrm->is_utc)
        current = g_date_time_new_now_utc ();
      else
        current = g_date_time_new_now_local ();

      cur_h = g_date_time_get_hour   (current);
      cur_m = g_date_time_get_minute (current);
      cur_s = g_date_time_get_second (current);
      g_date_time_unref (current);

      timeout_period = (alrm->time) * 60 - ((60 * cur_h + cur_m) * 60 + cur_s);

      if (timeout_period < 0)
        timeout_period += 24 * 60 * 60;

      alrm->is_countdown = FALSE;
    }
  /* Else 'alrm->selected->time' already gives the countdown period in seconds */
  else
    {
      alrm->is_countdown = TRUE;
      timeout_period = alrm->time;
    }

  alrm->timeout_period_in_sec = timeout_period;

  /* start the timer */
  alrm->timer = g_timer_new ();
  alrm->timer_on = TRUE;

  gtk_widget_set_tooltip_text (GTK_WIDGET (pd->base), alrm->info);

  g_timer_start (alrm->timer);
  alrm->timeout = g_timeout_add (UPDATE_INTERVAL, update_function, pd);
}



/**
 * This is the callback function called when the
 * start/stop item is selected in the popup menu
 **/
static void
start_stop_callback (GtkWidget* menuitem, gpointer list)
{
      GList *listitem = (GList *) list;
      plugin_data *pd;
      alarm_t *alrm;

      alrm = (alarm_t *) listitem->data;
      pd = (plugin_data *) alrm->pd;

  /* If counting down, we stop the timer and free the resources */
  if (alrm->timer_on)
    {

      if(alrm->timer)
         g_timer_destroy(alrm->timer);
      if(alrm->timeout)
         g_source_remove(alrm->timeout);

        alrm->timer = NULL;
        alrm->timeout = 0;
        alrm->is_paused = FALSE;
        alrm->timer_on = FALSE;

      /* Disable tooltips, reset pbar */
      gtk_widget_set_tooltip_text (GTK_WIDGET (pd->base), "");
      gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (pd->pbar), 0);

      return;

    }

  /* If we're here then the timer is off, so we start it */
  start_timer (pd, alrm);
}



static void
pause_resume_selected (GtkWidget* menuitem, gpointer data)
{
  alarm_t *alrm;
  alrm = (alarm_t *) data;

  /* If paused, we resume */
  if (alrm->is_paused)
    {
      g_timer_continue (alrm->timer);
      /* If we're here then the timer is runnig, so we pause */
      alrm->is_paused = FALSE;
    }
  else
    {
      alrm->is_paused = TRUE;
      g_timer_stop (alrm->timer);
    }
}



/* Callback when clicking on pbar. Pops the menu up/down */
static void
pbar_clicked (GtkWidget *pbar, GdkEventButton *event, gpointer data)
{
  plugin_data *pd = (plugin_data *) data;

  make_menu (pd);

  if (!pd->menu)
    return;

  if (event->button == 1)
    gtk_menu_popup_at_widget (GTK_MENU (pd->menu), pd->pbar,
                              GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST,
                              NULL);
  else
    gtk_menu_popdown (GTK_MENU (pd->menu));
}

/* This function help to remove left padding from menu */
/* src:https://gist.github.com/andreldm/62ecee0d87cde8ec5f3d0e36e404511c */
static GtkWidget*
create_menu_item (const gchar *str, const char *icon_name)
{
  GtkWidget *mi, *label, *box, *image;

  mi = gtk_menu_item_new ();
  label = gtk_label_new (str);
  image = gtk_image_new_from_icon_name (icon_name ? icon_name : "", GTK_ICON_SIZE_BUTTON);

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_set_halign (label, GTK_ALIGN_START);

  gtk_box_pack_start (GTK_BOX (box), image, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 6);
  gtk_container_add (GTK_CONTAINER (mi), box);

  return mi;
}


/* This function generates the popup menu */
void
make_menu (plugin_data *pd)
{
  GList *list = NULL;
  alarm_t *alrm;
  GtkWidget *menu_item;
  gchar *itemtext;

  /* Destroy the existing one */
  if (pd->menu)
    gtk_widget_destroy (pd->menu);

  pd->menu = gtk_menu_new ();
 /* remove left padding from menu, see also create_menu_item () */
  gtk_menu_set_reserve_toggle_size (GTK_MENU (pd->menu), FALSE);

  list = pd->alarm_list;

  menu_item = create_menu_item (_("Add new alarm"), "xfce4-timer-plugin");
  gtk_menu_shell_append (GTK_MENU_SHELL( pd->menu ), menu_item);

  g_signal_connect( G_OBJECT(menu_item), "activate",
                    G_CALLBACK (add_edit_clicked), pd);

  while (list)
    {
      /* Run through the list, read name and timer period info */

      alrm = (alarm_t *) list->data;
      itemtext = g_strdup_printf ("%s (%s)", alrm->name, alrm->info);

      menu_item = create_menu_item (itemtext, NULL);
      gtk_menu_shell_append (GTK_MENU_SHELL(pd->menu),menu_item);
      g_free (itemtext);

      if (alrm->timer_on) {
        gtk_widget_set_sensitive (GTK_WIDGET(menu_item), FALSE);

        /* Pause menu item */
        if (!alrm->is_paused && alrm->is_countdown) {
            menu_item = create_menu_item (_("Pause timer"), "media-playback-pause");
            gtk_menu_shell_append (GTK_MENU_SHELL(pd->menu), menu_item);
            g_signal_connect (G_OBJECT(menu_item),"activate",
                              G_CALLBACK(pause_resume_selected), alrm);
        }
        /* If the alarm is paused, the only option is to resume or stop */
        else if (alrm->is_paused) {
            menu_item = create_menu_item(_("Resume timer"), "media-playback-start");
            gtk_menu_shell_append (GTK_MENU_SHELL(pd->menu), menu_item);
            g_signal_connect (G_OBJECT(menu_item), "activate",
                              G_CALLBACK(pause_resume_selected), alrm);
        }

        menu_item = create_menu_item(_("Stop timer"), "media-playback-stop");
        gtk_menu_shell_append (GTK_MENU_SHELL(pd->menu), menu_item);
        g_signal_connect (G_OBJECT(menu_item), "activate",
                          G_CALLBACK(start_stop_callback), list);
      } else {
        g_signal_connect (G_OBJECT(menu_item), "activate",
                          G_CALLBACK (timer_selected), list);
        /* disable alarm menu entry if repeating command */
        if (alrm->is_repeating)
          gtk_widget_set_sensitive(GTK_WIDGET(menu_item),FALSE);
      }

      list = list->next;
      if (list) {
        /* Horizontal line (empty item) */
        menu_item = gtk_separator_menu_item_new();
        gtk_menu_shell_append (GTK_MENU_SHELL(pd->menu),menu_item);
      }
    }

  gtk_widget_show_all (pd->menu);
}



/* Callback to the OK button in the Add window */
static void
ok_add (GtkButton *button, gpointer data)
{
  alarm_data *adata = (alarm_data *) data;
  alarm_t *newalarm;
  GtkTreeIter iter;
  gint t1, t2, t3, t;
  gchar *timeinfo = NULL;

  /* Add item to the alarm list and liststore */
  newalarm = g_new0 (alarm_t, 1);
  newalarm->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (adata->name)));
  newalarm->command = g_strdup (
      gtk_entry_get_text (GTK_ENTRY (adata->command)));
  newalarm->is_countdown = gtk_toggle_button_get_active (
      GTK_TOGGLE_BUTTON (adata->rb1));
  newalarm->pd = (gpointer) adata->pd;
  newalarm->timer_on = FALSE;
  newalarm->timeout = 0;
  newalarm->timer = NULL;
  newalarm->is_paused = FALSE;
  newalarm->rem_repetitions = 1;
  newalarm->is_repeating = FALSE;
  newalarm->repeat_timeout = 0;

  adata->pd->alarm_list = g_list_append (adata->pd->alarm_list, newalarm);
  if (g_list_length (adata->pd->alarm_list) == 1)
    adata->pd->selected = adata->pd->alarm_list;

  gtk_list_store_append (adata->pd->liststore, &iter);

  gtk_list_store_set (GTK_LIST_STORE (adata->pd->liststore), &iter, 0,
                      g_list_last (adata->pd->alarm_list), 1, newalarm->name, 3,
                      newalarm->command, -1);

  /* Item count goes up by one */
  adata->pd->count = adata->pd->count + 1;

  /* If the h-m-s format (countdown) was chosen, convert time to seconds,
   save the choice into the list */
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (adata->rb1)))
    {
      t1 = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (adata->timeh));
      t2 = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (adata->timem));
      t3 = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (adata->times));
      t = t1 * 3600 + t2 * 60 + t3;

      if (t1 > 0)
        timeinfo = g_strdup_printf (_("%dh %dm %ds"), t1, t2, t3);
      else if (t2 > 0)
        timeinfo = g_strdup_printf (_("%dm %ds"), t2, t3);
      else
        timeinfo = g_strdup_printf (_("%ds"), t3);
    }
  else
    {
      /* The 24h format (alarm at specified time). Save time in minutes */
      t1 = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (adata->time_h));
      t2 = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (adata->time_m));
      t = t1 * 60 + t2;
      timeinfo = g_strdup_printf (_("At %02d:%02d"), t1, t2);
    }

  newalarm->time = t;
  newalarm->info = timeinfo;
  gtk_list_store_set (GTK_LIST_STORE (adata->pd->liststore), &iter, 2, timeinfo,
                      -1);

  /* Free resources */
  gtk_widget_destroy (GTK_WIDGET (adata->dialog));

  g_free (adata);
}



/* Callback for OK button on Edit dialog. See ok_add for comments */
static void
ok_edit (GtkButton *button, gpointer data)
{
  alarm_data *adata = (alarm_data *) data;
  GtkTreeIter iter, next_alarm_iter;
  gint t1, t2, t3, t;
  gchar *timeinfo = NULL;
  GList *list;
  alarm_t *alrm;
  GtkTreeSelection *select;
  GtkTreeModel *model;

  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (adata->pd->tree));

  if (gtk_tree_selection_get_selected (select, &model, &iter))
    {

      gtk_tree_model_get (GTK_TREE_MODEL (adata->pd->liststore), &iter, 0,
                          &list, -1);
      alrm = (alarm_t *) list->data;

      alrm->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (adata->name)));
      alrm->command = g_strdup (
          gtk_entry_get_text (GTK_ENTRY (adata->command)));
      alrm->is_countdown = gtk_toggle_button_get_active (
          GTK_TOGGLE_BUTTON (adata->rb1));

      if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (adata->next_alarm), &next_alarm_iter))
        gtk_tree_model_get (gtk_combo_box_get_model (GTK_COMBO_BOX (adata->next_alarm)),
                            &next_alarm_iter, 0, &alrm->next_alarm, -1);
      else
        alrm->next_alarm = NULL;

      alrm->is_auto_start = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(adata->
                                 autostart_cb));


      /* This should be unnecessary, but do it anyway */
      alrm->pd = (gpointer) adata->pd;

      gtk_list_store_set (GTK_LIST_STORE (adata->pd->liststore), &iter, 1,
                          alrm->name, 3, alrm->command, -1);

      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (adata->rb1)))
        {

          t1 = gtk_spin_button_get_value_as_int (
              GTK_SPIN_BUTTON (adata->timeh));
          t2 = gtk_spin_button_get_value_as_int (
              GTK_SPIN_BUTTON (adata->timem));
          t3 = gtk_spin_button_get_value_as_int (
              GTK_SPIN_BUTTON (adata->times));
          t = t1 * 3600 + t2 * 60 + t3;

          if (t1 > 0)
            timeinfo = g_strdup_printf (_("%dh %dm %ds"), t1, t2, t3);
          else if (t2 > 0)
            timeinfo = g_strdup_printf (_("%dm %ds"), t2, t3);
          else
            timeinfo = g_strdup_printf (_("%ds"), t3);

        }
      else
        {

          alrm->is_utc = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (adata->utc_cb));
          t1 = gtk_spin_button_get_value_as_int (
              GTK_SPIN_BUTTON (adata->time_h));
          t2 = gtk_spin_button_get_value_as_int (
              GTK_SPIN_BUTTON (adata->time_m));
          t = t1 * 60 + t2;

          if (alrm->is_utc)
            timeinfo = g_strdup_printf (_("At %02d:%02dZ"), t1, t2);
          else
            timeinfo = g_strdup_printf (_("At %02d:%02d"), t1, t2);

        }

      alrm->time = t;
      alrm->info = timeinfo;
      gtk_list_store_set (GTK_LIST_STORE (adata->pd->liststore), &iter, 2,
                          timeinfo, -1);
    }

  gtk_widget_destroy (GTK_WIDGET (adata->dialog));
  g_free (adata);
}



/* Callback for cancelling Add and Edit. Just closes the dialog :) */
static void
cancel_add_edit (GtkButton *button, gpointer data)
{
  alarm_data *adata = (alarm_data *) data;

  gtk_widget_destroy (GTK_WIDGET (adata->dialog));
  g_free (adata);
}



/* Callback when first radio button in dialog has been selected */
static void
alarmdialog_countdown_toggled (GtkButton *button, gpointer data)
{
  alarm_data *adata = (alarm_data *) data;
  gboolean active;

  active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));
  gtk_widget_set_sensitive (GTK_WIDGET (adata->timeh), active);
  gtk_widget_set_sensitive (GTK_WIDGET (adata->timem), active);
  gtk_widget_set_sensitive (GTK_WIDGET (adata->times), active);
}



/* Callback when second radio button in dialog has been selected */
static void
alarmdialog_alarmtime_toggled (GtkButton *button, gpointer data)
{
  alarm_data *adata = (alarm_data *) data;
  gboolean active;

  active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));
  gtk_widget_set_sensitive (GTK_WIDGET (adata->time_h), active);
  gtk_widget_set_sensitive (GTK_WIDGET (adata->time_m), active);
}



/**
 * Callback to the Add and Edit buttons in options window
 * Callback to the "Add new alarm" menu entry
 * Creates the Add window
 **/
static void
add_edit_clicked (GtkButton *buttonn, gpointer data)
{
  plugin_data *pd = (plugin_data *) data;
  GtkWindow *parent_window;
  GtkWidget *dialog;
  GtkWidget *box;
  GtkLabel *label;
  GtkEntry *name, *command;
  GtkSpinButton *timeh, *timem, *times, *time_h, *time_m;
  GtkRadioButton *rb1, *rb2;
  GtkListStore *next_alarm_list;
  GList *alarm_iter;
  GtkWidget *next_alarm;
  GtkCellRenderer *next_alarm_cell;
  GtkWidget *hbox, *vbox, *button;
  alarm_data *adata = g_new0 (alarm_data, 1);
  gint time;
  GtkTreeIter iter;
  GtkTreeSelection *select;
  GtkTreeModel *model;
  GList *list;
  alarm_t *alrm;

  parent_window = (GtkWindow *) gtk_widget_get_toplevel (GTK_WIDGET (buttonn));

  /* Create dialog */
  dialog = gtk_dialog_new ();

  /* Set it modal and transient for main window. */
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_transient_for (GTK_WINDOW (dialog),
                                GTK_WINDOW (parent_window));

  gtk_window_set_icon_name (GTK_WINDOW (dialog), "xfce4-timer-plugin");

  adata->dialog = dialog;
  adata->pd = pd;

  /* Set title */
  box = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  gtk_container_set_border_width (GTK_CONTAINER (box), 2);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_pack_start (GTK_BOX (box), vbox, FALSE, FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);

  /***********/
  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  label = (GtkLabel *) gtk_label_new (_("Name:"));
  name = (GtkEntry *) gtk_entry_new ();
  adata->name = name;

  gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (label), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (name), TRUE, TRUE, 0);

  gtk_box_pack_start (GTK_BOX (vbox), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 6);

  /**********/
  rb1 = (GtkRadioButton *) gtk_radio_button_new_with_label (
      NULL, _("Enter the countdown time"));
  g_signal_connect (G_OBJECT (rb1), "toggled",
                    G_CALLBACK (alarmdialog_countdown_toggled), adata);
  rb2 = (GtkRadioButton *) gtk_radio_button_new_with_label (
      gtk_radio_button_get_group (rb1),
      _("Enter the time of alarm (24h format)"));
  g_signal_connect (G_OBJECT (rb2), "toggled",
                    G_CALLBACK (alarmdialog_alarmtime_toggled), adata);
  adata->rb1 = rb1;

  gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (rb1), TRUE, TRUE, 0);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (hbox), TRUE, TRUE, 0);
  gtk_widget_set_margin_start (GTK_WIDGET (hbox), 12);

  timeh = (GtkSpinButton *) gtk_spin_button_new_with_range (0, 23, 1);
  gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (timeh), FALSE, FALSE, 0);
  adata->timeh = timeh;

  label = (GtkLabel *) gtk_label_new (_("h  "));
  gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (label), FALSE, FALSE, 0);

  timem = (GtkSpinButton *) gtk_spin_button_new_with_range (0, 59, 1);
  gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (timem), FALSE, FALSE, 0);
  adata->timem = timem;

  label = (GtkLabel *) gtk_label_new (_("m  "));
  gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (label), FALSE, FALSE, 0);

  times = (GtkSpinButton *) gtk_spin_button_new_with_range (0, 59, 1);
  gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (times), FALSE, FALSE, 0);
  adata->times = times;

  label = (GtkLabel *) gtk_label_new (_("s  "));
  gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (label), FALSE, FALSE, 0);

  label = (GtkLabel *) gtk_label_new (_("or"));
  gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (label), TRUE, TRUE, 6);

  gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (rb2), TRUE, TRUE, 0);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (hbox), TRUE, TRUE, 0);
  gtk_widget_set_margin_start (GTK_WIDGET (hbox), 12);

  time_h = (GtkSpinButton *) gtk_spin_button_new_with_range (0, 23, 1);
  gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (time_h), FALSE, FALSE, 0);
  adata->time_h = time_h;

  label = (GtkLabel *) gtk_label_new (":");
  gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (label), FALSE, FALSE, 0);

  time_m = (GtkSpinButton *) gtk_spin_button_new_with_range (0, 59, 1);
  gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (time_m), FALSE, FALSE, 0);
  adata->time_m = time_m;

  button = gtk_check_button_new_with_label (_("UTC"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), FALSE);
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);
  adata->utc_cb = button;

  gtk_box_pack_start (GTK_BOX (vbox), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 6);

  /****************/

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (hbox), TRUE, TRUE, 0);

  label = (GtkLabel *) gtk_label_new (_("Command to run:"));
  gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (label), FALSE, FALSE, 0);

  command = (GtkEntry *) gtk_entry_new ();
  gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (command), TRUE, TRUE, 0);
  adata->command = command;

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (hbox), TRUE, TRUE, 0);

  label = (GtkLabel *) gtk_label_new (_("Alarm to run:"));
  gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (label), FALSE, FALSE, 0);

  /* 'next alarm' combo box to set subsequent alarm (if any)
   * Separate list store created to add 'empty' item for no next alarm */
  next_alarm_list = gtk_list_store_new (2, G_TYPE_POINTER, G_TYPE_STRING);
  gtk_list_store_insert_with_values (next_alarm_list, &iter, -1, 0, NULL, 1, "", -1);
  alarm_iter = pd->alarm_list;
  while (alarm_iter)
  {
    alrm = alarm_iter->data;
    gtk_list_store_insert_with_values (next_alarm_list, &iter, -1, 0, alrm, 1, alrm->name, -1);
    alarm_iter = alarm_iter->next;
  }
  next_alarm = gtk_combo_box_new_with_model (GTK_TREE_MODEL (next_alarm_list));
  g_object_unref (next_alarm_list);
  next_alarm_cell = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (next_alarm), next_alarm_cell, TRUE);
  gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (next_alarm), next_alarm_cell, "text", 1);
  gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (next_alarm), FALSE, FALSE, 0);
  adata->next_alarm = next_alarm;

  /****************/

  gtk_box_pack_start (GTK_BOX (vbox), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 6);

   //add alarm autostart check button
  button = gtk_check_button_new_with_label(_("Auto start when plugin loads"));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);
  adata->autostart_cb=button;

  hbox = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_box_set_spacing (GTK_BOX (hbox), 6);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (hbox), GTK_BUTTONBOX_END);
  gtk_box_pack_start (GTK_BOX (box), hbox, TRUE, TRUE, 0);

  button = gtk_button_new_with_label (_("Cancel"));
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);
  g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (cancel_add_edit),
                    adata);

  button = gtk_button_new_with_label (_("Accept"));
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);
  if (GTK_WIDGET (buttonn) == pd->buttonedit)
    g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (ok_edit), adata);
  else /* Add button or "Add new alarm" menu entry*/
    g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (ok_add), adata);

  /* If this is not edit window (add button or add menu entry) we're done */
  if (GTK_WIDGET (buttonn) != pd->buttonedit)
    {
      gtk_window_set_title (GTK_WINDOW (dialog), _("Add new alarm"));
      gtk_widget_show_all (GTK_WIDGET (dialog));
      alarmdialog_alarmtime_toggled (GTK_BUTTON (rb2), adata);
      return;
    }

  /* Else fill the values in the boxes with the current choices */
  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (pd->tree));

  if (gtk_tree_selection_get_selected (select, &model, &iter))
    {

      gtk_tree_model_get (model, &iter, 0, &list, -1);
      alrm = (alarm_t *) list->data;

      gtk_entry_set_text (GTK_ENTRY (name), alrm->name);
      gtk_entry_set_text (GTK_ENTRY (command), alrm->command);

      gtk_combo_box_set_active (GTK_COMBO_BOX (next_alarm),
                                g_list_index (pd->alarm_list, alrm->next_alarm) + 1);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(adata->autostart_cb),alrm->is_auto_start);

      time = alrm->time;

      if (alrm->is_countdown)
        {
          gtk_spin_button_set_value (GTK_SPIN_BUTTON (timeh), time / 3600);
          gtk_spin_button_set_value (GTK_SPIN_BUTTON (timem),
                                     (time % 3600) / 60);
          gtk_spin_button_set_value (GTK_SPIN_BUTTON (times), time % 60);
          alarmdialog_alarmtime_toggled (GTK_BUTTON (rb2), adata);
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rb1), TRUE);
        }
      else
        {
          gtk_spin_button_set_value (GTK_SPIN_BUTTON (time_h), time / 60);
          gtk_spin_button_set_value (GTK_SPIN_BUTTON (time_m), time % 60);
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rb2), TRUE); // active by default
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (adata->utc_cb), alrm->is_utc);
        }
    }

  gtk_window_set_title (GTK_WINDOW (dialog), _("Edit alarm"));
  gtk_widget_show_all (GTK_WIDGET (dialog));
}



/* Calllback for the remove button in the options */
static void
remove_clicked (GtkButton *button, gpointer data)
{
  plugin_data *pd = (plugin_data *) data;
  GtkTreeIter iter;
  GtkTreeModel *model;
  GtkTreeSelection *select;
  GList *list, *alarm_iter;
  alarm_t *alrm;

  /* Get the selected row */
  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (pd->tree));

  if (!select)
    return;

  if (!gtk_tree_selection_get_selected (select, &model, &iter))
    return;

  gtk_tree_model_get (model, &iter, 0, &list, -1);

  // If removed alarm is used as a 'next alarm' somwhere, unreference it
  alarm_iter = pd->alarm_list;
  while (alarm_iter)
  {
    alrm = alarm_iter->data;
    if (alrm->next_alarm == list->data)
      alrm->next_alarm = NULL;
    alarm_iter = alarm_iter->next;
  }

  if (pd->selected == list)
    {
      pd->alarm_list = g_list_delete_link (pd->alarm_list, list);
      pd->selected = pd->alarm_list;
    }
  else
    {
      pd->alarm_list = g_list_delete_link (pd->alarm_list, list);
    }
  fill_liststore (pd, NULL);
}



/* Moves an alarm one row up in the list */
static void
up_clicked (GtkButton *button, gpointer data)
{
  plugin_data *pd = (plugin_data *) data;
  GtkTreeIter iter;
  GtkTreeModel *model;
  GtkTreeSelection *select;
  GList *list, *list_prev;

  /* Get the selected row */
  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (pd->tree));

  if (!select)
    return;

  if (!gtk_tree_selection_get_selected (select, &model, &iter))
    return;

  /* This is the item to be moved up */
  gtk_tree_model_get (model, &iter, 0, &list, -1);

  /* First item can't be moved up */
  if (g_list_position (pd->alarm_list, list) <= 0)
    return;

  /* swap places */
  list_prev = list->prev;
  if (list_prev->prev)
    list_prev->prev->next = list;
  if (list->next)
    list->next->prev = list_prev;
  list_prev->next = list->next;
  list->prev = list_prev->prev;
  list->next = list_prev;
  list_prev->prev = list;

  pd->alarm_list = g_list_first (list);

  fill_liststore (pd, list);
}



/* Moves an alarm one row down in the list */
static void
down_clicked (GtkButton *button, gpointer data)
{
  plugin_data *pd = (plugin_data *) data;
  GtkTreeIter iter;
  GtkTreeSelection *select;
  GtkTreeModel *model;
  GList *list, *list_next;

  /* Get the selected row */
  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (pd->tree));

  if (!select)
    return;

  if (!gtk_tree_selection_get_selected (select, &model, &iter))
    return;

  /* This is the item to be moved down */
  gtk_tree_model_get (model, &iter, 0, &list, -1);

  /* Last item can't go down) */
  if (list == g_list_last (pd->alarm_list))
    return;

  /* swap places */
  list_next = list->next;
  if (list_next->next)
    list_next->next->prev = list;
  if (list->prev)
    list->prev->next = list_next;
  list_next->prev = list->prev;
  list->next = list_next->next;
  list_next->next = list;
  list->prev = list_next;

  pd->alarm_list = g_list_first (list_next);

  fill_liststore (pd, list);
}



static void
update_pbar_orientation (XfcePanelPlugin *plugin, plugin_data *pd)
{
  if (xfce_panel_plugin_get_orientation (plugin) == GTK_ORIENTATION_HORIZONTAL)
    {
      /* vertical bar */
      gtk_orientable_set_orientation (GTK_ORIENTABLE (pd->box),
                                      GTK_ORIENTATION_HORIZONTAL);
      gtk_orientable_set_orientation (GTK_ORIENTABLE (pd->pbar),
                                      GTK_ORIENTATION_VERTICAL);

      gtk_progress_bar_set_inverted (GTK_PROGRESS_BAR (pd->pbar), TRUE);
      gtk_widget_set_halign (GTK_WIDGET (pd->pbar), GTK_ALIGN_CENTER);
      gtk_widget_set_hexpand (GTK_WIDGET (pd->pbar), TRUE);

      gtk_widget_set_size_request (GTK_WIDGET (plugin), -1,
                                   xfce_panel_plugin_get_size (plugin));
    }
  else
    {
      /* horizontal bar */
      gtk_orientable_set_orientation (GTK_ORIENTABLE (pd->box),
                                      GTK_ORIENTATION_VERTICAL);
      gtk_orientable_set_orientation (GTK_ORIENTABLE (pd->pbar),
                                      GTK_ORIENTATION_HORIZONTAL);

      gtk_progress_bar_set_inverted (GTK_PROGRESS_BAR (pd->pbar), FALSE);
      gtk_widget_set_valign (GTK_WIDGET (pd->pbar), GTK_ALIGN_CENTER);
      gtk_widget_set_hexpand (GTK_WIDGET (pd->pbar), FALSE);

      gtk_widget_set_size_request (GTK_WIDGET (plugin),
                                   xfce_panel_plugin_get_size (plugin), -1);
    }
}



/* Callback for orientation change of panel */
static void
orient_change (XfcePanelPlugin *plugin, GtkOrientation orient, plugin_data *pd)
{
  update_pbar_orientation (plugin, pd);
}



/* Callback for size change of panel */
static gboolean
size_changed (XfcePanelPlugin *plugin, gint size, plugin_data *pd)
{
  if (xfce_panel_plugin_get_orientation (plugin) == GTK_ORIENTATION_HORIZONTAL)
    gtk_widget_set_size_request (GTK_WIDGET (plugin), -1, size);
  else
    gtk_widget_set_size_request (GTK_WIDGET (plugin), size, -1);

  return TRUE;
}


/**
 * Loads the settings and alarm list from a keyfile, saves the
 * alarm list in the linked list pd->alarm_list 
 **/
static void
load_settings (plugin_data *pd)
{
  gchar groupname[8];
  const gchar *timerstring;
  gint groupnum, time;
  gboolean is_cd, is_utc, autostart;
  alarm_t *alrm;
  GList *alarm_iter;
  XfceRc *rc;
  gchar* rc_path;

  if ((rc_path = xfce_panel_plugin_lookup_rc_file (pd->base)))
    {
      rc = xfce_rc_simple_open (rc_path, TRUE);

      if (rc != NULL)
        {
          groupnum = 0;
          g_sprintf (groupname, "G0");

          while (xfce_rc_has_group (rc, groupname))
            {
              xfce_rc_set_group (rc, groupname);

              alrm = g_new0 (alarm_t, 1);
              pd->alarm_list = g_list_append (pd->alarm_list, alrm);

              timerstring = (gchar *) xfce_rc_read_entry (rc, "timername",
                                                          "No name");
              alrm->name = g_strdup (timerstring);

              timerstring = (gchar *) xfce_rc_read_entry (rc, "timercommand",
                                                          "");
              alrm->command = g_strdup (timerstring);

              timerstring = (gchar *) xfce_rc_read_entry (rc, "timerinfo", "");
              alrm->info = g_strdup (timerstring);

              is_cd = xfce_rc_read_bool_entry (rc, "is_countdown", TRUE);
              alrm->is_countdown = is_cd;

              is_utc = xfce_rc_read_bool_entry (rc, "is_utc", FALSE);
              alrm->is_utc = is_utc;

              alrm->next_alarm_groupnum = xfce_rc_read_int_entry (rc, "timernext", -1);
              /* If 'timernext' not specified, fallback to checking previously
               * used boolean setting 'is_recur' telling if alarm is self-recurring
               * NOTE: remove in version ?.?? */
              if (alrm->next_alarm_groupnum == -1)
                alrm->next_alarm_groupnum =
                  xfce_rc_read_bool_entry(rc, "is_recur", FALSE) ? groupnum : -1;

              autostart=xfce_rc_read_bool_entry(rc,"autostart",FALSE);
              alrm->is_auto_start = autostart;

              time = xfce_rc_read_int_entry (rc, "time", 0);
              alrm->time = time;

              /* Include a link to the whole data */
              alrm->pd = (gpointer) pd;

              groupnum++;
              g_snprintf (groupname, 5, "G%d", groupnum);

            } /* end of while loop */

          pd->count = groupnum;

          /* Once all rc groups (i.e. alarm configs) have been read, setup
           * references to 'next alarm's based on 'groupnum' read from rc */
          alarm_iter = pd->alarm_list;
          while (alarm_iter)
          {
            alrm = alarm_iter->data;
            alrm->next_alarm = g_list_nth_data(pd->alarm_list, alrm->next_alarm_groupnum);
            alarm_iter = alarm_iter->next;
          }

          /* Read other options */
          if (xfce_rc_has_group (rc, "others"))
            {
              xfce_rc_set_group (rc, "others");
              pd->nowin_if_alarm = xfce_rc_read_bool_entry (rc,
                                                            "nowin_if_alarm",
                                                            FALSE);
              pd->use_global_command = xfce_rc_read_bool_entry (
                  rc, "use_global_command", FALSE);

              if (pd->global_command)
                g_free (pd->global_command);
              pd->global_command = g_strdup (
                  (gchar *) xfce_rc_read_entry (rc, "global_command", ""));

              pd->repeat_alarm_command = xfce_rc_read_bool_entry (rc, "repeat_alarm",
                                                          FALSE);
              pd->repetitions = xfce_rc_read_int_entry (rc, "repetitions", 1);
              pd->repeat_interval = xfce_rc_read_int_entry (rc,
                                                            "repeat_interval",
                                                            10);
            }

          update_pbar_orientation (pd->base, pd);

          xfce_rc_close (rc);
        }
    }

  g_free (rc_path);
}



/* Saves the list to a keyfile, backup a permanent copy */
static void
save_settings (XfcePanelPlugin *plugin, plugin_data *pd)
{
  gchar groupname[8];
  gint row_count;
  GList *list;
  alarm_t *alrm;
  FILE *conffile;
  XfceRc *rc;
  gchar *file;

  if (!(file = xfce_panel_plugin_save_location (plugin, TRUE)))
    return;

  /**
   * We do this to start a fresh config file, otherwise if the old config file
   * is longer,   the tail will not get truncated
   * See http://bugzilla.xfce.org/show_bug.cgi?id=2647
   * for a related bug report
   **/
  conffile = fopen (file, "w");
  if (conffile)
    fclose (conffile);

  rc = xfce_rc_simple_open (file, FALSE);

  if (!rc)
    return;

  /* Assign groupnum to each alarm in the order they will be written to rc.
   * These groupnums will be saved whenever 'next alarm' is set
   * ('next alarm' is a pointer and saving it directly is meaningless). */
  row_count = 0;
  list = pd->alarm_list;
  while (list)
  {
    alrm = list->data;
    alrm->next_alarm_groupnum = row_count++;
    list = list->next;
  }

  list = pd->alarm_list;

  row_count = 0;

  while (list)
    {
      g_snprintf (groupname, 7, "G%d", row_count);

      xfce_rc_set_group (rc, groupname);

      alrm = (alarm_t *) list->data;

      xfce_rc_write_entry (rc, "timername", alrm->name);

      xfce_rc_write_int_entry (rc, "time", alrm->time);

      xfce_rc_write_entry (rc, "timercommand", alrm->command);

      xfce_rc_write_entry (rc, "timerinfo", alrm->info);

      xfce_rc_write_bool_entry (rc, "is_countdown", alrm->is_countdown);

      xfce_rc_write_bool_entry (rc, "is_utc", alrm->is_utc);

      if (alrm->next_alarm)
        xfce_rc_write_int_entry (rc, "timernext", alrm->next_alarm->next_alarm_groupnum);

      xfce_rc_write_bool_entry(rc,"autostart",alrm->is_auto_start);


      row_count++;
      list = list->next;
    }

  /* save the other options */
  xfce_rc_set_group (rc, "others");
  xfce_rc_write_bool_entry (rc, "nowin_if_alarm", pd->nowin_if_alarm);
  xfce_rc_write_bool_entry (rc, "use_global_command", pd->use_global_command);
  xfce_rc_write_entry (rc, "global_command", pd->global_command);
  xfce_rc_write_bool_entry (rc, "repeat_alarm", pd->repeat_alarm_command);
  xfce_rc_write_int_entry (rc, "repetitions", pd->repetitions);
  xfce_rc_write_int_entry (rc, "repeat_interval", pd->repeat_interval);
  xfce_rc_close (rc);

  g_free (file);
}



/* Activates the Edit and Remove buttons when an item in the list is selected */
static void
tree_selected (GtkTreeSelection *select, gpointer data)
{
  plugin_data *pd = (plugin_data *) data;
  gtk_widget_set_sensitive (pd->buttonedit, TRUE);
  gtk_widget_set_sensitive (pd->buttonremove, TRUE);
  gtk_widget_set_sensitive (pd->buttonup, TRUE);
  gtk_widget_set_sensitive (pd->buttondown, TRUE);

}



static void
plugin_free (XfcePanelPlugin *plugin, plugin_data *pd)
{
  GList *list = NULL;
  alarm_t *alrm;
  list = pd->alarm_list;

  while (list){
    alrm = (alarm_t *) list->data;
    /* remove timeouts */
    if (alrm->timeout!=0) g_source_remove(alrm->timeout);
    if (alrm->repeat_timeout!=0) g_source_remove(alrm->repeat_timeout);

    if(alrm->timer)
      g_timer_destroy(alrm->timer);

    list = g_list_next (list);
  }

  if (pd->global_command)
    g_free (pd->global_command);

  if (pd->liststore)
    {
      gtk_list_store_clear (pd->liststore);
    }

  if (pd->alarm_list)
    g_list_free (pd->alarm_list);

  /* destroy all widgets */
  gtk_widget_destroy (GTK_WIDGET (pd->box));

  /* free the plugin data structure */
  g_free (pd);
}



/* options dialog response */
static void
options_dialog_response (GtkWidget *dlg, int reponse, plugin_data *pd)
{
  if (pd->global_command)
    g_free (pd->global_command);
  pd->global_command = g_strdup (
      gtk_entry_get_text ((GtkEntry *) pd->glob_command_entry));
  gtk_widget_destroy (dlg);
  save_settings (pd->base, pd);
}



/* Alarm dialog response */
static void
dialog_response (GtkWidget *dlg, int response, alarm_t* alrm)
{
  plugin_data *pd;
  pd = (plugin_data *) alrm->pd;

  if (response != 1)
    {
      gtk_widget_destroy (dlg);
      return;
    }

  start_timer (pd, alrm);
  gtk_widget_destroy (dlg);
}



/* nowin_if_alarm toggle callback */
static void
toggle_nowin_if_alarm (GtkToggleButton *button, gpointer data)
{
  plugin_data *pd = (plugin_data *) data;

  pd->nowin_if_alarm = gtk_toggle_button_get_active (button);

}


/* toggle_global_command toggle callback */
static void
toggle_global_command (GtkToggleButton *button, gpointer data)
{
  plugin_data *pd = (plugin_data *) data;

  pd->use_global_command = gtk_toggle_button_get_active (button);
  gtk_widget_set_sensitive (pd->global_command_box, pd->use_global_command);

}



/* Toggle_repeat_alarm toggle callback */
static void
toggle_repeat_alarm (GtkToggleButton *button, gpointer data)
{
  plugin_data *pd = (plugin_data *) data;

  pd->repeat_alarm_command = gtk_toggle_button_get_active (button);
  gtk_widget_set_sensitive (pd->repeat_alarm_box, pd->repeat_alarm_command);
}



/* Spinbutton 1 (#of alarm repetitions value change callback */
static void
spin1_changed (GtkSpinButton *button, gpointer data)
{
  plugin_data *pd = (plugin_data *) data;

  pd->repetitions = gtk_spin_button_get_value_as_int (button);
}



/* Spinbutton 1 (alarm repetition interval) value change callback */
static void
spin2_changed (GtkSpinButton *button, gpointer data)
{
  plugin_data *pd = (plugin_data *) data;

  pd->repeat_interval = gtk_spin_button_get_value_as_int (button);
}

/* call: xfce4-panel --plugin-event=xfce4-timer-plugin:trigger:string:<alarm-name> */
static gboolean remote_trigger (XfcePanelPlugin *plugin,
                                    const gchar *name,
                                    const GValue *value,
                                    plugin_data *pd)
{
  g_return_val_if_fail (value != NULL, FALSE);
  if (strcmp (name, "trigger") == 0) {
    if (value != NULL && G_VALUE_HOLDS_STRING (value)) {
      const GList * list = NULL;
      const gchar *alarm_to_trigger = g_value_get_string (value);
      list = pd->alarm_list;
      while (list) {
        alarm_t *alarm = list->data;
        if (strcmp (alarm->name, alarm_to_trigger) == 0) {
          start_timer(pd, alarm);
          return TRUE;
        }
        list = g_list_next (list);
      }
    }
  }
  return TRUE;
}/* remote_trigger() */

/* Options dialog */
static void
plugin_create_options (XfcePanelPlugin *plugin, plugin_data *pd)
{
  GtkWidget *vbox; /*outermost box */
  GtkWidget *hbox; /* holds the treeview and buttons */
  GtkWidget *buttonbox, *button, *sw, *tree, *spinbutton;
  GtkWidget *dialog_vbox;
  GtkTreeSelection *select;
  GtkTreeViewColumn *column;
  GtkWidget *dlg = NULL;
  GtkCellRenderer *renderer;

  if (pd->settings_dialog != NULL) {
    gtk_window_present (GTK_WINDOW (pd->settings_dialog));
    return;
  }

  pd->settings_dialog = dlg = xfce_titled_dialog_new_with_mixed_buttons (
      _("Timer Options"),
      GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (plugin))),
      GTK_DIALOG_DESTROY_WITH_PARENT, "", _("Close"), GTK_RESPONSE_OK, NULL);
  g_object_add_weak_pointer (G_OBJECT (pd->settings_dialog), (gpointer *) &pd->settings_dialog);

  gtk_window_set_icon_name (GTK_WINDOW (dlg), "xfce4-timer-plugin");

  dialog_vbox = gtk_dialog_get_content_area (GTK_DIALOG (dlg));

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_pack_start (GTK_BOX (dialog_vbox), vbox, TRUE, TRUE, 0);

  g_signal_connect (dlg, "response", G_CALLBACK (options_dialog_response), pd);

  gtk_container_set_border_width (GTK_CONTAINER (vbox), 10);

  gtk_widget_set_size_request (dlg, 650, -1);
  gtk_window_set_position (GTK_WINDOW (dlg), GTK_WIN_POS_CENTER);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);

  sw = gtk_scrolled_window_new (NULL, NULL);

  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
                                       GTK_SHADOW_ETCHED_IN);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

  gtk_box_pack_start (GTK_BOX (hbox), sw, TRUE, TRUE, 0);

  fill_liststore (pd, NULL);

  tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL (pd->liststore));
  pd->tree = tree;
  gtk_tree_selection_set_mode (
      gtk_tree_view_get_selection (GTK_TREE_VIEW (tree)), GTK_SELECTION_SINGLE);

  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes (_("Timer name"), renderer,
                                                     "text", 1, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);

  column = gtk_tree_view_column_new_with_attributes (
      _("Countdown period /\nAlarm time"), renderer, "text", 2, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);

  column = gtk_tree_view_column_new_with_attributes (_("Alarm command"),
                                                     renderer, "text", 3, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree), column);

  if (tree)
    gtk_container_add (GTK_CONTAINER (sw), tree);

  gtk_widget_set_size_request (GTK_WIDGET (sw), 350, 200);

  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (pd->tree));
  gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
  g_signal_connect (G_OBJECT (select), "changed", G_CALLBACK (tree_selected),
                    pd);

  buttonbox = gtk_button_box_new (GTK_ORIENTATION_VERTICAL);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (buttonbox), GTK_BUTTONBOX_START);
  gtk_box_set_spacing (GTK_BOX (buttonbox), 6);
  gtk_box_pack_start (GTK_BOX (hbox), buttonbox, FALSE, FALSE, 0);

  button = gtk_button_new_with_label (_("Add"));
  pd->buttonadd = button;
  gtk_box_pack_start (GTK_BOX (buttonbox), button, FALSE, FALSE, 0);
  gtk_widget_set_sensitive (button, TRUE);
  g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (add_edit_clicked),
                    pd);

  button = gtk_button_new_with_label (_("Edit"));
  pd->buttonedit = button;
  gtk_box_pack_start (GTK_BOX (buttonbox), button, FALSE, FALSE, 0);
  gtk_widget_set_sensitive (button, FALSE);
  g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (add_edit_clicked),
                    pd);

  button = gtk_button_new_with_label (_("Remove"));
  pd->buttonremove = button;
  gtk_box_pack_start (GTK_BOX (buttonbox), button, FALSE, FALSE,
  WIDGET_SPACING);
  gtk_widget_set_sensitive (button, FALSE);
  g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (remove_clicked),
                    pd);

  button = gtk_button_new_with_label (_("Up"));
  pd->buttonup = button;
  gtk_box_pack_start (GTK_BOX (buttonbox), button, FALSE, FALSE,
  WIDGET_SPACING);
  gtk_widget_set_sensitive (button, FALSE);
  g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (up_clicked), pd);

  button = gtk_button_new_with_label (_("Down"));
  pd->buttondown = button;
  gtk_box_pack_start (GTK_BOX (buttonbox), button, FALSE, FALSE,
  WIDGET_SPACING);
  gtk_widget_set_sensitive (button, FALSE);
  g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (down_clicked),
                    pd);

  gtk_widget_set_size_request (hbox, -1, -1);

  gtk_box_pack_start (GTK_BOX (vbox),
                      gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), FALSE,
                      FALSE,
                      BORDER);

  button = gtk_check_button_new_with_label (
      _("Don't display a warning if an alarm command is set"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), pd->nowin_if_alarm);
  g_signal_connect (G_OBJECT (button), "toggled",
                    G_CALLBACK (toggle_nowin_if_alarm), pd);
  gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, WIDGET_SPACING);

  gtk_box_pack_start (GTK_BOX (vbox),
                      gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), FALSE,
                      FALSE,
                      BORDER);

  /* Default alarm command config */
  button = gtk_check_button_new_with_label (_("Use a default alarm command"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
                                pd->use_global_command);
  g_signal_connect (G_OBJECT (button), "toggled",
                    G_CALLBACK (toggle_global_command), pd);
  gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, WIDGET_SPACING);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  pd->global_command_box = hbox;
  gtk_box_pack_start (GTK_BOX (hbox), gtk_label_new (_("Default command: ")),
                      FALSE, FALSE, 0);
  gtk_widget_set_margin_start (GTK_WIDGET (hbox), 12);
  pd->glob_command_entry = (GtkWidget *) gtk_entry_new ();
  gtk_widget_set_size_request (pd->glob_command_entry, 400, -1);
  gtk_entry_set_text (GTK_ENTRY (pd->glob_command_entry), pd->global_command);
  gtk_box_pack_start (GTK_BOX (hbox), pd->glob_command_entry, FALSE, FALSE, 10);

  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, WIDGET_SPACING);
  gtk_widget_set_sensitive (hbox, pd->use_global_command);

  gtk_box_pack_start (GTK_BOX (vbox),
                      gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), FALSE,
                      FALSE,
                      BORDER);

  /* Alarm repetitions config */
  button = gtk_check_button_new_with_label (_("Repeat the alarm command"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), pd->repeat_alarm_command);
  g_signal_connect (G_OBJECT (button), "toggled",
                    G_CALLBACK (toggle_repeat_alarm), pd);
  gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, WIDGET_SPACING);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_margin_start (GTK_WIDGET (hbox), 12);

  pd->repeat_alarm_box = hbox;
  gtk_box_pack_start (GTK_BOX (hbox),
                      gtk_label_new (_("Number of repetitions")), FALSE, FALSE,
                      0);
  spinbutton = gtk_spin_button_new_with_range (1, 50, 1);
  pd->spin_repeat = spinbutton;
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (spinbutton), pd->repetitions);
  g_signal_connect (G_OBJECT (spinbutton), "value-changed",
                    G_CALLBACK (spin1_changed), pd);
  gtk_box_pack_start (GTK_BOX (hbox), spinbutton, FALSE, FALSE, 10);
  gtk_box_pack_start (GTK_BOX (hbox),
                      gtk_label_new (_("  Time interval (sec.)")), FALSE,
                      FALSE, 0);
  spinbutton = gtk_spin_button_new_with_range (1, 600, 1);
  pd->spin_interval = spinbutton;
  gtk_box_pack_start (GTK_BOX (hbox), spinbutton, FALSE, FALSE, 10);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (spinbutton), pd->repeat_interval);
  g_signal_connect (G_OBJECT (spinbutton), "value-changed",
                    G_CALLBACK (spin2_changed), pd);

  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, WIDGET_SPACING);
  gtk_widget_set_sensitive (hbox, pd->repeat_alarm_command);

  gtk_widget_show_all (GTK_WIDGET (dlg));
}



/* Show the "About" window */
static void
show_about_window (XfcePanelPlugin *plugin, plugin_data *pd)
{
  const gchar *author[] =
    { "Kemal Ilgar Eroğlu <ilgar_eroglu@yahoo.com>", NULL };
  const gchar *translators =
      "Mohammad Alhargan <malham1@hotmail.com> \n\
Marcos Antonio Alvarez Costales <marcoscostales@gmail.com> \n\
Harald Servat <redcrash@gmail.com> \n\
Michal Várady <miko.vaji@gmail.com>\n\
Per Kongstad <p_kongstad@op.pl>\n\
Simon Schneider <simon@schneiderimtal.de>\n\
Efstathios Iosifidis <iosifidis@opensuse.org>\n\
Jeff Bailes <thepizzaking@gmail.com>\n\
Sergio <oigres200@gmail.com>\n\
Piarres Beobide <pi@beobide.net>\n\
Maximilian Schleiss <maximilian@xfce.org>\n\
Leandro Regueiro <leandro.regueiro@gmail.com>\n\
Ivica Kolić <ikoli@yahoo.com>\n\
Gabor Kelemen <kelemeng at gnome dot hu>\n\
Andhika Padmawan <andhika.padmawan@gmail.com>\n\
Cristian Marchi <cri.penta@gmail.com>\n\
Nobuhiro Iwamatsu <iwamatsu@nigauri.org>\n\
Seong-ho Cho <darkcircle.0426@gmail.com>\n\
Rihards Priedītis <rprieditis@inbox.lv>\n\
Pjotr <pjotrvertaalt@gmail.com>\n\
Piotr Sokół <piotr.sokol@10g.pl>\n\
Sérgio Marques <smarquespt@gmail.com>\n\
Rafael Ferreira <rafael.f.f1@gmail.com>\n\
Dima Smirnov <arch@cnc-parts.info>\n\
Tomáš Vadina <kyberdev@gmail.com>\n\
Besnik Bleta <besnik@programeshqip.org>\n\
Саша Петровић <salepetronije@gmail.com>\n\
Daniel Nylander <po@danielnylander.se>\n\
Kemal Ilgar Eroğlu <ilgar_eroglu@yahoo.com>\n\
Gheyret T.Kenji <gheyret@gmail.com>\n\
Dmitry Nikitin <luckas_fb@mail.ru>\n\
Muhammad Ali Makki <makki.ma@gmail.com>\n\
Hunt Xu <huntxu@live.cn>\n\
Cheng-Chia Tseng <pswo10680@gmail.com>\n";

  gtk_show_about_dialog (
      NULL, "title", _("About Timer plugin"), "logo-icon-name", "xfce4-timer-plugin", "license",
      xfce_get_license_text (XFCE_LICENSE_TEXT_GPL), "version", VERSION_FULL,
      "program-name", PACKAGE_NAME, "comments",
      _("A plugin to define countdown timers or alarms at given times."),
      "website",
      "https://docs.xfce.org/panel-plugins/xfce4-timer-plugin",
      "copyright", "Copyright \302\251 2005-" COPYRIGHT_YEAR " The Xfce development team", "authors", author,
      "translator-credits", translators, NULL);
}



/**
 * create_sample_control
 * Create a new instance of the plugin.
 * @control : #Control parent container
 * Returns %TRUE on success, %FALSE on failure.
 **/
static void
create_plugin_control (XfcePanelPlugin *plugin)
{
  plugin_data *pd = g_new0 (plugin_data, 1);
  GList *list = NULL;
  alarm_t *alrm;

  xfce_textdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR, "UTF-8");

  pd->base = plugin;
  pd->count = 0;
  pd->pbar = gtk_progress_bar_new ();
  pd->liststore = gtk_list_store_new (4, G_TYPE_POINTER, /* Column 0: GList alarm list node */
                                      G_TYPE_STRING, /* Column 1: Name */
                                      G_TYPE_STRING, /* Column 2: Timer period/alarm time - info string */
                                      G_TYPE_STRING); /* Command to run */
  pd->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  pd->buttonadd = NULL;
  pd->buttonedit = NULL;
  pd->buttonremove = NULL;
  pd->menu = NULL;
  pd->nowin_if_alarm = FALSE;
  pd->repeat_alarm_command = FALSE;
  pd->use_global_command = FALSE;
  pd->glob_command_entry = NULL;
  pd->global_command = g_strdup (""); /* For Gtk >= 3.4 one could just set = NULL */
  pd->global_command_box = NULL;
  pd->repeat_alarm_box = NULL;
  pd->repetitions = 1;
  pd->repeat_interval = 10;
  pd->alarm_list = NULL;
  pd->selected = NULL;
  pd->num_active_timers=0;

  gtk_widget_set_tooltip_text (GTK_WIDGET (plugin), "");

  g_object_ref (pd->liststore);

  load_settings (pd);
  pd->selected = pd->alarm_list;
  //Check if an alarm is auto start to start it at creation
  list = pd->alarm_list;
  while (list){
      alrm = (alarm_t *) list->data;
      if(alrm->is_auto_start){
          start_timer(pd,alrm);
      }
      list = g_list_next (list);
  }

  gtk_container_set_border_width (GTK_CONTAINER (pd->box), BORDER / 2);
  gtk_container_add (GTK_CONTAINER (plugin), pd->box);

  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (pd->pbar), 0);
  gtk_box_pack_start (GTK_BOX (pd->box), pd->pbar, FALSE, FALSE, 0);

  update_pbar_orientation (pd->base, pd);

  g_signal_connect (G_OBJECT (plugin), "button_press_event",
                    G_CALLBACK (pbar_clicked), pd);

  gtk_widget_show_all (GTK_WIDGET (plugin));

  g_signal_connect (plugin, "free-data", G_CALLBACK (plugin_free), pd);

  g_signal_connect (plugin, "save", G_CALLBACK (save_settings), pd);

  g_signal_connect (plugin, "orientation-changed", G_CALLBACK (orient_change),
                    pd);

  g_signal_connect (plugin, "size-changed", G_CALLBACK (size_changed), pd);

  xfce_panel_plugin_menu_show_configure (plugin);
  g_signal_connect (plugin, "configure-plugin",
                    G_CALLBACK (plugin_create_options), pd);

  g_signal_connect (plugin, "remote-event", G_CALLBACK (remote_trigger), pd);

  xfce_panel_plugin_menu_show_about (plugin);
  g_signal_connect (plugin, "about", G_CALLBACK (show_about_window), pd);
}
