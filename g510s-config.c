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

#define LOG_MODULE "config"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>

#include "g510s.h"


static void get_pidfile_path(char *buf, size_t size) {
  const char *home = getenv("HOME");
  if (home)
    snprintf(buf, size, "%s/.g510s/g510s.pid", home);
  else
    snprintf(buf, size, "/tmp/g510s.pid");
}

void acquire_pidfile() {
  char path[512];
  get_pidfile_path(path, sizeof(path));

  FILE *f = fopen(path, "r");
  if (f) {
    pid_t old_pid = 0;
    fscanf(f, "%d", &old_pid);
    fclose(f);

    if (old_pid > 0 && kill(old_pid, 0) == 0) {
      LINFO("stopping previous instance (pid %d)", old_pid);
      kill(old_pid, SIGTERM);
      int waited = 0;
      while (waited < 20 && kill(old_pid, 0) == 0) {
        usleep(100000);
        waited++;
      }
      if (kill(old_pid, 0) == 0) {
        LWARN("previous instance (pid %d) did not exit, sending SIGKILL", old_pid);
        kill(old_pid, SIGKILL);
      }
    }
    /* give the kernel time to release the USB device */
    usleep(500000);
  }

  f = fopen(path, "w");
  if (f) {
    fprintf(f, "%d\n", (int)getpid());
    fclose(f);
  } else {
    LWARN("could not write pidfile %s", path);
  }
}

void release_pidfile() {
  char path[512];
  get_pidfile_path(path, sizeof(path));

  /* only remove if it's ours */
  FILE *f = fopen(path, "r");
  if (f) {
    pid_t stored = 0;
    fscanf(f, "%d", &stored);
    fclose(f);
    if (stored == getpid())
      remove(path);
  }
}

