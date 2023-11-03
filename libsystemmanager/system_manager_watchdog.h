#ifndef _SYSTEM_MANAGER_WATCHDOG_H_
#define _SYSTEM_MANAGER_WATCHDOG_H_

/*
 * ---------------------------------------------------------------------------
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * ---------------------------------------------------------------------------
 */
#ifdef __cplusplus
extern "C" {
#endif

/*
 * libsmwd_startWdPetting starts petting Systemd Software watchdog
 * IN : (unsigned int)time interval that will notify petting thread,
 * Return values should be interpreted as follows:
 *      0 : Watchdog petting thread started successfully
 *     -1 : Watchdog is not configured / Program is not Initilized by Init System Manager
 */
int libsmwd_startWdPetting(unsigned int MainThreadPetTime);

/*
 * libsmwd_notify reset the petting Thread timer
 */
void libsmwd_notify(void);

#ifdef __cplusplus
}
#endif

#endif // _SYSTEM_MANAGER_WATCHDOG_H_
