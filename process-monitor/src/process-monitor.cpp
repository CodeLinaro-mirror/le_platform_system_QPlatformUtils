/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 SPDX-License-Identifier: BSD-3-Clause-Clear */

#include "process-monitor.h"
#include "log.h"
#include "faultmgr_report_lib.h"
#include <atomic>
#include <fstream>
#include <unistd.h>
#include <json/json.h>
#include <iostream>
#define FM_CONF_FILE "/etc/process_monitor-cinder.json"
#define LOCAL_FS_TARGET "local-fs.target"
#define WAIT_TIME 20
#include <unordered_map>
#include <time.h>
#include <cstdint>
#include <string>

uint64_t getDeviceTime() {
    struct timespec ts;
    uint64_t event_time;

    // Returns Unix epoch time in milliseconds
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        LOGE_ERRNO("Failed to get system time, using default timestamp.");
        return 0;
    }

    event_time = static_cast<uint64_t>(ts.tv_sec) * 1000ULL + static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;

    return event_time;
}


std::string getTargetType() {
    const std::string socFile = "/sys/devices/soc0/machine";
    std::string socMachine;
    std::ifstream machineFile(socFile);
    if (machineFile.is_open()) {
        std::getline(machineFile, socMachine);
        machineFile.close();

        if (socMachine.empty()) {
            LOGE("Machine file is empty: " << socFile);
            return "none";
        }
    } else {
        LOGE("Failed to open file: " << socFile );
        return "none";
    }

    if (socMachine.find("CINDERRU") != std::string::npos) {
        return "CINDERRU";
    } else if (socMachine.find("CINDERDU") != std::string::npos) {
        return "CINDERDU";
    }

    LOGE("Unknown target type in machine file: " << socMachine);
    return "none";
}

bool readConfFile(const std::string& confFilePath, JsonConf& confVar) {
    try {
        std::ifstream confFile(confFilePath);
        if (!confFile.is_open()) {
            LOGE("Failed to open config file: " << confFilePath);
            return false;
        }

        Json::Value conf;
        confFile >> conf;
        confFile.close();

        // Get the current target type
        std::string targetType = getTargetType();
        // Exit if target type is not recognized
        if (targetType == "none") {
            LOGE("Unable to get target type");
            return false;
        }
        LOGI("Configuring services for : " << targetType);

        // Read common configuration
        if (conf.isMember("Timeout") && conf["Timeout"].isMember("Value")) {
            confVar.Timeout = conf["Timeout"]["Value"].asInt();
        } else {
            LOGE("Timeout value missing in config file.");
            return false;
        }

        confVar.IgnoreMissingServices = conf.get("IgnoreMissingServices", false).asBool();
        confVar.SystemdUnits.clear();

        // Add common SystemdUnits
        if (conf.isMember("SystemdUnits")) {
            for (const auto& service : conf["SystemdUnits"]) {
                confVar.SystemdUnits.push_back(service.asString());
                LOGD("Configured service: " << service.asString());
            }
        }

        // Add target-specific SystemdUnits based on the target type
        if (targetType == "CINDERRU" && conf.isMember("SystemdUnitsRU")) {
            for (const auto& service : conf["SystemdUnitsRU"]) {
                confVar.SystemdUnits.push_back(service.asString());
                LOGD("RU-specific configured service: " << service.asString());
            }
        } else if (targetType == "CINDERDU" && conf.isMember("SystemdUnitsDU")) {
            for (const auto& service : conf["SystemdUnitsDU"]) {
                confVar.SystemdUnits.push_back(service.asString());
                LOGD("DU-specific configured service: " << service.asString());
            }
        }

        if (confVar.SystemdUnits.empty()) {
            LOGW("No services configured to monitor for target: " << targetType);
        }

        return true;
    } catch (const std::exception& e) {
        LOGE("Exception while reading config file: " << confFilePath << " - " << e.what());
        return false;
    }
}