void init_data() {
  // gui
  g510s_data.gui_hidden = 0;
  
  // function
  g510s_data.mkey_state = 1;
  
  // color
  g510s_data.m1.red = 255;
  g510s_data.m1.green = 255;
  g510s_data.m1.blue = 255;
  g510s_data.m2.red = 255;
  g510s_data.m2.green = 255;
  g510s_data.m2.blue = 255;
  g510s_data.m3.red = 255;
  g510s_data.m3.green = 255;
  g510s_data.m3.blue = 255;
  g510s_data.mr.red = 255;
  g510s_data.mr.green = 255;
  g510s_data.mr.blue = 255;
  
  // cmd strings
  memset(g510s_data.m1.g1, 0, sizeof(g510s_data.m1.g1));
  memset(g510s_data.m1.g2, 0, sizeof(g510s_data.m1.g2));
  memset(g510s_data.m1.g3, 0, sizeof(g510s_data.m1.g3));
  memset(g510s_data.m1.g4, 0, sizeof(g510s_data.m1.g4));
  memset(g510s_data.m1.g5, 0, sizeof(g510s_data.m1.g5));
  memset(g510s_data.m1.g6, 0, sizeof(g510s_data.m1.g6));
  memset(g510s_data.m1.g7, 0, sizeof(g510s_data.m1.g7));
  memset(g510s_data.m1.g8, 0, sizeof(g510s_data.m1.g8));
  memset(g510s_data.m1.g9, 0, sizeof(g510s_data.m1.g9));
  memset(g510s_data.m1.g10, 0, sizeof(g510s_data.m1.g10));
  memset(g510s_data.m1.g11, 0, sizeof(g510s_data.m1.g11));
  memset(g510s_data.m1.g12, 0, sizeof(g510s_data.m1.g12));
  memset(g510s_data.m1.g13, 0, sizeof(g510s_data.m1.g13));
  memset(g510s_data.m1.g14, 0, sizeof(g510s_data.m1.g14));
  memset(g510s_data.m1.g15, 0, sizeof(g510s_data.m1.g15));
  memset(g510s_data.m1.g16, 0, sizeof(g510s_data.m1.g16));
  memset(g510s_data.m1.g17, 0, sizeof(g510s_data.m1.g17));
  memset(g510s_data.m1.g18, 0, sizeof(g510s_data.m1.g18));
  
  memset(g510s_data.m2.g1, 0, sizeof(g510s_data.m2.g1));
  memset(g510s_data.m2.g2, 0, sizeof(g510s_data.m2.g2));
  memset(g510s_data.m2.g3, 0, sizeof(g510s_data.m2.g3));
  memset(g510s_data.m2.g4, 0, sizeof(g510s_data.m2.g4));
  memset(g510s_data.m2.g5, 0, sizeof(g510s_data.m2.g5));
  memset(g510s_data.m2.g6, 0, sizeof(g510s_data.m2.g6));
  memset(g510s_data.m2.g7, 0, sizeof(g510s_data.m2.g7));
  memset(g510s_data.m2.g8, 0, sizeof(g510s_data.m2.g8));
  memset(g510s_data.m2.g9, 0, sizeof(g510s_data.m2.g9));
  memset(g510s_data.m2.g10, 0, sizeof(g510s_data.m2.g10));
  memset(g510s_data.m2.g11, 0, sizeof(g510s_data.m2.g11));
  memset(g510s_data.m2.g12, 0, sizeof(g510s_data.m2.g12));
  memset(g510s_data.m2.g13, 0, sizeof(g510s_data.m2.g13));
  memset(g510s_data.m2.g14, 0, sizeof(g510s_data.m2.g14));
  memset(g510s_data.m2.g15, 0, sizeof(g510s_data.m2.g15));
  memset(g510s_data.m2.g16, 0, sizeof(g510s_data.m2.g16));
  memset(g510s_data.m2.g17, 0, sizeof(g510s_data.m2.g17));
  memset(g510s_data.m2.g18, 0, sizeof(g510s_data.m2.g18));
  
  memset(g510s_data.m3.g1, 0, sizeof(g510s_data.m3.g1));
  memset(g510s_data.m3.g2, 0, sizeof(g510s_data.m3.g2));
  memset(g510s_data.m3.g3, 0, sizeof(g510s_data.m3.g3));
  memset(g510s_data.m3.g4, 0, sizeof(g510s_data.m3.g4));
  memset(g510s_data.m3.g5, 0, sizeof(g510s_data.m3.g5));
  memset(g510s_data.m3.g6, 0, sizeof(g510s_data.m3.g6));
  memset(g510s_data.m3.g7, 0, sizeof(g510s_data.m3.g7));
  memset(g510s_data.m3.g8, 0, sizeof(g510s_data.m3.g8));
  memset(g510s_data.m3.g9, 0, sizeof(g510s_data.m3.g9));
  memset(g510s_data.m3.g10, 0, sizeof(g510s_data.m3.g10));
  memset(g510s_data.m3.g11, 0, sizeof(g510s_data.m3.g11));
  memset(g510s_data.m3.g12, 0, sizeof(g510s_data.m3.g12));
  memset(g510s_data.m3.g13, 0, sizeof(g510s_data.m3.g13));
  memset(g510s_data.m3.g14, 0, sizeof(g510s_data.m3.g14));
  memset(g510s_data.m3.g15, 0, sizeof(g510s_data.m3.g15));
  memset(g510s_data.m3.g16, 0, sizeof(g510s_data.m3.g16));
  memset(g510s_data.m3.g17, 0, sizeof(g510s_data.m3.g17));
  memset(g510s_data.m3.g18, 0, sizeof(g510s_data.m3.g18));
  
  memset(g510s_data.mr.g1, 0, sizeof(g510s_data.mr.g1));
  memset(g510s_data.mr.g2, 0, sizeof(g510s_data.mr.g2));
  memset(g510s_data.mr.g3, 0, sizeof(g510s_data.mr.g3));
  memset(g510s_data.mr.g4, 0, sizeof(g510s_data.mr.g4));
  memset(g510s_data.mr.g5, 0, sizeof(g510s_data.mr.g5));
  memset(g510s_data.mr.g6, 0, sizeof(g510s_data.mr.g6));
  memset(g510s_data.mr.g7, 0, sizeof(g510s_data.mr.g7));
  memset(g510s_data.mr.g8, 0, sizeof(g510s_data.mr.g8));
  memset(g510s_data.mr.g9, 0, sizeof(g510s_data.mr.g9));
  memset(g510s_data.mr.g10, 0, sizeof(g510s_data.mr.g10));
  memset(g510s_data.mr.g11, 0, sizeof(g510s_data.mr.g11));
  memset(g510s_data.mr.g12, 0, sizeof(g510s_data.mr.g12));
  memset(g510s_data.mr.g13, 0, sizeof(g510s_data.mr.g13));
  memset(g510s_data.mr.g14, 0, sizeof(g510s_data.mr.g14));
  memset(g510s_data.mr.g15, 0, sizeof(g510s_data.mr.g15));
  memset(g510s_data.mr.g16, 0, sizeof(g510s_data.mr.g16));
  memset(g510s_data.mr.g17, 0, sizeof(g510s_data.mr.g17));
  memset(g510s_data.mr.g18, 0, sizeof(g510s_data.mr.g18));
  
  // clock settings
  g510s_data.clock_mode = 0;
  g510s_data.show_date = 1;
  g510s_data.internal_screen    = 0;
  g510s_data.sysmon_disk_offset = 0;
}

