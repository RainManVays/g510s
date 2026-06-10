/*
 *  This file is part of g510s.
 *
 *  g510s is  free  software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as published by
 *  the  Free Software Foundation; either version 3 of the License, or (at your
 *  option)  any later version.
 *
 *  g510s is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with g510s; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1335  USA
 *
 *  Copyright © 2015 John Augustine
 */

#define LOG_MODULE "main"

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>
#include <pthread.h>
#include <libg15.h>
#include <libg15render.h>
#include <gtk/gtk.h>
#include <libappindicator/app-indicator.h>

#include "g510s.h"
#include "g510s-display-registry.h"
#include "g510s-startup.h"

extern void displays_gui_init(GtkBuilder *builder);


GtkCheckMenuItem *menuhidden;
AppIndicator *indicator;

static gboolean check_leaving_cb(gpointer data) {
  (void)data;
  if (leaving) gtk_main_quit();
  return G_SOURCE_REMOVE;
}


/* Called asynchronously by GLib when org.kde.StatusNotifierWatcher appears
 * on the session bus.  At that point dbusmenu can sync without blocking, so
 * all AppIndicator/menu-item D-Bus calls here are near-instant.           */
static void on_sni_watcher_appeared(GDBusConnection *conn, const gchar *name,
                                    const gchar *owner, gpointer data) {
  (void)conn; (void)name; (void)owner; (void)data;
  LINFO("SNI watcher appeared — activating indicator");
  gtk_check_menu_item_set_active(menuhidden, g510s_data.gui_hidden ? TRUE : FALSE);
  if (device_found)
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
  else
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ATTENTION);
  LINFO("indicator activated");
}

static void sigchld_handler(int sig) {
  (void)sig;
  while (waitpid(-1, NULL, WNOHANG) > 0);
}

static void sigterm_handler(int sig) {
  (void)sig;
  leaving = 1;
  if (gtk_main_level() > 0)
    gtk_main_quit();
  g510s_log_close();
}

