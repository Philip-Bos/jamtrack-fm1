/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Philip Bos */
/* JAMTRACK FM-1: the app's half of the contract with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "jt_dsp.h"

#define APP_NAME "JAMTRACK"
#define APP_TAGLINE "backing band"
#define APP_SPLASH_RGB RGB(255, 176, 64)
#define APP_PALETTE 1u                   /* AMBER: fm1-base's own screens */
#define APP_BLOCK 256u                   /* 5.8 ms at 44.1 kHz */
#define APP_CANVAS_PX (240u * 80u)       /* the screen is drawn in bands of 80 rows at most */
#define FM1B_UAC_DEEP 1                  /* the USB audio input keeps a half more in hand */
#define FM1B_UAC_RING_POOL 1
#define APP_TOUCH                        /* velocity from the keys (lib/touch.h) */

/* the store: the app's objects and the panel's calibration (the platform's). Flash 0xD6000..0xDFFFF and the settings
 * pair at 0xFC000; the user kit's samples have 0x97000..0xC6FFF (kit.c). An object's number is in its record: keep
 * the numbering from one release to the next */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_SESSION, OBJ_JAMS0, OBJ_JAMS1, OBJ_KITDIR, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDE000u, 0xD8000u, 0xDA000u, 0xDC000u, 0xD6000u}

/* backup and restore over USB (fm1-base backup.c): settings, the jams and the session. The user kit's samples are
 * not in it (the companion keeps them as WAV files) */
#define APP_BACKUP_OBJS {OBJ_SETTINGS, OBJ_JAMS0, OBJ_JAMS1, OBJ_SESSION}
#define APP_BACKUP_MAGIC 0x314D544Au     /* "JTM1" */
#define APP_BACKUP_VERSION 1u
#define APP_BACKUP_WHAT "replaces jams and settings"
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);

/* the contract: the platform calls these */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

#define APP_REDRAW                       /* the mirror lost a part: everything again */
void app_redraw(void);
#define APP_USB_CMD                      /* the companion app's commands (proto.c) */
void app_usb_cmd(const uint8_t *d, uint32_t n);
#define APP_SPLASH
void app_splash(void);

#define APP_HOST_EXPECT                  /* the host simulator's checks (the tests directory) */
int app_expect(const char *what, int *got);
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
