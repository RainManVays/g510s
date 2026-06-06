/*
    This file is part of g15tools.

    g15tools is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    g15tools is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with libg15; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    (c) 2006-2007 The G15tools Project - g15tools.sf.net

    Ported to libusb-1.0 from libusb-0.1 (formerly used <usb.h>).
*/

#include <pthread.h>
#include "libg15.h"
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <libusb-1.0/libusb.h>
#include <string.h>
#include <errno.h>
#include "config.h"

static libusb_context      *usb_ctx        = NULL;
static libusb_device_handle *keyboard_device = NULL;
static int libg15_debugging_enabled = 0;
static int enospc_slowdown = 0;

static int found_devicetype = -1;
static int shared_device = 0;
static int g15_keys_endpoint = 0;
static int g15_lcd_endpoint = 0;
static int g15_claimed_interface = -1;
static pthread_mutex_t libusb_mutex;
static int light_state = 0;
static int joystick_x = 0;
static int joystick_y = 0;
static int last_pressed_keys = -1;

/* to add a new device, simply create a new DEVICE() in this list */
/* Fields are: "Name",VendorID,ProductID,Capabilities */
const libg15_devices_t g15_devices[] = {
    DEVICE("Logitech G15",0x46d,0xc222,G15_LCD|G15_KEYS),
    DEVICE("Logitech G11",0x46d,0xc225,G15_KEYS),
    DEVICE("Logitech Z-10",0x46d,0x0a07,G15_LCD|G15_KEYS|G15_DEVICE_IS_SHARED),
    DEVICE("Logitech G15 v2",0x46d,0xc227,G15_LCD|G15_KEYS|G15_DEVICE_5BYTE_RETURN),
    DEVICE("Logitech Gamepanel",0x46d,0xc251,G15_LCD|G15_KEYS|G15_DEVICE_IS_SHARED),
    DEVICE("Logitech G13",0x46d,0xc21c,G15_LCD|G15_KEYS|G15_DEVICE_G13|G15_DEVICE_COLOUR|G15_STORAGE),
    DEVICE("Logitech G110",0x46d,0xc22b,G15_KEYS|G15_DEVICE_G110|G15_DEVICE_COLOUR),
    DEVICE("Logitech G510",0x46d,0xc22d,G15_LCD|G15_KEYS|G15_DEVICE_IS_SHARED|G15_DEVICE_G510|G15_DEVICE_COLOUR), /* without audio activated */
    DEVICE("Logitech G510",0x46d,0xc22e,G15_LCD|G15_KEYS|G15_DEVICE_IS_SHARED|G15_DEVICE_G510|G15_DEVICE_COLOUR), /* with audio activated */
    DEVICE(NULL,0,0,0)
};

/* return device capabilities */
int g15DeviceCapabilities() {
    if(found_devicetype>-1)
        return g15_devices[found_devicetype].caps;
    else
        return -1;
}

/* get the current state of the backlight */
int getBacklightState() {
    return light_state;
}

/* get the current joystick X position  */
int getJoystickX() {
    return joystick_x;
}

/* get the current joystick Y position  */
int getJoystickY() {
    return joystick_y;
}

/* enable or disable debugging */
void libg15Debug(int option) {
    libg15_debugging_enabled = option;
    if (usb_ctx)
        libusb_set_option(usb_ctx, LIBUSB_OPTION_LOG_LEVEL, option);
}

/* debugging wrapper */
static int g15_log(FILE *fd, unsigned int level, const char *fmt, ...) {
    if (libg15_debugging_enabled && libg15_debugging_enabled>=level) {
        fprintf(fd,"libg15: ");
        va_list argp;
        va_start(argp, fmt);
        vfprintf(fd, fmt, argp);
        va_end(argp);
    }
    return 0;
}

/* return number of connected and supported devices */
int g15NumberOfConnectedDevices() {
    libusb_device **devs;
    ssize_t cnt = libusb_get_device_list(usb_ctx, &devs);
    if (cnt < 0) return 0;

    unsigned int found = 0;
    for (ssize_t d = 0; d < cnt; d++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(devs[d], &desc) < 0) continue;
        for (int i = 0; g15_devices[i].name != NULL; i++) {
            if (desc.idVendor  == g15_devices[i].vendorid &&
                desc.idProduct == g15_devices[i].productid)
                found++;
        }
    }
    libusb_free_device_list(devs, 1);
    g15_log(stderr, G15_LOG_INFO, "Found %i supported devices\n", found);
    return found;
}

static int initLibUsb() {
    g15_log(stderr, G15_LOG_INFO, "Initialising USB\n");
    int ret = libusb_init(&usb_ctx);
    if (ret < 0) {
        g15_log(stderr, G15_LOG_INFO, "libusb_init failed: %s\n", libusb_strerror(ret));
        return G15_ERROR_OPENING_USB_DEVICE;
    }
    return G15_NO_ERROR;
}

/*
 * Try to open a single device and claim all HID interfaces.
 * Called with found_devicetype already set so g15DeviceCapabilities() works.
 * Returns handle on success, NULL on any failure.
 */
