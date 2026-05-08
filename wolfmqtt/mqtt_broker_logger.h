/* mqtt_broker_logger.h
 *
 * Copyright (C) 2006-2026 wolfSSL Inc.
 *
 * This file is part of wolfMQTT.
 *
 * wolfMQTT is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef WOLFMQTT_BROKER_LOGGER_H
#define WOLFMQTT_BROKER_LOGGER_H

#include "wolfmqtt/mqtt_types.h"
#include "wolfmqtt/logger.h"

#ifdef __cplusplus
    extern "C" {
#endif

#ifdef WOLFMQTT_BROKER

/* Forward declaration */
typedef struct MqttBroker MqttBroker;

/* -------------------------------------------------------------------------- */
/* Broker Logger API                                                           */
/* -------------------------------------------------------------------------- */

/**
 * Initialize broker logger system
 * Sets up thread-local storage for recursion prevention
 */
void BrokerLogger_Init(void);

/**
 * Core logging function for all broker modules
 * 
 * @param broker    Broker instance (for log callback and level check)
 * @param level     Log level
 * @param format    Format string
 * @param ...       Format arguments
 * 
 * Features:
 * - Respects broker->log_level setting
 * - Calls broker->log callback
 * - Publishes to $sys/broker/logs if API enabled
 * - Prevents recursive calls via thread-local flag
 */
void BrokerLogger_Log(MqttBroker* broker, LogLevel level, const char* format, ...);

/* -------------------------------------------------------------------------- */
/* Logging Macros (unified for all broker modules)                            */
/* -------------------------------------------------------------------------- */

#ifdef WOLFMQTT_BROKER_LOG
    #define BROKER_LOG_DBG(b, ...)   BrokerLogger_Log(b, LOG_LEVEL_DEBUG, __VA_ARGS__)
    #define BROKER_LOG_INFO(b, ...)  BrokerLogger_Log(b, LOG_LEVEL_INFO, __VA_ARGS__)
    #define BROKER_LOG_WARN(b, ...)  BrokerLogger_Log(b, LOG_LEVEL_WARN, __VA_ARGS__)
    #define BROKER_LOG_ERR(b, ...)   BrokerLogger_Log(b, LOG_LEVEL_ERROR, __VA_ARGS__)
    #define BROKER_LOG_FATAL(b, ...) BrokerLogger_Log(b, LOG_LEVEL_FATAL, __VA_ARGS__)
#else
    #define BROKER_LOG_DBG(b, ...)
    #define BROKER_LOG_INFO(b, ...)
    #define BROKER_LOG_WARN(b, ...)
    #define BROKER_LOG_ERR(b, ...)
    #define BROKER_LOG_FATAL(b, ...)
#endif

/* Backward compatibility macros (deprecated, use BROKER_LOG_* instead) */
#define WBLOG_DBG   BROKER_LOG_DBG
#define WBLOG_INFO  BROKER_LOG_INFO
#define WBLOG_WARN  BROKER_LOG_WARN
#define WBLOG_ERR   BROKER_LOG_ERR
#define WBLOG_FATAL BROKER_LOG_FATAL

#endif /* WOLFMQTT_BROKER */

#ifdef __cplusplus
    } /* extern "C" */
#endif

#endif /* WOLFMQTT_BROKER_LOGGER_H */