bool monitorServices(SystemMngrInit::SystemMngrInit& manager, JsonConf& confVar, std::vector<ServiceStatus>& statusList) {
    std::vector<std::string> pendingServices = confVar.SystemdUnits;
    statusList.clear();
    int remainingTimeout = confVar.Timeout; // Use local copy of timeout

    do {
        std::unordered_map<std::string, std::string> serviceStatuses;
        for (const auto& service : pendingServices) {
            serviceStatuses[service] = manager.getServiceStatus(service);
        }

        for (auto it = pendingServices.begin(); it != pendingServices.end(); ) {
            const std::string& status = serviceStatuses[*it];
            LOGI(*it << " status: " << status);

        if (confVar.IgnoreMissingServices &&
            (status == "ResourceUnavailable" || status == "Unknown")) {
            LOGI("Ignoring " << *it << " due to IgnoreMissingServices flag.");
            it = pendingServices.erase(it);
            continue;
        }

        if (status == "active") {
            it = pendingServices.erase(it);
        } else {
            ++it;
        }
        }
        if (pendingServices.empty()) {
            return true;
        }

        LOGI("service is not active, waiting further till timeout !!");
        sleep(WAIT_TIME);
        remainingTimeout -= WAIT_TIME;

    } while (remainingTimeout > 0);

    // Populate statusList only with services that failed to become active
    for (const auto& service : pendingServices) {
        std::string status = manager.getServiceStatus(service);
        statusList.push_back({service, status});
        LOGI("Service " << service << " failed to become active. Final status: " << status);
    }

    return false;
}

int reportFaults(const std::vector<ServiceStatus>& statusList) {
    fm_init_params_t init_params = {
       .connection_name = "org.qti.process_monitor",
       .context = nullptr
    };

    if (!fm_reporter_init(&init_params)) {
        LOGE("Failed to initialize fault reporter.");
        return 1; 
    }

    dbus_fault_report_t report_data = {};
    report_data.source = APPS_FAULT_MGR_PLATFORM;
    report_data.severity = FM_ALARM_SEVERITY_CRITICAL;
    report_data.is_active = 1;
    report_data.event_time = getDeviceTime();
    report_data.internal_fault_id = FAULT_MGR_PLATFORM_FAULT_0;
    report_data.susbsystem = FM_SUBSYSTEM_TYPE_APPS;

    for (const auto& entry : statusList) {
        if (entry.status != "active") {
            std::string faultSource = "module:platform,component:" + entry.name;
            std::string payload = entry.name + " service is not active!";

            if (faultSource.length() >= sizeof(report_data.fault_source))
                LOGW("Fault source truncated for service: " << entry.name);

            if (payload.length() >= sizeof(report_data.payload))
                LOGW("Payload truncated for service: " << entry.name);

            snprintf(report_data.fault_source, sizeof(report_data.fault_source), "%s", faultSource.c_str());
            snprintf(report_data.payload, sizeof(report_data.payload), "%s", payload.c_str());
            FaultManager_report_fault(&report_data);
        } else {
            LOGI("Service is active: " << entry.name);
        }
    }

    return 0; // Success
}

int main() {
    JsonConf confVar;
    if (!readConfFile(FM_CONF_FILE, confVar)) {
        LOGE("Process Monitor: Configuration file read failed. Exiting...\n");
        return 1;
    }

    if (confVar.SystemdUnits.empty()) {
        LOGW("No services configured to monitor. Exiting...");
        return 0;
    }

    SystemMngrInit::SystemMngrInit manager;
    std::vector<ServiceStatus> statusList;

    bool success = monitorServices(manager, confVar, statusList);

    if (!success) {
        LOGI("Monitored services failed, Reporting fault now.");
        if (reportFaults(statusList) != 0) {
            LOGE("Fault reporting failed.");
            return 2;
        }
    }

    return 0;
}
