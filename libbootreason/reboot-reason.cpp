/*
 * ---------------------------------------------------------------------------
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * ---------------------------------------------------------------------------
 */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <string.h>

#include "boot_reason.h"

#define SYSNODE ("/sys/kernel/reset_reason/reset_reason")

#define TOKEN ("Last reset reason: ")
#define MAX_SYS_DATA_LENGTH 64

/*cat /sys/kernel/reset_reason/reset_reason
  Last reset reason: bootloader */

int boot_getReason()
{
    char buffer[MAX_SYS_DATA_LENGTH] = {0};
    char *reset_reason = NULL;
    int ret = BOOT_REASON_UNKNOWN;

    int fd = open(SYSNODE, O_RDONLY);
    if (fd == -1) {
        printf("%s Token: Not Found in %S node : %s\n", TOKEN, SYSNODE, strerror(errno));
        return ret;
    }

    int readbytes = read(fd, buffer, MAX_SYS_DATA_LENGTH - 1);
    if(readbytes < 0)
        return ret;
    close(fd);

    reset_reason = strstr(buffer, TOKEN);
    if(!reset_reason){
        printf("%s Token: Not Found in %S node\n", TOKEN, SYSNODE);
        return ret;
    }

    reset_reason += strlen(TOKEN);

    if(!reset_reason){
        return ret;
    }

    while (*reset_reason != '\0' && isspace((unsigned char)*reset_reason)) {
        reset_reason++;
    }

    size_t len = strlen(reset_reason);
    while(len > 0 && (isspace((unsigned char)reset_reason[len - 1]) || reset_reason[len - 1] == '\n' )) {
        reset_reason[len - 1] = '\0';
        len--;
    }
    std::string reset_reason_s = reset_reason;

    for (int i=0; i < sizeof(reasons)/sizeof(struct poweroff_reason); i++) {
        if(reasons[i].reason_char == reset_reason_s) {
            ret = reasons[i].reason;
            break;
        }
    }

    return ret;
}
