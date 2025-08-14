/*Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause-Clear */

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "system-mngr-bus-connection.h"

struct JsonConf {
    int Timeout = 0;
    std::vector<std::string> SystemdUnits;
    bool IgnoreMissingServices = false;
};

struct ServiceStatus {
    std::string name;
    std::string status;
};

bool readConfFile(const std::string& confFilePath, JsonConf& confVar);
bool monitorServices(SystemMngrInit::SystemMngrInit& manager, JsonConf& confVar, std::vector<ServiceStatus>& statusList);
int reportFaults(const std::vector<ServiceStatus>& statusList);
uint64_t getDeviceTime();
