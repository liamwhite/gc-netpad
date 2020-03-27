/**
 * client.c
 *
 * Copyright (C) 2014-2020 by Liam P. White
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
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>

// Virtual stick radius used by uinput.
static const int STICK_RADIUS = 128;

// Struct holding all data required for a complete controller stat
typedef struct __attribute__((packed)) padstat {
    uint16_t buttonsheld;
    int8_t   stick1X, stick1Y, stick2X, stick2Y;
} padstat;

static int net_init(const char *wii);
static void pad_data_to_uinput(const padstat *in);
static void init_uinput();

// Address information
static struct sockaddr_in client = {0};
static struct sockaddr_in server = {0};

// File descriptors
static int csock = -1;
static int dsock = -1;
static int uinput_fd = -1;

int main(int argc, char *argv[])
{
    padstat pad = {0};

    if (argc != 2) {
        printf("Usage: %s <Wii IP>\n", argv[0]);
        return 1;
    }

    if (net_init(argv[1]) != 0) {
        return 1;
    }

    printf("Net initialized\n");

    init_uinput();

    // Receieve data
    while (1) {
        recv(csock, &pad, sizeof(padstat), 0);
        pad.buttonsheld = bswap_16(pad.buttonsheld);
        pad_data_to_uinput(&pad);
    }
}

static int net_init(const char *wii)
{
    struct addrinfo *info = NULL;

    if (getaddrinfo(wii, NULL, NULL, &info) != 0) {
        perror("getaddrinfo");
        return -1;
    }

    // Open communication socket over TCP
    csock = socket(AF_INET, SOCK_STREAM, 6);
    if (csock == -1) {
        perror("socket");
        return -1;
    }

    // Connect socket
    server.sin_family = AF_INET;
    server.sin_port = htons(301);
    server.sin_addr = ((struct sockaddr_in *)info->ai_addr)->sin_addr;

    freeaddrinfo(info);

    return connect(csock, (struct sockaddr *) &server, sizeof(struct sockaddr_in));
}

// Shamelessly copied from libdrc.

static void init_uinput(void)
{
    // Try to open the uinput file descriptor.
    const char *uinput_filename[] = {
        "/dev/uinput",
        "/dev/input/uinput",
        "/dev/misc/uinput"
    };

    for (size_t i = 0; i < 3; ++i) {
        if ((uinput_fd = open(uinput_filename[i], O_RDWR)) != -1) {
            break;
        }
    }

    if (uinput_fd < 0) {
        perror("uinput feeder");
        exit(-1);
    }

    // Create the virtual device.
    struct uinput_user_dev dev = {0};
    strncpy(dev.name, "NetGamecube Controller", sizeof(dev.name));
    dev.id.bustype = BUS_VIRTUAL;

    // Enable joysticks: 4 axes (2 for each stick).
    for (int i = 0; i < 4; ++i) {
        dev.absmin[i] = -STICK_RADIUS;
        dev.absmax[i] = STICK_RADIUS;
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
}

static void pad_data_to_uinput(const padstat *in)
{
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
        evt[i].value = sticks[i - 16]; /*((int)(sticks[i - 16] * STICK_RADIUS));*/
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
