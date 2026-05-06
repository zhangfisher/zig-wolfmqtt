/* logger.c
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

/* Include the autoconf generated config.h */
#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#include "wolfmqtt/logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

/* Get level string */
const char* Log_GetLevelStr(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO ";
        case LOG_LEVEL_WARN:  return "WARN ";
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

/* Default log output function */
void Log_Output(LogLevel level, const char* format, ...) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    printf("[%s] %s - ", Log_GetLevelStr(level), time_str);
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    printf("\n");
    fflush(stdout);
}

/* ANSI color codes */
#define ANSI_COLOR_RESET   "\x1b[0m"
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_GRAY    "\x1b[90m"

/* Default log callback function with color output */
void Log_DefaultCallback(LogLevel level, const char* format, va_list args) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    /* Set color based on log level */
    switch (level) {
        case LOG_LEVEL_ERROR:
        case LOG_LEVEL_FATAL:
            printf("%s", ANSI_COLOR_RED);
            break;
        case LOG_LEVEL_WARN:
            printf("%s", ANSI_COLOR_YELLOW);
            break;
        case LOG_LEVEL_DEBUG:
            printf("%s", ANSI_COLOR_GRAY);
            break;
        default:
            break;
    }
    
    printf("[%s] %s - ", Log_GetLevelStr(level), time_str);
    
    /* Reset color for message content */
    printf("%s", ANSI_COLOR_RESET);
    
    vprintf(format, args);
    printf("\n");
    fflush(stdout);
}

/* Log output with callback support */
void Log_OutputEx(LogLevel level, MqttLogCb cb, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    if (cb != NULL) {
        cb(level, format, args);
    } else {
        Log_DefaultCallback(level, format, args);
    }
    
    va_end(args);
}