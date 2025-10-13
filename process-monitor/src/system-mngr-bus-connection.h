#ifndef _SM_BUS_CONNECTION_H_
#define _SM_BUS_CONNECTION_H_

/*
 Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <systemd/sd-bus.h>
#include <string>

namespace SystemMngrInit
{

class SystemMngrInit
{
public:
    SystemMngrInit();
    ~SystemMngrInit();

    std::string getServiceStatus(std::string serviceName);

private:
    sd_bus_error error;
    sd_bus_message *message;
    sd_bus *bus;

    const char *sd_path;
    const char *sd_destination;
    const char *sd_unit_interface;
    const char *sd_service_interface;
    const char *sd_manager_interface;

    std::string getServiceSdPath(std::string serviceName);

};

}

#endif // _SM_BUS_CONNECTION_H_
