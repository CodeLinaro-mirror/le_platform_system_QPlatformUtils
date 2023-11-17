#ifndef _LOG_H_
#define _LOG_H_

/*
 * ---------------------------------------------------------------------------
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 * ---------------------------------------------------------------------------
 */

#include <iostream>

#ifdef _DEBUG
#define LOGE(message) (std::cout << "[libsmwd]E: " << message <<" : "<< strerror(errno) << std::endl)
#define LOGI(message) (std::cout << "[libsmwd]I: " << message << std::endl)
#else
#define LOGE(message)
#define LOGI(message)
#endif

#endif // _LOG_H_