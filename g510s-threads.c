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

#define LOG_MODULE "threads"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <libg15.h>
#include <libusb-1.0/libusb.h>
#include <libappindicator/app-indicator.h>

#include "g510s.h"
#include "g510s-display-registry.h"


extern AppIndicator *indicator;

static gboolean cb_indicator_attention(gpointer data) {
  (void)data;
  app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ATTENTION);
  return G_SOURCE_REMOVE;
}

static gboolean cb_indicator_active(gpointer data) {
  (void)data;
  app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
  return G_SOURCE_REMOVE;
}

void *lcd_client_function(void *display) {
  lcdnode_t *g15node = display;
  lcd_t *client_lcd = g15node->lcd;
  int retval;
  unsigned int width, height, buflen, header = 4;
  
  int client_sock = client_lcd->connection;
  char helo[] = SERV_HELO;
  unsigned char *tmpbuf = malloc(6880);

  if (tmpbuf == NULL) {
    LERROR("failed to allocate client buffer");
    close(client_sock);
    lcdnode_remove(display);
    pthread_exit(NULL);
  }

  connected_clients++;
  
  if (g15_send(client_sock, (char*)helo, strlen(SERV_HELO)) < 0) {
    goto exitthread;
  }
  
  if (g15_recv(g15node, client_sock, (char*)tmpbuf, 4) < 4) {
    goto exitthread;
  }
  
  if (tmpbuf[0] == 'G') {
    while (leaving == 0) {
      retval = g15_recv(g15node, client_sock, (char *)tmpbuf, 6880);
      if (retval != 6880) {
        break;
      }
      pthread_mutex_lock(&lcdlist_mutex);
      memset(client_lcd->buf, 0, sizeof(client_lcd->buf));
      convert_buf(client_lcd, tmpbuf);
      client_lcd->ident = random();
      pthread_mutex_unlock(&lcdlist_mutex);
    }
  } else if (tmpbuf[0] == 'R') {
    while (leaving == 0) {
      retval = g15_recv(g15node, client_sock, (char *)tmpbuf, 1048);
      if (retval != 1048) {
        break;
      }
      pthread_mutex_lock(&lcdlist_mutex);
      memcpy(client_lcd->buf, tmpbuf, sizeof(client_lcd->buf));
      client_lcd->ident = random();
      pthread_mutex_unlock(&lcdlist_mutex);
    }
  } else if (tmpbuf[0] == 'W') {
    while (leaving == 0) {
      retval = g15_recv(g15node, client_sock, (char*)tmpbuf, 865);
      if (!retval) {
        break;
      }
      
      if (tmpbuf[2] & 1) {
        width = ((unsigned char)tmpbuf[2] ^ 1) | (unsigned char)tmpbuf[3];
        height = tmpbuf[4];
        header = 5;
      } else {
        width = tmpbuf[2];
        height = tmpbuf[3];
        header = 4;
      }
      
      buflen = (width / 8) * height;

      if (buflen > 860) {
        unsigned char *discard = malloc(buflen - 860);
        if (discard != NULL) {
          g15_recv(g15node, client_sock, (char *)discard, buflen - 860);
          free(discard);
        }
        buflen = 860;
      }

      if (width != 160) {
        goto exitthread;
      }

      pthread_mutex_lock(&lcdlist_mutex);
      memcpy(client_lcd->buf, tmpbuf + header, buflen);
      client_lcd->ident = random();
      pthread_mutex_unlock(&lcdlist_mutex);
    }
  }
  exitthread:
    close(client_sock);
    free(tmpbuf);
    lcdnode_remove(display);
    connected_clients--;
    pthread_exit(NULL);
}

