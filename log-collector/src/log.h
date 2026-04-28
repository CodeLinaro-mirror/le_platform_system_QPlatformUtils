#ifndef _LOG_H_
#define _LOG_H_
/*
 * --------------------------------------------------------------------------------
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * --------------------------------------------------------------------------------
 */
#include <iostream>
#include <cstring>
#include <cerrno>

#ifdef _DEBUG
#define LOGD(message) (std::cout << "[log-collector]D: " << message << std::endl)
#else
#define LOGD(message)
#endif

#define LOGE(message) (std::cout << "[log-collector]E: "<< __func__ << "" << message <<" : "<< std::strerror(errno) << std::endl)
#define LOGI(message) (std::cout << "[log-collector]I: " << message << std::endl)

#endif // _LOG_H_
