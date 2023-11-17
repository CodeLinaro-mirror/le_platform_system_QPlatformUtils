/*
 * ---------------------------------------------------------------------------
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * ---------------------------------------------------------------------------
 */

#include <thread>
#include <atomic>
#include <unistd.h>
#include <systemd/sd-daemon.h>

#include "log.h"
#include "system_manager_watchdog.h"

#define SEC_TO_USEC (1000000)

static std::atomic<uint64_t> local_wd_timer(0);

static void PettingWd(uint64_t WatchdogUSec,unsigned int MainThreadPetTime)
{
    while (1)
    {
        if (local_wd_timer >= ((uint64_t)MainThreadPetTime * SEC_TO_USEC) + WatchdogUSec) {
            /*local watchdog bite*/
            /*exit this thread, so systemd wd will not be petted*/
            return;
        }

        /*Keep Alive Signal*/
        int ret = sd_notify(0, "WATCHDOG=1");
        if (ret < 0) {
            LOGI("Failed to send keep alive status");
            return;
        } else if (ret == 0) {
            LOGI("Program is not Initilized by Init System Manager");
            return;
        }

        usleep(WatchdogUSec/2);

        /*ignore timer if MainThreadPetTime is set to 0*/
        if (MainThreadPetTime > 0)
            local_wd_timer += (WatchdogUSec/2);
    }
}

int libsmwd_startWdPetting(unsigned int MainThreadPetTime)
{
    uint64_t WatchdogUSec = 0;

    if(sd_watchdog_enabled(0, &WatchdogUSec) && (WatchdogUSec > 0)) {
        LOGI("Watchdog timer is configured with " <<WatchdogUSec<<"usec");
        std::thread WdPetThread(PettingWd, WatchdogUSec, MainThreadPetTime);
        WdPetThread.detach();
    } else {
        LOGI("Watchdog is not configured / Program is not Initilized by Init System Manager");
        return -1;
    }

    return 0;
}

void libsmwd_notify(void)
{
    /*Notification sent from main thread*/
    LOGI("Notification sent from Main thread");
    local_wd_timer = 0;
}
