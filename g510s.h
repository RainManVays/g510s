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


#include <sys/types.h>

#include "g510s-log.h"

#define G510S_VERSION "0.0.3"

#ifndef SO_PRIORITY
#define SO_PRIORITY 12
#endif

#define LISTEN_ADDR "127.0.0.1"
#define LISTEN_PORT 15550

#define MAX_CLIENTS 10
#define MAX_SCREENS 16

#define CLIENT_CMD_GET_KEYSTATE 'k'
#define CLIENT_CMD_SWITCH_PRIORITIES 'p'
#define CLIENT_CMD_IS_FOREGROUND 'v'
#define CLIENT_CMD_IS_USER_SELECTED 'u'
#define CLIENT_CMD_BACKLIGHT 0x80
#define CLIENT_CMD_CONTRAST 0x40
#define CLIENT_CMD_MKEY_LIGHTS 0x20
#define CLIENT_CMD_KEY_HANDLER 0x10

#define SERV_HELO "G15 daemon HELLO"


typedef struct lcd_s {
  int lcd_type;
  unsigned char buf[1048];
  int max_x;
  int max_y;
  int connection;
  long int ident;
  unsigned int backlight_state;
  unsigned int mkey_state;
  unsigned int contrast_state;
  unsigned int state_changed;
  unsigned int usr_foreground;
} lcd_t;

typedef struct lcdnode_s lcdnode_t;
typedef struct lcdlist_s lcdlist_t;

struct lcdnode_s {
  lcdlist_t *list;
  lcdnode_t *prev;
  lcdnode_t *next;
  lcdnode_t *last_priority;
  lcd_t *lcd;
} lcdnode_s;

struct lcdlist_s {
  lcdnode_t *head;
  lcdnode_t *tail;
  lcdnode_t *current;
} lcdlist_s;

pthread_mutex_t lcdlist_mutex;
pthread_mutex_t libg15_mutex;

struct m_data_s {
  int red;
  int green;
  int blue;
  char g1[256];
  char g2[256];
  char g3[256];
  char g4[256];
  char g5[256];
  char g6[256];
  char g7[256];
  char g8[256];
  char g9[256];
  char g10[256];
  char g11[256];
  char g12[256];
  char g13[256];
  char g14[256];
  char g15[256];
  char g16[256];
  char g17[256];
  char g18[256];
};

struct g510s_data_s {
  int gui_hidden;
  int mkey_state;
  struct m_data_s m1;
  struct m_data_s m2;
  struct m_data_s m3;
  struct m_data_s mr;
  int clock_mode;
  int show_date;
  int internal_screen;     /* 0=clock, 1=cpu, 2=sysmon, 3=claude */
  int sysmon_disk_offset;  /* disk page offset for sysmon (L2/L3) */
} g510s_data;

volatile int leaving;
volatile int update;
volatile int device_found;
volatile int startup_anim_done;
volatile int anim_frame_ready;
unsigned char anim_lcd_buf[1048];  /* animation frame shared with update_function */
volatile unsigned int connected_clients;
volatile unsigned int current_key_state;

typedef struct {
  char name[64];
  char cmd[512];
  pid_t pid;
} screen_t;

screen_t managed_screens[MAX_SCREENS];
int num_managed_screens;
volatile int current_screen_idx;
volatile int pending_foreground;

int init_uinput();
void exit_uinput();
void process_keys(lcdlist_t *displaylist, unsigned int key, unsigned int key_state);

void digital_clock(lcd_t *lcd);
void cpu_screen_init(void);
void cpu_screen(lcd_t *lcd);
void sysmon_screen_init(void);
void sysmon_screen(lcd_t *lcd);
void claude_screen_init(void);
void claude_maybe_scan(void);
void claude_screen(lcd_t *lcd);

void acquire_pidfile();
void release_pidfile();
void init_data();
int check_dir();
int load_config();
int save_config();
void load_screens();

lcdlist_t *lcdlist_init();
lcdnode_t *lcdnode_add(lcdlist_t **display_list);
void lcdnode_remove(lcdnode_t *oldnode);
void lcdlist_destroy(lcdlist_t **displaylist);

void send_keystate(lcd_t *client, unsigned int key);
int client_connect(lcdlist_t **lcdlist, int listening_socket);
int init_sockserver();
int g15_send(int sock, char *buf, unsigned int len);
int g15_recv(lcdnode_t *lcdnode, int sock, char *buf, unsigned int len);

int is_number(char number[]);
void convert_buf(lcd_t *lcd, unsigned char * orig_buf);
void set_mkey_state(int state);
void set_color();
void run_gkey_cmd(int gkey);

void *lcd_client_function(void *display);
void *key_function(void *lcdlist);
void *update_function(void *lcdlist);
void *server_function(void *lcdlist);

void display_registry_init(void);
void displays_gui_save(void);

void switch_log_mark(void);
long _switch_log_elapsed_ms(void);
/* switch_log() captures __func__ from the call site for accurate TRACE output */
#define switch_log(fmt, ...) \
    g510s_log(G510S_LOG_TRACE, "switch", __func__, \
              "[SW +%4ldms] " fmt, _switch_log_elapsed_ms(), ##__VA_ARGS__)
