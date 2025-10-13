#!/bin/bash
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear

if grep -q '^CINDERDU' /sys/devices/soc0/machine; then
    echo "Starting process monitor for CINDERDU"
    exit 0
else
    echo "Not a CINDERDU machine. Skipping process-monitor."
    # exit 99 means : not a cinderdu machine
    exit 99
fi