void *key_function(void *lcdlist) {
  int keyreturn = 0;
  unsigned int key = 0;
  static unsigned int key_state = 0;
  lcdlist_t *displaylist = (lcdlist_t*)(lcdlist);
  
  {
    int max_fd = 0;
    for (int fd = 0; fd < 4096; fd++)
      if (fcntl(fd, F_GETFD) != -1) max_fd = fd;
    LDEBUG("starting, highest open fd = %d", max_fd);
  }

  /* Set M-key LED state from saved config.  Done here (not in main thread)
   * so a slow USB wake-up never blocks the GTK event loop.  Same path as
   * the hotplug reconnect below. */
  if (device_found)
    set_mkey_state(g510s_data.mkey_state);

  while (!leaving) {
    if (device_found) {
      /* timeout=10ms: libusb_mutex held ≤10ms per poll, then released.
       * G510 only sends USB interrupt on key change (not continuously),
       * so timeout=0 (unlimited) would block libusb_mutex forever when
       * the keyboard is idle, starving set_mkey_state and writePixmapToLCD. */
      {
        struct timespec _k0, _k1;
        clock_gettime(CLOCK_MONOTONIC, &_k0);
        keyreturn = getPressedKeys(&key, 10);
        clock_gettime(CLOCK_MONOTONIC, &_k1);
        long _kms = (_k1.tv_sec - _k0.tv_sec)*1000L
                  + (_k1.tv_nsec - _k0.tv_nsec)/1000000L;
        if (_kms > 100)
          LWARN("getPressedKeys blocked: %ldms ret=%d", _kms, keyreturn);
      }

      // dont process normal keys; cap retries to avoid infinite spin
      // Use 1ms timeout in retries: TRY_AGAIN means a normal-key report
      // arrived; we want to drain those quickly without blocking long.
      int try_again = 0;
      while (keyreturn == G15_ERROR_TRY_AGAIN && !leaving && try_again++ < 10) {
        keyreturn = getPressedKeys(&key, 1);
      }

      /* Log unexpected USB return codes.
       * For G510, handle_usb_errors() translates LIBUSB_ERROR_TIMEOUT →
       * G15_ERROR_READING_USB_DEVICE (4): normal keyboard idle, not an error.
       * G15_ERROR_TIMEOUT (3) is the G13-path equivalent; also harmless. */
      if (keyreturn != G15_NO_ERROR &&
          keyreturn != G15_ERROR_TRY_AGAIN &&
          keyreturn != G15_ERROR_TIMEOUT &&
          keyreturn != G15_ERROR_READING_USB_DEVICE &&
          keyreturn != LIBUSB_ERROR_TIMEOUT &&
          keyreturn != LIBUSB_ERROR_NO_DEVICE) {
        LWARN("getPressedKeys unexpected: %d (%s)", keyreturn, libusb_strerror(keyreturn));
      }

      /* Heartbeat — confirms key thread is alive even when keyboard is idle */
      {
        static time_t _last_hb = 0;
        time_t _now = time(NULL);
        if (_now - _last_hb >= 30) {
          LDEBUG("heartbeat device=%d keyreturn=%d", device_found, keyreturn);
          _last_hb = _now;
        }
      }

      // process extra keys
      if ((keyreturn == G15_NO_ERROR) && (key != key_state)) {
        current_key_state = key;
        process_keys(displaylist, key, key_state);
        key_state = key;
      }
      
      // handle hotplugging of keyboard or sound devices
      if (keyreturn == LIBUSB_ERROR_NO_DEVICE) {
        pthread_mutex_lock(&libg15_mutex);
        device_found = 0;
        exit_uinput();
        exitLibG15();
        pthread_mutex_unlock(&libg15_mutex);
        g_idle_add(cb_indicator_attention, NULL);
        LINFO("device disconnected, retrying...");
        {
          int _attempt = 0;
          struct timespec _disc0;
          clock_gettime(CLOCK_MONOTONIC, &_disc0);
          while (!device_found && !leaving) {
            _attempt++;
            if (setupLibG15(0x46d, 0xc22d, 0) == G15_NO_ERROR) {
              struct timespec _disc1; clock_gettime(CLOCK_MONOTONIC, &_disc1);
              long _dms = (_disc1.tv_sec - _disc0.tv_sec)*1000L
                        + (_disc1.tv_nsec - _disc0.tv_nsec)/1000000L;
              LINFO("[hotplug] found 046d:c22d after %d attempt(s), %ldms", _attempt, _dms);
              device_found = 1;
              break;
            } else if (setupLibG15(0x46d, 0xc22e, 0) == G15_NO_ERROR) {
              struct timespec _disc1; clock_gettime(CLOCK_MONOTONIC, &_disc1);
              long _dms = (_disc1.tv_sec - _disc0.tv_sec)*1000L
                        + (_disc1.tv_nsec - _disc0.tv_nsec)/1000000L;
              LINFO("[hotplug] found 046d:c22e after %d attempt(s), %ldms", _attempt, _dms);
              device_found = 1;
              break;
            } else {
              exitLibG15();
              LDEBUG("[hotplug] attempt %d failed, retrying in 1s", _attempt);
            }
            sleep(1);
          }
        }
        if (!leaving) {
          if (init_uinput() != 0) {
            LWARN("failed to initialize uinput, media keys not available");
          }
          set_mkey_state(g510s_data.mkey_state);
          if (displaylist->tail == displaylist->current) {
            displaylist->current->lcd->ident = 0;
          }
          g_idle_add(cb_indicator_active, NULL);
        }
      }
      /* no usleep: the 10ms getPressedKeys timeout provides the same rate */
    } else { // device was not found
      // wait for a device
      LINFO("waiting for device...");
      while (!device_found && !leaving) {
        if (setupLibG15(0x46d, 0xc22d, 0) == G15_NO_ERROR) {
          LINFO("[thread] found device 046d:c22d");
          device_found = 1;
        } else if (setupLibG15(0x46d, 0xc22e, 0) == G15_NO_ERROR) {
          LINFO("[thread] found device 046d:c22e");
          device_found = 1;
        } else {
          exitLibG15();
        }
        sleep(1);
      }
      if (!leaving) {
        if (init_uinput() != 0) {
          LWARN("failed to initialize uinput, media keys not available");
        }
        set_mkey_state(g510s_data.mkey_state);
        if (displaylist->tail == displaylist->current) {
          displaylist->current->lcd->ident = 0;
        }
        g_idle_add(cb_indicator_active, NULL);
      }
    }
  }
  return NULL;
}

