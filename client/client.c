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

// Virtual stick radius used by uinput.
static const int kStickRadius = 128;

typedef struct vec3w_t {
    int32_t x, y, z;
} vec3w_t;

// Struct holding all data required for a complete controller stat
struct _padstat_pack {
    uint32_t buttonsheld; //, buttonsdown, buttonsup;

    struct vec3w_t accel;
    struct vec3w_t orient;
    //char stick1X, stick1Y, stick2X, stick2Y;
} __attribute__((packed));

// Swap the bytes of the shorts to make up for the different endian-ness of the PPC vs Intel
static inline void _endian_byteswap(struct _padstat_pack *in) {
    in->buttonsheld = bswap_32(in->buttonsheld);
    in->accel.x = bswap_32(in->accel.x);
    in->accel.y = bswap_32(in->accel.y);
    in->accel.z = bswap_32(in->accel.z);

    // FFS UINPUT GO FAK URSELF
    if (in->accel.x == 1) in->accel.x = 0;
    if (in->accel.y == 1) in->accel.y = 0;
    if (in->accel.z == 1) in->accel.z = 0;

    in->orient.x = bswap_32(in->orient.x);
    in->orient.y = bswap_32(in->orient.y);
    in->orient.z = bswap_32(in->orient.z);
}

static int _net_init(char const* wii);
static void _pad_data_to_uinput(struct _padstat_pack const *in);

// Socket descriptor
static int desc;

int main(int argc, char* argv[])
{
    struct _padstat_pack pad = {0};

    if (argc != 2) {
        printf("Usage: %s <Wii IP>\n", argv[0]);
        return 1;
    }

    if (_net_init(argv[1]) != 0) return 1;

    printf("Net initialized\n\n");

    // Receieve data
    while (1) {
        recv(desc, &pad, sizeof(struct _padstat_pack), 0);
        _endian_byteswap(&pad);
        printf("\r%d]%d]%d          ", pad.accel.x, pad.accel.y, pad.accel.z);
        fflush(stdout);
        _pad_data_to_uinput(&pad);
    }
}

static int _net_init(char const* wii)
{
    struct sockaddr_in sock = {0};
    struct addrinfo *info   = NULL;

    if (getaddrinfo(wii, NULL, NULL, &info) != 0) {
        printf("Failed to resolve %s\n", wii);
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
    sock.sin_port = htons(301);
    sock.sin_addr = ((struct sockaddr_in *)info->ai_addr)->sin_addr;

    freeaddrinfo(info);

    return connect(desc, (struct sockaddr *) &sock, sizeof(struct sockaddr_in));
}

// Shamelessly copied from libdrc.

static int _init_uinput(void)
{
    // Try to open the uinput file descriptor.
    const char* uinput_filename[] = {
        "/dev/uinput",
        "/dev/input/uinput",
        "/dev/misc/uinput"
    };

    int uinput_fd = -1;

    for (size_t i = 0; i < 3; ++i) {
        if ((uinput_fd = open(uinput_filename[i], O_RDWR)) != -1) {
            break;
        }
    }

    if (uinput_fd < 0) {
        return -1;
    }

    // Create the virtual device.
    struct uinput_user_dev dev = {0};
    strncpy(dev.name, "NetWii Controller", sizeof(dev.name));
    dev.id.bustype = BUS_VIRTUAL;

    // Enable joysticks: 6 axes.
    for (int i = 0; i < 6; ++i) {
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

    // 16 button events + 1 report event.
    struct input_event evt[16 + 6 + 1];
    memset(&evt, 0, sizeof(evt));

    // Read the 16 buttons and initialize their event object.
    for (int i = 0; i < 16; ++i) {
        evt[i].type = EV_KEY;
        evt[i].code = BTN_JOYSTICK + i;
        evt[i].value = !!(in->buttonsheld & (1 << i));
        memcpy(&evt[i].time, &tv, sizeof(tv));
    }


    // Read the 6 axis values.
    short sticks[] = {
        in->orient.x, in->orient.y, in->orient.z, 
        in->accel.x, in->accel.y, in->accel.z,
    };

    for (int i = 16; i < 16 + 6; ++i) {
        evt[i].type = EV_ABS;
        evt[i].code = i - 16;
        evt[i].value = sticks[i - 16]; /*((int)(sticks[i - 16] * kStickRadius));*/
        memcpy(&evt[i].time, &tv, sizeof(tv));
    }


    // Report event.
    evt[22].type = EV_SYN;
    evt[22].code = SYN_REPORT;
    memcpy(&evt[22].time, &tv, sizeof(tv));

    // Send the events to uinput.
    for (size_t i = 0; i < 23; ++i) {
        if (write(uinput_fd, &evt[i], sizeof(evt[i])) != sizeof(evt[i])) {
            perror("write failed - sending an event to uinput");
        }
    }
}
