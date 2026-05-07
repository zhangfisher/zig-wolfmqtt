/* mqtt_broker_transport.c
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

#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#include "wolfmqtt/mqtt_broker_transport.h"
#include "wolfmqtt/mqtt_broker.h"
#include "wolfmqtt/logger.h"
#include "wolfmqtt/mqtt_types.h"

#ifdef WOLFMQTT_BROKER

/* Broker logging macros - use printf for simplicity */
#ifdef WOLFMQTT_BROKER_LOG
    #include <stdio.h>
    #define WBLOG_DBG(b, ...)   fprintf(stderr, "[DEBUG] " __VA_ARGS__); fprintf(stderr, "\n")
    #define WBLOG_INFO(b, ...)  fprintf(stderr, "[INFO] " __VA_ARGS__); fprintf(stderr, "\n")
    #define WBLOG_WARN(b, ...)  fprintf(stderr, "[WARN] " __VA_ARGS__); fprintf(stderr, "\n")
    #define WBLOG_ERR(b, ...)   fprintf(stderr, "[ERROR] " __VA_ARGS__); fprintf(stderr, "\n")
    #define WBLOG_FATAL(b, ...) fprintf(stderr, "[FATAL] " __VA_ARGS__); fprintf(stderr, "\n")
#else
    #define WBLOG_DBG(b, ...)
    #define WBLOG_INFO(b, ...)
    #define WBLOG_WARN(b, ...)
    #define WBLOG_ERR(b, ...)
    #define WBLOG_FATAL(b, ...)
#endif

#ifdef ENABLE_MQTT_WEBSOCKET
    #include "wolfmqtt/mqtt_websocket.h"
#endif

#ifdef ENABLE_MQTT_TLS
    #include <wolfssl/options.h>
    #include <wolfssl/wolfcrypt/settings.h>
    #include <wolfssl/ssl.h>
#endif

/* -------------------------------------------------------------------------- */
/* TCP Transport                                                               */
/* -------------------------------------------------------------------------- */

static int tcp_init(BrokerClient* bc)
{
    (void)bc;
    return MQTT_CODE_SUCCESS;
}

static int tcp_handshake(BrokerClient* bc, MqttBroker* broker)
{
    (void)bc;
    (void)broker;
    return MQTT_CODE_SUCCESS;
}

static int tcp_is_handshake_done(BrokerClient* bc)
{
    (void)bc;
    return 1;
}

static int tcp_read(BrokerClient* bc, byte* buf, int buf_len, int timeout_ms)
{
    MqttBroker* broker = bc->broker;
    return broker->net.read(broker->net.ctx, bc->sock, buf, buf_len, timeout_ms);
}

static int tcp_write(BrokerClient* bc, const byte* buf, int buf_len, int timeout_ms)
{
    MqttBroker* broker = bc->broker;
    return broker->net.write(broker->net.ctx, bc->sock, buf, buf_len, timeout_ms);
}

static void tcp_close(BrokerClient* bc, MqttBroker* broker)
{
    if (bc->sock != BROKER_SOCKET_INVALID) {
        broker->net.close(broker->net.ctx, bc->sock);
        bc->sock = BROKER_SOCKET_INVALID;
    }
}

static void tcp_cleanup(BrokerClient* bc)
{
    (void)bc;
}

static const char* tcp_get_name(BrokerClient* bc)
{
    (void)bc;
    return "TCP";
}

static const BrokerTransportOps tcp_ops = {
    .init = tcp_init,
    .handshake = tcp_handshake,
    .is_handshake_done = tcp_is_handshake_done,
    .read = tcp_read,
    .write = tcp_write,
    .close = tcp_close,
    .cleanup = tcp_cleanup,
    .get_name = tcp_get_name
};

/* -------------------------------------------------------------------------- */
/* TLS Transport                                                               */
/* -------------------------------------------------------------------------- */

#ifdef ENABLE_MQTT_TLS

static int tls_init(BrokerClient* bc)
{
    bc->tls_handshake_done = 0;
    return MQTT_CODE_SUCCESS;
}