static libusb_device_handle *tryOpenDevice(libusb_device *dev) {
    libusb_device_handle *devh = NULL;
    int ret = libusb_open(dev, &devh);
    if (ret < 0 || !devh) {
        g15_log(stderr, G15_LOG_INFO, "Error, could not open the keyboard: %s\n",
                libusb_strerror(ret));
        return NULL;
    }

    usleep(50*1000);

    struct libusb_device_descriptor desc;
    libusb_get_device_descriptor(dev, &desc);   /* already succeeded above */

    g15_log(stderr, G15_LOG_INFO, "Device has %i possible configurations\n",
            desc.bNumConfigurations);

    if (g15DeviceCapabilities() & G15_DEVICE_IS_SHARED)
        shared_device = 1;

    for (int j = 0; j < desc.bNumConfigurations; j++) {
        struct libusb_config_descriptor *cfg;
        if (libusb_get_config_descriptor(dev, j, &cfg) < 0) continue;

        for (int i = 0; i < cfg->bNumInterfaces; i++) {
            if ((g15DeviceCapabilities() & G15_DEVICE_G510) &&
                i == G510_STANDARD_KEYBOARD_INTERFACE) continue;

            if (g15_keys_endpoint && g15_lcd_endpoint) break;

            const struct libusb_interface *ifp = &cfg->interface[i];
            g15_log(stderr, G15_LOG_INFO, "Device has %i Alternate Settings\n",
                    ifp->num_altsetting);

            for (int k = 0; k < ifp->num_altsetting; k++) {
                const struct libusb_interface_descriptor *as = &ifp->altsetting[k];
                if (as->bInterfaceClass != LIBUSB_CLASS_HID) continue;

                g15_log(stderr, G15_LOG_INFO, "Interface %i has %i Endpoints\n",
                        i, as->bNumEndpoints);
                usleep(50*1000);

                ret = libusb_kernel_driver_active(devh, i);
                if (ret == 1) {
                    g15_log(stderr, G15_LOG_INFO, "Kernel driver active, detaching\n");
                    ret = libusb_detach_kernel_driver(devh, i);
                    if (ret < 0) {
                        g15_log(stderr, G15_LOG_INFO,
                                "Could not detach kernel driver: %s\n",
                                libusb_strerror(ret));
                        libusb_free_config_descriptor(cfg);
                        libusb_close(devh);
                        return NULL;
                    }
                    g15_log(stderr, G15_LOG_INFO, "Success, detached the driver\n");
                }

                if (!shared_device) {
                    ret = libusb_set_configuration(devh, 1);
                    if (ret < 0) {
                        g15_log(stderr, G15_LOG_INFO,
                                "Error setting configuration: %s\n",
                                libusb_strerror(ret));
                        libusb_free_config_descriptor(cfg);
                        libusb_close(devh);
                        return NULL;
                    }
                }

                usleep(50*1000);
                int retries = 0;
                while ((ret = libusb_claim_interface(devh, i)) < 0 && retries < 10) {
                    usleep(50*1000);
                    retries++;
                    g15_log(stderr, G15_LOG_INFO, "Trying to claim interface\n");
                }

                if (ret < 0) {
                    g15_log(stderr, G15_LOG_INFO,
                            "Error claiming interface: %s\n", libusb_strerror(ret));
                    libusb_free_config_descriptor(cfg);
                    libusb_close(devh);
                    return NULL;
                }
                g15_claimed_interface = i;

                for (int l = 0; l < as->bNumEndpoints; l++) {
                    const struct libusb_endpoint_descriptor *ep = &as->endpoint[l];
                    g15_log(stderr, G15_LOG_INFO,
                            "Found %s endpoint %i with address 0x%X maxtransfersize=%i\n",
                            (0x80 & ep->bEndpointAddress) ? "\"Extra Keys\"" : "\"LCD\"",
                            ep->bEndpointAddress & 0x0f,
                            ep->bEndpointAddress, ep->wMaxPacketSize);

                    if (0x80 & ep->bEndpointAddress)
                        g15_keys_endpoint = ep->bEndpointAddress;
                    else
                        g15_lcd_endpoint = ep->bEndpointAddress;
                }
                break; /* use first matching HID altsetting */
            }
        }
        libusb_free_config_descriptor(cfg);
    }

    g15_log(stderr, G15_LOG_INFO, "Done opening the keyboard\n");
    usleep(500*1000);
    return devh;
}

static libusb_device_handle *findAndOpenDevice(libg15_devices_t handled_device,
                                                int device_index)
{
    libusb_device **devs;
    ssize_t cnt = libusb_get_device_list(usb_ctx, &devs);
    if (cnt < 0) return NULL;

    libusb_device_handle *devh = NULL;

    for (ssize_t d = 0; d < cnt; d++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(devs[d], &desc) < 0) continue;
        if (desc.idVendor  != handled_device.vendorid ||
            desc.idProduct != handled_device.productid) continue;

        found_devicetype = device_index;
        g15_log(stderr, G15_LOG_INFO, "Found %s, trying to open it\n",
                handled_device.name);

        devh = tryOpenDevice(devs[d]);
        if (devh) break;
    }

    libusb_free_device_list(devs, 1);
    return devh;
}

static libusb_device_handle *findAndOpen(unsigned int vendorid,
                                          unsigned int productid)
{
    for (int i = 0; g15_devices[i].name != NULL; i++) {
        if ((vendorid  == 0 || g15_devices[i].vendorid  == vendorid) &&
            (productid == 0 || g15_devices[i].productid == productid)) {
            g15_log(stderr, G15_LOG_INFO, "Trying to find %s\n",
                    g15_devices[i].name);
            keyboard_device = findAndOpenDevice(g15_devices[i], i);
            if (keyboard_device) break;
            g15_log(stderr, G15_LOG_INFO, "%s not found\n", g15_devices[i].name);
        } else {
            g15_log(stderr, G15_LOG_INFO, "%s skipped\n", g15_devices[i].name);
        }
    }
    return keyboard_device;
}

static libusb_device_handle *findAndOpenG15() {
    return findAndOpen(0, 0);
}

int re_initLibG15() {
    if (!usb_ctx) {
        int ret = libusb_init(&usb_ctx);
        if (ret < 0) return G15_ERROR_OPENING_USB_DEVICE;
    }

    keyboard_device = findAndOpenG15();
    if (!keyboard_device)
        return G15_ERROR_OPENING_USB_DEVICE;

    return G15_NO_ERROR;
}

