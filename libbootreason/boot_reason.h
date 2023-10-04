#ifndef _LIBRBRESN_H_
#define _LIBRBRESN_H_

/*
 * ---------------------------------------------------------------------------
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * ---------------------------------------------------------------------------
 */

struct poweroff_reason {
    const char *reason_char;
    unsigned int reason;
};

#define BOOT_REASON_UNKNOWN                    ( -1 )

#define BOOT_REASON_NORMAL                     (0x00)
#define BOOT_REASON_RECOVERY                   (0x01)
#define BOOT_REASON_BOOTLOADER                 (0x02)
#define BOOT_REASON_RTC                        (0x03)
#define BOOT_REASON_DM_VERITY_DEVICE_CORRUPTED (0x04)
#define BOOT_REASON_DM_VERITY_ENFORCING        (0x05)
#define BOOT_REASON_KEYS_CLEAR                 (0x06)
#define BOOT_REASON_PANIC                      (0x07)
#define BOOT_REASON_WATCHDOG_BARK              (0x08)

static struct poweroff_reason reasons[] = {
    {"normal",                     BOOT_REASON_NORMAL},
    {"recovery",                   BOOT_REASON_RECOVERY},
    {"bootloader",                 BOOT_REASON_BOOTLOADER},
    {"rtc",                        BOOT_REASON_RTC},
    {"dm-verity device corrupted", BOOT_REASON_DM_VERITY_DEVICE_CORRUPTED},
    {"dm-verity enforcing",        BOOT_REASON_DM_VERITY_ENFORCING},
    {"keys clear",                 BOOT_REASON_KEYS_CLEAR},
    {"panic",                      BOOT_REASON_PANIC},
    {"watchdog bark",              BOOT_REASON_WATCHDOG_BARK},
};

/*
 * int boot_getReason() returns the reason for the cause of last reboot
 * Return values should be interpreted as follows:
 *      BOOT_REASON_NORMAL                      : last reboot is normal/system reboot
 *      BOOT_REASON_RECOVERY                    : last reboot is due to recovery
 *      BOOT_REASON_BOOTLOADER                  : last reboot is due to bootloader
 *      BOOT_REASON_RTC                         : last reboot is due to rtc
 *      BOOT_REASON_DM_VERITY_DEVICE_CORRUPTED  : last reboot is due to dm-verity device corrupted
 *      BOOT_REASON_DM_VERITY_ENFORCING         : last reboot is due to dm-verity enforcing
 *      BOOT_REASON_KEYS_CLEAR                  : last reboot is due to keys clear
 *      BOOT_REASON_PANIC                       : last reboot is due to panic
 *      BOOT_REASON_WATCHDOG_BARK               : last reboot is due to watchdog bark
 */
int boot_getReason();

#endif
