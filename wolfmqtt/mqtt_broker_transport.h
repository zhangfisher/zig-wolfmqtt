/* mqtt_broker_transport.h
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

#ifndef WOLFMQTT_BROKER_TRANSPORT_H
#define WOLFMQTT_BROKER_TRANSPORT_H

#include "wolfmqtt/mqtt_types.h"

#ifdef __cplusplus
    extern "C" {
#endif

#ifdef WOLFMQTT_BROKER

/* Forward declarations */
typedef struct BrokerClient BrokerClient;
typedef struct MqttBroker MqttBroker;

/* Transport type enumeration */
typedef enum {
    BROKER_TRANSPORT_TCP = 0,
#ifdef ENABLE_MQTT_TLS
    BROKER_TRANSPORT_TLS,
#endif
#ifdef ENABLE_MQTT_WEBSOCKET
    BROKER_TRANSPORT_WEBSOCKET,
#endif
    BROKER_TRANSPORT_MAX
} BrokerTransportType;

/* Transport operations vtable */
typedef struct BrokerTransportOps {
    int (*init)(BrokerClient* bc);
    int (*handshake)(BrokerClient* bc, MqttBroker* broker);
    int (*is_handshake_done)(BrokerClient* bc);
    int (*read)(BrokerClient* bc, byte* buf, int buf_len, int timeout_ms);
    int (*write)(BrokerClient* bc, const byte* buf, int buf_len, int timeout_ms);
    void (*close)(BrokerClient* bc, MqttBroker* broker);
    void (*cleanup)(BrokerClient* bc);
    const char* (*get_name)(BrokerClient* bc);
} BrokerTransportOps;

/* Transport context embedded in BrokerClient */
typedef struct BrokerTransport {
    BrokerTransportType type;
    const BrokerTransportOps* ops;
    void* context;
} BrokerTransport;

/* Public API */
WOLFMQTT_API int BrokerTransport_Init(BrokerClient* bc, 
                                       BrokerTransportType type,
                                       MqttBroker* broker);

WOLFMQTT_API int BrokerTransport_Handshake(BrokerClient* bc, 
                                            MqttBroker* broker);

WOLFMQTT_API int BrokerTransport_IsHandshakeDone(BrokerClient* bc);

WOLFMQTT_API int BrokerTransport_Read(BrokerClient* bc, 
                                       byte* buf, 
                                       int buf_len, 
                                       int timeout_ms);

WOLFMQTT_API int BrokerTransport_Write(BrokerClient* bc, 
                                        const byte* buf, 
                                        int buf_len, 
                                        int timeout_ms);

WOLFMQTT_API void BrokerTransport_Close(BrokerClient* bc, 
                                         MqttBroker* broker);

WOLFMQTT_API void BrokerTransport_Cleanup(BrokerClient* bc);

WOLFMQTT_API const char* BrokerTransport_GetName(BrokerClient* bc);

#endif /* WOLFMQTT_BROKER */

#ifdef __cplusplus
    } /* extern "C" */
#endif

#endif /* WOLFMQTT_BROKER_TRANSPORT_H */
