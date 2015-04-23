/**
 * server.c
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <ogcsys.h>
#include <gccore.h>
#include <network.h>
#include <debug.h>
#include <errno.h>
#include <wiiuse/wpad.h>

static void *xfb = NULL;
static GXRModeObj *rmode = NULL;

static s32 sock, csock;

void *initialise();
static int _net_init(void);

/*struct vec3w_t {
    uint32_t x, y, z;
} vec3w_t;*/

#define _w_round(x) ((x)>=0?(long)((x)+0.5):(long)((x)-0.5))

typedef struct vec3sw_t {
    int32_t x, y, z;
} vec3sw_t;

struct _wpad_pack {
    int32_t buttonsheld;
    struct vec3sw_t accel;
    struct vec3sw_t orient;
    //struct vec3w_t shake; wait
} ATTRIBUTE_PACKED;


static void _evcb(int chan, const WPADData *data) {}

static void _get_pad_data(struct _wpad_pack *in, int pad)
{
    in->buttonsheld = WPAD_ButtonsHeld(pad);

    struct gforce_t force;
    WPAD_GForce(pad, &force);
    in->accel.x = _w_round(fabs(force.x));
    in->accel.y = _w_round(fabs(force.y));
    in->accel.z = _w_round(fabs(force.z));
    printf("\r %f]%f]%f     ", force.x, force.y, force.z);
    struct orient_t orient;
    WPAD_Orientation(pad, &orient);
    in->orient.x = (int)orient.roll;
    in->orient.y = (int)orient.pitch;
    in->orient.z = (int)orient.yaw;
}

int main(int argc, char **argv)
{
    //struct _padstat_pack pad1 = {0};
    //struct _padstat_pack pad_tmp = {0};
    u32 type; // shit nobody cares about
    struct _wpad_pack pad1 = {0};
    struct _wpad_pack pad_tmp = {0};

    xfb = initialise();

    if (_net_init() != 0) return 1;


    printf("\n\nSync up a wiimote now\n\n");
    // Wait for the first wiimote to sync up

    // Now we've trapped our client, start sending our payload
    while (1) {
        WPAD_ReadPending(WPAD_CHAN_ALL, _evcb);
        WPAD_ScanPads();
        s32 res = WPAD_Probe(0, &type);
        if (res == WPAD_ERR_NONE) {
            //showWiiMoteAccelerometer();
            _get_pad_data(&pad_tmp, 0);

            //net_send(csock, &pad_tmp, sizeof(struct _wpad_pack), 0);
            // Pad data are different
            if (memcmp(&pad1, &pad_tmp, sizeof(struct _wpad_pack)) != 0) {
                // Copy it over
                memcpy(&pad1, &pad_tmp, sizeof(struct _wpad_pack));
                // Fire
                net_send(csock, &pad1, sizeof(struct _wpad_pack), 0);
            }

            if (SYS_ResetButtonDown())
                exit(0);

            WPAD_SetIdleTimeout(300);
        }
    }

    net_close(csock);

    return 0;
}

static int _net_init(void)
{
    struct sockaddr_in client;
    struct sockaddr_in server;

    char localip[16] = {0};
    char gateway[16] = {0};
    char netmask[16] = {0};
    int bind = 0;

    u32 clientlen;

    // Configure the network interface
    bind = if_config (localip, netmask, gateway, TRUE);
    if (bind >= 0) {
        printf("network configured, ip: %s, gw: %s, mask %s\n", localip, gateway, netmask);
        VIDEO_WaitVSync();
    } else {
        printf("network configuration failed!\n");
        return 1;
    }

    clientlen = sizeof(client);
    sock = net_socket (AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock == INVALID_SOCKET) {
        printf("Cannot create a socket!\n");
        return 1;
    }

    memset (&server, 0, sizeof (server));
    memset (&client, 0, sizeof (client));

    server.sin_family = AF_INET;
    server.sin_port = htons(301);
    server.sin_addr.s_addr = INADDR_ANY;
    bind = net_bind (sock, (struct sockaddr *) &server, sizeof(server));
    if (bind) {
        printf("Error %d binding socket!\n", bind);
        return 1;
    }
    if ((bind = net_listen(sock, 5))) {
        printf("Error %d listening!\n", bind);
        return 1;
    }
    csock = net_accept(sock, (struct sockaddr *) &client, &clientlen);
    if (csock < 0) {
        printf("Error connecting socket %d!\n", csock);
        return 1;
    }

    // return execution to main
    return 0;
}

void *initialise()
{
    void *framebuffer;

    VIDEO_Init();
    PAD_Init();
    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR); // >_>

    rmode = VIDEO_GetPreferredMode(NULL);
    framebuffer = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    console_init(framebuffer,20,20,rmode->fbWidth,rmode->xfbHeight,rmode->fbWidth*VI_DISPLAY_PIX_SZ);

    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(framebuffer);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();

    return framebuffer;
}
