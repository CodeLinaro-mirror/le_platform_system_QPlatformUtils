/*
 * ---------------------------------------------------------------------------
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * ---------------------------------------------------------------------------
 */

#ifndef _CRASHCL_H_
#define _CRASHCL_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <ifaddrs.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define CRASH_EVENT_SIZE (sizeof(struct inotify_event))
#define CBUFF_LEN (1024* (CRASH_EVENT_SIZE+16))

#define MHISW_INTFC "mhi_swip0"
#define SERVER_IPQ_PORT 49999
#define LOG_TIMESTAMP "/data/logTimeStamp"
#define FLAG_EMMC     "/sys/kernel/dload/emmc_dload"
#define PATH_SSR_DUMP "/data/ramdump/"
#define PATH_FULL_DUMP "/dev/block/bootdevice/by-name/rawdump"
#define PATH_FULL_DUMP_SD "/mnt/sdcard/ram_dump"

enum DUMP_TYPE {
        LOG_FILE = 0,
        SSR_DUMP,
        FULL_CRASH_DUMP
};

#endif