int setupLibG15(unsigned int vendorId, unsigned int productId, unsigned int init_usb)
{
    if (init_usb) {
        int retval = initLibUsb();
        if (retval) return retval;
    } else {
        g15_log(stderr, G15_LOG_INFO, "Skipping libusb initialise\n");
        if (!usb_ctx) {
            int ret = libusb_init(&usb_ctx);
            if (ret < 0) return G15_ERROR_OPENING_USB_DEVICE;
        }
    }

    g15_log(stderr, G15_LOG_INFO, "%s\n", PACKAGE_STRING);

    g15NumberOfConnectedDevices();
    last_pressed_keys = -1;
    keyboard_device = findAndOpen(vendorId, productId);
    if (!keyboard_device)
        return G15_ERROR_OPENING_USB_DEVICE;

    pthread_mutex_init(&libusb_mutex, NULL);

    if (g15DeviceCapabilities() & G15_DEVICE_G13) {
        unsigned int pk = 0;
        getPressedKeys(&pk, 2000);
        g15_log(stderr, G15_LOG_INFO, "Initial keypress %u\n", pk);
    }

    if (g15DeviceCapabilities() & G15_DEVICE_G510) {
        g15_log(stderr, G15_LOG_INFO, "Sending G510 initialisation.\n");
        unsigned char usb_data[] = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                     0, 0, 0, 0, 0 };
        pthread_mutex_lock(&libusb_mutex);
        libusb_control_transfer(keyboard_device,
                LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
                9, 0x301, 1, usb_data, 19, 10000);
        unsigned char usb_data_2[] = { 0x09, 0x02, 0, 0, 0, 0, 0, 0 };
        libusb_control_transfer(keyboard_device,
                LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
                9, 0x309, 1, usb_data_2, 8, 10000);
        pthread_mutex_unlock(&libusb_mutex);
    }

    return G15_NO_ERROR;
}

int initLibG15() {
    return setupLibG15(0, 0, 1);
}

/* reset the keyboard, returning it to a known state */
int exitLibG15() {
    g15_keys_endpoint = 0;
    g15_lcd_endpoint  = 0;
    fprintf(stderr, "exitLibG15: called, claimed_interface=%d keyboard_device=%p\n",
            g15_claimed_interface, (void*)keyboard_device);
    if (keyboard_device) {
        if (g15_claimed_interface >= 0) {
            int r1 = libusb_release_interface(keyboard_device, g15_claimed_interface);
            fprintf(stderr, "exitLibG15: release_interface(%d) = %s\n",
                    g15_claimed_interface, libusb_strerror(r1));
            usleep(50*1000);
            int r2 = libusb_attach_kernel_driver(keyboard_device, g15_claimed_interface);
            fprintf(stderr, "exitLibG15: attach_kernel_driver(%d) = %s\n",
                    g15_claimed_interface, libusb_strerror(r2));
            g15_claimed_interface = -1;
        }
        libusb_close(keyboard_device);
        keyboard_device = NULL;
        pthread_mutex_destroy(&libusb_mutex);
        if (usb_ctx) {
            libusb_exit(usb_ctx);
            usb_ctx = NULL;
        }
        return G15_NO_ERROR;
    }
    return -1;
}


static void dumpPixmapIntoLCDFormat(unsigned char *lcd_buffer, unsigned char const *data)
{
/*

  For a set of bytes (A, B, C, etc.) the bits representing pixels will appear on the LCD like this:

	A0 B0 C0
	A1 B1 C1
	A2 B2 C2
	A3 B3 C3 ... and across for G15_LCD_WIDTH bytes
	A4 B4 C4
	A5 B5 C5
	A6 B6 C6
	A7 B7 C7

	A0
	A1  <- second 8-pixel-high row starts straight after the last byte on
	A2     the previous row
	A3
	A4
	A5
	A6
	A7
	A8

	A0
	...
	A0
	...
	A0
	...
	A0
	A1 <- only the first three bits are shown on the bottom row (the last three
	A2    pixels of the 43-pixel high display.)


*/

    unsigned int output_offset = G15_LCD_OFFSET;
    unsigned int base_offset = 0;
    unsigned int curr_row = 0;
    unsigned int curr_col = 0;

    /* Five 8-pixel rows + a little 3-pixel row.  This formula will calculate
       the minimum number of bytes required to hold a complete column.  (It
       basically divides by eight and rounds up the result to the nearest byte,
       but at compile time.
      */

#define G15_LCD_HEIGHT_IN_BYTES  ((G15_LCD_HEIGHT + ((8 - (G15_LCD_HEIGHT % 8)) % 8)) / 8)

    for (curr_row = 0; curr_row < G15_LCD_HEIGHT_IN_BYTES; ++curr_row)
    {
        for (curr_col = 0; curr_col < G15_LCD_WIDTH; ++curr_col)
        {
            unsigned int bit = curr_col % 8;
        /* Copy a 1x8 column of pixels across from the source image to the LCD buffer. */

            lcd_buffer[output_offset] =
            (((data[base_offset                        ] << bit) & 0x80) >> 7) |
            (((data[base_offset +  G15_LCD_WIDTH/8     ] << bit) & 0x80) >> 6) |
            (((data[base_offset + (G15_LCD_WIDTH/8 * 2)] << bit) & 0x80) >> 5) |
            (((data[base_offset + (G15_LCD_WIDTH/8 * 3)] << bit) & 0x80) >> 4) |
            (((data[base_offset + (G15_LCD_WIDTH/8 * 4)] << bit) & 0x80) >> 3) |
            (((data[base_offset + (G15_LCD_WIDTH/8 * 5)] << bit) & 0x80) >> 2) |
            (((data[base_offset + (G15_LCD_WIDTH/8 * 6)] << bit) & 0x80) >> 1) |
            (((data[base_offset + (G15_LCD_WIDTH/8 * 7)] << bit) & 0x80) >> 0);
            ++output_offset;
            if (bit == 7)
              base_offset++;
        }
    /* Jump down seven pixel-rows in the source image, since we've just
       done a row of eight pixels in one pass (and we counted one pixel-row
       while we were going, so now we skip the next seven pixel-rows.) */
    base_offset += G15_LCD_WIDTH - (G15_LCD_WIDTH / 8);
    }
}