int check_dir() {
  char home_path[255];
  char g510s_dir[] = "/.g510s";
  char *full_path;
  DIR *dir;
  const char *home_env;

  home_env = getenv("HOME");
  if (home_env == NULL) {
    LWARN("$HOME not set, cannot create config directory");
    return -1;
  }
  strncpy(home_path, home_env, sizeof(home_path) - 1);
  home_path[sizeof(home_path) - 1] = '\0';

  full_path = malloc(sizeof(home_path) + sizeof(g510s_dir) + 1);
  snprintf(full_path, sizeof(home_path) + sizeof(g510s_dir) + 1, "%s%s", home_path, g510s_dir);

  if ((dir = opendir(full_path)) == NULL) {
    if (mkdir(full_path, 0777) == -1) {
      LERROR("failed to create directory $HOME/.g510s");
      free(full_path);
      return -1;
    }
  } else {
    closedir(dir);
  }

  free(full_path);

  return 0;
}

int load_config() {
  char home_path[255];
  char file_name[] = "/.g510s/g510s.dat";
  char *full_path;
  FILE *file;
  const char *home_env;

  home_env = getenv("HOME");
  if (home_env == NULL) {
    LWARN("$HOME not set, using default settings");
    return -1;
  }
  strncpy(home_path, home_env, sizeof(home_path) - 1);
  home_path[sizeof(home_path) - 1] = '\0';

  full_path = malloc(sizeof(home_path) + sizeof(file_name) + 1);
  snprintf(full_path, sizeof(home_path) + sizeof(file_name) + 1, "%s%s", home_path, file_name);

  if ((file = fopen(full_path, "rb")) == NULL) {
    LWARN("failed to read save file, using default settings");
    free(full_path);
    return -1;
  }
  
  fread(&g510s_data, sizeof(g510s_data), 1, file);
  fclose(file);
  
  free(full_path);
  
  return 0;
}

void load_screens() {
  char home_path[255];
  const char *file_name = "/.g510s/screens.conf";
  char full_path[512];
  FILE *file;
  const char *home_env;
  char line[640];

  num_managed_screens = 0;
  current_screen_idx = -1;
  pending_foreground = 0;

  home_env = getenv("HOME");
  if (home_env == NULL) return;

  strncpy(home_path, home_env, sizeof(home_path) - 1);
  home_path[sizeof(home_path) - 1] = '\0';
  snprintf(full_path, sizeof(full_path), "%s%s", home_path, file_name);

  file = fopen(full_path, "r");
  if (!file) return;

  while (fgets(line, sizeof(line), file) && num_managed_screens < MAX_SCREENS) {
    char *nl = strchr(line, '\n');
    if (nl) *nl = '\0';

    char *p = line;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '\0' || *p == '#') continue;

    char *eq = strchr(p, '=');
    if (!eq) continue;

    *eq = '\0';
    char *name = p;
    char *cmd = eq + 1;

    char *end = name + strlen(name) - 1;
    while (end >= name && isspace((unsigned char)*end)) { *end = '\0'; end--; }

    while (isspace((unsigned char)*cmd)) cmd++;

    if (*name == '\0' || *cmd == '\0') continue;

    strncpy(managed_screens[num_managed_screens].name, name,
            sizeof(managed_screens[0].name) - 1);
    strncpy(managed_screens[num_managed_screens].cmd, cmd,
            sizeof(managed_screens[0].cmd) - 1);
    managed_screens[num_managed_screens].pid = 0;
    num_managed_screens++;
    LDEBUG("screen[%d] \"%s\" = %s",
           num_managed_screens - 1, name, cmd);
  }

  fclose(file);
  LINFO("loaded %d managed screen(s)", num_managed_screens);
}

int save_config() {
  char home_path[255];
  char file_name[] = "/.g510s/g510s.dat";
  char *full_path;
  FILE *file;
  const char *home_env;

  home_env = getenv("HOME");
  if (home_env == NULL) {
    LWARN("$HOME not set, skipping config save");
    return -1;
  }
  strncpy(home_path, home_env, sizeof(home_path) - 1);
  home_path[sizeof(home_path) - 1] = '\0';

  full_path = malloc(sizeof(home_path) + sizeof(file_name) + 1);
  snprintf(full_path, sizeof(home_path) + sizeof(file_name) + 1, "%s%s", home_path, file_name);

  if ((file = fopen(full_path, "wb")) == NULL) {
    LERROR("failed to write save file");
    free(full_path);
    return -1;
  }
  
  fwrite(&g510s_data, sizeof(g510s_data), 1, file);
  fclose(file);
  
  free(full_path);
  
  return 0;
}