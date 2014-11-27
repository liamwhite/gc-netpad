/**
 * client.c
 *
 * Copyright (C) 2014 by Liam P. White
 *
 * This file is part of GC-NetPad.
 * 
 * GC-NetPad is free software: you can redistribute 
 * it and/or modify it under the terms of the GNU General Public 
 * License as published by the Free Software Foundation, either 
 * version 3 of the License, or (at your option) any later version.
 * 
 * GC-NetPad is distributed in the hope that it will 
 * be useful, but WITHOUT ANY WARRANTY; without even the implied warranty 
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GC-NetPad.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <arpa/inet.h>
#include <byteswap.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

// Virtual stick radius used by uinput.
static const int kStickRadius = 128;

// Struct holding all data required for a complete controller stat
struct _padstat_pack {
    short buttonsheld; //, buttonsdown, buttonsup;
    char stick1X, stick1Y, stick2X, stick2Y;
} __attribute__((packed));

// Swap the bytes of the shorts to make up for the different endian-ness of the PPC vs Intel
static inline void _endian_byteswap(struct _padstat_pack *in) {
    in->buttonsheld = bswap_16(in->buttonsheld);
    //in->buttonsdown = bswap_16(in->buttonsdown);
    //in->buttonsup   = bswap_16(in->buttonsup);
}

static int _net_init(void);
static void _pad_data_to_uinput(struct _padstat_pack const *in);

// Socket descriptor
static int desc;

int main(void)
{
    struct _padstat_pack pad = {0};

    if (_net_init() != 0) return 1;

    printf("Net initialized\n");

    // Receieve data
    while (1) {
        recv(desc, &pad, sizeof(struct _padstat_pack), 0);
        _endian_byteswap(&pad);
        _pad_data_to_uinput(&pad);
    }
}

static int _net_init(void)
{
    struct sockaddr_in sock = {0};

    char const *wii = getenv("WII");
    if (!wii || *wii == '\0') {
        printf("Failed to get Wii's IP address from shell env WII");
        return -1;
    }

    // Open socket for TCP communication
    desc = socket(AF_INET, SOCK_STREAM, 6);

    if (desc == -1) {
    
        printf("Failed to open socket\n");
        return -1;
    }

    // Connect socket
    sock.sin_family = AF_INET;
    sock.sin_port = htons(80);
    inet_pton(AF_INET, wii, &sock.sin_addr);

    return connect(desc, (struct sockaddr *) &sock, sizeof(struct sockaddr_in));
}

// Shamelessly copied from libdrc.

static int _init_uinput(void)
{
    // Try to open the uinput file descriptor.
    /*const char* uinput_filename[] = {
        "/dev/uinput",
        "/dev/input/uinput",
        "/dev/misc/uinput"
    };

    int uinput_fd;
    for (size_t i = 0; i < 3; ++i) {
        if ((uinput_fd = open(uinput_filename[i], O_RDWR)) >= 0) {
            break;
        }
    }*/
    int uinput_fd = open("/dev/uinput", O_RDWR);

    if (uinput_fd < 0) {
        return -1;
    }

    // Create the virtual device.
    struct uinput_user_dev dev = {0};
    strncpy(dev.name, "NetGamecube Controller", sizeof(dev.name));
    dev.id.bustype = BUS_VIRTUAL;

    // Enable joysticks: 4 axes (2 for each stick).
    for (int i = 0; i < 4; ++i) {
        dev.absmin[i] = -kStickRadius;
        dev.absmax[i] = kStickRadius;
        ioctl(uinput_fd, UI_SET_ABSBIT, i);
    }

    // Enable 16 buttons (we only use 12 of these, but it makes things easier).
    for (int i = 0; i < 16; ++i) {
        ioctl(uinput_fd, UI_SET_KEYBIT, BTN_JOYSTICK + i);
    }

    // Set the absolute and key bits.
    ioctl(uinput_fd, UI_SET_EVBIT, EV_ABS);
    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);

    // Create the device.
    if (write(uinput_fd, &dev, sizeof (dev)) != sizeof (dev)) {
        perror("write failed - creating the device");
    }

    ioctl(uinput_fd, UI_DEV_CREATE);

    return uinput_fd;
}

static void _pad_data_to_uinput(struct _padstat_pack const *in)
{
    static _Bool initialized = 0;
    static int uinput_fd = -1;
    if (!initialized) {
        initialized = 1;
        uinput_fd = _init_uinput();
        if (uinput_fd == -1) {
            perror("uinput feeder");
        }
    }

    if (uinput_fd == -1) {
        return;
    }

    // Get the current timestamp
    struct timeval tv;
    gettimeofday(&tv, NULL);

    // 16 button events + 4 axis events + 1 report event.
    struct input_event evt[16 + 4 + 1];
    memset(&evt, 0, sizeof(evt));

    // Read the 16 buttons and initialize their event object.
    for (int i = 0; i < 16; ++i) {
        evt[i].type = EV_KEY;
        evt[i].code = BTN_JOYSTICK + i;
        evt[i].value = !!(in->buttonsheld & (1 << i));
        memcpy(&evt[i].time, &tv, sizeof(tv));
    }

    // Read the 4 axis values.
    short sticks[] = {
        in->stick1X, in->stick1Y,
        in->stick2X, in->stick2Y
    };

    for (int i = 16; i < 16 + 4; ++i) {
        evt[i].type = EV_ABS;
        evt[i].code = i - 16;
        evt[i].value = sticks[i - 16]; /*((int)(sticks[i - 16] * kStickRadius));*/
        memcpy(&evt[i].time, &tv, sizeof(tv));
    }

    // Report event.
    evt[20].type = EV_SYN;
    evt[20].code = SYN_REPORT;
    memcpy(&evt[20].time, &tv, sizeof(tv));

    // Send the events to uinput.
    for (size_t i = 0; i < 21; ++i) {
        if (write(uinput_fd, &evt[i], sizeof(evt[i])) != sizeof(evt[i])) {
            perror("write failed - sending an event to uinput");
        }
    }
}
