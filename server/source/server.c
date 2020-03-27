/**
 * server.c
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <ogcsys.h>
#include <gccore.h>
#include <network.h>
#include <debug.h>
#include <errno.h>

static void *initialize();
static int net_setup();

// Address information
static struct sockaddr_in client = {0};
static struct sockaddr_in server = {0};

// File descriptors
static int csock = -1;
static int dsock = -1;

typedef struct ATTRIBUTE_PACKED padstat {
    u16 buttonsheld;
    s8  stick1X, stick1Y, stick2X, stick2Y;
} padstat;

// Be sure to call PAD_Init() first
static void get_pad_data(padstat *in, int pad)
{
    PAD_ScanPads();

    in->buttonsheld = PAD_ButtonsHeld(pad);
    in->stick1X     = (s8) PAD_StickX(pad);
    in->stick1Y     = (s8) PAD_StickY(pad);
    in->stick2X     = (s8) PAD_SubStickX(pad);
    in->stick2Y     = (s8) PAD_SubStickY(pad);
}

int main(int argc, char *argv[])
{
    padstat pad1 = {0};
    padstat pad_tmp = {0};

    initialize();

    if (net_setup() != 0) {
        return 1;
    }

    // Now we've trapped our client, start sending our payload
    while (1) {
        get_pad_data(&pad_tmp, 0);

        // Pad data are different
        if (memcmp(&pad1, &pad_tmp, sizeof(padstat)) != 0) {
            // Copy it over
            memcpy(&pad1, &pad_tmp, sizeof(padstat));
            // Fire
            net_send(csock, &pad1, sizeof(padstat), 0);
        }

        if (SYS_ResetButtonDown())
            exit(0);
    }

    net_close(csock);

    return 0;
}

static int net_setup()
{
    u32 clientlen;
    char localip[16] = {0};
    char gateway[16] = {0};
    char netmask[16] = {0};
    int bind = 0;

    // Configure the network interface
    bind = if_config(localip, netmask, gateway, TRUE, 10);

    if (bind >= 0) {
        printf("Network configured, ip: %s, gw: %s, mask %s\n", localip, gateway, netmask);
        VIDEO_WaitVSync();
    } else {
        fprintf(stderr, "Network configuration failed\n");
        return 1;
    }

    clientlen = sizeof(client);
    csock = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (csock == INVALID_SOCKET) {
        fprintf(stderr, "Failed to create socket\n");
        return 1;
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(301);
    server.sin_addr.s_addr = INADDR_ANY;

    bind = net_bind(csock, (struct sockaddr *) &server, sizeof(server));

    if (bind) {
        fprintf(stderr, "Error %d binding socket\n", bind);
        return 1;
    }

    if ((bind = net_listen(csock, 5))) {
        fprintf(stderr, "Error %d listening\n", bind);
        return 1;
    }

    // Note that we are throwing away the reference to the original
    // socket here, since it no longer matters.
    csock = net_accept(csock, (struct sockaddr *) &client, &clientlen);

    if (csock < 0) {
        fprintf(stderr, "Error connecting socket %d\n", csock);
        return 1;
    }

    // return execution to main
    return 0;
}

void *initialize()
{
    void *framebuffer = NULL;
    GXRModeObj *rmode = NULL;

    VIDEO_Init();
    PAD_Init();

    rmode = VIDEO_GetPreferredMode(NULL);
    framebuffer = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    console_init(framebuffer, 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);

    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(framebuffer);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();

    if (rmode->viTVMode & VI_NON_INTERLACE) {
        VIDEO_WaitVSync();
    }

    return framebuffer;
}