int handle_usb_errors(const char *prefix, int ret) {
    switch (ret) {
        case LIBUSB_ERROR_TIMEOUT:
            return G15_ERROR_READING_USB_DEVICE;

        case LIBUSB_ERROR_OVERFLOW:
            g15_log(stderr, G15_LOG_INFO, "usb error: OVERFLOW — reducing speed\n");
            enospc_slowdown = 1;
            break;

        case LIBUSB_ERROR_NO_DEVICE:
        case LIBUSB_ERROR_IO:
        case LIBUSB_ERROR_INVALID_PARAM:
        case LIBUSB_ERROR_BUSY:
        case LIBUSB_ERROR_INTERRUPTED:
        case LIBUSB_ERROR_NOT_SUPPORTED:
            g15_log(stderr, G15_LOG_INFO, "usb error: %s %s (%i)\n",
                    prefix, libusb_strerror(ret), ret);
            break;

        case LIBUSB_ERROR_PIPE:
            g15_log(stderr, G15_LOG_INFO, "usb error: %s PIPE! clearing...\n", prefix);
            pthread_mutex_lock(&libusb_mutex);
            libusb_clear_halt(keyboard_device, 0x81);
            pthread_mutex_unlock(&libusb_mutex);
            break;

        default:
            g15_log(stderr, G15_LOG_INFO,
                    "Unknown usb error: %s !! (err is %i (%s))\n",
                    prefix, ret, libusb_strerror(ret));
            break;
    }
    return ret;
}

int writePixmapToLCD(unsigned char const *data)
{
    int ret = 0;
    int transfercount = 0;
    unsigned char lcd_buffer[G15_BUFFER_LEN];
    memset(lcd_buffer, 0, G15_LCD_OFFSET);

    dumpPixmapIntoLCDFormat(lcd_buffer, data);

    if (!(g15_devices[found_devicetype].caps & G15_LCD))
        return 0;

    /* the keyboard needs this magic byte */
    lcd_buffer[0] = 0x03;

    if (enospc_slowdown != 0) {
        /* Slow path: 32-byte chunks to reduce peak bus utilisation.
         * G15_BUFFER_LEN (992) divides evenly into 31 chunks of 32 bytes. */
        pthread_mutex_lock(&libusb_mutex);
        for (transfercount = 0; transfercount < G15_BUFFER_LEN / 32; transfercount++) {
            int actual = 0;
            ret = libusb_interrupt_transfer(keyboard_device, g15_lcd_endpoint,
                    lcd_buffer + (32 * transfercount), 32, &actual, 1000);
            if (ret != 0) {
                handle_usb_errors("LCDPixmap Slow Write", ret);
                pthread_mutex_unlock(&libusb_mutex);
                return G15_ERROR_WRITING_PIXMAP;
            }
            if (actual != 32) {
                g15_log(stderr, G15_LOG_INFO,
                        "LCDPixmap Slow Write: short transfer %d/32\n", actual);
                pthread_mutex_unlock(&libusb_mutex);
                return G15_ERROR_WRITING_PIXMAP;
            }
            usleep(100);
        }
        pthread_mutex_unlock(&libusb_mutex);
    } else {
        /* Fast path: single transfer. */
        int actual = 0;
        pthread_mutex_lock(&libusb_mutex);
        ret = libusb_interrupt_transfer(keyboard_device, g15_lcd_endpoint,
                lcd_buffer, G15_BUFFER_LEN, &actual, 1000);
        pthread_mutex_unlock(&libusb_mutex);
        if (ret != 0) {
            handle_usb_errors("LCDPixmap Write", ret);
            return G15_ERROR_WRITING_PIXMAP;
        }
        if (actual != G15_BUFFER_LEN) {
            g15_log(stderr, G15_LOG_INFO,
                    "LCDPixmap Write: short transfer %d/%d\n", actual, G15_BUFFER_LEN);
            return G15_ERROR_WRITING_PIXMAP;
        }
        usleep(100);
    }

    return 0;
}

int setLCDContrast(unsigned int level)
{
    int retval = 0;
    unsigned char usb_data[] = { 2, 32, 129, 0 };

    if (shared_device > 0)
        return G15_ERROR_UNSUPPORTED;

    switch (level) {
        case 1:  usb_data[3] = 22; break;
        case 2:  usb_data[3] = 26; break;
        default: usb_data[3] = 18; break;
    }
    pthread_mutex_lock(&libusb_mutex);
    retval = libusb_control_transfer(keyboard_device,
            LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
            9, 0x302, 0, usb_data, 4, 10000);
    pthread_mutex_unlock(&libusb_mutex);
    return retval;
}

int setLEDs(unsigned int leds)
{
    int retval = 0;

    pthread_mutex_lock(&libusb_mutex);

    if (g15DeviceCapabilities() & G15_DEVICE_G13) {
        unsigned char m_led_buf[5] = { 5, (unsigned char)leds, 0, 0, 0 };
        retval = libusb_control_transfer(keyboard_device,
                LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
                9, 0x305, 0, m_led_buf, 5, 10000);
    }
    else if (g15DeviceCapabilities() & G15_DEVICE_G110) {
        unsigned char m_led_buf[2] = { 3, (unsigned char)leds };
        retval = libusb_control_transfer(keyboard_device,
                LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
                9, 0x303, 1, m_led_buf, 2, 10000);
    }
    else if (g15DeviceCapabilities() & G15_DEVICE_G510) {
        /* M-key light mask is different on this model */
        int new_leds = 0;
        if (leds & 0x01) new_leds += 0x80;
        if (leds & 0x02) new_leds += 0x40;
        if (leds & 0x04) new_leds += 0x20;
        if (leds & 0x08) new_leds += 0x10;
        unsigned char m_led_buf[2] = { 4, (unsigned char)new_leds };
        retval = libusb_control_transfer(keyboard_device,
                LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
                9, 0x304, 1, m_led_buf, 2, 10000);
    }
    else {
        if (shared_device > 0) {
            pthread_mutex_unlock(&libusb_mutex);
            return G15_ERROR_UNSUPPORTED;
        }
        unsigned char m_led_buf[4] = { 2, 4, ~(unsigned char)leds, 0 };
        retval = libusb_control_transfer(keyboard_device,
                LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
                9, 0x302, 0, m_led_buf, 4, 10000);
    }

    pthread_mutex_unlock(&libusb_mutex);
    return retval;
}

