/* mqtt_broker_logger.c
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

#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#include "wolfmqtt/mqtt_broker_logger.h"
#include "wolfmqtt/mqtt_broker.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#ifdef WOLFMQTT_BROKER

/* -------------------------------------------------------------------------- */
/* Thread-local flag to prevent recursive log publishing                       */
/* -------------------------------------------------------------------------- */
static __thread int s_log_publishing = 0;

/* Forward declaration for publish function */
extern int BrokerPublish_Message(MqttBroker* broker, const char* topic,
                                 const byte* payload, word16 payload_len,
                                 MqttQoS qos, byte retain);

/* -------------------------------------------------------------------------- */
/* Initialize broker logger system                                             */
/* -------------------------------------------------------------------------- */
void BrokerLogger_Init(void)
{
    /* Thread-local variable is automatically initialized to 0 */
    s_log_publishing = 0;
}

/* -------------------------------------------------------------------------- */
/* Core logging function for all broker modules                                */
/* -------------------------------------------------------------------------- */
void BrokerLogger_Log(MqttBroker* broker, LogLevel level, const char* format, ...)
{
    if (broker == NULL || format == NULL) {
        return;
    }
    
    /* Check log level */
    if (level < broker->log_level) {
        return;
    }
    
    va_list args;
    va_start(args, format);
    
    /* Step 1: Call the log callback (console/file output) */
    if (broker->log != NULL) {
        broker->log(level, format, args);
    }
    
    /* Step 2: Publish to MQTT topic $sys/broker/logs if API enabled */
    /* Prevent recursion: only publish if we're not already publishing a log */
    if (broker->api_ctx != NULL && !s_log_publishing) {
        /* Format the log message with timestamp */
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        char time_str[20];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
        
        char log_msg[512];
        int len = snprintf(log_msg, sizeof(log_msg), "[%s] %s - ", 
                           Log_GetLevelStr(level), time_str);
        
        if (len > 0 && len < (int)sizeof(log_msg)) {
            /* Need to re-format the message for MQTT publish */
            va_list args_copy;
            va_copy(args_copy, args);
            vsnprintf(log_msg + len, sizeof(log_msg) - len, format, args_copy);
            va_end(args_copy);
            
            /* Set flag to prevent recursion */
            s_log_publishing = 1;
            
            /* Publish with QoS=0, retain=false for minimal overhead */
            BrokerPublish_Message(broker, "$sys/broker/logs", 
                                 (const byte*)log_msg, (word16)strlen(log_msg), 
                                 MQTT_QOS_0, 0);
            
            /* Clear flag */
            s_log_publishing = 0;
        }
    }
    
    va_end(args);
}

#endif /* WOLFMQTT_BROKER */