static int tls_handshake(BrokerClient* bc, MqttBroker* broker)
{
    int ret;
    
    if (bc->tls_handshake_done) {
        return MQTT_CODE_SUCCESS;
    }
    
    bc->client.tls.timeout_ms_read = broker->timeout_ms;
    bc->client.tls.timeout_ms_write = broker->timeout_ms;
    
    ret = wolfSSL_accept(bc->client.tls.ssl);
    if (ret == WOLFSSL_SUCCESS) {
        bc->tls_handshake_done = 1;
        WBLOG_DBG(broker, "TLS handshake done %s",
            wolfSSL_get_version(bc->client.tls.ssl));
        return MQTT_CODE_SUCCESS;
    }
    else {
        int err = wolfSSL_get_error(bc->client.tls.ssl, ret);
        if (err == WOLFSSL_ERROR_WANT_READ || err == WOLFSSL_ERROR_WANT_WRITE) {
            return MQTT_CODE_CONTINUE;
        }
        WBLOG_ERR(broker, "TLS handshake failed sock=%d err=%d",
            (int)bc->sock, err);
        return MQTT_CODE_ERROR_NETWORK;
    }
}

static int tls_is_handshake_done(BrokerClient* bc)
{
    return bc->tls_handshake_done;
}

static int tls_read(BrokerClient* bc, byte* buf, int buf_len, int timeout_ms)
{
    MqttBroker* broker = bc->broker;
    return broker->net.read(broker->net.ctx, bc->sock, buf, buf_len, timeout_ms);
}

static int tls_write(BrokerClient* bc, const byte* buf, int buf_len, int timeout_ms)
{
    MqttBroker* broker = bc->broker;
    return broker->net.write(broker->net.ctx, bc->sock, buf, buf_len, timeout_ms);
}

static void tls_close(BrokerClient* bc, MqttBroker* broker)
{
    if (bc->client.tls.ssl) {
        wolfSSL_shutdown(bc->client.tls.ssl);
        wolfSSL_free(bc->client.tls.ssl);
        bc->client.tls.ssl = NULL;
    }
    if (bc->sock != BROKER_SOCKET_INVALID) {
        broker->net.close(broker->net.ctx, bc->sock);
        bc->sock = BROKER_SOCKET_INVALID;
    }
}

static void tls_cleanup(BrokerClient* bc)
{
    if (bc->client.tls.ssl) {
        wolfSSL_free(bc->client.tls.ssl);
        bc->client.tls.ssl = NULL;
    }
}

static const char* tls_get_name(BrokerClient* bc)
{
    (void)bc;
    return "TLS";
}

static const BrokerTransportOps tls_ops = {
    .init = tls_init,
    .handshake = tls_handshake,
    .is_handshake_done = tls_is_handshake_done,
    .read = tls_read,
    .write = tls_write,
    .close = tls_close,
    .cleanup = tls_cleanup,
    .get_name = tls_get_name
};

#endif /* ENABLE_MQTT_TLS */

/* -------------------------------------------------------------------------- */
/* WebSocket Transport                                                         */
/* -------------------------------------------------------------------------- */

#ifdef ENABLE_MQTT_WEBSOCKET

static int ws_init(BrokerClient* bc)
{
    MqttWebSocketContext* ws_ctx;
    int rc;
    
    ws_ctx = (MqttWebSocketContext*)WOLFMQTT_MALLOC(sizeof(MqttWebSocketContext));
    if (ws_ctx == NULL) {
        return MQTT_CODE_ERROR_MEMORY;
    }
    
    rc = MqttWebSocket_Init(ws_ctx);
    if (rc != MQTT_CODE_SUCCESS) {
        WOLFMQTT_FREE(ws_ctx);
        return rc;
    }
    
    /* Set socket fd and broker net callbacks */
    ws_ctx->sock = bc->sock;
    ws_ctx->broker_net = &bc->broker->net;
    
    bc->transport.context = ws_ctx;
    bc->is_websocket = 1;
    
    return MQTT_CODE_SUCCESS;
}