int setLCDBrightness(unsigned int level)
{
    int retval = 0;
    unsigned char usb_data[] = { 2, 2, 0, 0 };

    if (shared_device > 0 || (g15DeviceCapabilities() & G15_DEVICE_COLOUR))
        return G15_ERROR_UNSUPPORTED;

    switch (level) {
        case 1:  usb_data[2] = 0x10; break;
        case 2:  usb_data[2] = 0x20; break;
        default: usb_data[2] = 0x00; break;
    }
    pthread_mutex_lock(&libusb_mutex);
    retval = libusb_control_transfer(keyboard_device,
            LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
            9, 0x302, 0, usb_data, 4, 10000);
    pthread_mutex_unlock(&libusb_mutex);
    return retval;
}

/* set the keyboard backlight. doesnt affect lcd backlight. 0==off,1==medium,2==high */
int setKBBrightness(unsigned int level)
{
    int retval = 0;
    unsigned char usb_data[] = { 2, 1, 0, 0 };

    if (shared_device > 0 || (g15DeviceCapabilities() & G15_DEVICE_COLOUR))
        return G15_ERROR_UNSUPPORTED;

    switch (level) {
        case 1:  usb_data[2] = 0x1; break;
        case 2:  usb_data[2] = 0x2; break;
        default: usb_data[2] = 0x0; break;
    }
    pthread_mutex_lock(&libusb_mutex);
    retval = libusb_control_transfer(keyboard_device,
            LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
            9, 0x302, 0, usb_data, 4, 10000);
    pthread_mutex_unlock(&libusb_mutex);
    return retval;
}

int setG510LEDColor(unsigned char r, unsigned char g, unsigned char b)
{
    if (!g15DeviceCapabilities() & G15_DEVICE_COLOUR)
        return G15_ERROR_UNSUPPORTED;

    int retval = 0;
    unsigned char usb_data[] = { 4, 0, 0, 0, 0 };

    usb_data[1] = r;
    usb_data[2] = g;
    usb_data[3] = b;

    pthread_mutex_lock(&libusb_mutex);

    if (g15DeviceCapabilities() & G15_DEVICE_G110) {
        usb_data[0] = 7;
        /* colour encoding for G110 */
        if (r == b) {
            usb_data[1] = 0x80;
            usb_data[4] = b >> 4;
        } else if (b > r) {
            usb_data[1] = 0xff - (0x80 * r) / b;
            usb_data[4] = b >> 4;
        } else {
            usb_data[1] = (0x80 * b) / r;
            usb_data[4] = r >> 4;
        }
        retval = libusb_control_transfer(keyboard_device,
                LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
                9, 0x307, 0, usb_data, 5, 10000);
    }
    else if (g15DeviceCapabilities() & G15_DEVICE_G13) {
        usb_data[0] = 5;
        retval = libusb_control_transfer(keyboard_device,
                LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
                9, 0x307, 0, usb_data, 4, 10000);
    }
    else {
        usb_data[0] = 5;
        retval = libusb_control_transfer(keyboard_device,
                LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
                9, 0x305, 1, usb_data, 4, 10000);
    }

    pthread_mutex_unlock(&libusb_mutex);
    return retval;
}

static unsigned char g15KeyToLogitechKeyCode(int key)
{
    /* first 12 G keys produce F1 - F12, thats 0x3a + key */
    if (key < 12)
        return 0x3a + key;
    /* the other keys produce Key '1' (above letters) + key, thats 0x1e + key */
    else
        return 0x1e + key - 12;
}

static void processKeyEventG13(unsigned int *pressed_keys, unsigned char *buffer)
{
    *pressed_keys = 0;

    if (buffer[0] == 0x01)
    {
        if (buffer[3]&0x01) *pressed_keys |= G15_KEY_G1;
        if (buffer[3]&0x02) *pressed_keys |= G15_KEY_G2;
        if (buffer[3]&0x04) *pressed_keys |= G15_KEY_G3;
        if (buffer[3]&0x08) *pressed_keys |= G15_KEY_G4;
        if (buffer[3]&0x10) *pressed_keys |= G15_KEY_G5;
        if (buffer[3]&0x20) *pressed_keys |= G15_KEY_G6;
        if (buffer[3]&0x40) *pressed_keys |= G15_KEY_G7;
        if (buffer[3]&0x80) *pressed_keys |= G15_KEY_G8;

        if (buffer[4]&0x01) *pressed_keys |= G15_KEY_G9;
        if (buffer[4]&0x02) *pressed_keys |= G15_KEY_G10;
        if (buffer[4]&0x04) *pressed_keys |= G15_KEY_G11;
        if (buffer[4]&0x08) *pressed_keys |= G15_KEY_G12;
        if (buffer[4]&0x10) *pressed_keys |= G15_KEY_G13;
        if (buffer[4]&0x20) *pressed_keys |= G15_KEY_G14;
        if (buffer[4]&0x40) *pressed_keys |= G15_KEY_G15;
        if (buffer[4]&0x80) *pressed_keys |= G15_KEY_G16;
        if (buffer[5]&0x01) *pressed_keys |= G15_KEY_G17;
        if (buffer[5]&0x02) *pressed_keys |= G15_KEY_G18;
        if (buffer[5]&0x80) *pressed_keys |= G15_KEY_LIGHT;

        if (buffer[6]&0x01) *pressed_keys |= G15_KEY_L1;
        if (buffer[6]&0x02) *pressed_keys |= G15_KEY_L2;
        if (buffer[6]&0x04) *pressed_keys |= G15_KEY_L3;
        if (buffer[6]&0x08) *pressed_keys |= G15_KEY_L4;
        if (buffer[6]&0x10) *pressed_keys |= G15_KEY_L5;

        if (buffer[6]&0x20) *pressed_keys |= G15_KEY_M1;
        if (buffer[6]&0x40) *pressed_keys |= G15_KEY_M2;
        if (buffer[6]&0x80) *pressed_keys |= G15_KEY_M3;
        if (buffer[7]&0x01) *pressed_keys |= G15_KEY_MR;
    }
}

