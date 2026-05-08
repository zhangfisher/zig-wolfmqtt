/* mqtt_websocket.c - Simplified WebSocket implementation without wslay
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

#include "wolfmqtt/mqtt_types.h"
#include "wolfmqtt/mqtt_broker.h"
#include "wolfmqtt/mqtt_websocket.h"
#include "wolfmqtt/mqtt_socket.h"
#include "wolfmqtt/logger.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#ifdef ENABLE_MQTT_WEBSOCKET

/* WebSocket logging macros - use generic logger */
#define WS_LOG_ERR(...)   Log_Output(LOG_LEVEL_ERROR, __VA_ARGS__)
#define WS_LOG_WARN(...)  Log_Output(LOG_LEVEL_WARN, __VA_ARGS__)
#define WS_LOG_INFO(...)  Log_Output(LOG_LEVEL_INFO, __VA_ARGS__)
#ifdef WOLFMQTT_DEBUG_WEBSOCKET
    #define WS_LOG_DBG(...) Log_Output(LOG_LEVEL_DEBUG, __VA_ARGS__)
#else
    #define WS_LOG_DBG(...)
#endif

/* Platform-specific sleep function */
#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #define WS_SLEEP_MS(ms) Sleep(ms)
#else
    #include <unistd.h>
    #define WS_SLEEP_MS(ms) usleep((unsigned)(ms) * 1000)
#endif

/* -------------------------------------------------------------------------- */
/* Constants                                                                   */
/* -------------------------------------------------------------------------- */
#define WS_GUID_STRING "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_GUID_LEN 36
#define WS_KEY_LEN 24
#define WS_ACCEPT_LEN 28
#define WS_HTTP_BUF_SIZE 4096
#define WS_SHA1_DIGEST_SIZE 20

/* WebSocket opcodes */
#define WS_OPCODE_CONTINUATION 0x0
#define WS_OPCODE_TEXT         0x1
#define WS_OPCODE_BINARY       0x2
#define WS_OPCODE_CLOSE        0x8
#define WS_OPCODE_PING         0x9
#define WS_OPCODE_PONG         0xA

/* Frame flags */
#define WS_FIN_BIT   0x80
#define WS_MASK_BIT  0x80
#define WS_OPCODE_MASK 0x0F

/* -------------------------------------------------------------------------- */
/* External crypto functions (from ws_crypto.c)                                */
/* -------------------------------------------------------------------------- */
extern void sha1_init(void *ctx);
extern void sha1_update(void *ctx, const unsigned char *data, size_t len);
extern void sha1_final(void *ctx, unsigned char *digest);
extern void base64_encode(const unsigned char *input, size_t input_len, char *output);

/* SHA-1 context type (defined in ws_crypto.c) */
typedef struct {
    unsigned int h0, h1, h2, h3, h4;
    unsigned char buffer[64];
    unsigned int buffer_len;
    unsigned long long total_len;
} sha1_ctx;

/* -------------------------------------------------------------------------- */
/* Helper Functions                                                            */
/* -------------------------------------------------------------------------- */

static const char* ws_find_header(const char* headers, 
                                   const char* field_name,
                                   char* value_buf,
                                   word32 value_buf_size)
{
    const char* line_start = headers;
    const char* line_end;
    word32 field_len = (word32)strlen(field_name);
    
    while ((line_end = strstr(line_start, "\r\n")) != NULL) {
        if (line_end - line_start > (int)field_len &&
            strncmp(line_start, field_name, field_len) == 0 &&
            line_start[field_len] == ':') {
            
            const char* value_start = line_start + field_len + 1;
            while (*value_start == ' ') value_start++;
            
            word32 value_len = (word32)(line_end - value_start);
            if (value_len >= value_buf_size) {
                value_len = value_buf_size - 1;
            }
            
            XMEMCPY(value_buf, value_start, value_len);
            value_buf[value_len] = '\0';
            return value_buf;
        }
        
        line_start = line_end + 2;
    }
    
    return NULL;
}