static int ws_handshake(BrokerClient* bc, MqttBroker* broker)
{
    MqttWebSocketContext* ws_ctx = (MqttWebSocketContext*)bc->transport.context;
    byte rx_buf[1024];
    byte tx_buf[1024];
    word32 tx_len = 0;
    int rc;
    
    if (ws_ctx == NULL) {
        WBLOG_ERR(broker, "WebSocket context is NULL on sock=%d", (int)bc->sock);
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    if (ws_ctx->handshake_done) {
        return MQTT_CODE_SUCCESS;
    }
    
    rc = broker->net.read(broker->net.ctx, bc->sock, rx_buf, sizeof(rx_buf), 0);
    if (rc <= 0) {
        if (rc == 0 || rc == MQTT_CODE_ERROR_TIMEOUT) {
            return MQTT_CODE_CONTINUE;
        }
        WBLOG_ERR(broker, "WebSocket read failed on sock=%d rc=%d", (int)bc->sock, rc);
        return MQTT_CODE_ERROR_NETWORK;
    }
    
    rc = MqttWebSocket_Handshake(ws_ctx, rx_buf, rc, tx_buf, sizeof(tx_buf), &tx_len);
    
    if (rc == MQTT_CODE_SUCCESS) {
        int write_rc = broker->net.write(broker->net.ctx, bc->sock, tx_buf, tx_len, broker->timeout_ms);
        if (write_rc > 0) {
            WBLOG_INFO(broker, "WebSocket handshake completed on sock=%d (%d bytes sent)", (int)bc->sock, write_rc);
            /* Mark handshake as done */
            ws_ctx->handshake_done = 1;
            return MQTT_CODE_SUCCESS;
        }
        WBLOG_ERR(broker, "WebSocket write failed on sock=%d rc=%d", (int)bc->sock, write_rc);
        return MQTT_CODE_ERROR_NETWORK;
    }
    else if (rc == MQTT_CODE_CONTINUE) {
        return MQTT_CODE_CONTINUE;
    }
    else {
        WBLOG_ERR(broker, "WebSocket handshake failed on sock=%d rc=%d", (int)bc->sock, rc);
        return MQTT_CODE_ERROR_NETWORK;
    }
}

static int ws_is_handshake_done(BrokerClient* bc)
{
    MqttWebSocketContext* ws_ctx = (MqttWebSocketContext*)bc->transport.context;
    return ws_ctx && ws_ctx->handshake_done;
}

static int ws_read(BrokerClient* bc, byte* buf, int buf_len, int timeout_ms)
{
    MqttWebSocketContext* ws_ctx = (MqttWebSocketContext*)bc->transport.context;
    int rc;
    
    (void)timeout_ms;  /* WebSocket 使用固定的超时时间 */
    
    if (!ws_ctx || !ws_ctx->handshake_done) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* 通过 WebSocket 接收帧 */
    rc = MqttWebSocket_Recv(ws_ctx);
    
    if (rc == MQTT_CODE_CONTINUE) {
        /* 没有数据，返回超时以便上层重试 */
        return MQTT_CODE_ERROR_TIMEOUT;
    }
    
    if (rc < 0 && rc != MQTT_CODE_SUCCESS) {
        WBLOG_ERR(bc->broker, "MqttWebSocket_Recv failed: %d", rc);
        return MQTT_CODE_ERROR_NETWORK;
    }
    
    /* rc >= 0 表示接收到的字节数（可能为 0） */
    if (rc == 0) {
        /* 没有数据 */
        return MQTT_CODE_ERROR_TIMEOUT;
    }
    
    /* rc > 0 表示接收到的字节数 */
    if ((word32)rc > buf_len) {
        /* 缓冲区太小，截断 */
        rc = buf_len;
    }
    
    /* 复制数据到调用者提供的缓冲区 */
    XMEMCPY(buf, ws_ctx->recv_buf, rc);
    
    return rc;  /* 返回实际读取的字节数 */
}

static int ws_write(BrokerClient* bc, const byte* buf, int buf_len, int timeout_ms)
{
    MqttWebSocketContext* ws_ctx = (MqttWebSocketContext*)bc->transport.context;
    
    if (!ws_ctx || !ws_ctx->handshake_done) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    return MqttWebSocket_Send(ws_ctx, buf, buf_len);
}

static void ws_close(BrokerClient* bc, MqttBroker* broker)
{
    MqttWebSocketContext* ws_ctx = (MqttWebSocketContext*)bc->transport.context;
    
    if (ws_ctx) {
        MqttWebSocket_Close(ws_ctx, 1000);
        
        if (bc->sock != BROKER_SOCKET_INVALID) {
            broker->net.close(broker->net.ctx, bc->sock);
            bc->sock = BROKER_SOCKET_INVALID;
        }
    }
}

static void ws_cleanup(BrokerClient* bc)
{
    MqttWebSocketContext* ws_ctx = (MqttWebSocketContext*)bc->transport.context;
    
    if (ws_ctx) {
        MqttWebSocket_Free(ws_ctx);
        WOLFMQTT_FREE(ws_ctx);
        bc->transport.context = NULL;
    }
}

static const char* ws_get_name(BrokerClient* bc)
{
    (void)bc;
    return "WebSocket";
}

static const BrokerTransportOps ws_ops = {
    .init = ws_init,
    .handshake = ws_handshake,
    .is_handshake_done = ws_is_handshake_done,
    .read = ws_read,
    .write = ws_write,
    .close = ws_close,
    .cleanup = ws_cleanup,
    .get_name = ws_get_name
};

#endif /* ENABLE_MQTT_WEBSOCKET */

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

int BrokerTransport_Init(BrokerClient* bc, BrokerTransportType type, MqttBroker* broker)
{
    if (bc == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    bc->transport.type = type;
    
    switch (type) {
        case BROKER_TRANSPORT_TCP:
            bc->transport.ops = &tcp_ops;
            break;
            
#ifdef ENABLE_MQTT_TLS
        case BROKER_TRANSPORT_TLS:
            bc->transport.ops = &tls_ops;
            break;
#endif
            
#ifdef ENABLE_MQTT_WEBSOCKET
        case BROKER_TRANSPORT_WEBSOCKET:
            bc->transport.ops = &ws_ops;
            break;
#endif
            
        default:
            return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    if (bc->transport.ops->init) {
        return bc->transport.ops->init(bc);
    }
    
    return MQTT_CODE_SUCCESS;
}

int BrokerTransport_Handshake(BrokerClient* bc, MqttBroker* broker)
{
    if (bc == NULL || bc->transport.ops == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    if (bc->transport.ops->handshake) {
        return bc->transport.ops->handshake(bc, broker);
    }
    
    return MQTT_CODE_SUCCESS;
}

int BrokerTransport_IsHandshakeDone(BrokerClient* bc)
{
    if (bc == NULL || bc->transport.ops == NULL) {
        return 0;
    }
    
    if (bc->transport.ops->is_handshake_done) {
        return bc->transport.ops->is_handshake_done(bc);
    }
    
    return 1;
}

int BrokerTransport_Read(BrokerClient* bc, byte* buf, int buf_len, int timeout_ms)
{
    if (bc == NULL || bc->transport.ops == NULL || buf == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    if (bc->transport.ops->read) {
        return bc->transport.ops->read(bc, buf, buf_len, timeout_ms);
    }
    
    return MQTT_CODE_ERROR_NETWORK;
}

int BrokerTransport_Write(BrokerClient* bc, const byte* buf, int buf_len, int timeout_ms)
{
    if (bc == NULL || bc->transport.ops == NULL || buf == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    if (bc->transport.ops->write) {
        return bc->transport.ops->write(bc, buf, buf_len, timeout_ms);
    }
    
    return MQTT_CODE_ERROR_NETWORK;
}

void BrokerTransport_Close(BrokerClient* bc, MqttBroker* broker)
{
    if (bc == NULL || bc->transport.ops == NULL) {
        return;
    }
    
    if (bc->transport.ops->close) {
        bc->transport.ops->close(bc, broker);
    }
}

void BrokerTransport_Cleanup(BrokerClient* bc)
{
    if (bc == NULL || bc->transport.ops == NULL) {
        return;
    }
    
    if (bc->transport.ops->cleanup) {
        bc->transport.ops->cleanup(bc);
    }
    
    bc->transport.ops = NULL;
    bc->transport.context = NULL;
}

const char* BrokerTransport_GetName(BrokerClient* bc)
{
    if (bc == NULL || bc->transport.ops == NULL) {
        return "Unknown";
    }
    
    if (bc->transport.ops->get_name) {
        return bc->transport.ops->get_name(bc);
    }
    
    return "Unknown";
}

#endif /* WOLFMQTT_BROKER */