static void processKeyEventG13Extended(unsigned int *pressed_keys, unsigned char *buffer)
{
    *pressed_keys = 0;

    if (buffer[0] == 0x01)
    {
        if (buffer[5]&0x04) { *pressed_keys |= G15_KEY_G19; *pressed_keys |= G15_EXTENDED_KEY; }
        if (buffer[5]&0x08) { *pressed_keys |= G15_KEY_G20; *pressed_keys |= G15_EXTENDED_KEY; }
        if (buffer[5]&0x10) { *pressed_keys |= G15_KEY_G21; *pressed_keys |= G15_EXTENDED_KEY; }
        if (buffer[5]&0x20) { *pressed_keys |= G15_KEY_G22; *pressed_keys |= G15_EXTENDED_KEY; }
        if (buffer[7]&0x02) { *pressed_keys |= G15_KEY_JOYBL; *pressed_keys |= G15_EXTENDED_KEY; }
        if (buffer[7]&0x04) { *pressed_keys |= G15_KEY_JOYBD; *pressed_keys |= G15_EXTENDED_KEY; }
        if (buffer[7]&0x08) { *pressed_keys |= G15_KEY_JOYBS; *pressed_keys |= G15_EXTENDED_KEY; }
    }

    /* Bytes 1 and 2 are the joystick positions */
    int jx = buffer[1];
    int jy = buffer[2];
    if (jx != joystick_x || jy != joystick_y) {
        joystick_x = jx;
        joystick_y = jy;
        *pressed_keys |= G15_JOY;
        *pressed_keys |= G15_EXTENDED_KEY;
    }
}

static void processG510AudioKeyEvent(unsigned int *pressed_keys, unsigned char *buffer)
{
    *pressed_keys = 0;
    if (buffer[0] == 0x03)
    {
        if (buffer[4]&0x20) { *pressed_keys |= G15_KEY_MUTE_OUTPUT; *pressed_keys |= G15_EXTENDED_KEY; }
        if (buffer[4]&0x40) { *pressed_keys |= G15_KEY_MUTE_INPUT;  *pressed_keys |= G15_EXTENDED_KEY; }
    }
}

static void processKeyEvent9Byte(unsigned int *pressed_keys, unsigned char *buffer)
{
    *pressed_keys = 0;

    g15_log(stderr,G15_LOG_INFO,"Keyboard: %x, %x, %x, %x, %x, %x, %x, %x, %x\n",
            buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],
            buffer[5],buffer[6],buffer[7],buffer[8]);

    if (buffer[0] == 0x02)
    {
        if (buffer[1]&0x01) *pressed_keys |= G15_KEY_G1;
        if (buffer[2]&0x02) *pressed_keys |= G15_KEY_G2;
        if (buffer[3]&0x04) *pressed_keys |= G15_KEY_G3;
        if (buffer[4]&0x08) *pressed_keys |= G15_KEY_G4;
        if (buffer[5]&0x10) *pressed_keys |= G15_KEY_G5;
        if (buffer[6]&0x20) *pressed_keys |= G15_KEY_G6;

        if (buffer[2]&0x01) *pressed_keys |= G15_KEY_G7;
        if (buffer[3]&0x02) *pressed_keys |= G15_KEY_G8;
        if (buffer[4]&0x04) *pressed_keys |= G15_KEY_G9;
        if (buffer[5]&0x08) *pressed_keys |= G15_KEY_G10;
        if (buffer[6]&0x10) *pressed_keys |= G15_KEY_G11;
        if (buffer[7]&0x20) *pressed_keys |= G15_KEY_G12;

        if (buffer[1]&0x04) *pressed_keys |= G15_KEY_G13;
        if (buffer[2]&0x08) *pressed_keys |= G15_KEY_G14;
        if (buffer[3]&0x10) *pressed_keys |= G15_KEY_G15;
        if (buffer[4]&0x20) *pressed_keys |= G15_KEY_G16;
        if (buffer[5]&0x40) *pressed_keys |= G15_KEY_G17;
        if (buffer[8]&0x40) *pressed_keys |= G15_KEY_G18;

        if (buffer[6]&0x01) *pressed_keys |= G15_KEY_M1;
        if (buffer[7]&0x02) *pressed_keys |= G15_KEY_M2;
        if (buffer[8]&0x04) *pressed_keys |= G15_KEY_M3;
        if (buffer[7]&0x40) *pressed_keys |= G15_KEY_MR;

        if (buffer[8]&0x80) *pressed_keys |= G15_KEY_L1;
        if (buffer[2]&0x80) *pressed_keys |= G15_KEY_L2;
        if (buffer[3]&0x80) *pressed_keys |= G15_KEY_L3;
        if (buffer[4]&0x80) *pressed_keys |= G15_KEY_L4;
        if (buffer[5]&0x80) *pressed_keys |= G15_KEY_L5;

        if (buffer[1]&0x80) *pressed_keys |= G15_KEY_LIGHT;
    }
}

