/* logger.h
 *
 * Copyright (C) 2006-2026 wolfSSL Inc.
 *
 * This file is part of wolfMQTT.
 *
 * wolfMQTT is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfMQTT is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#ifndef WOLFMQTT_LOGGER_H
#define WOLFMQTT_LOGGER_H

#include "wolfmqtt/mqtt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Log levels */
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL
} LogLevel;

/* Log callback function type */
typedef void (*MqttLogCb)(LogLevel level, const char* format, va_list args);

/* Get level string */
const char* Log_GetLevelStr(LogLevel level);

/* Default log output function */
void Log_Output(LogLevel level, const char* format, ...);

/* Log output with callback support */
void Log_OutputEx(LogLevel level, MqttLogCb cb, const char* format, ...);

/* Log macros for each level */
#define LOG_ERROR(...) Log_Output(LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_INFO(...)  Log_Output(LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_DEBUG(...) Log_Output(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_WARN(...)  Log_Output(LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_FATAL(...) Log_Output(LOG_LEVEL_FATAL, __VA_ARGS__)

/* Default log callback function (can be used as MqttLogCb) */
void Log_DefaultCallback(LogLevel level, const char* format, va_list args);

#ifdef __cplusplus
}
#endif

#endif /* WOLFMQTT_LOGGER_H */