static int ws_parse_handshake_request(MqttWebSocketContext* ws_ctx,
                                       const byte* rx_buf,
                                       word32 rx_len,
                                       char* client_key,
                                       char* protocol)
{
    char key_buf[256];
    char proto_buf[64];
    char upgrade_buf[64];
    char connection_buf[64];
    const char* upgrade;
    const char* connection;
    
    /* Check for GET request */
    if (rx_len < 4 || strncmp((const char*)rx_buf, "GET ", 4) != 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* Find Sec-WebSocket-Key */
    if (!ws_find_header((const char*)rx_buf, "Sec-WebSocket-Key", 
                        key_buf, sizeof(key_buf))) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* Find Sec-WebSocket-Protocol */
    if (!ws_find_header((const char*)rx_buf, "Sec-WebSocket-Protocol",
                        proto_buf, sizeof(proto_buf))) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* Verify Upgrade: websocket */
    upgrade = ws_find_header((const char*)rx_buf, "Upgrade", upgrade_buf, sizeof(upgrade_buf));
    if (!upgrade || strcmp(upgrade, "websocket") != 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* Verify Connection: Upgrade */
    connection = ws_find_header((const char*)rx_buf, "Connection", connection_buf, sizeof(connection_buf));
    if (!connection || strcmp(connection, "Upgrade") != 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* Copy the actual key (use strlen to get actual length) */
    word32 key_len = (word32)strlen(key_buf);
    XSTRNCPY(client_key, key_buf, key_len + 1);
    XSTRNCPY(protocol, proto_buf, 63);
    
    return MQTT_CODE_SUCCESS;
}

static int ws_build_handshake_response(MqttWebSocketContext* ws_ctx,
                                        const char* client_key,
                                        const char* protocol,
                                        byte* tx_buf,
                                        word32 tx_buf_size,
                                        word32* tx_len)
{
    char accept_key[WS_ACCEPT_LEN + 1];
    byte sha1_input[WS_KEY_LEN + WS_GUID_LEN];
    byte sha1_digest[WS_SHA1_DIGEST_SIZE];
    char base64_output[64];
    word32 response_len;
    sha1_ctx ctx;
    
    /* Concatenate client key + GUID */
    word32 key_len = (word32)strlen(client_key);
    XMEMCPY(sha1_input, client_key, key_len);
    XMEMCPY(sha1_input + key_len, WS_GUID_STRING, WS_GUID_LEN);
    
    /* SHA-1 hash */
    sha1_init(&ctx);
    sha1_update(&ctx, sha1_input, key_len + WS_GUID_LEN);
    sha1_final(&ctx, sha1_digest);
    
    /* Base64 encode */
    base64_encode(sha1_digest, WS_SHA1_DIGEST_SIZE, base64_output);
    XSTRNCPY(accept_key, base64_output, WS_ACCEPT_LEN);
    accept_key[WS_ACCEPT_LEN] = '\0';
    
    /* Build HTTP response */
    response_len = (word32)snprintf((char*)tx_buf, tx_buf_size,
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "Sec-WebSocket-Protocol: %s\r\n"
        "\r\n",
        accept_key, protocol);
    
    if (response_len >= tx_buf_size) {
        return MQTT_CODE_ERROR_OUT_OF_BUFFER;
    }
    
    *tx_len = response_len;
    
    return MQTT_CODE_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* WebSocket Frame Parsing                                                     */
/* -------------------------------------------------------------------------- */

typedef struct {
    byte fin;
    byte opcode;
    byte masked;
    byte mask[4];
    unsigned long long payload_len;  /* Use unsigned long long for 64-bit */
    word32 header_len;
} ws_frame_header;

static int ws_parse_frame_header(const byte* buf, word32 buf_len, ws_frame_header* hdr)
{
    word32 offset = 0;
    
    if (buf_len < 2) {
        return MQTT_CODE_ERROR_OUT_OF_BUFFER;
    }
    
    /* First byte */
    hdr->fin = (buf[0] & WS_FIN_BIT) ? 1 : 0;
    hdr->opcode = buf[0] & WS_OPCODE_MASK;
    
    /* Second byte */
    hdr->masked = (buf[1] & WS_MASK_BIT) ? 1 : 0;
    hdr->payload_len = buf[1] & 0x7F;
    offset = 2;
    
    /* Extended payload length */
    if (hdr->payload_len == 126) {
        if (buf_len < offset + 2) {
            return MQTT_CODE_ERROR_OUT_OF_BUFFER;
        }
        hdr->payload_len = ((word16)buf[offset] << 8) | buf[offset + 1];
        offset += 2;
    } else if (hdr->payload_len == 127) {
        if (buf_len < offset + 8) {
            return MQTT_CODE_ERROR_OUT_OF_BUFFER;
        }
        hdr->payload_len = 0;
        for (int i = 0; i < 8; i++) {
            hdr->payload_len = (hdr->payload_len << 8) | buf[offset + i];
        }
        offset += 8;
    }
    
    /* Masking key */
    if (hdr->masked) {
        if (buf_len < offset + 4) {
            return MQTT_CODE_ERROR_OUT_OF_BUFFER;
        }
        XMEMCPY(hdr->mask, buf + offset, 4);
        offset += 4;
    }
    
    hdr->header_len = offset;
    return MQTT_CODE_SUCCESS;
}

static void ws_unmask_payload(byte* payload, word32 len, const byte* mask)
{
    word32 i;
    for (i = 0; i < len; i++) {
        payload[i] ^= mask[i % 4];
    }
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

int MqttWebSocket_Init(MqttWebSocketContext* ws_ctx)
{
    if (ws_ctx == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    XMEMSET(ws_ctx, 0, sizeof(MqttWebSocketContext));
    
    /* Allocate HTTP handshake buffer */
    ws_ctx->http_buf = (byte*)WOLFMQTT_MALLOC(WS_HTTP_BUF_SIZE);
    if (ws_ctx->http_buf == NULL) {
        return MQTT_CODE_ERROR_MEMORY;
    }
    ws_ctx->http_capacity = WS_HTTP_BUF_SIZE;
    ws_ctx->http_len = 0;
    
    /* Allocate receive buffer */
    ws_ctx->recv_buf = (byte*)WOLFMQTT_MALLOC(4096);
    if (ws_ctx->recv_buf == NULL) {
        WOLFMQTT_FREE(ws_ctx->http_buf);
        return MQTT_CODE_ERROR_MEMORY;
    }
    ws_ctx->recv_capacity = 4096;
    ws_ctx->recv_len = 0;
    
    ws_ctx->handshake_done = 0;
    ws_ctx->closing = 0;
    ws_ctx->sock = BROKER_SOCKET_INVALID;
    ws_ctx->broker_net = NULL;
    
    return MQTT_CODE_SUCCESS;
}

void MqttWebSocket_Free(MqttWebSocketContext* ws_ctx)
{
    if (ws_ctx == NULL) {
        return;
    }
    
    if (ws_ctx->http_buf) {
        WOLFMQTT_FREE(ws_ctx->http_buf);
        ws_ctx->http_buf = NULL;
    }
    
    if (ws_ctx->recv_buf) {
        WOLFMQTT_FREE(ws_ctx->recv_buf);
        ws_ctx->recv_buf = NULL;
    }
}

int MqttWebSocket_Handshake(MqttWebSocketContext* ws_ctx,
                             byte* rx_buf,
                             word32 rx_len,
                             byte* tx_buf,
                             word32 tx_buf_size,
                             word32* tx_len)
{
    char client_key[WS_KEY_LEN + 1];
    char protocol[64];
    int rc;
    
    if (ws_ctx == NULL || rx_buf == NULL || tx_buf == NULL || tx_len == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    rc = ws_parse_handshake_request(ws_ctx, rx_buf, rx_len, client_key, protocol);
    if (rc != MQTT_CODE_SUCCESS) {
        return rc;
    }
    
    rc = ws_build_handshake_response(ws_ctx, client_key, protocol, 
                                      tx_buf, tx_buf_size, tx_len);
    
    return rc;
}

int MqttWebSocket_Recv(MqttWebSocketContext* ws_ctx)
{
    MqttBrokerNet* net;
    byte frame_buf[8192];
    ws_frame_header hdr;
    int rc;
    word32 total_read = 0;
    
    if (ws_ctx == NULL || !ws_ctx->handshake_done) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    net = ws_ctx->broker_net;
    if (net == NULL || net->read == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* Clear previous receive data */
    ws_ctx->recv_len = 0;
    
    /* Read frame header (minimum 2 bytes) */
    rc = net->read(net->ctx, ws_ctx->sock, frame_buf, 2, 0);
    if (rc <= 0) {
        if (rc == MQTT_CODE_ERROR_TIMEOUT || rc == MQTT_CODE_CONTINUE) {
            /* No data available yet, return CONTINUE for non-blocking retry */
            return MQTT_CODE_CONTINUE;
        }
        if (rc == 0) {
            /* Connection closed */
            return MQTT_CODE_ERROR_NETWORK;
        }
        /* Other errors - check for EAGAIN/EWOULDBLOCK */
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return MQTT_CODE_CONTINUE;
        }
        return MQTT_CODE_ERROR_NETWORK;
    }
    total_read = (word32)rc;
    
    /* Parse frame header */
    rc = ws_parse_frame_header(frame_buf, total_read, &hdr);
    if (rc != MQTT_CODE_SUCCESS) {
        /* Need more data for extended header */
        word32 need_more = (hdr.payload_len == 126) ? 2 : 
                          (hdr.payload_len == 127) ? 8 : 0;
        if (need_more > 0) {
            rc = net->read(net->ctx, ws_ctx->sock, frame_buf + total_read, 
                          need_more, 0);
            if (rc <= 0) {
                if (rc == MQTT_CODE_ERROR_TIMEOUT || rc == MQTT_CODE_CONTINUE) {
                    return MQTT_CODE_CONTINUE;
                }
                if (rc == 0) {
                    return MQTT_CODE_ERROR_NETWORK;
                }
                return MQTT_CODE_ERROR_NETWORK;
            }
            total_read += (word32)rc;
            
            rc = ws_parse_frame_header(frame_buf, total_read, &hdr);
            if (rc != MQTT_CODE_SUCCESS) {
                return MQTT_CODE_ERROR_NETWORK;
            }
        }
    }
    
    /* Read masking key if present */
    if (hdr.masked) {
        rc = net->read(net->ctx, ws_ctx->sock, frame_buf + total_read, 4, 0);
        if (rc <= 0) {
            if (rc == MQTT_CODE_ERROR_TIMEOUT || rc == MQTT_CODE_CONTINUE) {
                return MQTT_CODE_CONTINUE;
            }
            if (rc == 0) {
                return MQTT_CODE_ERROR_NETWORK;
            }
            return MQTT_CODE_ERROR_NETWORK;
        }
        total_read += (word32)rc;
        XMEMCPY(hdr.mask, frame_buf + total_read - 4, 4);
    }
    
    /* Check payload length */
    if (hdr.payload_len > ws_ctx->recv_capacity) {
        WS_LOG_ERR("Payload too large: %llu\n", 
                   (unsigned long long)hdr.payload_len);
        return MQTT_CODE_ERROR_OUT_OF_BUFFER;
    }
    
    /* Read payload */
    if (hdr.payload_len > 0) {
        word32 remaining = (word32)hdr.payload_len;
        byte* payload_ptr = ws_ctx->recv_buf;
        
        while (remaining > 0) {
            rc = net->read(net->ctx, ws_ctx->sock, payload_ptr, remaining, 0);
            if (rc <= 0) {
                if (rc == MQTT_CODE_ERROR_TIMEOUT || rc == MQTT_CODE_CONTINUE) {
                    /* Partial read, continue next time */
                    return MQTT_CODE_CONTINUE;
                }
                if (rc == 0) {
                    return MQTT_CODE_ERROR_NETWORK;
                }
                return MQTT_CODE_ERROR_NETWORK;
            }
            
            payload_ptr += rc;
            remaining -= (word32)rc;
        }
        
        /* Unmask payload if needed */
        if (hdr.masked) {
            ws_unmask_payload(ws_ctx->recv_buf, (word32)hdr.payload_len, hdr.mask);
        }
        
        ws_ctx->recv_len = (word32)hdr.payload_len;
    }
    
    /* Only handle binary frames for MQTT */
    if (hdr.opcode != WS_OPCODE_BINARY) {
        if (hdr.opcode == WS_OPCODE_CLOSE) {
            ws_ctx->closing = 1;
        }
        ws_ctx->recv_len = 0;
        return MQTT_CODE_SUCCESS;
    }
    
    return (ws_ctx->recv_len > 0) ? (int)ws_ctx->recv_len : MQTT_CODE_SUCCESS;
}

int MqttWebSocket_Send(MqttWebSocketContext* ws_ctx,
                        const byte* data,
                        word32 len)
{
    MqttBrokerNet* net;
    byte frame_header[14];
    word32 header_len = 0;
    int rc;
    
    if (ws_ctx == NULL || !ws_ctx->handshake_done || data == NULL || len == 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    net = ws_ctx->broker_net;
    if (net == NULL || net->write == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* Build WebSocket frame header (server to client, no mask) */
    frame_header[0] = WS_FIN_BIT | WS_OPCODE_BINARY;
    
    if (len < 126) {
        frame_header[1] = (byte)len;
        header_len = 2;
    } else if (len < 65536) {
        frame_header[1] = 126;
        frame_header[2] = (byte)(len >> 8);
        frame_header[3] = (byte)(len & 0xFF);
        header_len = 4;
    } else {
        frame_header[1] = 127;
        for (int i = 0; i < 8; i++) {
            frame_header[2 + i] = (byte)(len >> (56 - i * 8));
        }
        header_len = 10;
    }
    
    /* Send header - loop until all data is sent */
    word32 header_sent = 0;
    while (header_sent < header_len) {
        rc = net->write(net->ctx, ws_ctx->sock, frame_header + header_sent, header_len - header_sent, 0);
        if (rc == MQTT_CODE_CONTINUE || rc == MQTT_CODE_ERROR_TIMEOUT) {
            WS_SLEEP_MS(1);
            continue;
        }
        if (rc < 0) {
            return rc;
        }
        header_sent += rc;
    }
    
    /* Send payload - loop until all data is sent */
    word32 total_sent = 0;
    while (total_sent < len) {
        rc = net->write(net->ctx, ws_ctx->sock, data + total_sent, len - total_sent, 0);
        if (rc == MQTT_CODE_CONTINUE || rc == MQTT_CODE_ERROR_TIMEOUT) {
            WS_SLEEP_MS(1);  /* Sleep 1ms */
            continue;
        }
        if (rc < 0) {
            return rc;
        }
        total_sent += rc;
    }
    
    return (int)len;  /* Return total bytes sent */
}

int MqttWebSocket_Close(MqttWebSocketContext* ws_ctx, word16 status_code)
{
    byte close_frame[4];
    
    if (ws_ctx == NULL || !ws_ctx->handshake_done) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* Build close frame */
    close_frame[0] = WS_FIN_BIT | WS_OPCODE_CLOSE;
    close_frame[1] = 2;  /* 2 bytes for status code */
    close_frame[2] = (byte)(status_code >> 8);
    close_frame[3] = (byte)(status_code & 0xFF);
    
    /* Send close frame (ignore errors) */
    MqttWebSocket_Send(ws_ctx, close_frame, 4);
    
    ws_ctx->closing = 1;
    
    return MQTT_CODE_SUCCESS;
}

#endif /* ENABLE_MQTT_WEBSOCKET */