static void processKeyEvent5Byte(unsigned int *pressed_keys, unsigned char *buffer)
{
    *pressed_keys = 0;

    g15_log(stderr,G15_LOG_INFO,"Keyboard: %x, %x, %x, %x, %x\n",
            buffer[0],buffer[1],buffer[2],buffer[3],buffer[4]);

    if (buffer[0] == 0x02)
    {
        if (buffer[1]&0x01) *pressed_keys |= G15_KEY_G1;
        if (buffer[1]&0x02) *pressed_keys |= G15_KEY_G2;
        if (buffer[1]&0x04) *pressed_keys |= G15_KEY_G3;
        if (buffer[1]&0x08) *pressed_keys |= G15_KEY_G4;
        if (buffer[1]&0x10) *pressed_keys |= G15_KEY_G5;
        if (buffer[1]&0x20) *pressed_keys |= G15_KEY_G6;
        if (buffer[1]&0x40) *pressed_keys |= G15_KEY_M1;
        if (buffer[1]&0x80) *pressed_keys |= G15_KEY_M2;
        if (buffer[2]&0x20) *pressed_keys |= G15_KEY_M3;
        if (buffer[2]&0x40) *pressed_keys |= G15_KEY_MR;
        if (buffer[2]&0x80) *pressed_keys |= G15_KEY_L1;
        if (buffer[2]&0x2)  *pressed_keys |= G15_KEY_L2;
        if (buffer[2]&0x4)  *pressed_keys |= G15_KEY_L3;
        if (buffer[2]&0x8)  *pressed_keys |= G15_KEY_L4;
        if (buffer[2]&0x10) *pressed_keys |= G15_KEY_L5;
        if (buffer[2]&0x1)  *pressed_keys |= G15_KEY_LIGHT;
    }

    /* G510 */
    if (buffer[0] == 0x03)
    {
        if (buffer[1]&0x01) *pressed_keys |= G15_KEY_G1;
        if (buffer[1]&0x02) *pressed_keys |= G15_KEY_G2;
        if (buffer[1]&0x04) *pressed_keys |= G15_KEY_G3;
        if (buffer[1]&0x08) *pressed_keys |= G15_KEY_G4;
        if (buffer[1]&0x10) *pressed_keys |= G15_KEY_G5;
        if (buffer[1]&0x20) *pressed_keys |= G15_KEY_G6;
        if (buffer[1]&0x40) *pressed_keys |= G15_KEY_G7;
        if (buffer[1]&0x80) *pressed_keys |= G15_KEY_G8;

        if (buffer[2]&0x01) *pressed_keys |= G15_KEY_G9;
        if (buffer[2]&0x02) *pressed_keys |= G15_KEY_G10;
        if (buffer[2]&0x04) *pressed_keys |= G15_KEY_G11;
        if (buffer[2]&0x08) *pressed_keys |= G15_KEY_G12;
        if (buffer[2]&0x10) *pressed_keys |= G15_KEY_G13;
        if (buffer[2]&0x20) *pressed_keys |= G15_KEY_G14;
        if (buffer[2]&0x40) *pressed_keys |= G15_KEY_G15;
        if (buffer[2]&0x80) *pressed_keys |= G15_KEY_G16;

        if (buffer[3]&0x01) *pressed_keys |= G15_KEY_G17;
        if (buffer[3]&0x02) *pressed_keys |= G15_KEY_G18;

        if (buffer[3]&0x10) *pressed_keys |= G15_KEY_M1;
        if (buffer[3]&0x20) *pressed_keys |= G15_KEY_M2;
        if (buffer[3]&0x40) *pressed_keys |= G15_KEY_M3;
        if (buffer[3]&0x80) *pressed_keys |= G15_KEY_MR;

        if (buffer[4]&0x1)  *pressed_keys |= G15_KEY_L1;
        if (buffer[4]&0x2)  *pressed_keys |= G15_KEY_L2;
        if (buffer[4]&0x4)  *pressed_keys |= G15_KEY_L3;
        if (buffer[4]&0x8)  *pressed_keys |= G15_KEY_L4;
        if (buffer[4]&0x10) *pressed_keys |= G15_KEY_L5;

        if (buffer[3]&0x8)  *pressed_keys |= G15_KEY_LIGHT;
    }
}

static void processKeyEvent4Byte(unsigned int *pressed_keys, unsigned char *buffer)
{
    *pressed_keys = 0;

    g15_log(stderr,G15_LOG_INFO,"Keyboard: %x, %x, %x, %x\n",
            buffer[0],buffer[1],buffer[2],buffer[3]);

    if (buffer[0] == 0x02)
    {
        if (buffer[1]&0x01) *pressed_keys |= G15_KEY_G1;
        if (buffer[1]&0x02) *pressed_keys |= G15_KEY_G2;
        if (buffer[1]&0x04) *pressed_keys |= G15_KEY_G3;
        if (buffer[1]&0x08) *pressed_keys |= G15_KEY_G4;
        if (buffer[1]&0x10) *pressed_keys |= G15_KEY_G5;
        if (buffer[1]&0x20) *pressed_keys |= G15_KEY_G6;
        if (buffer[1]&0x40) *pressed_keys |= G15_KEY_G7;
        if (buffer[1]&0x80) *pressed_keys |= G15_KEY_G8;

        if (buffer[2]&0x01) *pressed_keys |= G15_KEY_G9;
        if (buffer[2]&0x02) *pressed_keys |= G15_KEY_G10;
        if (buffer[2]&0x04) *pressed_keys |= G15_KEY_G11;
        if (buffer[2]&0x08) *pressed_keys |= G15_KEY_G12;

        if (buffer[2]&0x10) *pressed_keys |= G15_KEY_M1;
        if (buffer[2]&0x20) *pressed_keys |= G15_KEY_M2;
        if (buffer[2]&0x40) *pressed_keys |= G15_KEY_M3;
        if (buffer[2]&0x80) *pressed_keys |= G15_KEY_MR;

        if (buffer[3]&0x1)  *pressed_keys |= G15_KEY_LIGHT;
    }
}

