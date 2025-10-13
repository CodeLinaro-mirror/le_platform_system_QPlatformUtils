/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
   SPDX-License-Identifier: BSD-3-Clause-Clear */

#ifndef _LOG_H_
#define _LOG_H_
#include <iostream>
#include <cstring>

#ifdef _DEBUG
#define LOGD(message) (std::cout << "D: " << message << std::endl)
#else
#define LOGD(message)
#endif
#define LOGW(message) (std::cout << "W: " << message << std::endl)
#define LOGE(message) (std::cout << "E: " << message << std::endl)
#define LOGE_ERRNO(message) (std::cout << "E: " << message <<" : "<< std::strerror(errno) << std::endl)
#define LOGI(message) (std::cout << "I: " << message << std::endl)

#endif // _LOG_H_
