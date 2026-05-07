/* mqtt_websocket.h
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

#ifndef WOLFMQTT_WEBSOCKET_H
#define WOLFMQTT_WEBSOCKET_H

#include "wolfmqtt/mqtt_types.h"
#include "wolfmqtt/mqtt_socket.h"

#ifdef __cplusplus
    extern "C" {
#endif

#ifdef ENABLE_MQTT_WEBSOCKET

/* Forward declaration */
typedef struct MqttBrokerNet MqttBrokerNet;

/* WebSocket 上下文结构 */
typedef struct MqttWebSocketContext {
    byte handshake_done;                 /* 握手完成标志 */
    byte closing;                        /* 正在关闭标志 */
    
    /* HTTP 握手缓冲区 */
    byte* http_buf;
    word32 http_len;
    word32 http_capacity;
    
    /* 接收数据缓冲区（用于存储从 WebSocket 帧提取的 MQTT 包） */
    byte* recv_buf;
    word32 recv_len;
    word32 recv_capacity;
    
    /* Socket fd (for BrokerNet callbacks) */
    BROKER_SOCKET_T sock;
    
    /* Broker network callbacks */
    MqttBrokerNet* broker_net;
} MqttWebSocketContext;

/* WebSocket 回调函数类型 */
typedef int (*MqttWebSocketMsgCb)(void* user_data, 
                                   const byte* msg, 
                                   word32 msg_len);

/* 初始化 WebSocket 上下文（服务端模式） */
WOLFMQTT_API int MqttWebSocket_Init(MqttWebSocketContext* ws_ctx);

/* 设置网络回调 */
WOLFMQTT_API int MqttWebSocket_SetNet(MqttWebSocketContext* ws_ctx,
                                       MqttNet* net,
                                       void* net_ctx);

/* 清理 WebSocket 资源 */
WOLFMQTT_API void MqttWebSocket_Free(MqttWebSocketContext* ws_ctx);

/* 处理 WebSocket 握手（HTTP Upgrade） */
WOLFMQTT_API int MqttWebSocket_Handshake(MqttWebSocketContext* ws_ctx,
                                          byte* rx_buf, 
                                          word32 rx_len,
                                          byte* tx_buf,
                                          word32 tx_capacity,
                                          word32* tx_len);

/* 接收 WebSocket 数据 */
WOLFMQTT_API int MqttWebSocket_Recv(MqttWebSocketContext* ws_ctx);

/* 发送 WebSocket 数据 */
WOLFMQTT_API int MqttWebSocket_Send(MqttWebSocketContext* ws_ctx,
                                     const byte* data,
                                     word32 data_len);

/* 检查是否有待发送的数据 */
WOLFMQTT_API int MqttWebSocket_WantWrite(MqttWebSocketContext* ws_ctx);

/* 关闭 WebSocket 连接 */
WOLFMQTT_API int MqttWebSocket_Close(MqttWebSocketContext* ws_ctx,
                                      word16 status_code);

/* 获取 WebSocket 状态 */
WOLFMQTT_API int MqttWebSocket_IsConnected(MqttWebSocketContext* ws_ctx);

#endif /* ENABLE_MQTT_WEBSOCKET */

#ifdef __cplusplus
    } /* extern "C" */
#endif

#endif /* WOLFMQTT_WEBSOCKET_H */
