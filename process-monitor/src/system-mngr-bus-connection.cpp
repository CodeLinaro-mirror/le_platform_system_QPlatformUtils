/*
 Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include "log.h"
#include "system-mngr-bus-connection.h"

namespace SystemMngrInit
{

SystemMngrInit::SystemMngrInit()
{
    bus = NULL;
    error = SD_BUS_ERROR_NULL;
    sd_path = "/org/freedesktop/systemd1";
    sd_destination = "org.freedesktop.systemd1";
    sd_unit_interface = "org.freedesktop.systemd1.Unit";
    sd_service_interface = "org.freedesktop.systemd1.Service";
    sd_manager_interface = "org.freedesktop.systemd1.Manager";

    if (sd_bus_default(&bus) < 0) {
        LOGE("Failed to connect to system bus");
        throw std::runtime_error("Failed to connect to system bus");
    }
}

SystemMngrInit::~SystemMngrInit()
{
    // sd_bus_message_unref(reply);
    sd_bus_unref(bus);
}

std::string SystemMngrInit::getServiceSdPath(std::string serviceName)
{
    message = NULL;
    std::string retString = "Unknown";
    error = SD_BUS_ERROR_NULL;

    int ret = sd_bus_call_method( bus, sd_destination, sd_path, sd_manager_interface,
                    "GetUnit", &error, &message, "s", serviceName.c_str());
    if (ret < 0) {
        LOGE("sd_bus_call_method failed for " << serviceName << " with error: " << strerror(-ret));
	retString = "ResourceUnavailable";
    sd_bus_error_free(&error);
        return retString;
    }

    char *unit_sd_path = NULL;
    ret = sd_bus_message_read(message, "o", &unit_sd_path);
    if (ret < 0) {
        LOGE("sd_bus_call_method failed for " << serviceName << " with error: " << strerror(-ret));
        sd_bus_message_unref(message);
        sd_bus_error_free(&error);
	return retString;
    }
    LOGD(serviceName <<" sd-bus path :" << unit_sd_path);

    retString = unit_sd_path;
    sd_bus_message_unref(message);
    sd_bus_error_free(&error);
    return retString;
}

std::string SystemMngrInit::getServiceStatus(std::string serviceName)
{
    int ret = -1;
    message = NULL;
    std::string retString = "Unknown";
    error = SD_BUS_ERROR_NULL;

    std::string unit_sd_path;
    unit_sd_path = this->getServiceSdPath(serviceName);

    char *serviceStatus = NULL;
    ret = sd_bus_get_property_string(bus, sd_destination, unit_sd_path.c_str(), sd_unit_interface,
                                    "ActiveState", &error, &serviceStatus);
    if (ret < 0) {
        LOGE("sd_bus_get_property_string failed for " << serviceName << " with error: " << strerror(-ret));
        return retString;
    }
    LOGD(serviceName <<" Status : " << serviceStatus);

    retString = serviceStatus;
    free(serviceStatus);
    sd_bus_error_free(&error);
    return retString;
}

}
