/*
 * ---------------------------------------------------------------------------
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * ---------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>

#include "boot_reason.h"

int main()
{
    int reboot_reason = boot_getReason();
    const char* reason_char = "unknown";

    for (int i=0; i < sizeof(reasons)/sizeof(struct poweroff_reason); i++) {
        if(reasons[i].reason == reboot_reason) {
            reason_char = reasons[i].reason_char;
            break;
        }
    }

    printf("Last reset reason:%s\n",reason_char);

    return 0;
}