int main(int argc, char *argv[]) {
  int i = 1;
  int help = 0;
  int opt_invalid = 0;
  int lflag = 0;
  g510s_log_level_t log_level = G510S_LOG_INFO;
  
  pthread_t key_thread;
  pthread_t update_thread;
  pthread_t server_thread;
  
  GtkBuilder *builder;
  GtkWidget *window;
  GtkAboutDialog *aboutdialog;
  GtkWidget *indicator_menu;
  
  GtkRange *redscale_m1;
  GtkRange *greenscale_m1;
  GtkRange *bluescale_m1;
  
  GtkRange *redscale_m2;
  GtkRange *greenscale_m2;
  GtkRange *bluescale_m2;
  
  GtkRange *redscale_m3;
  GtkRange *greenscale_m3;
  GtkRange *bluescale_m3;
  
  GtkRange *redscale_mr;
  GtkRange *greenscale_mr;
  GtkRange *bluescale_mr;
  
  GtkEntry *entry_m1g1;
  GtkEntry *entry_m1g2;
  GtkEntry *entry_m1g3;
  GtkEntry *entry_m1g4;
  GtkEntry *entry_m1g5;
  GtkEntry *entry_m1g6;
  GtkEntry *entry_m1g7;
  GtkEntry *entry_m1g8;
  GtkEntry *entry_m1g9;
  GtkEntry *entry_m1g10;
  GtkEntry *entry_m1g11;
  GtkEntry *entry_m1g12;
  GtkEntry *entry_m1g13;
  GtkEntry *entry_m1g14;
  GtkEntry *entry_m1g15;
  GtkEntry *entry_m1g16;
  GtkEntry *entry_m1g17;
  GtkEntry *entry_m1g18;
  
  GtkEntry *entry_m2g1;
  GtkEntry *entry_m2g2;
  GtkEntry *entry_m2g3;
  GtkEntry *entry_m2g4;
  GtkEntry *entry_m2g5;
  GtkEntry *entry_m2g6;
  GtkEntry *entry_m2g7;
  GtkEntry *entry_m2g8;
  GtkEntry *entry_m2g9;
  GtkEntry *entry_m2g10;
  GtkEntry *entry_m2g11;
  GtkEntry *entry_m2g12;
  GtkEntry *entry_m2g13;
  GtkEntry *entry_m2g14;
  GtkEntry *entry_m2g15;
  GtkEntry *entry_m2g16;
  GtkEntry *entry_m2g17;
  GtkEntry *entry_m2g18;
  
  GtkEntry *entry_m3g1;
  GtkEntry *entry_m3g2;
  GtkEntry *entry_m3g3;
  GtkEntry *entry_m3g4;
  GtkEntry *entry_m3g5;
  GtkEntry *entry_m3g6;
  GtkEntry *entry_m3g7;
  GtkEntry *entry_m3g8;
  GtkEntry *entry_m3g9;
  GtkEntry *entry_m3g10;
  GtkEntry *entry_m3g11;
  GtkEntry *entry_m3g12;
  GtkEntry *entry_m3g13;
  GtkEntry *entry_m3g14;
  GtkEntry *entry_m3g15;
  GtkEntry *entry_m3g16;
  GtkEntry *entry_m3g17;
  GtkEntry *entry_m3g18;
  
  GtkEntry *entry_mrg1;
  GtkEntry *entry_mrg2;
  GtkEntry *entry_mrg3;
  GtkEntry *entry_mrg4;
  GtkEntry *entry_mrg5;
  GtkEntry *entry_mrg6;
  GtkEntry *entry_mrg7;
  GtkEntry *entry_mrg8;
  GtkEntry *entry_mrg9;
  GtkEntry *entry_mrg10;
  GtkEntry *entry_mrg11;
  GtkEntry *entry_mrg12;
  GtkEntry *entry_mrg13;
  GtkEntry *entry_mrg14;
  GtkEntry *entry_mrg15;
  GtkEntry *entry_mrg16;
  GtkEntry *entry_mrg17;
  GtkEntry *entry_mrg18;
  
  lcdlist_t *lcdlist;
  
  leaving = 0;
  update = 0;
  device_found = 0;
  connected_clients = 0;
  current_key_state = 0;

  // parse command line options
  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i],"--help") || !strcmp(argv[i],"-h")) {
      help = 1;
      break;
    } else if (!strcmp(argv[i],"--log-level") || !strcmp(argv[i],"-l")) {
      if (i + 1 < argc) {
        i++;
        log_level = g510s_log_level_from_str(argv[i]);
        lflag++;
      } else {
        opt_invalid = 1;
        break;
      }
    } else {
      opt_invalid = 1;
      break;
    }
    if (lflag > 1) {
      opt_invalid = 1;
      break;
    }
  }

  if (opt_invalid) {
    printf("G510s: invalid option specified!\n\n");
    help = 1;
  }

  // print help and exit
  if (help) {
    printf("Usage: g510s [options]\n\n");
    printf("Options:\n");
    printf("  --help|-h              Show this help\n");
    printf("  --log-level|-l LEVEL   Log level: fatal, error, warn, info (default), debug, trace\n");
    printf("\nUninstall:\n");
    printf("  sudo make uninstall    Remove binary, resources, udev rules, autostart\n");
    printf("\nDisable autostart:\n");
    printf("  sudo rm /etc/xdg/autostart/g510s.desktop\n");
    return 0;
  }

  /* Init logging (stdout only until check_dir creates ~/.g510s) */
  g510s_log_init(log_level, 0);
  LINFO("starting g510s %s", G510S_VERSION);

  /* Enable libg15 debug output only at TRACE — it prints every USB transfer */
  if (log_level >= G510S_LOG_TRACE) {
    libg15Debug(1);
    LINFO("libg15 debug enabled");
  }
  
  // init libg15 early (before gtk_init), so hotplug detection in key_thread
  // can simply call re_initLibG15() without racing against the initial open.
  LINFO("libg15 init...");
  {
    int tries = 30;  // up to 3 seconds
    while (tries-- > 0 && !device_found) {
      if (setupLibG15(0x46d, 0xc22d, 0) == G15_NO_ERROR) {
        LINFO("found device 046d:c22d");
        device_found = 1;
      } else {
        exitLibG15();
        if (setupLibG15(0x46d, 0xc22e, 0) == G15_NO_ERROR) {
          LINFO("found device 046d:c22e");
          device_found = 1;
        } else {
          exitLibG15();
          if (tries > 0) usleep(100000);
        }
      }
    }
    if (!device_found)
      LWARN("libg15 init failed — no device found, continuing without keyboard");
  }
  LINFO("libg15 init done");

  if (device_found)
    startup_animation();

  // init uinput only if a device is found
  if (device_found) {
    // media keys wont work without uinput
    if (init_uinput() != 0) {
      LWARN("failed to initialize uinput, media keys not available");
    }
  }

  // init data structure
  init_data();

  // init lcd list
  lcdlist = lcdlist_init();

  // try to create user save directory
  check_dir();

  /* Now that ~/.g510s exists, open the log file */
  g510s_log_init_file();

  // kill any previous instance and claim the pid file
  acquire_pidfile();

  // try to load previously saved data
  load_config();
  LINFO("config loaded");

  display_registry_init();

  // load on-demand screen definitions from ~/.g510s/screens.conf
  signal(SIGCHLD, sigchld_handler);
  signal(SIGTERM, sigterm_handler);
  signal(SIGINT,  sigterm_handler);
  signal(SIGHUP,  sigterm_handler);
  load_screens();

  LINFO("screen init...");
  cpu_screen_init();
  sysmon_screen_init();
  claude_screen_init();
  LINFO("screen init done");

  // start threads
  pthread_create(&server_thread, NULL, server_function, lcdlist);
  pthread_create(&update_thread, NULL, update_function, lcdlist);
  pthread_create(&key_thread, NULL, key_function, lcdlist);
  LINFO("threads started");

  // init gtk
  LINFO("gtk_init...");
  gtk_init(&argc, &argv);
  LINFO("gtk_init done");

  builder = gtk_builder_new();
  gtk_builder_add_from_file(builder, G510S_DATA_DIR "/g510s.glade", NULL);
  LINFO("glade loaded");
  
  window = GTK_WIDGET(gtk_builder_get_object(builder, "window"));
  aboutdialog = GTK_ABOUT_DIALOG(gtk_builder_get_object(builder, "aboutdialog"));
  
  // scales
  redscale_m1 = GTK_RANGE(gtk_builder_get_object(builder, "redscale_m1"));
  greenscale_m1 = GTK_RANGE(gtk_builder_get_object(builder, "greenscale_m1"));
  bluescale_m1 = GTK_RANGE(gtk_builder_get_object(builder, "bluescale_m1"));
  
  redscale_m2 = GTK_RANGE(gtk_builder_get_object(builder, "redscale_m2"));
  greenscale_m2 = GTK_RANGE(gtk_builder_get_object(builder, "greenscale_m2"));
  bluescale_m2 = GTK_RANGE(gtk_builder_get_object(builder, "bluescale_m2"));
  
  redscale_m3 = GTK_RANGE(gtk_builder_get_object(builder, "redscale_m3"));
  greenscale_m3 = GTK_RANGE(gtk_builder_get_object(builder, "greenscale_m3"));
  bluescale_m3 = GTK_RANGE(gtk_builder_get_object(builder, "bluescale_m3"));
  
  redscale_mr = GTK_RANGE(gtk_builder_get_object(builder, "redscale_mr"));
  greenscale_mr = GTK_RANGE(gtk_builder_get_object(builder, "greenscale_mr"));
  bluescale_mr = GTK_RANGE(gtk_builder_get_object(builder, "bluescale_mr"));
  
  // text entries
  entry_m1g1 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m1g1"));
  entry_m1g2 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m1g2"));
  entry_m1g3 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m1g3"));
  entry_m1g4 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m1g4"));
  entry_m1g5 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m1g5"));
  entry_m1g6 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m1g6"));
  entry_m1g7 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m1g7"));
  entry_m1g8 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m1g8"));
  entry_m1g9 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m1g9"));
  entry_m1g10 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m1g10"));
  entry_m1g11 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m1g11"));
  entry_m1g12 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m1g12"));
  entry_m1g13 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m1g13"));
  entry_m1g14 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m1g14"));
  entry_m1g15 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m1g15"));
  entry_m1g16 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m1g16"));
  entry_m1g17 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m1g17"));
  entry_m1g18 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m1g18"));
  
  entry_m2g1 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m2g1"));
  entry_m2g2 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m2g2"));
  entry_m2g3 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m2g3"));
  entry_m2g4 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m2g4"));
  entry_m2g5 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m2g5"));
  entry_m2g6 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m2g6"));
  entry_m2g7 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m2g7"));
  entry_m2g8 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m2g8"));
  entry_m2g9 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m2g9"));
  entry_m2g10 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m2g10"));
  entry_m2g11 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m2g11"));
  entry_m2g12 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m2g12"));
  entry_m2g13 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m2g13"));
  entry_m2g14 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m2g14"));
  entry_m2g15 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m2g15"));
  entry_m2g16 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m2g16"));
  entry_m2g17 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m2g17"));
  entry_m2g18 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m2g18"));
  
  entry_m3g1 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m3g1"));
  entry_m3g2 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m3g2"));
  entry_m3g3 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m3g3"));
  entry_m3g4 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m3g4"));
  entry_m3g5 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m3g5"));
  entry_m3g6 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m3g6"));
  entry_m3g7 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m3g7"));
  entry_m3g8 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m3g8"));
  entry_m3g9 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m3g9"));
  entry_m3g10 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m3g10"));
  entry_m3g11 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m3g11"));
  entry_m3g12 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m3g12"));
  entry_m3g13 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m3g13"));
  entry_m3g14 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m3g14"));
  entry_m3g15 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m3g15"));
  entry_m3g16 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m3g16"));
  entry_m3g17 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m3g17"));
  entry_m3g18 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_m3g18"));
  
  entry_mrg1 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_mrg1"));
  entry_mrg2 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_mrg2"));
  entry_mrg3 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_mrg3"));
  entry_mrg4 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_mrg4"));
  entry_mrg5 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_mrg5"));
  entry_mrg6 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_mrg6"));
  entry_mrg7 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_mrg7"));
  entry_mrg8 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_mrg8"));
  entry_mrg9 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_mrg9"));
  entry_mrg10 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_mrg10"));
  entry_mrg11 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_mrg11"));
  entry_mrg12 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_mrg12"));
  entry_mrg13 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_mrg13"));
  entry_mrg14 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_mrg14"));
  entry_mrg15 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_mrg15"));
  entry_mrg16 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_mrg16"));
  entry_mrg17 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_mrg17"));
  entry_mrg18 = GTK_ENTRY(gtk_builder_get_object(builder, "entry_mrg18"));
  
  menuhidden = GTK_CHECK_MENU_ITEM(gtk_builder_get_object(builder, "menuhidden"));
  
  // indicator
  LDEBUG("app_indicator_new...");
  indicator_menu = GTK_WIDGET(gtk_builder_get_object(builder, "indicator_menu"));
  indicator = app_indicator_new("G510s", G510S_DATA_DIR "/g510s.svg", APP_INDICATOR_CATEGORY_HARDWARE);
  LDEBUG("app_indicator_new done");
  app_indicator_set_attention_icon(indicator, G510S_DATA_DIR "/g510s-alert.svg");
  LDEBUG("app_indicator_set_menu...");
  app_indicator_set_menu(indicator, GTK_MENU(indicator_menu));
  LDEBUG("app_indicator_set_menu done");

  LDEBUG("gtk_builder_connect_signals...");
  gtk_builder_connect_signals(builder, NULL);
  LDEBUG("gtk_builder_connect_signals done");
  LDEBUG("displays_gui_init...");
  displays_gui_init(builder);
  LDEBUG("displays_gui_init done");
  g_object_unref(G_OBJECT(builder));
  
  // set program version
  gtk_about_dialog_set_version(aboutdialog, G510S_VERSION);
  
  // set our range values
  gtk_range_set_value(redscale_m1, g510s_data.m1.red);
  gtk_range_set_value(greenscale_m1, g510s_data.m1.green);
  gtk_range_set_value(bluescale_m1, g510s_data.m1.blue);
  
  gtk_range_set_value(redscale_m2, g510s_data.m2.red);
  gtk_range_set_value(greenscale_m2, g510s_data.m2.green);
  gtk_range_set_value(bluescale_m2, g510s_data.m2.blue);
  
  gtk_range_set_value(redscale_m3, g510s_data.m3.red);
  gtk_range_set_value(greenscale_m3, g510s_data.m3.green);
  gtk_range_set_value(bluescale_m3, g510s_data.m3.blue);
  
  gtk_range_set_value(redscale_mr, g510s_data.mr.red);
  gtk_range_set_value(greenscale_mr, g510s_data.mr.green);
  gtk_range_set_value(bluescale_mr, g510s_data.mr.blue);
  
  // set our entry text
  gtk_entry_set_text(entry_m1g1, g510s_data.m1.g1);
  gtk_entry_set_text(entry_m1g2, g510s_data.m1.g2);
  gtk_entry_set_text(entry_m1g3, g510s_data.m1.g3);
  gtk_entry_set_text(entry_m1g4, g510s_data.m1.g4);
  gtk_entry_set_text(entry_m1g5, g510s_data.m1.g5);
  gtk_entry_set_text(entry_m1g6, g510s_data.m1.g6);
  gtk_entry_set_text(entry_m1g7, g510s_data.m1.g7);
  gtk_entry_set_text(entry_m1g8, g510s_data.m1.g8);
  gtk_entry_set_text(entry_m1g9, g510s_data.m1.g9);
  gtk_entry_set_text(entry_m1g10, g510s_data.m1.g10);
  gtk_entry_set_text(entry_m1g11, g510s_data.m1.g11);
  gtk_entry_set_text(entry_m1g12, g510s_data.m1.g12);
  gtk_entry_set_text(entry_m1g13, g510s_data.m1.g13);
  gtk_entry_set_text(entry_m1g14, g510s_data.m1.g14);
  gtk_entry_set_text(entry_m1g15, g510s_data.m1.g15);
  gtk_entry_set_text(entry_m1g16, g510s_data.m1.g16);
  gtk_entry_set_text(entry_m1g17, g510s_data.m1.g17);
  gtk_entry_set_text(entry_m1g18, g510s_data.m1.g18);
  
  gtk_entry_set_text(entry_m2g1, g510s_data.m2.g1);
  gtk_entry_set_text(entry_m2g2, g510s_data.m2.g2);
  gtk_entry_set_text(entry_m2g3, g510s_data.m2.g3);
  gtk_entry_set_text(entry_m2g4, g510s_data.m2.g4);
  gtk_entry_set_text(entry_m2g5, g510s_data.m2.g5);
  gtk_entry_set_text(entry_m2g6, g510s_data.m2.g6);
  gtk_entry_set_text(entry_m2g7, g510s_data.m2.g7);
  gtk_entry_set_text(entry_m2g8, g510s_data.m2.g8);
  gtk_entry_set_text(entry_m2g9, g510s_data.m2.g9);
  gtk_entry_set_text(entry_m2g10, g510s_data.m2.g10);
  gtk_entry_set_text(entry_m2g11, g510s_data.m2.g11);
  gtk_entry_set_text(entry_m2g12, g510s_data.m2.g12);
  gtk_entry_set_text(entry_m2g13, g510s_data.m2.g13);
  gtk_entry_set_text(entry_m2g14, g510s_data.m2.g14);
  gtk_entry_set_text(entry_m2g15, g510s_data.m2.g15);
  gtk_entry_set_text(entry_m2g16, g510s_data.m2.g16);
  gtk_entry_set_text(entry_m2g17, g510s_data.m2.g17);
  gtk_entry_set_text(entry_m2g18, g510s_data.m2.g18);
  
  gtk_entry_set_text(entry_m3g1, g510s_data.m3.g1);
  gtk_entry_set_text(entry_m3g2, g510s_data.m3.g2);
  gtk_entry_set_text(entry_m3g3, g510s_data.m3.g3);
  gtk_entry_set_text(entry_m3g4, g510s_data.m3.g4);
  gtk_entry_set_text(entry_m3g5, g510s_data.m3.g5);
  gtk_entry_set_text(entry_m3g6, g510s_data.m3.g6);
  gtk_entry_set_text(entry_m3g7, g510s_data.m3.g7);
  gtk_entry_set_text(entry_m3g8, g510s_data.m3.g8);
  gtk_entry_set_text(entry_m3g9, g510s_data.m3.g9);
  gtk_entry_set_text(entry_m3g10, g510s_data.m3.g10);
  gtk_entry_set_text(entry_m3g11, g510s_data.m3.g11);
  gtk_entry_set_text(entry_m3g12, g510s_data.m3.g12);
  gtk_entry_set_text(entry_m3g13, g510s_data.m3.g13);
  gtk_entry_set_text(entry_m3g14, g510s_data.m3.g14);
  gtk_entry_set_text(entry_m3g15, g510s_data.m3.g15);
  gtk_entry_set_text(entry_m3g16, g510s_data.m3.g16);
  gtk_entry_set_text(entry_m3g17, g510s_data.m3.g17);
  gtk_entry_set_text(entry_m3g18, g510s_data.m3.g18);
  
  gtk_entry_set_text(entry_mrg1, g510s_data.mr.g1);
  gtk_entry_set_text(entry_mrg2, g510s_data.mr.g2);
  gtk_entry_set_text(entry_mrg3, g510s_data.mr.g3);
  gtk_entry_set_text(entry_mrg4, g510s_data.mr.g4);
  gtk_entry_set_text(entry_mrg5, g510s_data.mr.g5);
  gtk_entry_set_text(entry_mrg6, g510s_data.mr.g6);
  gtk_entry_set_text(entry_mrg7, g510s_data.mr.g7);
  gtk_entry_set_text(entry_mrg8, g510s_data.mr.g8);
  gtk_entry_set_text(entry_mrg9, g510s_data.mr.g9);
  gtk_entry_set_text(entry_mrg10, g510s_data.mr.g10);
  gtk_entry_set_text(entry_mrg11, g510s_data.mr.g11);
  gtk_entry_set_text(entry_mrg12, g510s_data.mr.g12);
  gtk_entry_set_text(entry_mrg13, g510s_data.mr.g13);
  gtk_entry_set_text(entry_mrg14, g510s_data.mr.g14);
  gtk_entry_set_text(entry_mrg15, g510s_data.mr.g15);
  gtk_entry_set_text(entry_mrg16, g510s_data.mr.g16);
  gtk_entry_set_text(entry_mrg17, g510s_data.mr.g17);
  gtk_entry_set_text(entry_mrg18, g510s_data.mr.g18);
  LDEBUG("widgets populated");

  LDEBUG("gtk_widget_hide/show...");
  if (g510s_data.gui_hidden)
    gtk_widget_hide(window);
  else
    gtk_widget_show(window);
  LDEBUG("gtk_widget_hide/show done");

  /* set_mkey_state is called from key_function thread at startup —
   * see g510s-threads.c.  SNI watcher is async via g_bus_watch_name. */
  g_bus_watch_name(G_BUS_TYPE_SESSION,
                   "org.kde.StatusNotifierWatcher",
                   G_BUS_NAME_WATCHER_FLAGS_AUTO_START,
                   on_sni_watcher_appeared, NULL, NULL, NULL);

  g_idle_add(check_leaving_cb, NULL);

  LINFO("ready — entering gtk_main");
  gtk_main();

  // notify threads to exit
  LINFO("gtk_main returned — shutting down");
  leaving = 1;

  // key_thread and update_thread both check !leaving and exit within
  // 100ms/50ms respectively — pthread_cancel is avoided because it can
  // fire inside a libusb transfer, corrupting device state before the
  // cleanup code below calls writePixmapToLCD/exitLibG15.
  LINFO("joining threads...");
  pthread_join(key_thread, NULL);
  pthread_join(update_thread, NULL);
  pthread_cancel(server_thread);
  pthread_join(server_thread, NULL);
  LINFO("threads joined");

  // kill any managed screen processes
  for (int si = 0; si < num_managed_screens; si++) {
    if (managed_screens[si].pid > 0) {
      kill(managed_screens[si].pid, SIGTERM);
      managed_screens[si].pid = 0;
    }
  }

  // save data before leaving
  save_config();
  LINFO("config saved");

  // close gracefully
  if (device_found) {
    // clear the screen
    g15canvas *canvas = (g15canvas *)malloc(sizeof(g15canvas));
    if (canvas == NULL) {
      LERROR("failed to allocate clearing canvas");
    } else {
      memset(canvas->buffer, 0, G15_BUFFER_LEN);
      g15r_clearScreen(canvas, 0);
      if (writePixmapToLCD(canvas->buffer) != 0) {
        LERROR("failed to clear LCD on exit");
      }
      free(canvas);
    }

    // shut off the lights
    if (setLEDs(0) < 0) {
      LERROR("failed to clear LEDs on exit");
    }
    if (setG510LEDColor(0, 0, 0) < 0) {
      LERROR("failed to clear LED color on exit");
    }

    // close uinput
    exit_uinput();

    // close libg15 (exitLibG15 re-attaches the kernel driver via libusb_attach_kernel_driver)
    LINFO("exitLibG15...");
    exitLibG15();
    LINFO("exitLibG15 done");
  }

  // clean up lcdlist
  lcdlist_destroy(&lcdlist);

  release_pidfile();
  LINFO("done");
  g510s_log_close();

  return 0;
}