static void processKeyEvent2Byte(unsigned int *pressed_keys, unsigned char *buffer)
{
    *pressed_keys = 0;

    g15_log(stderr, G15_LOG_WARN, "Keyboard: %x, %x\n", buffer[0], buffer[1]);

    if (buffer[0] == 0x02)
    {
        if (buffer[1] & 0x08) { *pressed_keys |= G15_KEY_PLAY;         *pressed_keys |= G15_EXTENDED_KEY; }
        if (buffer[1] & 0x04) { *pressed_keys |= G15_KEY_STOP;         *pressed_keys |= G15_EXTENDED_KEY; }
        if (buffer[1] & 0x02) { *pressed_keys |= G15_KEY_PREV;         *pressed_keys |= G15_EXTENDED_KEY; }
        if (buffer[1] & 0x01) { *pressed_keys |= G15_KEY_NEXT;         *pressed_keys |= G15_EXTENDED_KEY; }
        if (buffer[1] & 0x10) { *pressed_keys |= G15_KEY_MUTE;         *pressed_keys |= G15_EXTENDED_KEY; }
        if (buffer[1] & 0x20) { *pressed_keys |= G15_KEY_RAISE_VOLUME; *pressed_keys |= G15_EXTENDED_KEY; }
        if (buffer[1] & 0x40) { *pressed_keys |= G15_KEY_LOWER_VOLUME; *pressed_keys |= G15_EXTENDED_KEY; }
    }
}


int getPressedKeys(unsigned int *pressed_keys, unsigned int timeout)
{
    if (last_pressed_keys > -1) {
        *pressed_keys = last_pressed_keys;
        last_pressed_keys = -1;
        return G15_NO_ERROR;
    }

    unsigned char buffer[G15_KEY_READ_LENGTH];
    int actual = 0;
    int ret    = 0;
    int caps   = g15DeviceCapabilities();
    int read_length = G15_KEY_READ_LENGTH;

    if (caps & G15_DEVICE_G13)
        read_length = G13_KEY_READ_LENGTH;
    if (caps & G15_DEVICE_G510)
        read_length = G13_KEY_READ_LENGTH;

    memset(buffer, 0, read_length);

    /* Serialize with writePixmapToLCD: both use keyboard_device on the same
     * USB interface. Without this lock, concurrent LCD writes and key reads
     * cause a segfault in libusb internals (observed on G510 at high I/O).
     * Callers must pass a short timeout (0 = non-blocking) so this lock
     * is held for at most ~1ms, keeping the LCD thread unblocked. */
    pthread_mutex_lock(&libusb_mutex);
    ret = libusb_interrupt_transfer(keyboard_device, g15_keys_endpoint,
                                    buffer, read_length, &actual, timeout);
    pthread_mutex_unlock(&libusb_mutex);

    if (libg15_debugging_enabled == G15_LOG_INFO) {
        g15_log(stderr, G15_LOG_INFO, "rl: %d ret: %d actual: %d buf[0]: %x\n",
                read_length, ret, actual, buffer[0]);
        if (ret == 0) {
            for (int i = 0; i < actual; i++)
                g15_log(stderr, G15_LOG_INFO, "    %x\n", buffer[i]);
        }
    }

    if (caps & G15_DEVICE_G13) {
        /* G13 sometimes times out but still fills the buffer with key data */
        if (ret == LIBUSB_ERROR_TIMEOUT && buffer[0] == 1)
            ret = 0;
        else if (ret != 0)
            return handle_usb_errors("Keyboard Read", ret);
    } else {
        if (ret != 0)
            return handle_usb_errors("Keyboard Read", ret);
        if (actual > 0 && buffer[0] == 1)
            return G15_ERROR_TRY_AGAIN;
    }

    if ((caps & G15_DEVICE_G13) && buffer[0] == 0x01) {
        /* backlight state is encoded in bit 7 of byte 5 */
        if (buffer[5] & 0x80) {
            if (light_state == 0)
                light_state = 1;
            buffer[5] &= ~(0x80);
        } else {
            if (light_state == 1)
                light_state = 0;
        }
        buffer[7] &= ~(0x80);
        processKeyEventG13(pressed_keys, buffer);

        unsigned int pressed_ext_keys = 0;
        processKeyEventG13Extended(&pressed_ext_keys, buffer);
        if (pressed_ext_keys > 0) {
            last_pressed_keys = *pressed_keys;
            *pressed_keys = pressed_ext_keys;
        }
        return G15_NO_ERROR;
    }

    switch (actual) {
        case 4:
            processKeyEvent4Byte(pressed_keys, buffer);
            return G15_NO_ERROR;
        case 5:
            processKeyEvent5Byte(pressed_keys, buffer);
            if (caps & G15_DEVICE_G510) {
                unsigned int pressed_ext_keys = 0;
                processG510AudioKeyEvent(&pressed_ext_keys, buffer);
                if (pressed_ext_keys > 0) {
                    last_pressed_keys = *pressed_keys;
                    *pressed_keys = pressed_ext_keys;
                }
            }
            return G15_NO_ERROR;
        case 9:
            processKeyEvent9Byte(pressed_keys, buffer);
            return G15_NO_ERROR;
        case 2:
            if (caps & G15_DEVICE_G510) {
                processKeyEvent2Byte(pressed_keys, buffer);
                return G15_NO_ERROR;
            }
            /* fallthrough */
        default:
            return G15_ERROR_READING_USB_DEVICE;
    }
}