void *update_function(void *lcdlist) {
  lcdlist_t *displaylist = (lcdlist_t*)(lcdlist);
  static long int lastlcd = 1;
  
  lcd_t *displaying = displaylist->tail->lcd;
  memset(displaying->buf, 0, sizeof(displaying->buf));
  displaying->ident = 0;
  
  static int screen_prev = -1;  /* screen id seen on previous iteration */

  while (!leaving) {
    /* Heartbeat — confirms update thread is alive even when LCD is unchanged */
    {
      static time_t _last_hb = 0;
      time_t _now = time(NULL);
      if (_now - _last_hb >= 30) {
        LDEBUG("heartbeat screen=%d ident=%ld", g510s_data.internal_screen, displaying->ident);
        _last_hb = _now;
      }
    }

    /* Scan JSONL files outside the mutex — cl_load_all() takes 100-500ms and
     * must not hold libg15_mutex during I/O.
     *
     * Guard: only scan if Claude has been the active screen for at least TWO
     * consecutive 50ms cycles.  This prevents a rapid L1 pass-through
     * (Sysmon→Claude→Clock in <50ms) from triggering a 5-second scan and
     * blocking the render thread. */
    {
      int screen_now = g510s_data.internal_screen;
      if (screen_now == DISP_CLAUDE && screen_prev == DISP_CLAUDE) {
        struct timespec _cs0, _cs1;
        clock_gettime(CLOCK_MONOTONIC, &_cs0);
        switch_log("claude_maybe_scan: start");
        claude_maybe_scan();
        switch_log("claude_maybe_scan: done");
        clock_gettime(CLOCK_MONOTONIC, &_cs1);
        long _cms = (_cs1.tv_sec - _cs0.tv_sec)*1000L
                  + (_cs1.tv_nsec - _cs0.tv_nsec)/1000000L;
        if (_cms > 1000)
          LWARN("claude_maybe_scan slow: %ldms", _cms);
      }
      screen_prev = screen_now;
    }

    pthread_mutex_lock(&libg15_mutex);
    if (device_found) {
      // only update the color if the changes will be visible
      if (update == g510s_data.mkey_state) {
        switch (g510s_data.mkey_state) {
          case 1:
            setG510LEDColor(g510s_data.m1.red, g510s_data.m1.green, g510s_data.m1.blue);
            break;
          case 2:
            setG510LEDColor(g510s_data.m2.red, g510s_data.m2.green, g510s_data.m2.blue);
            break;
          case 3:
            setG510LEDColor(g510s_data.m3.red, g510s_data.m3.green, g510s_data.m3.blue);
            break;
          case 4:
            setG510LEDColor(g510s_data.mr.red, g510s_data.mr.green, g510s_data.mr.blue);
            break;
          default:
            LERROR("invalid mkey_state: %d", g510s_data.mkey_state);
            break;
        }
        update = 0;
      }

      displaying = displaylist->current->lcd;

      if (displaylist->tail == displaylist->current) {
        display_entry_t *d = display_registry_by_id(g510s_data.internal_screen);
        int was_zero = (displaying->ident == 0);
        if (was_zero)
          switch_log("update: calling render id=%d (%s)",
                     d ? d->id : -1, d ? d->name : "null→clock");
        {
          struct timespec _r0, _r1;
          clock_gettime(CLOCK_MONOTONIC, &_r0);
          if (d && d->render_preview)
            d->render_preview(displaying);
          else
            digital_clock(displaying);
          clock_gettime(CLOCK_MONOTONIC, &_r1);
          long _rms = (_r1.tv_sec - _r0.tv_sec)*1000L
                    + (_r1.tv_nsec - _r0.tv_nsec)/1000000L;
          if (_rms > 50)
            LWARN("screen render slow: screen=%d %ldms", g510s_data.internal_screen, _rms);
        }
        if (was_zero)
          switch_log("update: render done, ident_after=%ld%s",
                     displaying->ident,
                     displaying->ident == 0 ? " ← STILL 0 (early return in screen fn!)" : "");
      }

      if (displaying->ident != lastlcd) {
        if (lastlcd == 0)
          switch_log("update: writePixmapToLCD (first frame after switch)");
        struct timespec _w0, _w1;
        clock_gettime(CLOCK_MONOTONIC, &_w0);
        int _wret = writePixmapToLCD(displaying->buf);
        clock_gettime(CLOCK_MONOTONIC, &_w1);
        long _wms = (_w1.tv_sec - _w0.tv_sec)*1000L
                  + (_w1.tv_nsec - _w0.tv_nsec)/1000000L;
        if (_wret != 0)
          LWARN("writePixmapToLCD failed: %d (took %ldms)", _wret, _wms);
        else if (_wms > 200)
          LWARN("writePixmapToLCD slow: %ldms", _wms);
        lastlcd = displaying->ident;
      }

      // we dont do anything here
      if (displaying->state_changed) {
        displaying->state_changed = 0;
      }
    }
    pthread_mutex_unlock(&libg15_mutex);
    usleep(50000);
  }
  return NULL;
}

void *server_function(void *lcdlist) {
  lcdlist_t *displaylist = (lcdlist_t*)(lcdlist);
  int g15_socket = -1;
  
  if ((g15_socket = init_sockserver()) < 0) {
    LERROR("unable to initialise server at port %d", LISTEN_PORT);
    return NULL;
  }

  if (fcntl(g15_socket, F_SETFL, O_NONBLOCK) < 0) {
    LWARN("unable to set socket to nonblocking");
  }
  
  while (!leaving) {
    client_connect(&displaylist, g15_socket);
  }
  
  close(g15_socket);
  return NULL;
}