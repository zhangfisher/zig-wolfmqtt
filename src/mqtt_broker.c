/* mqtt_broker.c
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

#include "wolfmqtt/mqtt_types.h"
#include "wolfmqtt/mqtt_broker.h"
#include "wolfmqtt/mqtt_broker_transport.h"
#include "wolfmqtt/mqtt_client.h"
#include "wolfmqtt/mqtt_packet.h"
#include "wolfmqtt/mqtt_socket.h"
#include "wolfmqtt/logger.h"

#include <stdlib.h>
#include <string.h>

#ifdef WOLFMQTT_BROKER

#define BROKER_FORCE_ZERO(mem, len) Mqtt_ForceZero(mem, (word32)(len))

/* -------------------------------------------------------------------------- */
/* Platform includes                                                           */
/* -------------------------------------------------------------------------- */
#if defined(WOLFMQTT_WOLFIP)
    #include "wolfip.h"
#elif !defined(WOLFMQTT_BROKER_CUSTOM_NET)
    #include <errno.h>
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <netinet/in.h>
#ifdef WOLFMQTT_BROKER_EPOLL
    #include <sys/epoll.h>
#else
    #include <sys/select.h>
#endif
    #include <sys/socket.h>
    #include <time.h>
    #include <unistd.h>
#endif

#ifdef ENABLE_MQTT_WEBSOCKET
    #include "wolfmqtt/mqtt_websocket.h"
#endif

/* -------------------------------------------------------------------------- */
/* Default time abstraction                                                    */
/* -------------------------------------------------------------------------- */
#ifndef WOLFMQTT_BROKER_GET_TIME_S
    #if defined(WOLFMQTT_WOLFIP)
        /* wolfIP has no default time source. Define
         * WOLFMQTT_BROKER_GET_TIME_S in user_settings.h to provide one.
         * Example: #define WOLFMQTT_BROKER_GET_TIME_S() myGetTimeSec() */
        #error "WOLFMQTT_WOLFIP requires WOLFMQTT_BROKER_GET_TIME_S to be defined"
    #else
        #define WOLFMQTT_BROKER_GET_TIME_S() \
            ((WOLFMQTT_BROKER_TIME_T)time(NULL))
    #endif
#endif

/* -------------------------------------------------------------------------- */
/* Default sleep abstraction                                                   */
/* -------------------------------------------------------------------------- */
#ifndef BROKER_SLEEP_MS
    #if defined(WOLFMQTT_WOLFIP)
        /* No-op: wolfIP uses cooperative scheduling via MqttBroker_Step().
         * Do not use MqttBroker_Run() on wolfIP - it will busy-spin.
         * Override BROKER_SLEEP_MS in user_settings.h if a yield/delay
         * primitive is available on your platform. */
        #define BROKER_SLEEP_MS(ms) do {} while(0)
    #elif defined(USE_WINDOWS_API)
        #define BROKER_SLEEP_MS(ms) Sleep(ms)
    #else
        #define BROKER_SLEEP_MS(ms) usleep((unsigned)(ms) * 1000)
    #endif
#endif

/* Logging macros with level filtering and customizable callback. */
#include <stdarg.h>

static inline void broker_log(MqttBroker* b, LogLevel level, const char* format, ...) {
    if (b->log_level >= level) {
        va_list args;
        va_start(args, format);
        b->log(level, format, args);
        va_end(args);
    }
}

#define WBLOG_DBG(b, ...)   broker_log(b, LOG_LEVEL_DEBUG, __VA_ARGS__)
#define WBLOG_INFO(b, ...)  broker_log(b, LOG_LEVEL_INFO, __VA_ARGS__)
#define WBLOG_WARN(b, ...)  broker_log(b, LOG_LEVEL_WARN, __VA_ARGS__)
#define WBLOG_ERR(b, ...)   broker_log(b, LOG_LEVEL_ERROR, __VA_ARGS__)
#define WBLOG_FATAL(b, ...) broker_log(b, LOG_LEVEL_FATAL, __VA_ARGS__)

/* Buffer size accessors - unify static/dynamic code paths */
#ifdef WOLFMQTT_STATIC_MEMORY
    #define BROKER_CLIENT_TX_SZ(bc) ((bc)->broker->tx_buf_sz)
    #define BROKER_CLIENT_RX_SZ(bc) ((bc)->broker->rx_buf_sz)
#else
    #define BROKER_CLIENT_TX_SZ(bc) ((bc)->tx_buf_len)
    #define BROKER_CLIENT_RX_SZ(bc) ((bc)->rx_buf_len)
#endif

/* String validity check - static arrays vs dynamic pointers */
#ifdef WOLFMQTT_STATIC_MEMORY
    #define BROKER_STR_VALID(s) ((s)[0] != '\0')
#else
    #define BROKER_STR_VALID(s) ((s) != NULL)
#endif

/* No-op stubs when features are compiled out */
#ifndef WOLFMQTT_BROKER_RETAINED
    #define BrokerRetained_Store(b, t, p, l, q, e)         (0)
    #define BrokerRetained_Delete(b, t)                 do {} while(0)
    #define BrokerRetained_FreeAll(b)                   do {} while(0)
    #define BrokerRetained_DeliverToClient(b, c, f, q)  do {} while(0)
#endif
#ifndef WOLFMQTT_BROKER_WILL
    #define BrokerClient_ClearWill(bc)                  do {} while(0)
    #define BrokerClient_PublishWill(b, bc)             do {} while(0)
    #define BrokerPendingWill_Cancel(b, id)             do {} while(0)
    #define BrokerPendingWill_Process(b)                (0)
    #define BrokerPendingWill_FreeAll(b)                do {} while(0)
#endif

#ifdef WOLFMQTT_BROKER_AUTH
/* Constant-time string comparison to prevent timing attacks on auth.
 * Compares all bytes regardless of where differences occur.
 * Returns 0 if equal, non-zero if different. */
static int BrokerStrCompare(const char* a, const char* b)
{
    int result = 0;
    int len_a = (int)XSTRLEN(a);
    int len_b = (int)XSTRLEN(b);
    int max_len = (len_a > len_b) ? len_a : len_b;
    int i;
    for (i = 0; i < max_len; i++) {
        /* Branchless index clamp: when i >= len, reads position 0.
         * Length mismatch is caught by the final XOR below. */
        unsigned int maskA = 0u - (unsigned int)(i < len_a);
        unsigned int maskB = 0u - (unsigned int)(i < len_b);
        int ia = (int)((unsigned int)i & maskA);
        int ib = (int)((unsigned int)i & maskB);
        result |= (a[ia] ^ b[ib]);
    }
    result |= (len_a ^ len_b);
    return result;
}
#endif /* WOLFMQTT_BROKER_AUTH */

/* Store a string of known length into a BrokerClient field.
 * Static mode: copies into fixed-size buffer with truncation.
 * Dynamic mode: frees old value, allocates new buffer, copies. */
#ifdef WOLFMQTT_STATIC_MEMORY
static void BrokerStore_String(char* dst, int max_len,
    const char* src, word16 src_len)
{
    if (src_len >= (word16)max_len) {
        src_len = (word16)(max_len - 1);
    }
    XMEMCPY(dst, src, src_len);
    dst[src_len] = '\0';
}
static void BrokerStore_StringSensitive(char* dst, int max_len,
    const char* src, word16 src_len)
{
    /* Wipe old value before overwriting */
    BROKER_FORCE_ZERO(dst, max_len);
    if (src_len >= (word16)max_len) {
        src_len = (word16)(max_len - 1);
    }
    XMEMCPY(dst, src, src_len);
    dst[src_len] = '\0';
}
#else
static void BrokerStore_String(char** dst_ptr,
    const char* src, word16 src_len, int sensitive)
{
    if (*dst_ptr != NULL) {
        if (sensitive) {
            BROKER_FORCE_ZERO(*dst_ptr, XSTRLEN(*dst_ptr) + 1);
        }
        WOLFMQTT_FREE(*dst_ptr);
        *dst_ptr = NULL;
    }
    *dst_ptr = (char*)WOLFMQTT_MALLOC(src_len + 1);
    if (*dst_ptr != NULL) {
        XMEMCPY(*dst_ptr, src, src_len);
        (*dst_ptr)[src_len] = '\0';
    }
}
#endif

/* Wrapper macros to unify static/dynamic calling convention */
#ifdef WOLFMQTT_STATIC_MEMORY
    #define BROKER_STORE_STR(dst, src, len, maxlen) \
        BrokerStore_String(dst, maxlen, src, len)
    #define BROKER_STORE_STR_SENSITIVE(dst, src, len, maxlen) \
        BrokerStore_StringSensitive(dst, maxlen, src, len)
#else
    #define BROKER_STORE_STR(dst, src, len, maxlen) \
        BrokerStore_String(&(dst), src, len, 0)
    #define BROKER_STORE_STR_SENSITIVE(dst, src, len, maxlen) \
        BrokerStore_String(&(dst), src, len, 1)
#endif

/* -------------------------------------------------------------------------- */
/* Helper Macros and Functions (辅助宏和函数)                                   */
/* -------------------------------------------------------------------------- */

/* Unified client ID accessor with fallback */
#define BROKER_CLIENT_ID(bc) \
    (BROKER_STR_VALID((bc)->client_id) ? (bc)->client_id : "(null)")

/* Safe memory free with optional zeroing */
static inline void BrokerSafeFree(void** ptr, size_t size, int sensitive) {
    if (ptr == NULL || *ptr == NULL) return;
    if (sensitive && size > 0) {
        BROKER_FORCE_ZERO(*ptr, size);
    }
    WOLFMQTT_FREE(*ptr);
    *ptr = NULL;
}

/* Simple safe free macro for common cases (no sensitive data) */
#define BROKER_SAFE_FREE(ptr) do { \
    if ((ptr) != NULL) { \
        WOLFMQTT_FREE(ptr); \
        (ptr) = NULL; \
    } \
} while(0)

/* Structure initialization helpers */
#define BROKER_INIT_STRUCT(var) do { \
    XMEMSET(&(var), 0, sizeof(var)); \
} while(0)

#ifdef WOLFMQTT_V5
#define BROKER_INIT_MQTT_STRUCT(var, client) do { \
    BROKER_INIT_STRUCT(var); \
    (var).protocol_level = (client)->protocol_level; \
} while(0)
#else
#define BROKER_INIT_MQTT_STRUCT(var, client) BROKER_INIT_STRUCT(var)
#endif

/* Return code check helper for simple error cases */
#define BROKER_CHECK_RC(rc, broker, msg) do { \
    if ((rc) != MQTT_CODE_SUCCESS) { \
        WBLOG_ERR(broker, msg " rc=%d", (rc)); \
        return (rc); \
    } \
} while(0)

/* Magic number constants */
#define BROKER_MAX_PROP_CHAIN_LENGTH 200
#define BROKER_MAX_PAYLOAD_PREVIEW 128

/* Check if payload is printable text */
static int BrokerIsPrintableText(const byte* data, word32 len) {
    word32 i;
    if (data == NULL || len == 0) return 0;
    for (i = 0; i < len; i++) {
        byte c = data[i];
        /* Allow printable ASCII (32-126), tab (9), newline (10), carriage return (13) */
        if (c != '\t' && c != '\n' && c != '\r' && (c < 32 || c > 126)) {
            return 0;
        }
    }
    return 1;
}

/* Format payload for logging */
static void BrokerFormatPayload(char* buf, int buf_size, const byte* data, word32 len) {
    if (data == NULL || len == 0) {
        buf[0] = '\0';
        return;
    }
    
    if (BrokerIsPrintableText(data, len)) {
        /* Printable text - show directly, limit to preview size */
        word32 show_len = (len < BROKER_MAX_PAYLOAD_PREVIEW) ? len : BROKER_MAX_PAYLOAD_PREVIEW;
        XMEMCPY(buf, data, show_len);
        buf[show_len] = '\0';
        if (len > BROKER_MAX_PAYLOAD_PREVIEW) {
            /* Indicate truncation */
            int remaining = buf_size - (int)show_len;
            if (remaining > 4) {
                buf[show_len] = '.';
                buf[show_len + 1] = '.';
                buf[show_len + 2] = '.';
                buf[show_len + 3] = '\0';
            }
        }
    } else {
        /* Binary data - show hex */
        word32 show_len = (len < 16) ? len : 16; /* Show first 16 bytes */
        int pos = 0;
        word32 i;
        for (i = 0; i < show_len && pos < buf_size - 3; i++) {
            pos += sprintf(buf + pos, "%02X", data[i]);
        }
        if (len > 16) {
            if (pos < buf_size - 4) {
                buf[pos] = '.';
                buf[pos + 1] = '.';
                buf[pos + 2] = '.';
                buf[pos + 3] = '\0';
            }
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Topic Alias Management (主题别名管理)                                        */
/* -------------------------------------------------------------------------- */
#ifdef WOLFMQTT_V5

/* 初始化客户端的主题别名映射表 */
static void BrokerTopicAlias_Init(BrokerClient* bc)
{
#ifdef WOLFMQTT_STATIC_MEMORY
    int i;
    for (i = 0; i < BROKER_MAX_TOPIC_ALIASES; i++) {
        bc->topic_aliases[i].in_use = 0;
        bc->topic_aliases[i].alias_id = 0;
        bc->topic_aliases[i].topic[0] = '\0';
    }
#else
    bc->topic_aliases = NULL;
#endif
    bc->topic_alias_maximum = 0;
}

/* 清空客户端的主题别名映射表 */
static void BrokerTopicAlias_Clear(BrokerClient* bc)
{
#ifdef WOLFMQTT_STATIC_MEMORY
    int i;
    for (i = 0; i < BROKER_MAX_TOPIC_ALIASES; i++) {
        bc->topic_aliases[i].in_use = 0;
        bc->topic_aliases[i].alias_id = 0;
        bc->topic_aliases[i].topic[0] = '\0';
    }
#else
    BrokerTopicAlias* alias = bc->topic_aliases;
    while (alias != NULL) {
        BrokerTopicAlias* next = alias->next;
        BROKER_SAFE_FREE(alias->topic);
        BROKER_SAFE_FREE(alias);
        alias = next;
    }
    bc->topic_aliases = NULL;
#endif
}

/* 查找主题别名映射，返回主题名（静态模式返回内部指针，动态模式返回内部指针） */
static const char* BrokerTopicAlias_Find(BrokerClient* bc, word16 alias_id)
{
#ifdef WOLFMQTT_STATIC_MEMORY
    int i;
    for (i = 0; i < BROKER_MAX_TOPIC_ALIASES; i++) {
        if (bc->topic_aliases[i].in_use && bc->topic_aliases[i].alias_id == alias_id) {
            return bc->topic_aliases[i].topic;
        }
    }
#else
    BrokerTopicAlias* alias = bc->topic_aliases;
    while (alias != NULL) {
        if (alias->alias_id == alias_id) {
            return alias->topic;
        }
        alias = alias->next;
    }
#endif
    return NULL;
}

/* 添加或更新主题别名映射 */
static int BrokerTopicAlias_Set(BrokerClient* bc, word16 alias_id,
    const char* topic_name, word16 topic_len)
{
    /* 检查别名ID是否有效（MQTT 5.0 规范：1-65535）*/
    if (alias_id == 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

#ifdef WOLFMQTT_STATIC_MEMORY
    /* 静态内存模式：检查是否超过 broker 的实际限制 */
    if (alias_id > BROKER_MAX_TOPIC_ALIASES) {
        WBLOG_ERR(bc->broker,
            "Topic alias %u exceeds broker limit %d",
            alias_id, BROKER_MAX_TOPIC_ALIASES);
        return MQTT_CODE_ERROR_BAD_ARG;
    }
#else
    /* 动态内存模式：检查是否超过客户端声明的最大值 */
    if (bc->topic_alias_maximum > 0 && alias_id > bc->topic_alias_maximum) {
        WBLOG_ERR(bc->broker,
            "Topic alias %u exceeds client declared maximum %u",
            alias_id, bc->topic_alias_maximum);
        return MQTT_CODE_ERROR_BAD_ARG;
    }
#endif

#ifdef WOLFMQTT_STATIC_MEMORY
    /* 静态内存模式：查找空闲槽位或已存在的同ID条目 */
    int i;
    int free_slot = -1;
    for (i = 0; i < BROKER_MAX_TOPIC_ALIASES; i++) {
        if (bc->topic_aliases[i].in_use) {
            if (bc->topic_aliases[i].alias_id == alias_id) {
                /* 更新现有条目 */
                word16 copy_len = topic_len;
                if (copy_len >= BROKER_MAX_TOPIC_LEN) {
                    copy_len = BROKER_MAX_TOPIC_LEN - 1;
                }
                XMEMCPY(bc->topic_aliases[i].topic, topic_name, copy_len);
                bc->topic_aliases[i].topic[copy_len] = '\0';
                return MQTT_CODE_SUCCESS;
            }
        } else if (free_slot < 0) {
            free_slot = i;
        }
    }

    /* 没有找到现有条目，使用空闲槽位 */
    if (free_slot >= 0) {
        word16 copy_len = topic_len;
        if (copy_len >= BROKER_MAX_TOPIC_LEN) {
            copy_len = BROKER_MAX_TOPIC_LEN - 1;
        }
        XMEMCPY(bc->topic_aliases[free_slot].topic, topic_name, copy_len);
        bc->topic_aliases[free_slot].topic[copy_len] = '\0';
        bc->topic_aliases[free_slot].alias_id = alias_id;
        bc->topic_aliases[free_slot].in_use = 1;
        return MQTT_CODE_SUCCESS;
    }

    /* 没有空闲槽位 */
    return MQTT_CODE_ERROR_OUT_OF_BUFFER;
#else
    /* 动态内存模式：使用链表 */
    BrokerTopicAlias* alias = bc->topic_aliases;
    BrokerTopicAlias* prev = NULL;

    /* 查找已存在的同ID条目 */
    while (alias != NULL) {
        if (alias->alias_id == alias_id) {
            /* 更新现有条目 */
            BROKER_SAFE_FREE(alias->topic);
            alias->topic = (char*)WOLFMQTT_MALLOC(topic_len + 1);
            if (alias->topic == NULL) {
                return MQTT_CODE_ERROR_MEMORY;
            }
            XMEMCPY(alias->topic, topic_name, topic_len);
            alias->topic[topic_len] = '\0';
            alias->topic_len = topic_len;
            return MQTT_CODE_SUCCESS;
        }
        prev = alias;
        alias = alias->next;
    }

    /* 创建新条目 */
    alias = (BrokerTopicAlias*)WOLFMQTT_MALLOC(sizeof(BrokerTopicAlias));
    if (alias == NULL) {
        return MQTT_CODE_ERROR_MEMORY;
    }
    alias->topic = (char*)WOLFMQTT_MALLOC(topic_len + 1);
    if (alias->topic == NULL) {
        BROKER_SAFE_FREE(alias);
        return MQTT_CODE_ERROR_MEMORY;
    }
    XMEMCPY(alias->topic, topic_name, topic_len);
    alias->topic[topic_len] = '\0';
    alias->topic_len = topic_len;
    alias->alias_id = alias_id;
    alias->next = NULL;

    /* 添加到链表 */
    if (prev == NULL) {
        bc->topic_aliases = alias;
    } else {
        prev->next = alias;
    }

    return MQTT_CODE_SUCCESS;
#endif
}

#endif /* WOLFMQTT_V5 */

/* -------------------------------------------------------------------------- */
/* Property deep copy helper for safe forwarding (属性深拷贝辅助函数)        */
/* -------------------------------------------------------------------------- */
#ifdef WOLFMQTT_V5
/* Deep copy a single property - returns 0 on success, error code on failure */
static int BrokerProp_Copy(MqttProp* dest, const MqttProp* src)
{
    if (dest == NULL || src == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    /* Copy the entire structure first (includes type and numeric values) */
    XMEMCPY(dest, src, sizeof(MqttProp));

    /* CRITICAL: Reset next pointer to NULL to prevent list corruption!
     * MqttProps_Add will set this when adding to the list. */
    dest->next = NULL;

    /* Initialize pointers to NULL for error handling */
    dest->data_str.str = NULL;
    dest->data_str2.str = NULL;
    dest->data_bin.data = NULL;

    /* Deep copy based on property type */
    switch (src->type) {
        case MQTT_PROP_CONTENT_TYPE:          /* data_str */
        case MQTT_PROP_RESP_TOPIC:            /* data_str */
        case MQTT_PROP_ASSIGNED_CLIENT_ID:    /* data_str */
        case MQTT_PROP_AUTH_METHOD:           /* data_str */
        case MQTT_PROP_REQ_PROB_INFO:         /* data_str */
        case MQTT_PROP_RESP_INFO:             /* data_str */
        case MQTT_PROP_SERVER_REF:            /* data_str */
        case MQTT_PROP_REASON_STR:            /* data_str */
            if (src->data_str.str != NULL && src->data_str.len > 0) {
                dest->data_str.str = (char*)WOLFMQTT_MALLOC(src->data_str.len + 1);
                if (dest->data_str.str == NULL) {
                    return MQTT_CODE_ERROR_MEMORY;
                }
                XMEMCPY(dest->data_str.str, src->data_str.str, src->data_str.len);
                dest->data_str.str[src->data_str.len] = '\0';
            }
            break;

        case MQTT_PROP_CORRELATION_DATA:      /* data_bin */
        case MQTT_PROP_AUTH_DATA:             /* data_bin */
            if (src->data_bin.data != NULL && src->data_bin.len > 0) {
                dest->data_bin.data = (byte*)WOLFMQTT_MALLOC(src->data_bin.len);
                if (dest->data_bin.data == NULL) {
                    return MQTT_CODE_ERROR_MEMORY;
                }
                XMEMCPY(dest->data_bin.data, src->data_bin.data, src->data_bin.len);
            }
            break;

        case MQTT_PROP_USER_PROP:             /* data_str + data_str2 (key + value) */
            /* Copy key */
            if (src->data_str.str != NULL && src->data_str.len > 0) {
                dest->data_str.str = (char*)WOLFMQTT_MALLOC(src->data_str.len + 1);
                if (dest->data_str.str == NULL) {
                    return MQTT_CODE_ERROR_MEMORY;
                }
                XMEMCPY(dest->data_str.str, src->data_str.str, src->data_str.len);
                dest->data_str.str[src->data_str.len] = '\0';
            }
            /* Copy value */
            if (src->data_str2.str != NULL && src->data_str2.len > 0) {
                dest->data_str2.str = (char*)WOLFMQTT_MALLOC(src->data_str2.len + 1);
                if (dest->data_str2.str == NULL) {
                    BROKER_SAFE_FREE(dest->data_str.str);
                    return MQTT_CODE_ERROR_MEMORY;
                }
                XMEMCPY(dest->data_str2.str, src->data_str2.str, src->data_str2.len);
                dest->data_str2.str[src->data_str2.len] = '\0';
            }
            break;

        /* Numeric types - already copied by XMEMCPY, no additional work needed */
        case MQTT_PROP_PAYLOAD_FORMAT_IND:
        case MQTT_PROP_MSG_EXPIRY_INTERVAL:
        case MQTT_PROP_SUBSCRIPTION_ID:
        case MQTT_PROP_SESSION_EXPIRY_INTERVAL:
        case MQTT_PROP_WILL_DELAY_INTERVAL:
        case MQTT_PROP_TOPIC_ALIAS:
        case MQTT_PROP_MAX_QOS:
        case MQTT_PROP_RETAIN_AVAIL:
        default:
            break;
    }

    return MQTT_CODE_SUCCESS;
}
#endif

#if defined(ENABLE_MQTT_TLS) && !defined(WOLFMQTT_BROKER_CUSTOM_NET)
static int BrokerTls_Init(MqttBroker* broker)
{
    WOLFSSL_CTX* ctx = NULL;
    int wolf_rc; /* wolfSSL return codes (compared against WOLFSSL_SUCCESS) */
    int mqtt_rc = MQTT_CODE_SUCCESS; /* normalized MQTT return code */

    wolf_rc = wolfSSL_Init();
    if (wolf_rc != WOLFSSL_SUCCESS) {
        WBLOG_ERR(broker, "wolfSSL_Init failed %d", wolf_rc);
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    /* Select TLS method based on version preference */
    if (broker->tls_version == 12) {
        ctx = wolfSSL_CTX_new(wolfTLSv1_2_server_method());
    }
    else if (broker->tls_version == 13) {
        ctx = wolfSSL_CTX_new(wolfTLSv1_3_server_method());
    }
    else {
        ctx = wolfSSL_CTX_new(wolfSSLv23_server_method());
        if (ctx != NULL) {
            wolfSSL_CTX_SetMinVersion(ctx, WOLFSSL_TLSV1_2);
        }
    }
    if (ctx == NULL) {
        WBLOG_ERR(broker, "wolfSSL_CTX_new failed");
        mqtt_rc = MQTT_CODE_ERROR_MEMORY;
    }

    /* Load server certificate */
    if (mqtt_rc == MQTT_CODE_SUCCESS) {
        if (broker->tls_cert == NULL) {
            WBLOG_ERR(broker, "TLS cert not set (-c)");
            mqtt_rc = MQTT_CODE_ERROR_BAD_ARG;
        }
    }
    if (mqtt_rc == MQTT_CODE_SUCCESS) {
#ifndef NO_FILESYSTEM
        wolf_rc = wolfSSL_CTX_use_certificate_file(ctx, broker->tls_cert,
            WOLFSSL_FILETYPE_PEM);
        if (wolf_rc != WOLFSSL_SUCCESS) {
            WBLOG_ERR(broker, "load cert failed %d (%s)",
                wolf_rc, broker->tls_cert);
            mqtt_rc = MQTT_CODE_ERROR_BAD_ARG;
        }
#else
        /* File operations not available in NO_FILESYSTEM builds */
        mqtt_rc = MQTT_CODE_ERROR_BAD_ARG;
#endif
    }

    /* Load server private key */
    if (mqtt_rc == MQTT_CODE_SUCCESS) {
        if (broker->tls_key == NULL) {
            WBLOG_ERR(broker, "TLS key not set (-K)");
            mqtt_rc = MQTT_CODE_ERROR_BAD_ARG;
        }
    }
    if (mqtt_rc == MQTT_CODE_SUCCESS) {
#ifndef NO_FILESYSTEM
        wolf_rc = wolfSSL_CTX_use_PrivateKey_file(ctx, broker->tls_key,
            WOLFSSL_FILETYPE_PEM);
        if (wolf_rc != WOLFSSL_SUCCESS) {
            WBLOG_ERR(broker, "load key failed %d (%s)",
                wolf_rc, broker->tls_key);
            mqtt_rc = MQTT_CODE_ERROR_BAD_ARG;
        }
#else
        mqtt_rc = MQTT_CODE_ERROR_BAD_ARG;
#endif
    }

    /* Set wolfSSL IO callbacks */
    if (mqtt_rc == MQTT_CODE_SUCCESS) {
        wolfSSL_CTX_SetIORecv(ctx, MqttSocket_TlsSocketReceive);
        wolfSSL_CTX_SetIOSend(ctx, MqttSocket_TlsSocketSend);
    }

    /* Mutual TLS: load CA and require client certificate */
    if (mqtt_rc == MQTT_CODE_SUCCESS && broker->tls_ca != NULL) {
#ifndef NO_FILESYSTEM
        wolf_rc = wolfSSL_CTX_load_verify_locations(ctx, broker->tls_ca,
            NULL);
        if (wolf_rc != WOLFSSL_SUCCESS) {
            WBLOG_ERR(broker, "load CA failed %d (%s)",
                wolf_rc, broker->tls_ca);
            mqtt_rc = MQTT_CODE_ERROR_BAD_ARG;
        }
#else
        mqtt_rc = MQTT_CODE_ERROR_BAD_ARG;
#endif
        if (mqtt_rc == MQTT_CODE_SUCCESS) {
            wolfSSL_CTX_set_verify(ctx,
                WOLFSSL_VERIFY_PEER | WOLFSSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                NULL);
            WBLOG_INFO(broker, "mutual TLS enabled (CA=%s)",
                broker->tls_ca);
        }
    }

    if (mqtt_rc == MQTT_CODE_SUCCESS) {
        broker->tls_ctx = ctx;
        broker->tls_ctx_owned = 1;
    }
    else {
        if (ctx != NULL) {
            wolfSSL_CTX_free(ctx);
        }
        wolfSSL_Cleanup();
    }
    return mqtt_rc;
}

static void BrokerTls_Free(MqttBroker* broker)
{
    if (broker->tls_ctx != NULL) {
        wolfSSL_CTX_free(broker->tls_ctx);
        broker->tls_ctx = NULL;
    }
    wolfSSL_Cleanup();
}
#endif /* ENABLE_MQTT_TLS && !WOLFMQTT_BROKER_CUSTOM_NET */

/* -------------------------------------------------------------------------- */
/* wolfIP network backend                                                      */
/* -------------------------------------------------------------------------- */
#if defined(WOLFMQTT_WOLFIP)

/* Context passed through MqttBrokerNet.ctx */
#ifndef WOLFMQTT_WOLFIP_CTX_DEFINED
#define WOLFMQTT_WOLFIP_CTX_DEFINED
typedef struct BrokerWolfIP_Ctx {
    struct wolfIP *stack;
} BrokerWolfIP_Ctx;
#endif

/* Single-instance context: wolfIP targets are typically embedded systems
 * with one broker instance. For multiple instances, use
 * WOLFMQTT_BROKER_CUSTOM_NET and provide per-instance context. */
static BrokerWolfIP_Ctx broker_wolfip_ctx;

static int BrokerWolfIP_Listen(void* ctx, BROKER_SOCKET_T* sock,
    word16 port, int backlog)
{
    BrokerWolfIP_Ctx* wctx = (BrokerWolfIP_Ctx*)ctx;
    struct wolfIP_sockaddr_in addr;
    BROKER_SOCKET_T fd;

    if (wctx == NULL || wctx->stack == NULL || sock == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    fd = wolfIP_sock_socket(wctx->stack, AF_INET, IPSTACK_SOCK_STREAM, 0);
    if (fd < 0) {
        return MQTT_CODE_ERROR_NETWORK;
    }

    XMEMSET(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = ee16(port);
    addr.sin_addr.s_addr = 0; /* INADDR_ANY */

    if (wolfIP_sock_bind(wctx->stack, fd,
            (struct wolfIP_sockaddr*)&addr, sizeof(addr)) < 0) {
        wolfIP_sock_close(wctx->stack, fd);
        return MQTT_CODE_ERROR_NETWORK;
    }
    if (wolfIP_sock_listen(wctx->stack, fd, backlog) < 0) {
        wolfIP_sock_close(wctx->stack, fd);
        return MQTT_CODE_ERROR_NETWORK;
    }

    *sock = fd;
    return MQTT_CODE_SUCCESS;
}

static int BrokerWolfIP_Accept(void* ctx, BROKER_SOCKET_T listen_sock,
    BROKER_SOCKET_T* client_sock)
{
    BrokerWolfIP_Ctx* wctx = (BrokerWolfIP_Ctx*)ctx;
    BROKER_SOCKET_T fd;

    if (wctx == NULL || wctx->stack == NULL || client_sock == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    fd = wolfIP_sock_accept(wctx->stack, listen_sock, NULL, NULL);
    if (fd == -WOLFIP_EAGAIN) {
        /* No pending connection */
        return MQTT_CODE_CONTINUE;
    }
    if (fd < 0) {
        return MQTT_CODE_ERROR_NETWORK;
    }

    *client_sock = fd;
    return MQTT_CODE_SUCCESS;
}

static int BrokerWolfIP_Read(void* ctx, BROKER_SOCKET_T sock,
    byte* buf, int buf_len, int timeout_ms)
{
    BrokerWolfIP_Ctx* wctx = (BrokerWolfIP_Ctx*)ctx;
    int rc;
    (void)timeout_ms;

    if (wctx == NULL || wctx->stack == NULL || buf == NULL || buf_len <= 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    rc = wolfIP_sock_recv(wctx->stack, sock, buf, (size_t)buf_len, 0);
    /* -WOLFIP_EAGAIN: no data yet; -1: socket not yet in ESTABLISHED state */
    if (rc == -WOLFIP_EAGAIN || rc == -1) {
        return MQTT_CODE_CONTINUE;
    }
    if (rc <= 0) {
        return MQTT_CODE_ERROR_NETWORK;
    }
    return rc;
}

static int BrokerWolfIP_Write(void* ctx, BROKER_SOCKET_T sock,
    const byte* buf, int buf_len, int timeout_ms)
{
    BrokerWolfIP_Ctx* wctx = (BrokerWolfIP_Ctx*)ctx;
    int rc;
    (void)timeout_ms;

    if (wctx == NULL || wctx->stack == NULL || buf == NULL || buf_len <= 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    rc = wolfIP_sock_send(wctx->stack, sock, buf, (size_t)buf_len, 0);
    /* -WOLFIP_EAGAIN: send buffer full; -1: socket not yet in ESTABLISHED state */
    if (rc == -WOLFIP_EAGAIN || rc == -1) {
        return MQTT_CODE_CONTINUE;
    }
    if (rc <= 0) {
        return MQTT_CODE_ERROR_NETWORK;
    }
    return rc;
}

static int BrokerWolfIP_Close(void* ctx, BROKER_SOCKET_T sock)
{
    BrokerWolfIP_Ctx* wctx = (BrokerWolfIP_Ctx*)ctx;

    if (wctx != NULL && wctx->stack != NULL &&
        sock != BROKER_SOCKET_INVALID) {
        wolfIP_sock_close(wctx->stack, sock);
    }
    return MQTT_CODE_SUCCESS;
}

int MqttBrokerNet_wolfIP_Init(MqttBrokerNet* net, void* wolfIP_stack)
{
    if (net == NULL || wolfIP_stack == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    XMEMSET(net, 0, sizeof(*net));
    XMEMSET(&broker_wolfip_ctx, 0, sizeof(broker_wolfip_ctx));
    broker_wolfip_ctx.stack = (struct wolfIP*)wolfIP_stack;

    net->listen = BrokerWolfIP_Listen;
    net->accept = BrokerWolfIP_Accept;
    net->read   = BrokerWolfIP_Read;
    net->write  = BrokerWolfIP_Write;
    net->close  = BrokerWolfIP_Close;
    net->ctx    = &broker_wolfip_ctx;
    return MQTT_CODE_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* Default POSIX network backend                                               */
/* -------------------------------------------------------------------------- */
#elif !defined(WOLFMQTT_BROKER_CUSTOM_NET)

static int BrokerPosix_SetNonBlocking(BROKER_SOCKET_T fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return MQTT_CODE_ERROR_SYSTEM;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return MQTT_CODE_ERROR_SYSTEM;
    }
    return MQTT_CODE_SUCCESS;
}

static int BrokerPosix_Listen(void* ctx, BROKER_SOCKET_T* sock,
    word16 port, int backlog)
{
    struct sockaddr_in addr;
    int opt = 1;
    BROKER_SOCKET_T fd;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        WBLOG_ERR((MqttBroker*)ctx, "socket failed (%d)", errno);
        return MQTT_CODE_ERROR_NETWORK;
    }

    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (BrokerPosix_SetNonBlocking(fd) != MQTT_CODE_SUCCESS) {
        WBLOG_ERR((MqttBroker*)ctx, "set nonblocking failed (%d)", errno);
        close(fd);
        return MQTT_CODE_ERROR_SYSTEM;
    }

    XMEMSET(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        WBLOG_ERR((MqttBroker*)ctx, "bind failed (%d)", errno);
        close(fd);
        return MQTT_CODE_ERROR_NETWORK;
    }
    if (listen(fd, backlog) < 0) {
        WBLOG_ERR((MqttBroker*)ctx, "listen failed (%d)", errno);
        close(fd);
        return MQTT_CODE_ERROR_NETWORK;
    }

    *sock = fd;
    return MQTT_CODE_SUCCESS;
}

static int BrokerPosix_Accept(void* ctx, BROKER_SOCKET_T listen_sock,
    BROKER_SOCKET_T* client_sock)
{
    BROKER_SOCKET_T fd;
    (void)ctx;

    fd = accept(listen_sock, NULL, NULL);
    if (fd < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return MQTT_CODE_CONTINUE;
        }
        return MQTT_CODE_ERROR_NETWORK;
    }
    if (BrokerPosix_SetNonBlocking(fd) != MQTT_CODE_SUCCESS) {
        close(fd);
        return MQTT_CODE_ERROR_SYSTEM;
    }
    *client_sock = fd;
    return MQTT_CODE_SUCCESS;
}

static int BrokerPosix_Read(void* ctx, BROKER_SOCKET_T sock,
    byte* buf, int buf_len, int timeout_ms)
{
    fd_set rfds;
    struct timeval tv;
    int rc;

    if (buf == NULL || buf_len <= 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    if (sock < 0 || sock >= FD_SETSIZE) {
        return MQTT_CODE_ERROR_NETWORK;
    }

    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    rc = select(sock + 1, &rfds, NULL, NULL, &tv);
    if (rc == 0) {
        return MQTT_CODE_ERROR_TIMEOUT;
    }
    if (rc < 0) {
        return MQTT_CODE_ERROR_NETWORK;
    }

    rc = (int)recv(sock, buf, (size_t)buf_len, 0);
    if (rc < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return MQTT_CODE_CONTINUE;
        }
        return MQTT_CODE_ERROR_NETWORK;
    }
    else if (rc == 0) {
        /* Connection closed by peer - return error to trigger cleanup */
        return MQTT_CODE_ERROR_NETWORK;
    }
    return rc;
}

static int BrokerPosix_Write(void* ctx, BROKER_SOCKET_T sock,
    const byte* buf, int buf_len, int timeout_ms)
{
    fd_set wfds;
    struct timeval tv;
    int rc;

    if (buf == NULL || buf_len <= 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    if (sock < 0 || sock >= FD_SETSIZE) {
        return MQTT_CODE_ERROR_NETWORK;
    }

    FD_ZERO(&wfds);
    FD_SET(sock, &wfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    rc = select(sock + 1, NULL, &wfds, NULL, &tv);
    if (rc == 0) {
        return MQTT_CODE_ERROR_TIMEOUT;
    }
    if (rc < 0) {
        return MQTT_CODE_ERROR_NETWORK;
    }

    rc = (int)send(sock, buf, (size_t)buf_len, 0);
    if (rc <= 0) {
        if (rc < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
            return MQTT_CODE_CONTINUE;
        }
        WBLOG_ERR((MqttBroker*)ctx, "send error sock=%d rc=%d errno=%d",
            (int)sock, rc, errno);
        return MQTT_CODE_ERROR_NETWORK;
    }
    return rc;
}

static int BrokerPosix_Close(void* ctx, BROKER_SOCKET_T sock)
{
    (void)ctx;
    if (sock != BROKER_SOCKET_INVALID) {
        close(sock);
    }
    return MQTT_CODE_SUCCESS;
}

int MqttBrokerNet_Init(MqttBrokerNet* net)
{
    if (net == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    XMEMSET(net, 0, sizeof(*net));
    net->listen = BrokerPosix_Listen;
    net->accept = BrokerPosix_Accept;
    net->read   = BrokerPosix_Read;
    net->write  = BrokerPosix_Write;
    net->close  = BrokerPosix_Close;
    net->ctx    = NULL;
    return MQTT_CODE_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* epoll helper functions (Linux I/O multiplexing)                             */
/* -------------------------------------------------------------------------- */
#ifdef WOLFMQTT_BROKER_EPOLL

/* Initialize epoll for the broker */
static int BrokerEpoll_Init(MqttBroker* broker)
{
    if (broker == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    /* Use configured max_events or default */
    if (broker->epoll_max_events <= 0) {
        broker->epoll_max_events = BROKER_EPOLL_MAX_EVENTS_DEFAULT;
    }

    /* Create epoll instance */
    broker->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (broker->epoll_fd < 0) {
        WBLOG_ERR(broker, "epoll_create1 failed: %s", strerror(errno));
        return MQTT_CODE_ERROR_SYSTEM;
    }

    /* Allocate event array */
    broker->epoll_events = (struct epoll_event*)malloc(
        sizeof(struct epoll_event) * broker->epoll_max_events);
    if (broker->epoll_events == NULL) {
        close(broker->epoll_fd);
        broker->epoll_fd = -1;
        return MQTT_CODE_ERROR_MEMORY;
    }

    WBLOG_INFO(broker, "epoll initialized (max_events=%d)", broker->epoll_max_events);
    return MQTT_CODE_SUCCESS;
}

/* Cleanup epoll resources */
static void BrokerEpoll_Cleanup(MqttBroker* broker)
{
    if (broker == NULL) {
        return;
    }

    if (broker->epoll_events != NULL) {
        free(broker->epoll_events);
        broker->epoll_events = NULL;
    }

    if (broker->epoll_fd >= 0) {
        close(broker->epoll_fd);
        broker->epoll_fd = -1;
    }
}

/* Add socket to epoll monitoring */
static int BrokerEpoll_AddSocket(MqttBroker* broker, BROKER_SOCKET_T sock, uint32_t events)
{
    struct epoll_event ev;
    int rc;

    if (broker == NULL || sock < 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.fd = sock;

    rc = epoll_ctl(broker->epoll_fd, EPOLL_CTL_ADD, sock, &ev);
    if (rc < 0) {
        WBLOG_ERR(broker, "epoll_ctl ADD failed for sock=%d: %s",
            (int)sock, strerror(errno));
        return MQTT_CODE_ERROR_NETWORK;
    }

    return MQTT_CODE_SUCCESS;
}

/* Remove socket from epoll monitoring */
static int BrokerEpoll_DelSocket(MqttBroker* broker, BROKER_SOCKET_T sock)
{
    int rc;

    if (broker == NULL || sock < 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    rc = epoll_ctl(broker->epoll_fd, EPOLL_CTL_DEL, sock, NULL);
    if (rc < 0) {
        WBLOG_ERR(broker, "epoll_ctl DEL failed for sock=%d: %s",
            (int)sock, strerror(errno));
        return MQTT_CODE_ERROR_NETWORK;
    }

    return MQTT_CODE_SUCCESS;
}

/* Modify socket events in epoll */
static int BrokerEpoll_ModSocket(MqttBroker* broker, BROKER_SOCKET_T sock, uint32_t events)
{
    struct epoll_event ev;
    int rc;

    if (broker == NULL || sock < 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.fd = sock;

    rc = epoll_ctl(broker->epoll_fd, EPOLL_CTL_MOD, sock, &ev);
    if (rc < 0) {
        WBLOG_ERR(broker, "epoll_ctl MOD failed for sock=%d: %s",
            (int)sock, strerror(errno));
        return MQTT_CODE_ERROR_NETWORK;
    }

    return MQTT_CODE_SUCCESS;
}

/* Wait for events using epoll */
static int BrokerEpoll_Wait(MqttBroker* broker, int timeout_ms)
{
    int nfds;

    if (broker == NULL || broker->epoll_fd < 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    nfds = epoll_wait(broker->epoll_fd, broker->epoll_events,
        broker->epoll_max_events, timeout_ms);

    if (nfds < 0) {
        if (errno == EINTR) {
            return MQTT_CODE_CONTINUE; /* Interrupted by signal */
        }
        WBLOG_ERR(broker, "epoll_wait failed: %s", strerror(errno));
        return MQTT_CODE_ERROR_NETWORK;
    }

    return nfds; /* Number of events ready */
}

#endif /* WOLFMQTT_BROKER_EPOLL */



#endif /* WOLFMQTT_WOLFIP / !WOLFMQTT_BROKER_CUSTOM_NET */

/* -------------------------------------------------------------------------- */
/* Per-client MqttNet callbacks (route through MqttBrokerNet)                  */
/* -------------------------------------------------------------------------- */


/* -------------------------------------------------------------------------- */
/* Per-client MqttNet callbacks (route through MqttBrokerNet)                  */
/* -------------------------------------------------------------------------- */
static int BrokerNetConnect(void* context, const char* host, word16 port,
    int timeout_ms)
{
    /* Server side: connection already established via accept() */
    (void)context;
    (void)host;
    (void)port;
    (void)timeout_ms;
    return MQTT_CODE_SUCCESS;
}

static int BrokerNetRead(void* context, byte* buf, int buf_len,
    int timeout_ms)
{
    BrokerClient* bc = (BrokerClient*)context;
    if (bc == NULL || bc->broker == NULL || buf == NULL || buf_len <= 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* Use unified transport layer */
    return BrokerTransport_Read(bc, buf, buf_len, timeout_ms);
}

static int BrokerNetWrite(void* context, const byte* buf, int buf_len,
    int timeout_ms)
{
    BrokerClient* bc = (BrokerClient*)context;
    if (bc == NULL || bc->broker == NULL || buf == NULL || buf_len <= 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* Use unified transport layer */
    return BrokerTransport_Write(bc, buf, buf_len, timeout_ms);
}

static int BrokerNetDisconnect(void* context)
{
    BrokerClient* bc = (BrokerClient*)context;
    if (bc != NULL && bc->broker != NULL &&
        bc->sock != BROKER_SOCKET_INVALID) {
        bc->broker->net.close(bc->broker->net.ctx, bc->sock);
        bc->sock = BROKER_SOCKET_INVALID;
    }
    return MQTT_CODE_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* Client management                                                           */
/* -------------------------------------------------------------------------- */
static void BrokerClient_Free(BrokerClient* bc)
{
    if (bc == NULL) {
        return;
    }

    /* WebSocket cleanup is now handled by BrokerTransport_Cleanup */

#ifdef ENABLE_MQTT_TLS
    if (bc->client.tls.ssl) {
        /* Send close_notify before closing the socket, because
         * wolfSSL_shutdown uses I/O callbacks that need a valid fd */
        if (bc->tls_handshake_done) {
            wolfSSL_shutdown(bc->client.tls.ssl);
        }
        wolfSSL_free(bc->client.tls.ssl);
        bc->client.tls.ssl = NULL;
    }
#endif
    (void)BrokerNetDisconnect(bc);
    MqttClient_DeInit(&bc->client);
#ifdef WOLFMQTT_STATIC_MEMORY
    XMEMSET(bc, 0, sizeof(*bc));
    /* in_use is now 0 after memset */
#else
    if (bc->client_id) {
        WOLFMQTT_FREE(bc->client_id);
    }
#ifdef WOLFMQTT_BROKER_AUTH
    if (bc->username) {
        BROKER_FORCE_ZERO(bc->username, XSTRLEN(bc->username) + 1);
        WOLFMQTT_FREE(bc->username);
    }
    if (bc->password) {
        BROKER_FORCE_ZERO(bc->password, XSTRLEN(bc->password) + 1);
        WOLFMQTT_FREE(bc->password);
    }
#endif
#ifdef WOLFMQTT_BROKER_WILL
    if (bc->will_topic) {
        BROKER_FORCE_ZERO(bc->will_topic, XSTRLEN(bc->will_topic) + 1);
        WOLFMQTT_FREE(bc->will_topic);
    }
    if (bc->will_payload) {
        BROKER_FORCE_ZERO(bc->will_payload, bc->will_payload_len);
        WOLFMQTT_FREE(bc->will_payload);
    }
#endif
    if (bc->tx_buf) {
        WOLFMQTT_FREE(bc->tx_buf);
    }
    if (bc->rx_buf) {
        WOLFMQTT_FREE(bc->rx_buf);
    }
#ifdef WOLFMQTT_V5
    /* Clean up topic alias mappings */
    BrokerTopicAlias_Clear(bc);
#endif
    WOLFMQTT_FREE(bc);
#endif
}

static BrokerClient* BrokerClient_Add(MqttBroker* broker,
    BROKER_SOCKET_T sock, int is_tls)
{
    BrokerClient* bc = NULL;
    int rc = MQTT_CODE_SUCCESS;

#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        for (i = 0; i < broker->max_clients; i++) {
            if (!broker->clients[i].in_use) {
                bc = &broker->clients[i];
                break;
            }
        }
        if (bc == NULL) {
            rc = MQTT_CODE_ERROR_MEMORY;
        }
        if (rc == MQTT_CODE_SUCCESS) {
            XMEMSET(bc, 0, sizeof(*bc));
            bc->in_use = 1;
        }
    }
#else
    bc = (BrokerClient*)WOLFMQTT_MALLOC(sizeof(BrokerClient));
    if (bc == NULL) {
        rc = MQTT_CODE_ERROR_MEMORY;
    }
    if (rc == MQTT_CODE_SUCCESS) {
        XMEMSET(bc, 0, sizeof(*bc));
        bc->tx_buf_len = broker->tx_buf_sz;
        bc->rx_buf_len = broker->rx_buf_sz;
        bc->tx_buf = (byte*)WOLFMQTT_MALLOC(bc->tx_buf_len);
        bc->rx_buf = (byte*)WOLFMQTT_MALLOC(bc->rx_buf_len);
        if (bc->tx_buf == NULL || bc->rx_buf == NULL) {
            rc = MQTT_CODE_ERROR_MEMORY;
        }
    }
#endif

    if (rc == MQTT_CODE_SUCCESS) {
        bc->sock = sock;
        bc->broker = broker;
        bc->protocol_level = 0;
        bc->keep_alive_sec = 0;
        bc->last_rx = WOLFMQTT_BROKER_GET_TIME_S();

        /* Get client IP address */
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        if (getpeername(sock, (struct sockaddr*)&addr, &addr_len) == 0) {
            inet_ntop(AF_INET, &addr.sin_addr, bc->client_ip, sizeof(bc->client_ip));
        } else {
            strncpy(bc->client_ip, "unknown", sizeof(bc->client_ip) - 1);
            bc->client_ip[sizeof(bc->client_ip) - 1] = '\0';
        }

        bc->net.context = bc;
        bc->net.connect = BrokerNetConnect;
        bc->net.read = BrokerNetRead;
        bc->net.write = BrokerNetWrite;
        bc->net.disconnect = BrokerNetDisconnect;

        rc = MqttClient_Init(&bc->client, &bc->net, NULL,
                bc->tx_buf, BROKER_CLIENT_TX_SZ(bc),
                bc->rx_buf, BROKER_CLIENT_RX_SZ(bc), broker->timeout_ms);
        if (rc != MQTT_CODE_SUCCESS) {
            WBLOG_ERR(broker, "client init failed rc=%d", rc);
        }
        /* Restore broker pointer that may have been zeroed by MqttClient_Init */
        bc->broker = broker;
    }

#ifdef ENABLE_MQTT_TLS
    if (rc == MQTT_CODE_SUCCESS) {
        if (is_tls && broker->tls_ctx) {
            bc->client.tls.ssl = wolfSSL_new(broker->tls_ctx);
            if (bc->client.tls.ssl == NULL) {
                WBLOG_ERR(broker, "wolfSSL_new failed sock=%d", (int)sock);
                rc = MQTT_CODE_ERROR_MEMORY;
            }
            else {
                wolfSSL_SetIOReadCtx(bc->client.tls.ssl, &bc->client);
                wolfSSL_SetIOWriteCtx(bc->client.tls.ssl, &bc->client);
                MqttClient_Flags(&bc->client, 0, MQTT_CLIENT_FLAG_IS_TLS);
                bc->tls_handshake_done = 0;
                
                /* Initialize TLS transport */
                rc = BrokerTransport_Init(bc, BROKER_TRANSPORT_TLS, broker);
                if (rc != MQTT_CODE_SUCCESS) {
                    WBLOG_ERR(broker, "TLS transport init failed rc=%d", rc);
                }
            }
        }
        else if (is_tls) {
            WBLOG_ERR(broker, "TLS ctx not set, rejecting sock=%d",
                (int)sock);
            rc = MQTT_CODE_ERROR_BAD_ARG;
        }
        else {
            bc->tls_handshake_done = 1;
            
            /* Initialize TCP transport */
            rc = BrokerTransport_Init(bc, BROKER_TRANSPORT_TCP, broker);
            if (rc != MQTT_CODE_SUCCESS) {
                WBLOG_ERR(broker, "TCP transport init failed rc=%d", rc);
            }
        }
    }
#else
    (void)is_tls;
    
    /* Initialize TCP transport for non-TLS builds */
    if (rc == MQTT_CODE_SUCCESS) {
        rc = BrokerTransport_Init(bc, BROKER_TRANSPORT_TCP, broker);
        if (rc != MQTT_CODE_SUCCESS) {
            WBLOG_ERR(broker, "TCP transport init failed rc=%d", rc);
        }
    }
#endif

    if (rc == MQTT_CODE_SUCCESS) {
#ifndef WOLFMQTT_STATIC_MEMORY
        /* Prepend to linked list */
        bc->next = broker->clients;
        broker->clients = bc;
#endif

#ifdef WOLFMQTT_BROKER_EPOLL
        /* Add socket to epoll monitoring (read events) */
        /* Use level-triggered mode for better compatibility with WebSocket */
        rc = BrokerEpoll_AddSocket(broker, sock, EPOLLIN);
        if (rc != MQTT_CODE_SUCCESS) {
            WBLOG_ERR(broker, "failed to add sock=%d to epoll", (int)sock);
            /* Continue anyway - client will be handled in next iteration */
        }
#endif
    }
    else if (bc != NULL) {
        BrokerClient_Free(bc);
        bc = NULL;
    }

    return bc;
}

static void BrokerClient_Remove(MqttBroker* broker, BrokerClient* bc, int reason)
{
    int count_before = 0;

    if (broker == NULL || bc == NULL) {
        return;
    }

    /* Count connected clients before removal if client was connected */
    if (bc->connected) {
        /* 触发 on_disconnect 回调 */
        if (broker->on_disconnect) {
            broker->on_disconnect(broker, bc->sock,
                BROKER_STR_VALID(bc->client_id) ? bc->client_id : "",
                bc->client_ip, reason);
        }

        /* 更新连接统计 */
        if (broker->enable_stats && broker->stats.conns > 0) {
            broker->stats.conns--;
        }
#ifdef WOLFMQTT_STATIC_MEMORY
        int i;
        for (i = 0; i < broker->max_clients; i++) {
            if (broker->clients[i].in_use && broker->clients[i].connected) {
                count_before++;
            }
        }
#else
        BrokerClient* tmp = broker->clients;
        while (tmp != NULL) {
            if (tmp->connected) {
                count_before++;
            }
            tmp = tmp->next;
        }
#endif
        WBLOG_INFO(broker, "client disconnected client_id=%s ip=%s total_clients=%d",
            BROKER_CLIENT_ID(bc), bc->client_ip, count_before - 1);
    }

    /* Cleanup transport layer resources */
    BrokerTransport_Close(bc, broker);
    BrokerTransport_Cleanup(bc);

#ifdef WOLFMQTT_BROKER_EPOLL
    /* Remove socket from epoll monitoring */
    if (bc->sock >= 0) {
        BrokerEpoll_DelSocket(broker, bc->sock);
    }
#endif

#ifndef WOLFMQTT_STATIC_MEMORY
    {
        BrokerClient* cur = broker->clients;
        BrokerClient* prev = NULL;
        while (cur) {
            if (cur == bc) {
                if (prev) {
                    prev->next = cur->next;
                }
                else {
                    broker->clients = cur->next;
                }
                break;
            }
            prev = cur;
            cur = cur->next;
        }
    }
#endif
    BrokerClient_Free(bc);
}

/* -------------------------------------------------------------------------- */
/* Subscription management                                                     */
/* -------------------------------------------------------------------------- */

/* Orphan subscriptions for session persistence (clean_session=0).
 * Sets client pointer to NULL but keeps the subscription for reconnect. */
static void BrokerSubs_OrphanClient(MqttBroker* broker, BrokerClient* bc)
{
    int count = 0;
    WOLFMQTT_BROKER_TIME_T now = WOLFMQTT_BROKER_GET_TIME_S();
#ifdef WOLFMQTT_STATIC_MEMORY
    int i;
    for (i = 0; i < broker->max_subs; i++) {
        if (broker->subs[i].in_use && broker->subs[i].client == bc) {
            broker->subs[i].client = NULL;
#ifdef WOLFMQTT_V5
            broker->subs[i].disconnect_time = now;
            /* Update session expiry interval from client (may have changed via DISCONNECT) */
            broker->subs[i].session_expiry_interval = bc->session_expiry_interval;
#endif
            count++;
        }
    }
#else
    BrokerSub* cur = broker->subs;
    while (cur) {
        if (cur->client == bc) {
            cur->client = NULL;
#ifdef WOLFMQTT_V5
            cur->disconnect_time = now;
            /* Update session expiry interval from client (may have changed via DISCONNECT) */
            cur->session_expiry_interval = bc->session_expiry_interval;
#endif
            count++;
        }
        cur = cur->next;
    }
#endif
    if (count > 0) {
        /* Orphaned subscriptions tracked for session persistence */
    }
}

#ifdef WOLFMQTT_V5
/* Check for expired sessions and remove their subscriptions
 * Returns the number of expired sessions removed */
static int BrokerSubs_CheckSessionExpiry(MqttBroker* broker)
{
    int removed = 0;
    WOLFMQTT_BROKER_TIME_T now = WOLFMQTT_BROKER_GET_TIME_S();

    if (broker == NULL) {
        return 0;
    }

#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        for (i = 0; i < broker->max_subs; i++) {
            BrokerSub* sub = &broker->subs[i];
            /* Check if this is an orphaned subscription (client == NULL) */
            if (sub->in_use && sub->client == NULL && sub->disconnect_time > 0) {
                /* Check if session has expired */
                if (sub->session_expiry_interval > 0) {
                    WOLFMQTT_BROKER_TIME_T elapsed = now - sub->disconnect_time;
                    if (elapsed >= (WOLFMQTT_BROKER_TIME_T)sub->session_expiry_interval) {
                        XMEMSET(sub, 0, sizeof(BrokerSub));
                        removed++;
                    }
                }
                else {
                    /* session_expiry_interval == 0 means session ends on disconnect */
                    /* Remove immediately if disconnect time > 0 */
                    XMEMSET(sub, 0, sizeof(BrokerSub));
                    removed++;
                }
            }
        }
    }
#else
    {
        BrokerSub* cur = broker->subs;
        BrokerSub* prev = NULL;

        while (cur) {
            BrokerSub* next = cur->next;
            /* Check if this is an orphaned subscription (client == NULL) */
            if (cur->client == NULL && cur->disconnect_time > 0) {
                /* Check if session has expired */
                if (cur->session_expiry_interval > 0) {
                    WOLFMQTT_BROKER_TIME_T elapsed = now - cur->disconnect_time;
                    if (elapsed >= (WOLFMQTT_BROKER_TIME_T)cur->session_expiry_interval) {
                        /* Remove from linked list */
                        if (prev) {
                            prev->next = next;
                        } else {
                            broker->subs = next;
                        }
                        /* Free memory */
                        if (cur->filter) {
                            WOLFMQTT_FREE(cur->filter);
                        }
                        if (cur->client_id) {
                            WOLFMQTT_FREE(cur->client_id);
                        }
                        WOLFMQTT_FREE(cur);
                        removed++;
                        cur = next;
                        continue;
                    }
                }
                else {
                    /* session_expiry_interval == 0 means session ends on disconnect */
                    /* Remove immediately if disconnect time > 0 */                    
                    /* Remove from linked list */
                    if (prev) {
                        prev->next = next;
                    } else {
                        broker->subs = next;
                    }
                    /* Free memory */
                    if (cur->filter) {
                        WOLFMQTT_FREE(cur->filter);
                    }
                    if (cur->client_id) {
                        WOLFMQTT_FREE(cur->client_id);
                    }
                    WOLFMQTT_FREE(cur);
                    removed++;
                    cur = next;
                    continue;
                }
            }
            prev = cur;
            cur = next;
        }
    }
#endif

    return removed;
}
#endif /* WOLFMQTT_V5 */

static void BrokerSubs_RemoveClient(MqttBroker* broker, BrokerClient* bc)
{
#ifdef WOLFMQTT_STATIC_MEMORY
    int i;
    for (i = 0; i < broker->max_subs; i++) {
        if (broker->subs[i].in_use && broker->subs[i].client == bc) {
            XMEMSET(&broker->subs[i], 0, sizeof(BrokerSub));
        }
    }
#else
    BrokerSub* cur = broker->subs;
    BrokerSub* prev = NULL;

    while (cur) {
        BrokerSub* next = cur->next;
        if (cur->client == bc) {
            if (prev) {
                prev->next = next;
            }
            else {
                broker->subs = next;
            }
            if (cur->filter) {
                WOLFMQTT_FREE(cur->filter);
            }
            if (cur->client_id) {
                WOLFMQTT_FREE(cur->client_id);
            }
            WOLFMQTT_FREE(cur);
        }
        else {
            prev = cur;
        }
        cur = next;
    }
#endif
}

static int BrokerSubs_Add(MqttBroker* broker, BrokerClient* bc,
    const char* filter, word16 filter_len, MqttQoS qos
#ifdef WOLFMQTT_V5
    , byte no_local, byte rap, byte retain_handling
#endif
    )
{
    BrokerSub* sub = NULL;
    int rc = MQTT_CODE_SUCCESS;
    int is_new_sub = 0;  /* Track if this is a new subscription */

#ifdef WOLFMQTT_V5
    /* 共享订阅解析 (Shared Subscription Parsing) */
    const char* actual_filter = filter;
    word16 actual_flen = filter_len;
    byte is_shared = 0;
    const char* share_group_src = NULL;  /* 指向原始 filter 中的组名 */
    word16 share_group_len = 0;

    /* 检查 $share/ 前缀 */
    if (filter_len > 8 && XMEMCMP(filter, "$share/", 7) == 0) {
        /* 查找组名结束位置 */
        const char* group_start = filter + 7;
        const char* group_end = group_start;

        while (group_end < filter + filter_len && *group_end != '/') {
            group_end++;
        }

        /* 验证格式有效性 */
        if (group_end > group_start && group_end < filter + filter_len - 1) {
            /* 提取组名长度和位置 */
            share_group_len = (word16)(group_end - group_start);
            share_group_src = group_start;

            /* 提取实际过滤器 */
            actual_filter = group_end + 1;
            actual_flen = filter_len - (actual_filter - filter);
            is_shared = 1; 
        }
    }
#endif

    /* Check for existing subscription to same filter by same client */
#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        for (i = 0; i < broker->max_subs; i++) {
            if (broker->subs[i].in_use && broker->subs[i].client == bc &&
                (word16)XSTRLEN(broker->subs[i].filter) == actual_flen &&
                XMEMCMP(broker->subs[i].filter, actual_filter, actual_flen) == 0) {
#ifdef WOLFMQTT_V5
                /* For shared subscriptions, also verify group name matches */
                if (is_shared) {
                    /* Compare share_group - both must be shared with same group */
                    if (!broker->subs[i].is_shared ||
                        (share_group_src == NULL) != (broker->subs[i].share_group[0] == '\0') ||
                        (share_group_src != NULL && XMEMCMP(share_group_src, broker->subs[i].share_group, share_group_len) != 0)) {
                        continue;  /* Different groups - not a duplicate */
                    }
                } else if (broker->subs[i].is_shared) {
                    continue;  /* Existing is shared, new is not - not a duplicate */
                }
#endif
                broker->subs[i].qos = qos;
#ifdef WOLFMQTT_V5
                broker->subs[i].retain_handling = retain_handling;
                broker->subs[i].no_local = no_local;
                broker->subs[i].rap = rap;
#endif
                return MQTT_CODE_SUCCESS;  /* Existing subscription updated */
            }
        }
    }
#else
    {
        BrokerSub* cur = broker->subs;
        while (cur) {
            if (cur->client == bc && cur->filter != NULL &&
                (word16)XSTRLEN(cur->filter) == actual_flen &&
                XMEMCMP(cur->filter, actual_filter, actual_flen) == 0) {
#ifdef WOLFMQTT_V5
                /* For shared subscriptions, also verify group name matches */
                if (is_shared) {
                    /* Compare share_group - both must be shared with same group */
                    if (!cur->is_shared ||
                        (share_group_src == NULL) != (cur->share_group == NULL) ||
                        (share_group_src != NULL && XMEMCMP(share_group_src, cur->share_group, share_group_len) != 0)) {
                        cur = cur->next;
                        continue;  /* Different groups - not a duplicate */
                    }
                } else if (cur->is_shared) {
                    cur = cur->next;
                    continue;  /* Existing is shared, new is not - not a duplicate */
                }
#endif
                cur->qos = qos;
#ifdef WOLFMQTT_V5
                cur->retain_handling = retain_handling;
                cur->no_local = no_local;
                cur->rap = rap;
#endif
                return MQTT_CODE_SUCCESS;  /* Existing subscription updated */
            }
            cur = cur->next;
        }
    }
#endif

    /* No existing subscription found - this is a new subscription */
    is_new_sub = 1;

#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        for (i = 0; i < broker->max_subs; i++) {
            if (!broker->subs[i].in_use) {
                sub = &broker->subs[i];
                break;
            }
        }
        if (sub == NULL) {
            rc = MQTT_CODE_ERROR_MEMORY;
        }
        if (rc == MQTT_CODE_SUCCESS) {
            if (filter_len >= BROKER_MAX_FILTER_LEN) {
                rc = MQTT_CODE_ERROR_OUT_OF_BUFFER;
            }
        }
        if (rc == MQTT_CODE_SUCCESS) {
            XMEMSET(sub, 0, sizeof(*sub));
            sub->in_use = 1;
            /* Store actual filter (without $share/<group>/ prefix for shared subs) */
            XMEMCPY(sub->filter, actual_filter, actual_flen);
            sub->filter[actual_flen] = '\0';
        }
    }
#else
    sub = (BrokerSub*)WOLFMQTT_MALLOC(sizeof(BrokerSub));
    if (sub == NULL) {
        rc = MQTT_CODE_ERROR_MEMORY;
    }
    if (rc == MQTT_CODE_SUCCESS) {
        XMEMSET(sub, 0, sizeof(*sub));
        /* Allocate space for actual filter (without $share/<group>/ prefix) */
        sub->filter = (char*)WOLFMQTT_MALLOC(actual_flen + 1);
        if (sub->filter == NULL) {
            rc = MQTT_CODE_ERROR_MEMORY;
        }
    }
    if (rc == MQTT_CODE_SUCCESS) {
        /* Store actual filter (without $share/<group>/ prefix for shared subs) */
        XMEMCPY(sub->filter, actual_filter, actual_flen);
        sub->filter[actual_flen] = '\0';
        sub->next = broker->subs;
        broker->subs = sub;
    }
    else if (sub != NULL) {
        WOLFMQTT_FREE(sub);
    }
#endif

    if (rc == MQTT_CODE_SUCCESS) {
        sub->client = bc;
        sub->qos = qos;
#ifdef WOLFMQTT_V5
        sub->no_local = no_local;
        sub->rap = rap;
        sub->retain_handling = retain_handling;
        /* Store session expiry interval from client */
        sub->session_expiry_interval = bc->session_expiry_interval;
        sub->disconnect_time = 0; /* Active connection, no disconnect time */

        /* 初始化共享订阅字段 */
        sub->is_shared = is_shared;
        sub->rr_index = 0;  /* 新订阅者从 0 开始 */
#ifdef WOLFMQTT_STATIC_MEMORY
        /* 静态模式：复制组名到 share_group 字段 */
        if (is_shared && share_group_src != NULL && share_group_len > 0) {
            word16 copy_len = (share_group_len < BROKER_MAX_FILTER_LEN) ? share_group_len : BROKER_MAX_FILTER_LEN - 1;
            XMEMCPY(sub->share_group, share_group_src, copy_len);
            sub->share_group[copy_len] = '\0';
        } else {
            sub->share_group[0] = '\0';
        }
#else
        /* 动态模式：分配并复制组名 */
        if (is_shared && share_group_src != NULL && share_group_len > 0) {
            sub->share_group = (char*)WOLFMQTT_MALLOC(share_group_len + 1);
            if (sub->share_group != NULL) {
                XMEMCPY(sub->share_group, share_group_src, share_group_len);
                sub->share_group[share_group_len] = '\0';
            }
        } else {
            sub->share_group = NULL;
        }
#endif
#else
        /* Initialize v5 fields to 0 for non-v5 builds */
        sub->no_local = 0;
        sub->rap = 0;
        sub->retain_handling = 0;
#ifdef WOLFMQTT_V5
        sub->is_shared = 0;
        sub->rr_index = 0;
        sub->share_group = NULL;
#endif
#endif
        /* Store client_id for session persistence */
#ifdef WOLFMQTT_STATIC_MEMORY
        if (BROKER_STR_VALID(bc->client_id)) {
            int id_len = (int)XSTRLEN(bc->client_id);
            if (id_len >= BROKER_MAX_CLIENT_ID_LEN) {
                id_len = BROKER_MAX_CLIENT_ID_LEN - 1;
            }
            XMEMCPY(sub->client_id, bc->client_id, (size_t)id_len);
            sub->client_id[id_len] = '\0';
        }
#else
        if (BROKER_STR_VALID(bc->client_id)) {
            int id_len = (int)XSTRLEN(bc->client_id);
            sub->client_id = (char*)WOLFMQTT_MALLOC((size_t)id_len + 1);
            if (sub->client_id != NULL) {
                XMEMCPY(sub->client_id, bc->client_id, (size_t)id_len + 1);
            }
        }
#endif
        {
#ifdef WOLFMQTT_V5
            byte rh = retain_handling;
#else
            byte rh = 0;
#endif
            /* 新增订阅，更新统计 */
            if (broker->enable_stats) {
                broker->stats.subs++;
            }
        }
    }
    /* Return special code to indicate new subscription vs update */
    return (is_new_sub) ? MQTT_CODE_CONTINUE : rc;
}

static void BrokerSubs_Remove(MqttBroker* broker, BrokerClient* bc,
    const char* filter, word16 filter_len)
{
#ifdef WOLFMQTT_V5
    /* Parse shared subscription prefix if present */
    const char* actual_filter = filter;
    word16 actual_flen = filter_len;
    const char* share_group_src = NULL;
    word16 share_group_len = 0;

    if (filter_len > 8 && XMEMCMP(filter, "$share/", 7) == 0) {
        /* Extract group name and actual filter */
        const char* group_start = filter + 7;
        const char* group_end = group_start;
        while (group_end < filter + filter_len && *group_end != '/') {
            group_end++;
        }
        if (group_end > group_start && group_end < filter + filter_len) {
            share_group_src = group_start;
            share_group_len = (word16)(group_end - group_start);
            actual_filter = group_end + 1;
            actual_flen = filter_len - (actual_filter - filter);
        }
    }
#endif

#ifdef WOLFMQTT_STATIC_MEMORY
    int i;
    for (i = 0; i < broker->max_subs; i++) {
        BrokerSub* s = &broker->subs[i];
        if (s->in_use && s->client == bc &&
            s->filter[0] != '\0' &&
            (word16)XSTRLEN(s->filter) == actual_flen &&
            XMEMCMP(s->filter, actual_filter, actual_flen) == 0) {
#ifdef WOLFMQTT_V5
            /* For shared subscriptions, also verify group name matches */
            if (share_group_src != NULL || s->is_shared) {
                if (!s->is_shared || share_group_src == NULL ||
                    XMEMCMP(share_group_src, s->share_group, share_group_len) != 0) {
                    continue;  /* Different groups - not the subscription to remove */
                }
            }
#endif
            XMEMSET(s, 0, sizeof(BrokerSub));
            /* 删除订阅，更新统计 */
            if (broker->enable_stats && broker->stats.subs > 0) {
                broker->stats.subs--;
            }
            return;
        }
    }
#else
    BrokerSub* cur = broker->subs;
    BrokerSub* prev = NULL;

    while (cur) {
        BrokerSub* next = cur->next;
        if (cur->client == bc &&
            cur->filter != NULL &&
            (word16)XSTRLEN(cur->filter) == actual_flen &&
            XMEMCMP(cur->filter, actual_filter, actual_flen) == 0) {
#ifdef WOLFMQTT_V5
            /* For shared subscriptions, also verify group name matches */
            if (share_group_src != NULL || cur->is_shared) {
                if (!cur->is_shared || share_group_src == NULL ||
                    XMEMCMP(share_group_src, cur->share_group, share_group_len) != 0) {
                    prev = cur;
                    cur = next;
                    continue;  /* Different groups - not the subscription to remove */
                }
            }
#endif
            if (prev) {
                prev->next = next;
            }
            else {
                broker->subs = next;
            }

#ifdef WOLFMQTT_V5
            /* 动态模式：释放 share_group 内存 */
            if (cur->share_group != NULL) {
                WOLFMQTT_FREE(cur->share_group);
                cur->share_group = NULL;
            }
#endif

            WOLFMQTT_FREE(cur->filter);
            if (cur->client_id) {
                WOLFMQTT_FREE(cur->client_id);
            }
            WOLFMQTT_FREE(cur);
            /* 删除订阅，更新统计 */
            if (broker->enable_stats && broker->stats.subs > 0) {
                broker->stats.subs--;
            }
            return;
        }
        prev = cur;
        cur = next;
    }
#endif
}

/* -------------------------------------------------------------------------- */
/* Packet ID generation                                                        */
/* -------------------------------------------------------------------------- */
static word16 BrokerNextPacketId(MqttBroker* broker)
{
    word16 id = broker->next_packet_id;
    broker->next_packet_id++;
    if (broker->next_packet_id == 0) {
        broker->next_packet_id = 1; /* wrap: skip 0 */
    }
    return id;
}

/* -------------------------------------------------------------------------- */
/* Client lookup by ID                                                         */
/* -------------------------------------------------------------------------- */
static BrokerClient* BrokerClient_FindByClientId(MqttBroker* broker,
    const char* client_id, BrokerClient* exclude)
{
    if (broker == NULL || client_id == NULL || client_id[0] == '\0') {
        return NULL;
    }
#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        for (i = 0; i < broker->max_clients; i++) {
            BrokerClient* bc = &broker->clients[i];
            if (!bc->in_use) continue;
            if (bc != exclude && BROKER_STR_VALID(bc->client_id) &&
                XSTRCMP(bc->client_id, client_id) == 0) {
                return bc;
            }
        }
    }
#else
    {
        BrokerClient* bc = broker->clients;
        while (bc) {
            if (bc != exclude && BROKER_STR_VALID(bc->client_id) &&
                XSTRCMP(bc->client_id, client_id) == 0) {
                return bc;
            }
            bc = bc->next;
        }
    }
#endif
    return NULL;
}

/* -------------------------------------------------------------------------- */
/* Subscription helpers for clean session                                      */
/* -------------------------------------------------------------------------- */
static void BrokerSubs_RemoveByClientId(MqttBroker* broker,
    const char* client_id)
{
    if (broker == NULL || client_id == NULL || client_id[0] == '\0') {
        return;
    }
#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        for (i = 0; i < broker->max_subs; i++) {
            BrokerSub* s = &broker->subs[i];
            if (!s->in_use) continue;
            /* Check active client subs */
            if (s->client != NULL &&
                s->client->client_id[0] != '\0' &&
                XSTRCMP(s->client->client_id, client_id) == 0) {
                XMEMSET(s, 0, sizeof(BrokerSub));
            }
            /* Check orphaned subs (stored client_id) */
            else if (s->client == NULL &&
                BROKER_STR_VALID(s->client_id) &&
                XSTRCMP(s->client_id, client_id) == 0) {
                XMEMSET(s, 0, sizeof(BrokerSub));
            }
        }
    }
#else
    {
        BrokerSub* cur = broker->subs;
        BrokerSub* prev = NULL;
        while (cur) {
            BrokerSub* next = cur->next;
            int remove = 0;
            /* Check active client subs */
            if (cur->client != NULL && cur->client->client_id != NULL &&
                XSTRCMP(cur->client->client_id, client_id) == 0) {
                remove = 1;
            }
            /* Check orphaned subs (stored client_id) */
            else if (cur->client == NULL &&
                BROKER_STR_VALID(cur->client_id) &&
                XSTRCMP(cur->client_id, client_id) == 0) {
                remove = 1;
            }
            if (remove) {
                if (prev) {
                    prev->next = next;
                }
                else {
                    broker->subs = next;
                }
                if (cur->filter) {
                    WOLFMQTT_FREE(cur->filter);
                }
                if (cur->client_id) {
                    WOLFMQTT_FREE(cur->client_id);
                }
                WOLFMQTT_FREE(cur);
            }
            else {
                prev = cur;
            }
            cur = next;
        }
    }
#endif
}

static void BrokerSubs_ReassociateClient(MqttBroker* broker,
    const char* client_id, BrokerClient* new_bc)
{
    int count = 0;
    if (broker == NULL || client_id == NULL || client_id[0] == '\0' ||
        new_bc == NULL) {
        return;
    }
#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        for (i = 0; i < broker->max_subs; i++) {
            BrokerSub* s = &broker->subs[i];
            if (!s->in_use) continue;
            /* Check orphaned subs (client=NULL, client_id stored in sub) */
            if (s->client == NULL && BROKER_STR_VALID(s->client_id) &&
                XSTRCMP(s->client_id, client_id) == 0) {
                s->client = new_bc;
                count++;
            }
            /* Check subs with active client (takeover scenario) */
            else if (s->client != NULL && BROKER_STR_VALID(s->client->client_id) &&
                XSTRCMP(s->client->client_id, client_id) == 0) {
                s->client = new_bc;
                count++;
            }
        }
    }
#else
    {
        BrokerSub* s = broker->subs;
        while (s) {
            /* Check orphaned subs (client=NULL, client_id stored in sub) */
            if (s->client == NULL && BROKER_STR_VALID(s->client_id) &&
                XSTRCMP(s->client_id, client_id) == 0) {
                s->client = new_bc;
                count++;
            }
            /* Check subs with active client (takeover scenario) */
            else if (s->client != NULL && BROKER_STR_VALID(s->client->client_id) &&
                XSTRCMP(s->client->client_id, client_id) == 0) {
                s->client = new_bc;
                count++;
            }
            s = s->next;
        }
    }
#endif
    if (count > 0) {
        /* Subscriptions reassociated for client takeover */
    }
}

/* -------------------------------------------------------------------------- */
/* Retained message management                                                 */
/* -------------------------------------------------------------------------- */
#ifdef WOLFMQTT_BROKER_RETAINED
static int BrokerRetained_Store(MqttBroker* broker, const char* topic,
    const byte* payload, word32 payload_len, MqttQoS qos, word32 expiry_sec)
{
    BrokerRetainedMsg* msg = NULL;
    int rc = MQTT_CODE_SUCCESS;

    if (broker == NULL || topic == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        /* Look for existing retained msg on this topic */
        for (i = 0; i < broker->max_retained; i++) {
            if (broker->retained[i].in_use &&
                XSTRCMP(broker->retained[i].topic, topic) == 0) {
                msg = &broker->retained[i];
                break;
            }
        }
        /* If not found, find a free slot */
        if (msg == NULL) {
            for (i = 0; i < broker->max_retained; i++) {
                if (!broker->retained[i].in_use) {
                    msg = &broker->retained[i];
                    break;
                }
            }
        }
        if (msg == NULL) {
            rc = MQTT_CODE_ERROR_MEMORY;
        }
        if (rc == MQTT_CODE_SUCCESS) {
            int tlen = (int)XSTRLEN(topic);
            if (tlen >= BROKER_MAX_TOPIC_LEN) {
                rc = MQTT_CODE_ERROR_OUT_OF_BUFFER;
            }
            else if (payload_len > BROKER_MAX_PAYLOAD_LEN) {
                rc = MQTT_CODE_ERROR_OUT_OF_BUFFER;
            }
            if (rc == MQTT_CODE_SUCCESS) {
                XMEMSET(msg, 0, sizeof(*msg));
                msg->in_use = 1;
                XMEMCPY(msg->topic, topic, (size_t)tlen);
                msg->topic[tlen] = '\0';
                if (payload_len > 0 && payload != NULL) {
                    XMEMCPY(msg->payload, payload, payload_len);
                }
                msg->payload_len = payload_len;
            }
        }
    }
#else
    {
        byte is_new = 0;
        BrokerRetainedMsg* cur = broker->retained;
        while (cur) {
            if (cur->topic != NULL && XSTRCMP(cur->topic, topic) == 0) {
                msg = cur;
                break;
            }
            cur = cur->next;
        }
        if (msg != NULL) {
            /* Replace existing: free old payload */
            if (msg->payload) {
                WOLFMQTT_FREE(msg->payload);
                msg->payload = NULL;
            }
            msg->payload_len = 0;
        }
        else {
            /* Allocate new */
            int tlen = (int)XSTRLEN(topic);
            msg = (BrokerRetainedMsg*)WOLFMQTT_MALLOC(
                sizeof(BrokerRetainedMsg));
            if (msg == NULL) {
                rc = MQTT_CODE_ERROR_MEMORY;
            }
            if (rc == MQTT_CODE_SUCCESS) {
                XMEMSET(msg, 0, sizeof(*msg));
                msg->topic = (char*)WOLFMQTT_MALLOC((size_t)tlen + 1);
                if (msg->topic == NULL) {
                    WOLFMQTT_FREE(msg);
                    msg = NULL;
                    rc = MQTT_CODE_ERROR_MEMORY;
                }
            }
            if (rc == MQTT_CODE_SUCCESS) {
                XMEMCPY(msg->topic, topic, (size_t)tlen);
                msg->topic[tlen] = '\0';
                is_new = 1;
            }
        }
        if (rc == MQTT_CODE_SUCCESS && payload_len > 0 && payload != NULL) {
            msg->payload = (byte*)WOLFMQTT_MALLOC(payload_len);
            if (msg->payload == NULL) {
                rc = MQTT_CODE_ERROR_MEMORY;
            }
            else {
                XMEMCPY(msg->payload, payload, payload_len);
            }
        }
        if (rc == MQTT_CODE_SUCCESS) {
            msg->payload_len = payload_len;
            if (is_new) {
                msg->next = broker->retained;
                broker->retained = msg;
                /* 新增保留消息，更新统计 */
                if (broker->enable_stats) {
                    broker->stats.retained++;
                }
            }
        }
        else if (is_new && msg != NULL) {
            if (msg->topic) {
                WOLFMQTT_FREE(msg->topic);
            }
            WOLFMQTT_FREE(msg);
        }
    }
#endif

    if (rc == MQTT_CODE_SUCCESS) {
        msg->qos = qos;
        msg->store_time = WOLFMQTT_BROKER_GET_TIME_S();
        msg->expiry_sec = expiry_sec;
    }
    return rc;
}

static void BrokerRetained_Delete(MqttBroker* broker, const char* topic)
{
    if (broker == NULL || topic == NULL) {
        return;
    }
#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        for (i = 0; i < broker->max_retained; i++) {
            if (broker->retained[i].in_use &&
                XSTRCMP(broker->retained[i].topic, topic) == 0) {
                XMEMSET(&broker->retained[i], 0, sizeof(BrokerRetainedMsg));
                /* 删除保留消息，更新统计 */
                if (broker->enable_stats && broker->stats.retained > 0) {
                    broker->stats.retained--;
                }
                return;
            }
        }
    }
#else
    {
        BrokerRetainedMsg* cur = broker->retained;
        BrokerRetainedMsg* prev = NULL;
        while (cur) {
            BrokerRetainedMsg* next = cur->next;
            if (cur->topic != NULL && XSTRCMP(cur->topic, topic) == 0) {
                if (prev) {
                    prev->next = next;
                }
                else {
                    broker->retained = next;
                }
                WOLFMQTT_FREE(cur->topic);
                if (cur->payload) {
                    WOLFMQTT_FREE(cur->payload);
                }
                WOLFMQTT_FREE(cur);
                /* 删除保留消息，更新统计 */
                if (broker->enable_stats && broker->stats.retained > 0) {
                    broker->stats.retained--;
                }
                return;
            }
            prev = cur;
            cur = next;
        }
    }
#endif
}

static void BrokerRetained_FreeAll(MqttBroker* broker)
{
#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        for (i = 0; i < broker->max_retained; i++) {
            XMEMSET(&broker->retained[i], 0, sizeof(BrokerRetainedMsg));
        }
    }
#else
    {
        BrokerRetainedMsg* cur = broker->retained;
        while (cur) {
            BrokerRetainedMsg* next = cur->next;
            if (cur->topic) {
                WOLFMQTT_FREE(cur->topic);
            }
            if (cur->payload) {
                WOLFMQTT_FREE(cur->payload);
            }
            WOLFMQTT_FREE(cur);
            cur = next;
        }
        broker->retained = NULL;
    }
#endif
}
#endif /* WOLFMQTT_BROKER_RETAINED */

/* Forward declaration - used by retained delivery, will publish, and PUBLISH handler */
static int BrokerTopicMatch(const char* filter, const char* topic);

/* -------------------------------------------------------------------------- */
/* LWT (Last Will and Testament) helpers                                       */
/* -------------------------------------------------------------------------- */
#ifdef WOLFMQTT_BROKER_WILL
static void BrokerClient_ClearWill(BrokerClient* bc)
{
    if (bc == NULL) {
        return;
    }
    bc->has_will = 0;
    bc->will_qos = MQTT_QOS_0;
    bc->will_retain = 0;
    bc->will_delay_sec = 0;
#ifdef WOLFMQTT_STATIC_MEMORY
    bc->will_payload_len = 0;
    bc->will_topic[0] = '\0';
#else
    if (bc->will_topic) {
        BROKER_FORCE_ZERO(bc->will_topic, XSTRLEN(bc->will_topic) + 1);
        WOLFMQTT_FREE(bc->will_topic);
        bc->will_topic = NULL;
    }
    if (bc->will_payload) {
        BROKER_FORCE_ZERO(bc->will_payload, bc->will_payload_len);
        WOLFMQTT_FREE(bc->will_payload);
        bc->will_payload = NULL;
    }
    bc->will_payload_len = 0;
#endif
#ifdef WOLFMQTT_V5
    /* Free will properties */
    if (bc->will_props != NULL) {
        (void)MqttProps_Free(bc->will_props);
        bc->will_props = NULL;
    }
#endif
}

/* -------------------------------------------------------------------------- */
/* Pending will management (v5 Will Delay Interval)                            */
/* -------------------------------------------------------------------------- */

/* Add a pending will to be published after delay expires */
static int BrokerPendingWill_Add(MqttBroker* broker, BrokerClient* bc)
{
    BrokerPendingWill* pw = NULL;
    WOLFMQTT_BROKER_TIME_T now = WOLFMQTT_BROKER_GET_TIME_S();
    int rc = MQTT_CODE_SUCCESS;

#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        for (i = 0; i < broker->max_pending_wills; i++) {
            if (!broker->pending_wills[i].in_use) {
                pw = &broker->pending_wills[i];
                break;
            }
        }
        if (pw == NULL) {
            rc = MQTT_CODE_ERROR_MEMORY;
        }
        if (rc == MQTT_CODE_SUCCESS) {
            int id_len = (int)XSTRLEN(bc->client_id);
            int t_len = (int)XSTRLEN(bc->will_topic);
            if (id_len >= BROKER_MAX_CLIENT_ID_LEN) {
                rc = MQTT_CODE_ERROR_OUT_OF_BUFFER;
            }
            else if (t_len >= BROKER_MAX_TOPIC_LEN) {
                rc = MQTT_CODE_ERROR_OUT_OF_BUFFER;
            }
            else if (bc->will_payload_len > BROKER_MAX_WILL_PAYLOAD_LEN) {
                rc = MQTT_CODE_ERROR_OUT_OF_BUFFER;
            }
            if (rc == MQTT_CODE_SUCCESS) {
                XMEMSET(pw, 0, sizeof(*pw));
                pw->in_use = 1;
                XMEMCPY(pw->client_id, bc->client_id, id_len);
                pw->client_id[id_len] = '\0';
                XMEMCPY(pw->topic, bc->will_topic, t_len);
                pw->topic[t_len] = '\0';
                if (bc->will_payload_len > 0) {
                    XMEMCPY(pw->payload, bc->will_payload,
                        bc->will_payload_len);
                    pw->payload_len = bc->will_payload_len;
                }
            }
        }
    }
#else
    pw = (BrokerPendingWill*)WOLFMQTT_MALLOC(sizeof(BrokerPendingWill));
    if (pw == NULL) {
        rc = MQTT_CODE_ERROR_MEMORY;
    }
    if (rc == MQTT_CODE_SUCCESS) {
        int id_len = (int)XSTRLEN(bc->client_id);
        int t_len = (int)XSTRLEN(bc->will_topic);
        XMEMSET(pw, 0, sizeof(*pw));
        pw->client_id = (char*)WOLFMQTT_MALLOC((size_t)id_len + 1);
        if (pw->client_id != NULL) {
            XMEMCPY(pw->client_id, bc->client_id, (size_t)id_len + 1);
        }
        else {
            rc = MQTT_CODE_ERROR_MEMORY;
        }
        if (rc == MQTT_CODE_SUCCESS) {
            pw->topic = (char*)WOLFMQTT_MALLOC((size_t)t_len + 1);
            if (pw->topic != NULL) {
                XMEMCPY(pw->topic, bc->will_topic, (size_t)t_len + 1);
            }
            else {
                rc = MQTT_CODE_ERROR_MEMORY;
            }
        }
        if (rc == MQTT_CODE_SUCCESS && bc->will_payload_len > 0) {
            pw->payload = (byte*)WOLFMQTT_MALLOC(bc->will_payload_len);
            if (pw->payload != NULL) {
                XMEMCPY(pw->payload, bc->will_payload, bc->will_payload_len);
                pw->payload_len = bc->will_payload_len;
            }
            else {
                rc = MQTT_CODE_ERROR_MEMORY;
            }
        }
    }
    if (rc == MQTT_CODE_SUCCESS) {
        pw->next = broker->pending_wills;
        broker->pending_wills = pw;
    }
    else if (pw != NULL) {
        if (pw->topic) {
            BROKER_FORCE_ZERO(pw->topic, XSTRLEN(pw->topic) + 1);
            WOLFMQTT_FREE(pw->topic);
        }
        if (pw->client_id) {
            WOLFMQTT_FREE(pw->client_id);
        }
        if (pw->payload) {
            BROKER_FORCE_ZERO(pw->payload, pw->payload_len);
            WOLFMQTT_FREE(pw->payload);
        }
        WOLFMQTT_FREE(pw);
    }
#endif

    if (rc == MQTT_CODE_SUCCESS) {
        pw->qos = bc->will_qos;
        pw->retain = bc->will_retain;
        pw->publish_time = now + (WOLFMQTT_BROKER_TIME_T)bc->will_delay_sec;
    }
    return rc;
}

/* Cancel a pending will for the given client_id (client reconnected) */
static void BrokerPendingWill_Cancel(MqttBroker* broker,
    const char* client_id)
{
    if (broker == NULL || client_id == NULL) {
        return;
    }
#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        for (i = 0; i < broker->max_pending_wills; i++) {
            if (broker->pending_wills[i].in_use &&
                XSTRCMP(broker->pending_wills[i].client_id, client_id) == 0) {
                XMEMSET(&broker->pending_wills[i], 0,
                    sizeof(BrokerPendingWill));
                return;
            }
        }
    }
#else
    {
        BrokerPendingWill* pw = broker->pending_wills;
        BrokerPendingWill* prev = NULL;
        while (pw) {
            BrokerPendingWill* next = pw->next;
            if (pw->client_id != NULL &&
                XSTRCMP(pw->client_id, client_id) == 0) {
                if (prev) {
                    prev->next = next;
                }
                else {
                    broker->pending_wills = next;
                }
                WOLFMQTT_FREE(pw->client_id);
                if (pw->topic) {
                    BROKER_FORCE_ZERO(pw->topic, XSTRLEN(pw->topic) + 1);
                    WOLFMQTT_FREE(pw->topic);
                }
                if (pw->payload) {
                    BROKER_FORCE_ZERO(pw->payload, pw->payload_len);
                    WOLFMQTT_FREE(pw->payload);
                }
                WOLFMQTT_FREE(pw);
                return;
            }
            prev = pw;
            pw = next;
        }
    }
#endif
}

static void BrokerPendingWill_FreeAll(MqttBroker* broker)
{
    if (broker == NULL) {
        return;
    }
#ifdef WOLFMQTT_STATIC_MEMORY
    XMEMSET(broker->pending_wills, 0, sizeof(broker->pending_wills));
#else
    {
        BrokerPendingWill* pw = broker->pending_wills;
        while (pw) {
            BrokerPendingWill* next = pw->next;
            if (pw->client_id) WOLFMQTT_FREE(pw->client_id);
            if (pw->topic) {
                BROKER_FORCE_ZERO(pw->topic, XSTRLEN(pw->topic) + 1);
                WOLFMQTT_FREE(pw->topic);
            }
            if (pw->payload) {
                BROKER_FORCE_ZERO(pw->payload, pw->payload_len);
                WOLFMQTT_FREE(pw->payload);
            }
            WOLFMQTT_FREE(pw);
            pw = next;
        }
        broker->pending_wills = NULL;
    }
#endif
}

static void BrokerClient_PublishWillImmediate(MqttBroker* broker,
    const char* topic, const byte* payload, word16 payload_len,
    MqttQoS qos, byte retain
#ifdef WOLFMQTT_V5
    ,MqttProp* will_props
#endif
);

/* Process pending wills - publish any that have expired their delay */
static int BrokerPendingWill_Process(MqttBroker* broker)
{
    int activity = 0;
    WOLFMQTT_BROKER_TIME_T now = WOLFMQTT_BROKER_GET_TIME_S();

    if (broker == NULL) {
        return 0;
    }

#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        for (i = 0; i < broker->max_pending_wills; i++) {
            BrokerPendingWill* pw = &broker->pending_wills[i];
            if (!pw->in_use) {
                continue;
            }
            if (now >= pw->publish_time) {                
                BrokerClient_PublishWillImmediate(broker, pw->topic,
                    pw->payload, pw->payload_len, pw->qos, pw->retain
#ifdef WOLFMQTT_V5
                    , NULL  /* Pending wills don't have stored properties */
#endif
                    );
                XMEMSET(pw, 0, sizeof(BrokerPendingWill));
                activity = 1;
            }
        }
    }
#else
    {
        BrokerPendingWill* pw = broker->pending_wills;
        BrokerPendingWill* prev = NULL;
        while (pw) {
            BrokerPendingWill* next = pw->next;
            if (now >= pw->publish_time) {
                BrokerClient_PublishWillImmediate(broker, pw->topic,
                    pw->payload, pw->payload_len, pw->qos, pw->retain
#ifdef WOLFMQTT_V5
                    , NULL  /* Pending wills don't have stored properties */
#endif
                    );
                if (prev) {
                    prev->next = next;
                }
                else {
                    broker->pending_wills = next;
                }
                if (pw->client_id) WOLFMQTT_FREE(pw->client_id);
                if (pw->topic) {
                    BROKER_FORCE_ZERO(pw->topic, XSTRLEN(pw->topic) + 1);
                    WOLFMQTT_FREE(pw->topic);
                }
                if (pw->payload) {
                    BROKER_FORCE_ZERO(pw->payload, pw->payload_len);
                    WOLFMQTT_FREE(pw->payload);
                }
                WOLFMQTT_FREE(pw);
                activity = 1;
            }
            else {
                prev = pw;
            }
            pw = next;
        }
    }
#endif

    return activity;
}
#endif /* WOLFMQTT_BROKER_WILL */

#ifdef WOLFMQTT_BROKER_RETAINED
static void BrokerRetained_DeliverToClient(MqttBroker* broker,
    BrokerClient* bc, const char* filter, MqttQoS sub_qos
#ifdef WOLFMQTT_V5
    , byte retain_handling, byte rap
#endif
    )
{
    WOLFMQTT_BROKER_TIME_T now;
    (void)sub_qos; /* retained always delivered at QoS 0 in this broker */

    if (broker == NULL || bc == NULL || filter == NULL) {
        return;
    }
    now = WOLFMQTT_BROKER_GET_TIME_S();

#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        for (i = 0; i < broker->max_retained; i++) {
            BrokerRetainedMsg* rm = &broker->retained[i];
            if (!rm->in_use || rm->topic[0] == '\0') {
                continue;
            }
            /* Skip expired messages */
            if (rm->expiry_sec > 0 &&
                (now - rm->store_time) >= rm->expiry_sec) {
                XMEMSET(rm, 0, sizeof(BrokerRetainedMsg));
                continue;
            }
            if (BrokerTopicMatch(filter, rm->topic)) {
                MqttPublish out_pub;
                int enc_rc;
                XMEMSET(&out_pub, 0, sizeof(out_pub));
                out_pub.topic_name = rm->topic;
                out_pub.qos = rm->qos;  /* Use original QoS of retained message */
                /* Set packet_id for QoS 1 and QoS 2 messages */
                if (out_pub.qos >= MQTT_QOS_1) {
                    out_pub.packet_id = BrokerNextPacketId(broker);
                }
#ifdef WOLFMQTT_V5
                /* Retain As Published: rap=1 means use original retain flag,
                 * rap=0 means force retain=0 regardless of Retain Handling.
                 * Since retained messages are always stored with retain=1,
                 * the "original" retain flag is 1. */
                if (rap == 1) {
                    /* RAP=1: Use Retain Handling value */
                    out_pub.retain = (retain_handling == 0) ? 1 : 0;
                } else {
                    /* RAP=0: Force retain=0 */
                    out_pub.retain = 0;
                }
#else
                out_pub.retain = 1;  /* Non-v5: always send with retain=1 */
#endif
                out_pub.duplicate = 0;
                out_pub.buffer = (rm->payload_len > 0) ? rm->payload : NULL;
                out_pub.total_len = rm->payload_len;
#ifdef WOLFMQTT_V5
                out_pub.protocol_level = bc->protocol_level;
#endif
                enc_rc = MqttEncode_Publish(bc->tx_buf,
                    BROKER_CLIENT_TX_SZ(bc), &out_pub, 0);
                if (enc_rc > 0) {
                    (void)MqttPacket_Write(&bc->client, bc->tx_buf, enc_rc);
                }
            }
        }
    }
#else
    {
        BrokerRetainedMsg* rm = broker->retained;
        BrokerRetainedMsg* rm_prev = NULL;
        while (rm) {
            BrokerRetainedMsg* rm_next = rm->next;
            /* Skip and remove expired messages */
            if (rm->expiry_sec > 0 &&
                (now - rm->store_time) >= rm->expiry_sec) {
                if (rm_prev) {
                    rm_prev->next = rm_next;
                }
                else {
                    broker->retained = rm_next;
                }
                if (rm->topic) WOLFMQTT_FREE(rm->topic);
                if (rm->payload) WOLFMQTT_FREE(rm->payload);
                WOLFMQTT_FREE(rm);
                rm = rm_next;
                continue;
            }
            if (rm->topic != NULL && BrokerTopicMatch(filter, rm->topic)) {
                MqttPublish out_pub;
                int enc_rc;
                XMEMSET(&out_pub, 0, sizeof(out_pub));
                out_pub.topic_name = rm->topic;
                out_pub.qos = rm->qos;  /* Use original QoS of retained message */
                /* Set packet_id for QoS 1 and QoS 2 messages */
                if (out_pub.qos >= MQTT_QOS_1) {
                    out_pub.packet_id = BrokerNextPacketId(broker);
                }
#ifdef WOLFMQTT_V5
                /* Retain As Published: rap=1 means use original retain flag,
                 * rap=0 means force retain=0 regardless of Retain Handling.
                 * Since retained messages are always stored with retain=1,
                 * the "original" retain flag is 1. */
                if (rap == 1) {
                    /* RAP=1: Use Retain Handling value */
                    out_pub.retain = (retain_handling == 0) ? 1 : 0;
                } else {
                    /* RAP=0: Force retain=0 */
                    out_pub.retain = 0;
                }
#else
                out_pub.retain = 1;  /* Non-v5: always send with retain=1 */
#endif
                out_pub.duplicate = 0;
                out_pub.buffer = (rm->payload_len > 0) ? rm->payload : NULL;
                out_pub.total_len = rm->payload_len;
#ifdef WOLFMQTT_V5
                out_pub.protocol_level = bc->protocol_level;
#endif
                enc_rc = MqttEncode_Publish(bc->tx_buf,
                    BROKER_CLIENT_TX_SZ(bc), &out_pub, 0);
                if (enc_rc > 0) {
                    (void)MqttPacket_Write(&bc->client, bc->tx_buf, enc_rc);
                }
            }
            rm_prev = rm;
            rm = rm_next;
        }
    }
#endif
}
#endif /* WOLFMQTT_BROKER_RETAINED */

#ifdef WOLFMQTT_BROKER_WILL
static void BrokerClient_PublishWill(MqttBroker* broker, BrokerClient* bc)
{
    if (broker == NULL || bc == NULL || !bc->has_will) {
        return;
    }
    if (!BROKER_STR_VALID(bc->will_topic)) {
        return;
    }

    /* v5 Will Delay Interval: defer publication */
    if (bc->will_delay_sec > 0) {
        if (BrokerPendingWill_Add(broker, bc) == MQTT_CODE_SUCCESS) {
            BrokerClient_ClearWill(bc);
            return; /* will deferred, not published now */
        }
        /* If add failed (out of slots), publish immediately as fallback */
    }

    BrokerClient_PublishWillImmediate(broker, bc->will_topic,
        bc->will_payload, bc->will_payload_len, bc->will_qos,
        bc->will_retain
#ifdef WOLFMQTT_V5
        , bc->will_props  /* Forward will properties */
#endif
        );
    /* Note: will_props will be freed by BrokerClient_ClearWill */
}

/* Publish a will message immediately (shared by direct and deferred paths) */
static void BrokerClient_PublishWillImmediate(MqttBroker* broker,
    const char* topic, const byte* payload, word16 payload_len,
    MqttQoS qos, byte retain
#ifdef WOLFMQTT_V5
    ,MqttProp* will_props
#endif
)
{
    if (broker == NULL || topic == NULL) {
        return;
    }

    /* Handle retain flag on will message */
    if (retain) {
        if (payload_len == 0) {
            BrokerRetained_Delete(broker, topic);
        }
        else {
            int ret_rc = BrokerRetained_Store(broker, topic, payload,
                payload_len, qos, 0);
            if (ret_rc != MQTT_CODE_SUCCESS) {
                WBLOG_ERR(broker, "Retained store failed: %s",
                    MqttClient_ReturnCodeToString(ret_rc));
            }
        }
    }

    /* Fan out to matching subscribers */
#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        for (i = 0; i < broker->max_subs; i++) {
            BrokerSub* sub = &broker->subs[i];
            if (!sub->in_use) continue;
#else
    {
        BrokerSub* sub = broker->subs;
        while (sub) {
#endif
            if (sub->client != NULL && sub->client->protocol_level != 0 &&
                BROKER_STR_VALID(sub->filter) &&
                BrokerTopicMatch(sub->filter, topic)) {
                MqttPublish out_pub;
                MqttQoS eff_qos;
                int enc_rc;
                XMEMSET(&out_pub, 0, sizeof(out_pub));
                out_pub.topic_name = (char*)topic;
                eff_qos = (qos < sub->qos) ? qos : sub->qos;
                out_pub.qos = eff_qos;
                out_pub.retain = 0;
                out_pub.duplicate = 0;
                out_pub.buffer = (payload_len > 0) ? (byte*)payload : NULL;
                out_pub.total_len = payload_len;
#ifdef WOLFMQTT_V5
                /* Check receiveMaximum flow control for QoS 1/2 */
                if (eff_qos >= MQTT_QOS_1 &&
                    sub->client->protocol_level >= MQTT_CONNECT_PROTOCOL_LEVEL_5) {
                    if (sub->client->inflight_count >= sub->client->receive_maximum) {
                        /* Skip this subscriber - inflight window full */
                        WBLOG_INFO(broker,
                            "Will PUBLISH deferred sock=%d inflight=%d max=%d",
                            (int)sub->client->sock,
                            sub->client->inflight_count,
                            sub->client->receive_maximum);
                        continue;
                    }
                }
#endif
                if (eff_qos >= MQTT_QOS_1) {
                    out_pub.packet_id = BrokerNextPacketId(broker);
                }
#ifdef WOLFMQTT_V5
                out_pub.protocol_level = sub->client->protocol_level;
                /* Forward will properties (deep copy for each subscriber) */
                if (sub->client->protocol_level >=
                    MQTT_CONNECT_PROTOCOL_LEVEL_5 && will_props != NULL) {
                    MqttProp* prop = will_props;
                    int prop_count = 0;
                    while (prop != NULL && prop_count++ < 100) {
                        MqttProp* new_prop = MqttProps_Add(&out_pub.props);
                        if (new_prop != NULL) {
                            int copy_rc = BrokerProp_Copy(new_prop, prop);
                            if (copy_rc != MQTT_CODE_SUCCESS) {
                                WBLOG_ERR(broker,
                                    "Failed to copy will property type %d rc=%d, skipping",
                                    prop->type, copy_rc);
                                new_prop->type = MQTT_PROP_NONE;
                            }
                        }
                        prop = prop->next;
                    }
                }
#endif
                enc_rc = MqttEncode_Publish(sub->client->tx_buf,
                    BROKER_CLIENT_TX_SZ(sub->client), &out_pub, 0);
                if (enc_rc > 0) {
                    (void)MqttPacket_Write(&sub->client->client,
                        sub->client->tx_buf, enc_rc);
#ifdef WOLFMQTT_V5
                    /* Increment inflight counter for QoS 1/2 messages */
                    if (eff_qos >= MQTT_QOS_1 &&
                        sub->client->protocol_level >= MQTT_CONNECT_PROTOCOL_LEVEL_5) {
                        sub->client->inflight_count++;
                    }
#endif
                }
#ifdef WOLFMQTT_V5
                /* Clean up properties copied for this subscriber */
                if (out_pub.props != NULL) {
                    (void)MqttProps_Free(out_pub.props);
                    out_pub.props = NULL;
                }
#endif
            }
#ifndef WOLFMQTT_STATIC_MEMORY
            sub = sub->next;
#endif
        }
    }
}
#endif /* WOLFMQTT_BROKER_WILL */

/* -------------------------------------------------------------------------- */
/* External API for publishing messages (HTTP API etc.)                        */
/* -------------------------------------------------------------------------- */
/* Publish message from external source (e.g., HTTP API) */
int BrokerPublish_Message(MqttBroker* broker, const char* topic,
                         const byte* payload, word16 payload_len,
                         MqttQoS qos, byte retain)
{
    if (broker == NULL || topic == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    /* Handle retain flag */
    if (retain) {
        if (payload_len == 0) {
            BrokerRetained_Delete(broker, topic);
        }
        else {
            int ret_rc = BrokerRetained_Store(broker, topic, payload,
                payload_len, qos, 0);
            if (ret_rc != MQTT_CODE_SUCCESS) {
                WBLOG_ERR(broker, "Retained store failed: %s",
                    MqttClient_ReturnCodeToString(ret_rc));
            }
        }
    }

    /* Fan out to matching subscribers */
#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        for (i = 0; i < broker->max_subs; i++) {
            BrokerSub* sub = &broker->subs[i];
            if (!sub->in_use) continue;
#else
    {
        BrokerSub* sub = broker->subs;
        while (sub) {
#endif
            if (sub->client != NULL && sub->client->protocol_level != 0 &&
                BROKER_STR_VALID(sub->filter) &&
                BrokerTopicMatch(sub->filter, topic)) {
                MqttPublish out_pub;
                MqttQoS eff_qos;
                int enc_rc;
                XMEMSET(&out_pub, 0, sizeof(out_pub));
                out_pub.topic_name = (char*)topic;
                eff_qos = (qos < sub->qos) ? qos : sub->qos;
                out_pub.qos = eff_qos;
                out_pub.retain = 0;
                out_pub.duplicate = 0;
                out_pub.buffer = (payload_len > 0) ? (byte*)payload : NULL;
                out_pub.total_len = payload_len;
#ifdef WOLFMQTT_V5
                /* Check receiveMaximum flow control for QoS 1/2 */
                if (eff_qos >= MQTT_QOS_1 &&
                    sub->client->protocol_level >= MQTT_CONNECT_PROTOCOL_LEVEL_5) {
                    if (sub->client->inflight_count >= sub->client->receive_maximum) {
                        /* Skip this subscriber - inflight window full */
                        WBLOG_INFO(broker,
                            "API PUBLISH deferred sock=%d inflight=%d max=%d",
                            (int)sub->client->sock,
                            sub->client->inflight_count,
                            sub->client->receive_maximum);
                        goto next_sub;
                    }
                }
#endif
                if (eff_qos >= MQTT_QOS_1) {
                    out_pub.packet_id = BrokerNextPacketId(broker);
                }
#ifdef WOLFMQTT_V5
                out_pub.protocol_level = sub->client->protocol_level;
#endif
                enc_rc = MqttEncode_Publish(sub->client->tx_buf,
                    BROKER_CLIENT_TX_SZ(sub->client), &out_pub, 0);
                if (enc_rc > 0) {
                    (void)MqttPacket_Write(&sub->client->client,
                        sub->client->tx_buf, enc_rc);
#ifdef WOLFMQTT_V5
                    /* Increment inflight counter for QoS 1/2 messages */
                    if (eff_qos >= MQTT_QOS_1 &&
                        sub->client->protocol_level >= MQTT_CONNECT_PROTOCOL_LEVEL_5) {
                        sub->client->inflight_count++;
                    }
#endif
                }
            }
#ifdef WOLFMQTT_V5
            next_sub:
#endif
#ifndef WOLFMQTT_STATIC_MEMORY
            sub = sub->next;
#endif
        }
    }

    return MQTT_CODE_SUCCESS;
}

/* Kick/disconnect a client by client_id or IP address */
int BrokerKick_Client(MqttBroker* broker, const char* client_identifier)
{
    BrokerClient* bc;
    int found = 0;
    int is_ip_address = 0;
    
    /* Check if the identifier looks like an IP address (contains dots) */
    if (client_identifier != NULL) {
        const char* dot = XSTRCHR(client_identifier, '.');
        if (dot != NULL) {
            is_ip_address = 1;
        }
    }

    if (broker == NULL || client_identifier == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    /* Search for client by client_id or IP address */
#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        for (i = 0; i < broker->max_clients; i++) {
            bc = &broker->clients[i];
            if (!bc->in_use) continue;
#else
    {
        bc = broker->clients;
        while (bc) {
#endif
            if (bc->connected) {
                int should_kick = 0;
                
                /* For IP addresses, match all clients with same IP */
                if (is_ip_address) {
                    if (XSTRCMP(bc->client_ip, client_identifier) == 0) {
                        should_kick = 1;
                    }
                } 
                /* For client IDs, match exact client ID */
                else {
                    if (BROKER_STR_VALID(bc->client_id) &&
                        XSTRCMP(bc->client_id, client_identifier) == 0) {
                        should_kick = 1;
                    }
                }
                
                if (should_kick) {
                    WBLOG_INFO(broker, "Kicking client client_id=%s ip=%s",
                        BROKER_CLIENT_ID(bc), bc->client_ip);
                    /* Remove subscriptions */
                    BrokerSubs_RemoveClient(broker, bc);
                    /* Remove client */
                    BrokerClient_Remove(broker, bc, 0);
                    found = 1;
                    
                    /* For client IDs, only kick the first match and break */
                    if (!is_ip_address) {
                        break;
                    }
                    /* For IP addresses, continue to kick all matching clients */
                }
            }
#ifndef WOLFMQTT_STATIC_MEMORY
            bc = bc->next;
#endif
        }
    }

    if (!found) {
        WBLOG_WARN(broker, "Client not found for kick: %s", client_identifier);
        return MQTT_CODE_ERROR_NOT_FOUND;
    }

    return MQTT_CODE_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* Topic matching                                                              */
/* -------------------------------------------------------------------------- */
#ifdef WOLFMQTT_BROKER_WILDCARDS
static int BrokerTopicMatch(const char* filter, const char* topic)
{
    const char* f = filter;
    const char* t = topic;

    if (filter == NULL || topic == NULL) {
        return 0;
    }

    /* [MQTT-4.7.2] Wildcard filters must not match $-prefixed topics */
    if (*t == '$' && (*f == '+' || *f == '#')) {
        return 0;
    }

    while (*f && *t) {
        if (*f == '#') {
            return (f[1] == '\0');
        }
        if (*f == '+') {
            while (*t && *t != '/') {
                t++;
            }
            f++;
        }
        else {
            if (*f != *t) {
                return 0;
            }
            f++;
            t++;
        }
        if (*t == '/' && *f == '/') {
            t++;
            f++;
        }
        else if (*t == '/' || *f == '/') {
            /* [MQTT-4.7.1.2] 'topic/#' must also match 'topic' itself */
            if (*f == '/' && f[1] == '#' && f[2] == '\0' && *t == '\0') {
                return 1;
            }
            return 0;
        }
    }

    if (*f == '#') {
        return (f[1] == '\0');
    }
    if (*f == '+' && f[1] == '\0' && *t == '\0') {
        return 1;
    }
    return (*f == '\0' && *t == '\0');
}
#else
/* Exact match only when wildcards are disabled */
static int BrokerTopicMatch(const char* filter, const char* topic)
{
    if (filter == NULL || topic == NULL) {
        return 0;
    }
    return (XSTRCMP(filter, topic) == 0);
}
#endif /* WOLFMQTT_BROKER_WILDCARDS */

/* -------------------------------------------------------------------------- */
/* Packet send helpers                                                         */
/* -------------------------------------------------------------------------- */
static int BrokerSend_PingResp(BrokerClient* bc)
{
    if (bc == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    bc->tx_buf[0] = MQTT_PACKET_TYPE_SET(MQTT_PACKET_TYPE_PING_RESP);
    bc->tx_buf[1] = 0;
    return MqttPacket_Write(&bc->client, bc->tx_buf, 2);
}

static int BrokerSend_SubAck(BrokerClient* bc, word16 packet_id,
    const byte* return_codes, int return_code_count)
{
    int remain_len;
    int pos = 0;
    int i;

    if (bc == NULL || return_codes == NULL || return_code_count <= 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    remain_len = MQTT_DATA_LEN_SIZE + return_code_count;
#ifdef WOLFMQTT_V5
    if (bc->protocol_level >= MQTT_CONNECT_PROTOCOL_LEVEL_5) {
        remain_len += 1; /* property length (0) */
    }
#endif

    /* 1 (type) + 4 (max VBI) + remain_len */
    if (1 + 4 + remain_len > (int)BROKER_CLIENT_TX_SZ(bc)) {
        return MQTT_CODE_ERROR_OUT_OF_BUFFER;
    }

    bc->tx_buf[pos++] = MQTT_PACKET_TYPE_SET(MQTT_PACKET_TYPE_SUBSCRIBE_ACK);
    pos += MqttEncode_Vbi(&bc->tx_buf[pos], remain_len);
    pos += MqttEncode_Num(&bc->tx_buf[pos], packet_id);
#ifdef WOLFMQTT_V5
    if (bc->protocol_level >= MQTT_CONNECT_PROTOCOL_LEVEL_5) {
        bc->tx_buf[pos++] = 0; /* property length */
    }
#endif
    for (i = 0; i < return_code_count; i++) {
        bc->tx_buf[pos++] = return_codes[i];
    }

    return MqttPacket_Write(&bc->client, bc->tx_buf, pos);
}

#ifdef WOLFMQTT_V5
/* Find a property by type in a property list */
static MqttProp* BrokerProps_Find(MqttBroker* broker, MqttProp* head, MqttPropertyType type)
{
    MqttProp* prop = head;
    int count = 0;
    /* Guard against circular property list */
    while (prop != NULL && count++ < BROKER_MAX_PROP_CHAIN_LENGTH) {
        if (prop->type == type) {
            return prop;
        }
        prop = prop->next;
    }
    if (count >= BROKER_MAX_PROP_CHAIN_LENGTH) {
        /* Property list is corrupted (circular or too long) */
        WBLOG_ERR(broker, "Property list corrupted - circular or too many properties");
    }
    return NULL;
}

/* Extract integer property from property list */
static int BrokerExtract_IntProp(MqttBroker* broker, MqttProp* props,
                                  MqttPropertyType type, word32* dest) {
    if (props == NULL) return MQTT_CODE_SUCCESS;
    MqttProp* prop = BrokerProps_Find(broker, props, type);
    if (prop != NULL) {
        *dest = prop->data_int;
        return MQTT_CODE_SUCCESS;
    }
    return MQTT_CODE_ERROR_NOT_FOUND;
}

/* Extract short property from property list */
static int BrokerExtract_ShortProp(MqttBroker* broker, MqttProp* props,
                                    MqttPropertyType type, word16* dest) {
    if (props == NULL) return MQTT_CODE_SUCCESS;
    MqttProp* prop = BrokerProps_Find(broker, props, type);
    if (prop != NULL) {
        *dest = prop->data_short;
        return MQTT_CODE_SUCCESS;
    }
    return MQTT_CODE_ERROR_NOT_FOUND;
}

static int BrokerSend_Disconnect(BrokerClient* bc, byte reason_code)
{
    int rc;
    MqttDisconnect disc;

    if (bc == NULL ||
        bc->protocol_level < MQTT_CONNECT_PROTOCOL_LEVEL_5) {
        return 0;
    }

    XMEMSET(&disc, 0, sizeof(disc));
    disc.protocol_level = bc->protocol_level;
    disc.reason_code = reason_code;

    rc = MqttEncode_Disconnect(bc->tx_buf, BROKER_CLIENT_TX_SZ(bc), &disc);
    if (rc > 0) {
        rc = MqttPacket_Write(&bc->client, bc->tx_buf, rc);
    }
    return rc;
}
#endif

/* -------------------------------------------------------------------------- */
/* Packet handlers                                                             */
/* -------------------------------------------------------------------------- */

/* Returns: > 0 success, 0 auth rejected (CONNACK sent with refused),
 *          < 0 error */
static int BrokerHandle_Connect(BrokerClient* bc, int rx_len,
    MqttBroker* broker)
{
    int rc;
    MqttConnect mc;
    MqttConnectAck ack;
    MqttMessage lwt;

    BROKER_INIT_STRUCT(mc);
    BROKER_INIT_STRUCT(ack);
    BROKER_INIT_STRUCT(lwt);
    mc.lwt_msg = &lwt;

    rc = MqttDecode_Connect(bc->rx_buf, rx_len, &mc);
    if (rc < 0) {
        WBLOG_ERR(broker, "CONNECT decode failed rc=%d", rc);
    #ifdef WOLFMQTT_V5
        if (mc.props) { (void)MqttProps_Free(mc.props); }
        if (lwt.props) { (void)MqttProps_Free(lwt.props); }
    #endif
        return rc;
    }   

    /* Store client ID */
#ifdef WOLFMQTT_STATIC_MEMORY
    bc->client_id[0] = '\0';
#endif
    if (mc.client_id) {
        word16 id_len = 0;
        if (MqttDecode_Num((byte*)mc.client_id - MQTT_DATA_LEN_SIZE,
                &id_len, MQTT_DATA_LEN_SIZE) == MQTT_DATA_LEN_SIZE) {
        #ifdef WOLFMQTT_STATIC_MEMORY
            if (id_len >= BROKER_MAX_CLIENT_ID_LEN) {
                WBLOG_ERR(broker,
                    "client_id too long (%u >= %d) sock=%d",
                    (unsigned)id_len, BROKER_MAX_CLIENT_ID_LEN,
                    (int)bc->sock);
            #ifdef WOLFMQTT_V5
                if (mc.protocol_level >= MQTT_CONNECT_PROTOCOL_LEVEL_5) {
                    ack.return_code = MQTT_REASON_CLIENT_ID_NOT_VALID;
                }
                else
            #endif
                {
                    ack.return_code =
                        MQTT_CONNECT_ACK_CODE_REFUSED_ID;
                }
                goto send_connack;
            }
        #endif
            BROKER_STORE_STR(bc->client_id, mc.client_id, id_len,
                BROKER_MAX_CLIENT_ID_LEN);
        }
    }

    bc->protocol_level = mc.protocol_level;
    bc->keep_alive_sec = mc.keep_alive_sec;
    bc->last_rx = WOLFMQTT_BROKER_GET_TIME_S();

    /* Client ID uniqueness and clean session handling */
    bc->clean_session = mc.clean_session;
#ifdef WOLFMQTT_V5
    /* Initialize session expiry interval (default 0 = session ends on disconnect) */
    bc->session_expiry_interval = 0;
    bc->disconnect_time = 0;

    /* Initialize topic alias mapping */
    BrokerTopicAlias_Init(bc);

    /* Initialize MQTT 5 flow control state */
    bc->receive_maximum = 65535;  /* Default: unlimited */
    bc->inflight_count = 0;
    bc->max_packet_size = 0;      /* Default: no limit */

    /* Extract Session Expiry Interval from CONNECT properties (v5) */
    if (mc.props != NULL) {
        MqttProp* prop = BrokerProps_Find(broker, mc.props,
            MQTT_PROP_SESSION_EXPIRY_INTERVAL);
        if (prop != NULL) {
            bc->session_expiry_interval = prop->data_int;
        }
    }
    /* If no Session Expiry Interval property and clean session=0, use broker default */
    if (bc->session_expiry_interval == 0 && !mc.clean_session) {
        bc->session_expiry_interval = broker->default_session_expiry_interval;
        WBLOG_INFO(broker, "CONNECT client_id=%s using default_session_expiry_interval=%u seconds",
            BROKER_STR_VALID(bc->client_id) ? bc->client_id : "(null)",
            (unsigned int)bc->session_expiry_interval);
    }
#else
    /* MQTT 3.1.1: Initialize session_expiry_interval for clean_session=0 */
    bc->session_expiry_interval = 0;
    if (!mc.clean_session) {
        /* Use broker default session expiry interval for persistent sessions */
        bc->session_expiry_interval = broker->default_session_expiry_interval;
        WBLOG_INFO(broker, "CONNECT client_id=%s (MQTT 3.1.1) clean=0, using default_session_expiry_interval=%u seconds",
            BROKER_STR_VALID(bc->client_id) ? bc->client_id : "(null)",
            (unsigned int)bc->session_expiry_interval);
    }
#endif
#ifdef WOLFMQTT_V5
    /* Extract Topic Alias Maximum from CONNECT properties (v5) */
    if (mc.props != NULL) {
        MqttProp* prop = BrokerProps_Find(broker, mc.props, MQTT_PROP_TOPIC_ALIAS_MAX);
        if (prop != NULL) {
            bc->topic_alias_maximum = prop->data_short;
        WBLOG_DBG(broker, "CONNECT: client_id=%s topic_alias_maximum=%u",
                BROKER_STR_VALID(bc->client_id) ? bc->client_id : "(null)",
                (unsigned int)bc->topic_alias_maximum);
        }

        /* Extract Receive Maximum from CONNECT properties (v5) */
        prop = BrokerProps_Find(broker, mc.props, MQTT_PROP_RECEIVE_MAX);
        if (prop != NULL) {
            bc->receive_maximum = prop->data_short;
            /* Minimum value is 1 per MQTT 5 spec */
            if (bc->receive_maximum == 0) {
                bc->receive_maximum = 1;
            }
            WBLOG_INFO(broker, "CONNECT client_id=%s receive_maximum=%u",
                BROKER_STR_VALID(bc->client_id) ? bc->client_id : "(null)",
                (unsigned int)bc->receive_maximum);
        }

        /* Extract Maximum Packet Size from CONNECT properties (v5) */
        prop = BrokerProps_Find(broker, mc.props, MQTT_PROP_MAX_PACKET_SZ);
        if (prop != NULL) {
            bc->max_packet_size = prop->data_int;
            WBLOG_INFO(broker, "CONNECT client_id=%s max_packet_size=%u",
                BROKER_STR_VALID(bc->client_id) ? bc->client_id : "(null)",
                (unsigned int)bc->max_packet_size);
        }
    }
#endif
    if (BROKER_STR_VALID(bc->client_id)) {
        BrokerClient* old;

        /* Cancel any pending will for this client_id (reconnect) */
        BrokerPendingWill_Cancel(broker, bc->client_id);

        old = BrokerClient_FindByClientId(broker, bc->client_id, bc);
        if (old != NULL) {
            WBLOG_INFO(broker, "duplicate client_id=%s, disconnecting "
                "old sock=%d", bc->client_id, (int)old->sock);
            /* Publish old client's will on takeover */
#ifdef WOLFMQTT_V5
            if (old->protocol_level < MQTT_CONNECT_PROTOCOL_LEVEL_5) {
                BrokerClient_PublishWill(broker, old);
            }
            else {
                BrokerSend_Disconnect(old,
                    MQTT_REASON_SESSION_TAKEN_OVER);
                BrokerClient_ClearWill(old);
            }
#else
            BrokerClient_PublishWill(broker, old);
#endif
            /* Determine if session should persist based on protocol version */
#ifdef WOLFMQTT_V5
            /* MQTT 5: Use Session Expiry Interval (0 = session ends on disconnect) */
            if (bc->session_expiry_interval > 0) {
                /* Reassociate old client's subs to new client */
                BrokerSubs_ReassociateClient(broker, bc->client_id, bc);
            }
#else
            /* MQTT 3.1.1: Use clean_session flag (0 = persistent) */
            if (!mc.clean_session) {
                /* Reassociate old client's subs to new client */
                BrokerSubs_ReassociateClient(broker, bc->client_id, bc);
            }
#endif
            BrokerSubs_RemoveClient(broker, old);
            BrokerClient_Remove(broker, old, -1);
        }
        else {
            /* No existing client, check for orphaned subs from previous session */
#ifdef WOLFMQTT_V5
            /* MQTT 5: Use Session Expiry Interval */
            if (bc->session_expiry_interval > 0) {
                /* Reassociate orphaned subs to new client */
                BrokerSubs_ReassociateClient(broker, bc->client_id, bc);
            }
            else {
                /* Session ends on disconnect, remove any orphaned subs */
                BrokerSubs_RemoveByClientId(broker, bc->client_id);
            }
#else
            /* MQTT 3.1.1: Use clean_session flag */
            if (!mc.clean_session) {
                /* Reassociate orphaned subs to new client */
                BrokerSubs_ReassociateClient(broker, bc->client_id, bc);
            }
            else {
                /* Clean session, remove any orphaned subs */
                BrokerSubs_RemoveByClientId(broker, bc->client_id);
            }
#endif
        }
    }

    /* Store Last Will and Testament */
    BrokerClient_ClearWill(bc);
#ifdef WOLFMQTT_BROKER_WILL
    if (mc.enable_lwt && mc.lwt_msg != NULL) {
        if (mc.lwt_msg->topic_name != NULL &&
            mc.lwt_msg->topic_name_len > 0) {
        #ifdef WOLFMQTT_STATIC_MEMORY
            if (mc.lwt_msg->topic_name_len >= BROKER_MAX_TOPIC_LEN) {
                WBLOG_ERR(broker,
                    "LWT topic too long (%u >= %d) sock=%d",
                    (unsigned)mc.lwt_msg->topic_name_len,
                    BROKER_MAX_TOPIC_LEN, (int)bc->sock);
                ack.return_code =
                    MQTT_CONNECT_ACK_CODE_REFUSED_UNAVAIL;
                goto send_connack;
            }
        #endif
            BROKER_STORE_STR(bc->will_topic, mc.lwt_msg->topic_name,
                mc.lwt_msg->topic_name_len, BROKER_MAX_TOPIC_LEN);
        }
        if (mc.lwt_msg->total_len > 0 && mc.lwt_msg->buffer != NULL) {
            word16 wp_len;
            if (mc.lwt_msg->total_len > BROKER_MAX_WILL_PAYLOAD_LEN) {
                WBLOG_ERR(broker,
                    "LWT payload too large (%u > %d) sock=%d",
                    (unsigned)mc.lwt_msg->total_len,
                    BROKER_MAX_WILL_PAYLOAD_LEN, (int)bc->sock);
                ack.return_code =
                    MQTT_CONNECT_ACK_CODE_REFUSED_UNAVAIL;
                goto send_connack;
            }
            else {
                wp_len = (word16)mc.lwt_msg->total_len;
            }
#ifdef WOLFMQTT_STATIC_MEMORY
            XMEMCPY(bc->will_payload, mc.lwt_msg->buffer, wp_len);
#else
            bc->will_payload = (byte*)WOLFMQTT_MALLOC(wp_len);
            if (bc->will_payload != NULL) {
                XMEMCPY(bc->will_payload, mc.lwt_msg->buffer, wp_len);
            }
            else {
                wp_len = 0;
            }
#endif
            bc->will_payload_len = wp_len;
        }
        bc->will_qos = mc.lwt_msg->qos;
        bc->will_retain = mc.lwt_msg->retain;
        bc->will_delay_sec = 0;
        bc->will_props = NULL;
#ifdef WOLFMQTT_V5
        if (mc.lwt_msg->props != NULL) {
            MqttProp* prop;

            /* Extract Will Delay Interval */
            prop = BrokerProps_Find(broker, mc.lwt_msg->props,
                MQTT_PROP_WILL_DELAY_INTERVAL);
            if (prop != NULL) {
                bc->will_delay_sec = prop->data_int;
            }

            /* Deep copy all will properties (for forwarding on will publish) */
            prop = mc.lwt_msg->props;
            while (prop != NULL) {
                MqttProp* new_prop = MqttProps_Add(&bc->will_props);
                if (new_prop != NULL) {
                    int copy_rc = BrokerProp_Copy(new_prop, prop);
                    if (copy_rc != MQTT_CODE_SUCCESS) {
                        WBLOG_ERR(broker,
                            "Failed to copy will property type %d rc=%d",
                            prop->type, copy_rc);
                        new_prop->type = MQTT_PROP_NONE;
                    }
                }
                prop = prop->next;
            }
        }
#endif
        bc->has_will = 1;
        WBLOG_DBG(broker, "LWT stored sock=%d topic=%s qos=%d retain=%d "
            "len=%u delay=%u", (int)bc->sock, bc->will_topic,
            bc->will_qos, bc->will_retain,
            (unsigned)bc->will_payload_len,
            (unsigned)bc->will_delay_sec);
    }
#endif /* WOLFMQTT_BROKER_WILL */

    /* Store credentials */
#ifdef WOLFMQTT_BROKER_AUTH
#ifdef WOLFMQTT_STATIC_MEMORY
    bc->username[0] = '\0';
    bc->password[0] = '\0';
#endif
    if (mc.username) {
        word16 ulen = 0;
        if (MqttDecode_Num((byte*)mc.username - MQTT_DATA_LEN_SIZE,
                &ulen, MQTT_DATA_LEN_SIZE) == MQTT_DATA_LEN_SIZE) {
        #ifdef WOLFMQTT_STATIC_MEMORY
            if (ulen >= BROKER_MAX_USERNAME_LEN) {
                WBLOG_ERR(broker,
                    "username too long (%u >= %d) sock=%d",
                    (unsigned)ulen, BROKER_MAX_USERNAME_LEN,
                    (int)bc->sock);
            #ifdef WOLFMQTT_V5
                if (mc.protocol_level >= MQTT_CONNECT_PROTOCOL_LEVEL_5) {
                    ack.return_code = MQTT_REASON_BAD_USER_OR_PASS;
                }
                else
            #endif
                {
                    ack.return_code =
                        MQTT_CONNECT_ACK_CODE_REFUSED_BAD_USER_PWD;
                }
                goto send_connack;
            }
        #endif
            BROKER_STORE_STR_SENSITIVE(bc->username, mc.username, ulen,
                BROKER_MAX_USERNAME_LEN);
        }
    }
    if (mc.password) {
        word16 plen = 0;
        if (MqttDecode_Num((byte*)mc.password - MQTT_DATA_LEN_SIZE,
                &plen, MQTT_DATA_LEN_SIZE) == MQTT_DATA_LEN_SIZE) {
        #ifdef WOLFMQTT_STATIC_MEMORY
            if (plen >= BROKER_MAX_PASSWORD_LEN) {
                WBLOG_ERR(broker,
                    "password too long (%u >= %d) sock=%d",
                    (unsigned)plen, BROKER_MAX_PASSWORD_LEN,
                    (int)bc->sock);
            #ifdef WOLFMQTT_V5
                if (mc.protocol_level >= MQTT_CONNECT_PROTOCOL_LEVEL_5) {
                    ack.return_code = MQTT_REASON_BAD_USER_OR_PASS;
                }
                else
            #endif
                {
                    ack.return_code =
                        MQTT_CONNECT_ACK_CODE_REFUSED_BAD_USER_PWD;
                }
                goto send_connack;
            }
        #endif
            BROKER_STORE_STR_SENSITIVE(bc->password, mc.password, plen,
                BROKER_MAX_PASSWORD_LEN);
        }
    }
#endif /* WOLFMQTT_BROKER_AUTH */

    /* Check auth before sending CONNACK */
    ack.flags = 0;
    ack.return_code = MQTT_CONNECT_ACK_CODE_ACCEPTED;
#ifdef WOLFMQTT_V5
    ack.protocol_level = mc.protocol_level;
    ack.props = NULL;
#endif

#ifdef WOLFMQTT_BROKER_AUTH
    if (broker->username || broker->password) {
        int auth_ok = 1;
        if (broker->username && (
        #ifndef WOLFMQTT_STATIC_MEMORY
            bc->username == NULL ||
        #endif
            bc->username[0] == '\0' ||
            BrokerStrCompare(broker->username, bc->username) != 0)) {
            auth_ok = 0;
        }
        if (broker->password && (
        #ifndef WOLFMQTT_STATIC_MEMORY
            bc->password == NULL ||
        #endif
            bc->password[0] == '\0' ||
            BrokerStrCompare(broker->password, bc->password) != 0)) {
            auth_ok = 0;
        }
        if (!auth_ok) {
            WBLOG_WARN(broker, "authentication failed client_id=%s user=%s ip=%s",
            #ifdef WOLFMQTT_STATIC_MEMORY
                BROKER_CLIENT_ID(bc), bc->username[0] ? bc->username : "(null)", bc->client_ip);
            #else
                BROKER_CLIENT_ID(bc), (bc->username && bc->username[0]) ? bc->username : "(null)", bc->client_ip);
            #endif
        #ifdef WOLFMQTT_V5
            if (mc.protocol_level >= MQTT_CONNECT_PROTOCOL_LEVEL_5) {
                ack.return_code = MQTT_REASON_BAD_USER_OR_PASS;
            }
            else
        #endif
            {
                ack.return_code =
                    MQTT_CONNECT_ACK_CODE_REFUSED_BAD_USER_PWD;
            }
        }
    }
#endif /* WOLFMQTT_BROKER_AUTH */

#ifdef WOLFMQTT_V5
    if (bc->protocol_level >= MQTT_CONNECT_PROTOCOL_LEVEL_5 &&
        ack.return_code == MQTT_CONNECT_ACK_CODE_ACCEPTED) {
        MqttProp* prop;

        /* If client sent empty client ID, generate one and inform client */
        if (!BROKER_STR_VALID(bc->client_id)) {
            char auto_id[32];
            int id_len = XSNPRINTF(auto_id, (int)sizeof(auto_id),
                "auto-%04x", broker->next_packet_id++);
            if (broker->next_packet_id == 0) {
                broker->next_packet_id = 1;
            }
            if (id_len > 0) {
                BROKER_STORE_STR(bc->client_id, auto_id, (word16)id_len,
                    BROKER_MAX_CLIENT_ID_LEN);
            }
            if (BROKER_STR_VALID(bc->client_id)) {
                prop = MqttProps_Add(&ack.props);
                if (prop != NULL) {
                    prop->type = MQTT_PROP_ASSIGNED_CLIENT_ID;
                    prop->data_str.str = bc->client_id;
                    prop->data_str.len = (word16)XSTRLEN(bc->client_id);
                }
            }
        }

        /* Advertise feature availability (always enabled) */
        prop = MqttProps_Add(&ack.props);
        if (prop != NULL) {
            prop->type = MQTT_PROP_RETAIN_AVAIL;
            prop->data_byte = 1; /* retain always available */
        }

        prop = MqttProps_Add(&ack.props);
        if (prop != NULL) {
            prop->type = MQTT_PROP_WILDCARD_SUB_AVAIL;
            prop->data_byte = 1; /* wildcard subscriptions always available */
        }

        prop = MqttProps_Add(&ack.props);
        if (prop != NULL) {
            prop->type = MQTT_PROP_SUBSCRIPTION_ID_AVAIL;
            prop->data_byte = 1; /* subscription IDs always available */
        }

        prop = MqttProps_Add(&ack.props);
        if (prop != NULL) {
            prop->type = MQTT_PROP_SHARED_SUBSCRIPTION_AVAIL;
            prop->data_byte = 1; /* shared subscriptions always available */
        }

        prop = MqttProps_Add(&ack.props);
        if (prop != NULL) {
            prop->type = MQTT_PROP_MAX_QOS;
            prop->data_byte = 2; /* QoS 0, 1, 2 always supported */
        }

        /* Advertise Topic Alias Maximum (from BrokerOptions) */
        prop = MqttProps_Add(&ack.props);
        if (prop != NULL) {
            prop->type = MQTT_PROP_TOPIC_ALIAS_MAX;
            prop->data_short = broker->topic_alias_max;
        }

        /* Advertise Maximum Packet Size (from BrokerOptions if non-zero) */
        if (broker->max_packet_size > 0) {
            prop = MqttProps_Add(&ack.props);
            if (prop != NULL) {
                prop->type = MQTT_PROP_MAX_PACKET_SZ;
                prop->data_int = broker->max_packet_size;
            }
        }
    }
#endif

#if defined(WOLFMQTT_BROKER_WILL) || defined(WOLFMQTT_STATIC_MEMORY)
send_connack:
#endif
    rc = MqttEncode_ConnectAck(bc->tx_buf, BROKER_CLIENT_TX_SZ(bc), &ack);
    if (rc > 0) {
        rc = MqttPacket_Write(&bc->client, bc->tx_buf, rc);
        if (rc < 0 && rc != MQTT_CODE_CONTINUE) {
            WBLOG_ERR(broker, "MqttPacket_Write failed: %d", rc);
        }
    } else {
        WBLOG_ERR(broker, "MqttEncode_ConnectAck failed: %d", rc);
    }

#ifdef WOLFMQTT_V5
    if (ack.props) {
        (void)MqttProps_Free(ack.props);
    }
    if (mc.props) {
        (void)MqttProps_Free(mc.props);
    }
    if (lwt.props) {
        (void)MqttProps_Free(lwt.props);
    }
#endif

    /* Return 0 if auth rejected so caller can disconnect */
    if (ack.return_code != MQTT_CONNECT_ACK_CODE_ACCEPTED) {
        return 0;
    }
    return rc;
}

static int BrokerHandle_Subscribe(BrokerClient* bc, int rx_len,
    MqttBroker* broker)
{
    int rc;
    int i;
    MqttSubscribe sub;
    MqttTopic topic_buf[MAX_MQTT_TOPICS];
    byte return_codes[MAX_MQTT_TOPICS];

    BROKER_INIT_MQTT_STRUCT(sub, bc);
    XMEMSET(topic_buf, 0, sizeof(topic_buf));
    sub.topics = topic_buf;

    rc = MqttDecode_Subscribe(bc->rx_buf, rx_len, &sub);
    if (rc < 0) {
        WBLOG_ERR(broker, "SUBSCRIBE decode failed rc=%d", rc);
        return rc;
    }
    WBLOG_INFO(broker, "SUBSCRIBE decoded successfully, topic_count=%d", sub.topic_count);

    /* Register subscriptions and build return codes */
    for (i = 0; i < sub.topic_count && i < MAX_MQTT_TOPICS; i++) {
        const char* f = sub.topics[i].topic_filter;
        word16 flen = 0;
        MqttQoS topic_qos = sub.topics[i].qos;
        MqttQoS granted_qos;
#ifdef WOLFMQTT_V5
        byte no_local = sub.topics[i].no_local;
        byte rap = sub.topics[i].rap;
        byte retain_handling = sub.topics[i].retain_handling;
#else
        byte no_local = 0;
        byte rap = 0;
        byte retain_handling = 0;  /* Default: send retained messages */
#endif

        /* Cap at QoS 2 */
        if (topic_qos > MQTT_QOS_2) {
            topic_qos = MQTT_QOS_2;
        }
        granted_qos = topic_qos;

        if (f && MqttDecode_Num((byte*)f - MQTT_DATA_LEN_SIZE,
                &flen, MQTT_DATA_LEN_SIZE) == MQTT_DATA_LEN_SIZE) {
            int sub_rc = BrokerSubs_Add(broker, bc, f, flen, topic_qos
#ifdef WOLFMQTT_V5
                , no_local, rap, retain_handling
#endif
                );
            int is_new_sub = (sub_rc == MQTT_CODE_CONTINUE);

            if (sub_rc != MQTT_CODE_SUCCESS && sub_rc != MQTT_CODE_CONTINUE) {
                granted_qos = (MqttQoS)MQTT_SUBSCRIBE_ACK_CODE_FAILURE;
            }
#ifdef WOLFMQTT_BROKER_RETAINED
            else if (retain_handling != 2) {
                /* Retain Handling: 0=Send retained (with retain=1), 1=Send without retain
                 * 2=Don't send retained messages (skip entirely) */
                char filter_z[BROKER_MAX_FILTER_LEN];
                word16 copy_len = flen;
                if (copy_len >= BROKER_MAX_FILTER_LEN) {
                    copy_len = BROKER_MAX_FILTER_LEN - 1;
                }
                XMEMCPY(filter_z, f, copy_len);
                filter_z[copy_len] = '\0';

                /* For retain_handling=2, only send on new subscriptions, not updates */
                if (retain_handling == 2 && !is_new_sub) {
WBLOG_DBG(broker, "SUBSCRIBE: Skip retained (existing sub, rh=2)");
                } else {
                    BrokerRetained_DeliverToClient(broker, bc, filter_z,
                        topic_qos
#ifdef WOLFMQTT_V5
                        , retain_handling, rap
#endif
                        );
                }
            }
else {
                WBLOG_DBG(broker, "SUBSCRIBE: Skip retained (rh=2)");
            }
#endif
        }
        return_codes[i] = (byte)granted_qos;
    }

    /* Debug: Log subscription details */
    for (int j = 0; j < i && j < MAX_MQTT_TOPICS; j++) {
        WBLOG_DBG(broker, "client %s subscribes to topic<%s>(qos=%d,retain=%d,total topics=%d)",
            BROKER_CLIENT_ID(bc),
            sub.topics[j].topic_filter,
            sub.topics[j].qos
#ifdef WOLFMQTT_V5
            , sub.topics[j].rap
#else
            , 0
#endif
            , i);
    }

    /* Use i (capped at MAX_MQTT_TOPICS) instead of sub.topic_count to
     * avoid reading past the end of the return_codes array */
    rc = BrokerSend_SubAck(bc, sub.packet_id, return_codes, i);

#ifdef WOLFMQTT_V5
    if (sub.props) {
        (void)MqttProps_Free(sub.props);
    }
#endif
    return rc;
}

static int BrokerHandle_Unsubscribe(BrokerClient* bc, int rx_len,
    MqttBroker* broker)
{
    int rc;
    int i;
    MqttUnsubscribe unsub;
    MqttUnsubscribeAck ack;
    MqttTopic topic_buf[MAX_MQTT_TOPICS];
#ifdef WOLFMQTT_V5
    byte reasons[MAX_MQTT_TOPICS];
#endif

    XMEMSET(&unsub, 0, sizeof(unsub));
#ifdef WOLFMQTT_V5
    unsub.protocol_level = bc->protocol_level;
#endif
    XMEMSET(topic_buf, 0, sizeof(topic_buf));
    unsub.topics = topic_buf;

    rc = MqttDecode_Unsubscribe(bc->rx_buf, rx_len, &unsub);
    if (rc < 0) {
        WBLOG_ERR(broker, "UNSUBSCRIBE decode failed rc=%d", rc);
        return rc;
    }

    /* Remove subscriptions and populate reason codes */
    for (i = 0; i < unsub.topic_count && i < MAX_MQTT_TOPICS; i++) {
        const char* f = unsub.topics[i].topic_filter;
        word16 flen = 0;
        if (f && MqttDecode_Num((byte*)f - MQTT_DATA_LEN_SIZE,
                &flen, MQTT_DATA_LEN_SIZE) == MQTT_DATA_LEN_SIZE) {
            BrokerSubs_Remove(broker, bc, f, flen);
        }
#ifdef WOLFMQTT_V5
        reasons[i] = MQTT_REASON_SUCCESS;
#endif
    }

    /* Debug: Log unsubscription details */
    for (int j = 0; j < i && j < MAX_MQTT_TOPICS; j++) {
        WBLOG_DBG(broker, "client %s unsubscribes from topic<%s>(total topics=%d)",
            BROKER_CLIENT_ID(bc),
            unsub.topics[j].topic_filter,
            i);
    }

    XMEMSET(&ack, 0, sizeof(ack));
    ack.packet_id = unsub.packet_id;
#ifdef WOLFMQTT_V5
    ack.protocol_level = bc->protocol_level;
    ack.props = NULL;
    if (bc->protocol_level >= MQTT_CONNECT_PROTOCOL_LEVEL_5) {
        ack.reason_codes = reasons;
        ack.reason_code_count = (word16)unsub.topic_count;
    }
    else {
        ack.reason_codes = NULL;
        ack.reason_code_count = 0;
    }
#endif
    rc = MqttEncode_UnsubscribeAck(bc->tx_buf,
            BROKER_CLIENT_TX_SZ(bc), &ack);
    if (rc > 0) {
        rc = MqttPacket_Write(&bc->client, bc->tx_buf, rc);
    }

#ifdef WOLFMQTT_V5
    if (unsub.props) {
        (void)MqttProps_Free(unsub.props);
    }
#endif
    return rc;
}

#ifdef WOLFMQTT_BROKER_COMMANDS
/* 前向声明 - Broker control commands function */
static int BrokerCommand_Process(MqttBroker* broker, BrokerClient* sender,
                                  const char* payload, int payload_len);
#endif

static int BrokerHandle_Publish(BrokerClient* bc, int rx_len,
    MqttBroker* broker)
{
    int rc;
    MqttPublish pub;
    MqttPublishResp resp;
    byte* payload = NULL;
    char* topic = NULL;
#ifdef WOLFMQTT_STATIC_MEMORY
    char topic_buf[BROKER_MAX_TOPIC_LEN];
#endif

    XMEMSET(&pub, 0, sizeof(pub));
#ifdef WOLFMQTT_V5
    pub.protocol_level = bc->protocol_level;
#endif
    rc = MqttDecode_Publish(bc->rx_buf, rx_len, &pub);
    if (rc < 0) {
        WBLOG_ERR(broker, "PUBLISH decode failed rc=%d", rc);
        return rc;
    }

    /* Debug: Log PUBLISH details */
    {
        char payload_str[256];
        BrokerFormatPayload(payload_str, sizeof(payload_str),
            pub.buffer, pub.buffer_len);
        WBLOG_DBG(broker, "client %s publish to topic=<%s>, len=%u, payload=%s",
            BROKER_CLIENT_ID(bc), pub.topic_name,
            (unsigned int)pub.buffer_len, payload_str);
    }
#ifdef WOLFMQTT_V5
    /* Log Response Topic and Correlation Data if present */
    if (pub.props != NULL) {
        MqttProp* prop;

        prop = BrokerProps_Find(broker, pub.props, MQTT_PROP_RESP_TOPIC);
        if (prop != NULL && prop->data_str.str != NULL) {
            WBLOG_DBG(broker, "PUBLISH has Response Topic: %s", prop->data_str.str);
        }

        prop = BrokerProps_Find(broker, pub.props, MQTT_PROP_CORRELATION_DATA);
        if (prop != NULL && prop->data_bin.data != NULL && prop->data_bin.len > 0) {
            WBLOG_DBG(broker, "PUBLISH has Correlation Data: %d bytes", prop->data_bin.len);
        }
    }
#endif

    /* 更新接收消息统计 */
    if (broker->enable_stats) {
        broker->stats.rx_msgs++;
        broker->stats.rx_bytes += rx_len;
    }

#ifdef WOLFMQTT_V5
    /* Check Maximum Packet Size (client's limit) */
    if (bc->protocol_level >= MQTT_CONNECT_PROTOCOL_LEVEL_5 &&
        bc->max_packet_size > 0 && rc > (int)bc->max_packet_size) {        
        /* Send DISCONNECT with Packet Too Large reason code */
        BrokerSend_Disconnect(bc, MQTT_REASON_PACKET_TOO_LARGE);
        return MQTT_CODE_ERROR_MALFORMED_DATA;
    }
#endif

    /* [MQTT-3.3.2-2] PUBLISH topic must not contain wildcard characters */
#ifdef WOLFMQTT_V5
    /* Handle Topic Alias (v5) */
    if (pub.props != NULL && bc->protocol_level >= MQTT_CONNECT_PROTOCOL_LEVEL_5) {
        MqttProp* alias_prop = BrokerProps_Find(broker, pub.props, MQTT_PROP_TOPIC_ALIAS);
        if (alias_prop != NULL) {
            word16 alias_id = alias_prop->data_short;

            if (pub.topic_name != NULL && pub.topic_name_len > 0) {
                /* Case: Both topic name and alias present - establish/update mapping */
                int alias_rc = BrokerTopicAlias_Set(bc, alias_id,
                    pub.topic_name, pub.topic_name_len);
                if (alias_rc != MQTT_CODE_SUCCESS) {
                    WBLOG_ERR(broker, "Failed to set topic alias %u rc=%d",
                        alias_id, alias_rc);
                    /* Continue with the topic name that was provided */
                }
            } else {
                /* Case: Only alias present - look up topic name from mapping */
                const char* alias_topic = BrokerTopicAlias_Find(bc, alias_id);
                if (alias_topic != NULL) {
                    /* Use the topic from alias mapping */
                    word16 alias_topic_len = (word16)XSTRLEN(alias_topic);
                    pub.topic_name = alias_topic;
                    pub.topic_name_len = alias_topic_len;
                } else {
                    WBLOG_ERR(broker,
                        "PUBLISH with unknown topic alias %u sock=%d",
                        alias_id, (int)bc->sock);
                    return MQTT_CODE_ERROR_BAD_ARG;
                }
            }
        }
    }
#endif

    if (pub.topic_name && pub.topic_name_len > 0) {
        word16 i;
        for (i = 0; i < pub.topic_name_len; i++) {
            if (pub.topic_name[i] == '+' || pub.topic_name[i] == '#') {
                WBLOG_ERR(broker,
                    "PUBLISH topic contains wildcard sock=%d",
                    (int)bc->sock);
                return MQTT_CODE_ERROR_BAD_ARG;
            }
        }
    }

    /* Create null-terminated topic copy for matching/logging */
    if (pub.topic_name && pub.topic_name_len > 0) {
#ifdef WOLFMQTT_STATIC_MEMORY
        word16 tlen = pub.topic_name_len;
        if (tlen >= BROKER_MAX_TOPIC_LEN) {
            tlen = BROKER_MAX_TOPIC_LEN - 1;
        }
        XMEMCPY(topic_buf, pub.topic_name, tlen);
        topic_buf[tlen] = '\0';
        topic = topic_buf;
#else
        topic = (char*)WOLFMQTT_MALLOC(pub.topic_name_len + 1);
        if (topic != NULL) {
            XMEMCPY(topic, pub.topic_name, pub.topic_name_len);
            topic[pub.topic_name_len] = '\0';
        }
#endif
    }
    /* Use payload pointer directly from decoded packet — rx_buf is not
     * modified during fan-out (each subscriber encodes into their own
     * tx_buf), so this pointer remains valid throughout. */
    payload = pub.buffer;

#ifdef WOLFMQTT_BROKER_RETAINED
    /* Handle retained messages */
    if (topic != NULL && pub.retain) {
        if (pub.total_len == 0) {
            BrokerRetained_Delete(broker, topic);
        }
        else if (payload != NULL) {
            word32 expiry = 0;
#ifdef WOLFMQTT_V5
            if (pub.props != NULL) {
                MqttProp* prop = BrokerProps_Find(broker, pub.props,
                    MQTT_PROP_MSG_EXPIRY_INTERVAL);
                if (prop != NULL) {
                    expiry = prop->data_int;
                }
            }
#endif
            {
                int ret_rc = BrokerRetained_Store(broker, topic, payload,
                    pub.total_len, pub.qos, expiry);
                if (ret_rc != MQTT_CODE_SUCCESS) {
                    WBLOG_ERR(broker, "Retained store failed: %s",
                        MqttClient_ReturnCodeToString(ret_rc));
                }
            }
        }
    }
#endif /* WOLFMQTT_BROKER_RETAINED */

#ifdef WOLFMQTT_BROKER_COMMANDS
    /* 检查是否为控制命令 */
    if (topic != NULL && XSTRCMP(topic, "$sys/broker/commands") == 0) {
        int cmd_rc = BrokerCommand_Process(broker, bc,
            (payload != NULL) ? (char*)payload : "", pub.total_len);
        if (cmd_rc != MQTT_CODE_SUCCESS) {
            WBLOG_ERR(broker, "command processing failed rc=%d", cmd_rc);
        }
        if (payload != NULL) WOLFMQTT_FREE(payload);
        if (topic != NULL) WOLFMQTT_FREE(topic);
        return cmd_rc;
    }
#endif

    if (topic != NULL && (payload != NULL || pub.total_len == 0)) {
        /* Fan out to matching subscribers */
#ifdef WOLFMQTT_V5
        /* 共享订阅：跟踪已发送的组 (Shared Subscription: Track sent groups) */
        typedef struct {
            const char* group_name;
            byte sent;
        } SentGroup;

        SentGroup sent_groups[16];  /* 单次消息最多跟踪 16 个共享组 */
        byte sent_count = 0;
#endif

#ifdef WOLFMQTT_STATIC_MEMORY
        {
            int i;
            for (i = 0; i < broker->max_subs; i++) {
                BrokerSub* sub = &broker->subs[i];
                if (!sub->in_use) continue;
#else
        {
            BrokerSub* sub = broker->subs;
            while (sub) {
                BrokerSub* next_sub = sub->next;  /* 提前保存下一个，防止 continue 导致无限循环 */
#endif
                if (sub->client != NULL &&
                    sub->client->protocol_level != 0 &&
                    BROKER_STR_VALID(sub->filter) &&
                    BrokerTopicMatch(sub->filter, topic)) {
#ifdef WOLFMQTT_V5
                    /* 共享订阅检查 (Shared Subscription Check) */
                    if (sub->is_shared) {
                        /* 检查该组是否已发送 */
                        byte already_sent = 0;

                        for (byte i = 0; i < sent_count; i++) {
                            if (sent_groups[i].group_name != NULL &&
                                sub->share_group != NULL &&
                                XSTRCMP(sent_groups[i].group_name, sub->share_group) == 0) {
                                already_sent = 1;
                                break;
                            }
                        }

                        if (already_sent) {
                            /* 该组已发送，跳过 */
#ifndef WOLFMQTT_STATIC_MEMORY
                            sub = next_sub;
#endif
                            continue;
                        }

                        /* 轮询选择：选择 rr_index 最小的订阅者 */
                        BrokerSub* selected = sub;
                        byte min_index = sub->rr_index;

                        /* 遍历该组的所有订阅者，找到 rr_index 最小的 */
#ifdef WOLFMQTT_STATIC_MEMORY
                        for (int j = i + 1; j < broker->max_subs; j++) {
                            BrokerSub* other = &broker->subs[j];
                            if (other->in_use && other->client != NULL &&
                                other->is_shared && other->share_group != NULL &&
                                XSTRCMP(other->share_group, sub->share_group) == 0 &&
                                BrokerTopicMatch(other->filter, topic) &&
                                other->rr_index < min_index) {
                                min_index = other->rr_index;
                                selected = other;
                            }
                        }
#else
                        BrokerSub* other = sub->next;
                        while (other) {
                            if (other->client != NULL &&
                                other->is_shared && other->share_group != NULL &&
                                XSTRCMP(other->share_group, sub->share_group) == 0 &&
                                BrokerTopicMatch(other->filter, topic) &&
                                other->rr_index < min_index) {
                                min_index = other->rr_index;
                                selected = other;
                            }
                            other = other->next;
                        }
#endif

                        /* 标记该组已发送 */
                        if (sent_count < 16) {
                            sent_groups[sent_count].group_name = selected->share_group;
                            sent_groups[sent_count].sent = 1;
                            sent_count++;
                        }

                        /* 使用选中的订阅者，并立即递增其 rr_index */
                        sub = selected;
                        if (sub->rr_index < 255) {
                            sub->rr_index++;
                        }
                    }

                    /* No Local: Don't receive own messages */
                    if (sub->no_local && sub->client == bc) {
                        continue;
                    }
#endif
                    MqttPublish out_pub;
                    MqttQoS eff_qos;
                    XMEMSET(&out_pub, 0, sizeof(out_pub));
                    out_pub.topic_name = topic;
                    eff_qos = (pub.qos < sub->qos) ? pub.qos : sub->qos;
                    out_pub.qos = eff_qos;
#ifdef WOLFMQTT_V5
                    /* Check receiveMaximum flow control for QoS 1/2 */
                    if (eff_qos >= MQTT_QOS_1 &&
                        sub->client->protocol_level >= MQTT_CONNECT_PROTOCOL_LEVEL_5) {
                        if (sub->client->inflight_count >= sub->client->receive_maximum) {                        
                            continue;
                        }
                    }
#endif
                    if (eff_qos >= MQTT_QOS_1) {
                        out_pub.packet_id = BrokerNextPacketId(broker);
                    }
                    out_pub.retain = 0;
                    out_pub.duplicate = 0;
                    out_pub.buffer = payload;
                    out_pub.total_len = pub.total_len;
#ifdef WOLFMQTT_V5
                    out_pub.protocol_level = sub->client->protocol_level;
                    if (sub->client->protocol_level >=
                        MQTT_CONNECT_PROTOCOL_LEVEL_5) {
                        /* Forward all properties except TopicAlias (use deep copy) */
                        if (pub.props != NULL) {
                            MqttProp* prop = pub.props;
                            int prop_count = 0;
                            /* Guard against corrupted property list */
                            while (prop != NULL && prop_count++ < 100) {
                                /* Skip TopicAlias - broker uses full topic name */
                                if (prop->type != MQTT_PROP_TOPIC_ALIAS) {
                                    MqttProp* new_prop = MqttProps_Add(&out_pub.props);
                                    if (new_prop != NULL) {
                                        int copy_rc = BrokerProp_Copy(new_prop, prop);
                                        if (copy_rc != MQTT_CODE_SUCCESS) {                                            
                                            /* On failure, mark property as unused to skip it */
                                            new_prop->type = MQTT_PROP_NONE;
                                        }
                                    }
                                }
                                prop = prop->next;
                            }
                            if (prop_count >= 100) {
                                WBLOG_ERR(broker, "PUBLISH property list corrupted (circular or too many props)");
                            }
                        }
                    }
#endif
                    rc = MqttEncode_Publish(sub->client->tx_buf,
                            BROKER_CLIENT_TX_SZ(sub->client), &out_pub, 0);
                    if (rc > 0) {
#ifdef WOLFMQTT_V5
                        word16 old_inflight = sub->client->inflight_count;
#endif
                        (void)MqttPacket_Write(&sub->client->client,
                            sub->client->tx_buf, rc);
                        /* 更新发送消息统计 */
                        if (broker->enable_stats) {
                            broker->stats.tx_msgs++;
                            broker->stats.tx_bytes += rc;
                        }
#ifdef WOLFMQTT_V5
                        /* Increment inflight counter for QoS 1/2 messages */
                        if (eff_qos >= MQTT_QOS_1 &&
                            sub->client->protocol_level >= MQTT_CONNECT_PROTOCOL_LEVEL_5) {
                            sub->client->inflight_count++;                           
                        }

                        /* 注意：共享订阅的 rr_index 递增已在轮询选择时完成，这里不再重复递增 */
#endif
                    }
#ifdef WOLFMQTT_V5
                    /* Clean up properties copied for forwarding */
                    if (out_pub.props != NULL) {
                        (void)MqttProps_Free(out_pub.props);
                        out_pub.props = NULL;
                    }
#endif
                }
#ifndef WOLFMQTT_STATIC_MEMORY
                sub = next_sub;
#endif
            }
        }
    }

    if (pub.qos == MQTT_QOS_1 || pub.qos == MQTT_QOS_2) {
        BROKER_INIT_MQTT_STRUCT(resp, bc);
        resp.packet_id = pub.packet_id;
#ifdef WOLFMQTT_V5
        resp.reason_code = MQTT_REASON_SUCCESS;
        resp.props = NULL;
#endif
        rc = MqttEncode_PublishResp(bc->tx_buf, BROKER_CLIENT_TX_SZ(bc),
                (pub.qos == MQTT_QOS_1) ? MQTT_PACKET_TYPE_PUBLISH_ACK :
                MQTT_PACKET_TYPE_PUBLISH_REC, &resp);
        if (rc > 0) {           
            rc = MqttPacket_Write(&bc->client, bc->tx_buf, rc);
        }
    }

#ifdef WOLFMQTT_V5
    if (pub.props) {
        (void)MqttProps_Free(pub.props);
    }
#endif
#ifndef WOLFMQTT_STATIC_MEMORY
    if (topic) {
        WOLFMQTT_FREE(topic);
    }
#endif

    return rc;
}

/* Unified handler for QoS 2 handshake responses (PUBREL->PUBCOMP, PUBREC->PUBREL) */
static int BrokerHandle_QoS2_Response(BrokerClient* bc, int rx_len,
                                       byte recv_type, byte send_type,
                                       const char* log_name)
{
    int rc;
    MqttPublishResp resp;

    BROKER_INIT_MQTT_STRUCT(resp, bc);
    WBLOG_DBG(bc->broker, "%s recv sock=%d len=%d", log_name, (int)bc->sock, rx_len);
    
    rc = MqttDecode_PublishResp(bc->rx_buf, rx_len, recv_type, &resp);
    if (rc < 0) {
        WBLOG_ERR(bc->broker, "%s decode failed rc=%d", log_name, rc);
        return rc;
    }

#ifdef WOLFMQTT_V5
    resp.reason_code = MQTT_REASON_SUCCESS;
    resp.props = NULL;
#endif
    
    rc = MqttEncode_PublishResp(bc->tx_buf, BROKER_CLIENT_TX_SZ(bc),
            send_type, &resp);
    if (rc > 0) {
        const char* send_log = (send_type == MQTT_PACKET_TYPE_PUBLISH_COMP) ? 
                               "PUBCOMP" : "PUBREL";
        WBLOG_DBG(bc->broker, "%s send sock=%d packet_id=%u",
            send_log, (int)bc->sock, resp.packet_id);
        rc = MqttPacket_Write(&bc->client, bc->tx_buf, rc);
    }
#ifdef WOLFMQTT_V5
    if (resp.props) {
        (void)MqttProps_Free(resp.props);
    }
#endif
    return rc;
}

/* Handle PUBREL from publisher: broker responds with PUBCOMP */
static int BrokerHandle_PublishRel(BrokerClient* bc, int rx_len)
{
    return BrokerHandle_QoS2_Response(bc, rx_len,
        MQTT_PACKET_TYPE_PUBLISH_REL,
        MQTT_PACKET_TYPE_PUBLISH_COMP,
        "PUBLISH_REL");
}

/* Handle PUBREC from subscriber: broker sent QoS 2 PUBLISH, subscriber
 * responds with PUBREC, broker sends PUBREL */
static int BrokerHandle_PublishRec(BrokerClient* bc, int rx_len)
{
    return BrokerHandle_QoS2_Response(bc, rx_len,
        MQTT_PACKET_TYPE_PUBLISH_REC,
        MQTT_PACKET_TYPE_PUBLISH_REL,
        "PUBLISH_REC");
}

/* -------------------------------------------------------------------------- */
/* Per-client processing (called from Step)                                    */
/* -------------------------------------------------------------------------- */
static int BrokerClient_Process(MqttBroker* broker, BrokerClient* bc)
{
    int rc;
    int activity = 0;

    /* Execute transport layer handshake if needed */
    if (!BrokerTransport_IsHandshakeDone(bc)) {
        rc = BrokerTransport_Handshake(bc, broker);
        if (rc == MQTT_CODE_CONTINUE) {
            return 0; /* Handshake in progress */
        }
        if (rc != MQTT_CODE_SUCCESS) {
            /* Handshake failed, disconnect */
            WBLOG_ERR(broker, "Transport handshake failed sock=%d transport=%s rc=%d",
                     (int)bc->sock, BrokerTransport_GetName(bc), rc);
            BrokerTransport_Close(bc, broker);
            BrokerTransport_Cleanup(bc);
            BrokerClient_Remove(broker, bc, -1);
            return 0;
        }
        WBLOG_INFO(broker, "Transport handshake completed sock=%d transport=%s",
                  (int)bc->sock, BrokerTransport_GetName(bc));
        return 0; /* No activity - let main loop sleep */
    }

#ifdef ENABLE_MQTT_TLS
    /* Complete TLS handshake before processing MQTT packets */
    if (!bc->tls_handshake_done) {
        int ret;
        bc->client.tls.timeout_ms_read = broker->timeout_ms;
        bc->client.tls.timeout_ms_write = broker->timeout_ms;
        ret = wolfSSL_accept(bc->client.tls.ssl);
        if (ret == WOLFSSL_SUCCESS) {
            bc->tls_handshake_done = 1;
            WBLOG_DBG(broker, "TLS handshake done %s",
                wolfSSL_get_version(bc->client.tls.ssl));
            /* Log client certificate CN if mutual TLS.
             * Requires wolfSSL built with KEEP_PEER_CERT or similar. */
        #if defined(KEEP_PEER_CERT) || defined(OPENSSL_EXTRA) || \
            defined(OPENSSL_EXTRA_X509_SMALL) || defined(SESSION_CERTS)
            if (broker->tls_ca != NULL) {
                WOLFSSL_X509* peer = wolfSSL_get_peer_certificate(
                    bc->client.tls.ssl);
                if (peer != NULL) {
                    char* cn = wolfSSL_X509_get_subjectCN(peer);
                    WBLOG_DBG(broker, "TLS client cert CN=%s",
                        cn ? cn : "(unknown)");
                    wolfSSL_X509_free(peer);
                }
            }
        #endif
            return 1; /* activity */
        }
        else {
            int err = wolfSSL_get_error(bc->client.tls.ssl, ret);
            if (err == WOLFSSL_ERROR_WANT_READ ||
                err == WOLFSSL_ERROR_WANT_WRITE) {
                return 0; /* handshake in progress */
            }
            WBLOG_ERR(broker, "TLS handshake failed sock=%d err=%d",
                (int)bc->sock, err);
            BrokerSubs_RemoveClient(broker, bc);
            BrokerClient_Remove(broker, bc, -1);
            return 0;
        }
    }
#endif /* ENABLE_MQTT_TLS */

    /* Try non-blocking read (timeout=0) */
    rc = MqttPacket_Read(&bc->client, bc->rx_buf, BROKER_CLIENT_RX_SZ(bc), 0);

    if (rc == MQTT_CODE_ERROR_TIMEOUT || rc == MQTT_CODE_CONTINUE) {
        /* No data available - not an error, silently continue */
        rc = 0;
    }
    else if (rc < 0) {
        WBLOG_ERR(broker, "read failed sock=%d rc=%d", (int)bc->sock, rc);
        BrokerClient_PublishWill(broker, bc); /* abnormal disconnect */
        /* Session persistence: keep subs if clean_session=0 */
        if (bc->clean_session) {
            BrokerSubs_RemoveClient(broker, bc);
        }
        else {
            BrokerSubs_OrphanClient(broker, bc);
        }
        BrokerClient_Remove(broker, bc, -1);
        return 0;
    }

    if (rc > 0) {
        byte type = MQTT_PACKET_TYPE_GET(bc->rx_buf[0]);
        bc->last_rx = WOLFMQTT_BROKER_GET_TIME_S();
        activity = 1;
        /* [MQTT-3.1.0-1] First packet must be CONNECT */
        if (type != MQTT_PACKET_TYPE_CONNECT && !bc->connected) {
            WBLOG_ERR(broker,
                "packet type %u before CONNECT sock=%d",
                type, (int)bc->sock);
            BrokerSubs_RemoveClient(broker, bc);
            BrokerClient_Remove(broker, bc, -1);
            return 0;
        }
        /* [MQTT-3.1.0-2] Second CONNECT is a protocol violation */
        if (type == MQTT_PACKET_TYPE_CONNECT && bc->connected) {
            WBLOG_ERR(broker,
                "second CONNECT on sock=%d [MQTT-3.1.0-2]",
                (int)bc->sock);
            BrokerSubs_RemoveClient(broker, bc);
            BrokerClient_Remove(broker, bc, -1);
            return 0;
        }
        switch (type) {
            case MQTT_PACKET_TYPE_CONNECT:
            {
                int c_rc = BrokerHandle_Connect(bc, rc, broker);
                if (c_rc < 0 && c_rc != MQTT_CODE_CONTINUE) {
                    /* Decode failed or auth rejected, disconnect */
                    WBLOG_ERR(broker, "CONNECT handling failed: %d", c_rc);
                    BrokerSubs_RemoveClient(broker, bc);
                    BrokerClient_Remove(broker, bc, -1);
                    return 0;
                }
                if (c_rc == MQTT_CODE_CONTINUE) {
                    /* Write in progress, will complete on next iteration */
                    return 0;  /* Don't mark as connected yet */
                }
                bc->connected = 1;
                /* 更新连接统计 */
                if (broker->enable_stats) {
                    broker->stats.conns++;
                }
                /* 触发 on_connect 回调 */
                if (broker->on_connect) {
                    broker->on_connect(broker, bc->sock,
                        BROKER_STR_VALID(bc->client_id) ? bc->client_id : "",
                        bc->client_ip);
                }
                /* Count and log connected clients */
                {
                    int count = 0;
#ifdef WOLFMQTT_STATIC_MEMORY
                    int i;
                    for (i = 0; i < broker->max_clients; i++) {
                        if (broker->clients[i].in_use && broker->clients[i].connected) {
                            count++;
                        }
                    }
#else
                    BrokerClient* tmp = broker->clients;
                    while (tmp != NULL) {
                        if (tmp->connected) {
                            count++;
                        }
                        tmp = tmp->next;
                    }
#endif
                    WBLOG_INFO(broker, "%s client %s is connected, ip=%s proto=%u clean=%d will=%d total_clients=%d",
#ifdef ENABLE_MQTT_WEBSOCKET
                        bc->is_websocket ? "WebSocket" : "TCP",
#else
                        "TCP",
#endif
                        BROKER_CLIENT_ID(bc), bc->client_ip,
                        bc->protocol_level, bc->clean_session, bc->will_topic != NULL ? 1 : 0,
                        count);
                }
                break;
            }
            case MQTT_PACKET_TYPE_PUBLISH:
                (void)BrokerHandle_Publish(bc, rc, broker);
                break;
            case MQTT_PACKET_TYPE_PUBLISH_ACK: {
                /* QoS 1 ack from subscriber - delivery complete */
                MqttPublishResp resp;
                int decode_rc;
                BROKER_INIT_MQTT_STRUCT(resp, bc);                
                decode_rc = MqttDecode_PublishResp(bc->rx_buf, rc,
                        MQTT_PACKET_TYPE_PUBLISH_ACK, &resp);
                if (decode_rc < 0) {
                    WBLOG_ERR(bc->broker, "PUBACK decode failed rc=%d", decode_rc);
                    break;
                }
#ifdef WOLFMQTT_V5
                if (bc->protocol_level >= MQTT_CONNECT_PROTOCOL_LEVEL_5 &&
                    bc->inflight_count > 0) {
                    bc->inflight_count--;
                }
#endif
                break;
            }
            case MQTT_PACKET_TYPE_PUBLISH_REC:
                /* QoS 2 step 2: subscriber sends PUBREC, broker
                 * responds with PUBREL */
                (void)BrokerHandle_PublishRec(bc, rc);
                break;
            case MQTT_PACKET_TYPE_PUBLISH_REL:
                /* QoS 2 step 3: publisher sends PUBREL, broker
                 * responds with PUBCOMP */
                (void)BrokerHandle_PublishRel(bc, rc);
                break;
            case MQTT_PACKET_TYPE_PUBLISH_COMP: {
                /* QoS 2 step 4: subscriber sends PUBCOMP - delivery complete */
                MqttPublishResp resp;
                int decode_rc;
                BROKER_INIT_MQTT_STRUCT(resp, bc);                
                decode_rc = MqttDecode_PublishResp(bc->rx_buf, rc,
                        MQTT_PACKET_TYPE_PUBLISH_COMP, &resp);
                if (decode_rc < 0) {                    
                    break;
                }                
#ifdef WOLFMQTT_V5
                if (bc->protocol_level >= MQTT_CONNECT_PROTOCOL_LEVEL_5 &&
                    bc->inflight_count > 0) {
                    bc->inflight_count--;                    
                }
#endif
                break;
            }
            case MQTT_PACKET_TYPE_SUBSCRIBE:
                (void)BrokerHandle_Subscribe(bc, rc, broker);
                break;
            case MQTT_PACKET_TYPE_UNSUBSCRIBE:
                (void)BrokerHandle_Unsubscribe(bc, rc, broker);
                break;
            case MQTT_PACKET_TYPE_PING_REQ:
                (void)BrokerSend_PingResp(bc);
                break;
            case MQTT_PACKET_TYPE_DISCONNECT: {
#ifdef WOLFMQTT_V5
                /* Parse DISCONNECT packet to get reason code (MQTT 5.0) */
                MqttDisconnect disc;
                int disc_rc;

                XMEMSET(&disc, 0, sizeof(disc));
                disc.protocol_level = bc->protocol_level;
                disc_rc = MqttDecode_Disconnect(bc->rx_buf, rc, &disc);

                if (disc_rc >= 0) {
                    /* Extract Session Expiry Interval from DISCONNECT properties (v5) */
                    if (disc.props != NULL) {
                        MqttProp* prop = BrokerProps_Find(broker, disc.props,
                            MQTT_PROP_SESSION_EXPIRY_INTERVAL);
                        if (prop != NULL) {
                            /* Update client's session expiry interval */
                            bc->session_expiry_interval = prop->data_int;
                        }
                    }

                    /* Check if client wants will message to be sent */
                    if (disc.reason_code == MQTT_REASON_NORMAL_DISCONNECTION) {
                        /* Normal disconnection (0x00): clear will */                        
                        BrokerClient_ClearWill(bc);
                    } else {
                        /* Non-zero reason code: send will message if present */                        
                        BrokerClient_PublishWill(broker, bc);
                    }
                } else {
                    /* Parse failed: assume normal disconnection for safety */                    
                    BrokerClient_ClearWill(bc);
                }
#else
                /* MQTT 3.1.1: DISCONNECT always means normal disconnect */
                BrokerClient_ClearWill(bc); /* normal disconnect */
#endif
                /* Session persistence: keep subs if clean_session=0 */
                if (bc->clean_session) {
                    BrokerSubs_RemoveClient(broker, bc);
                }
                else {
                    BrokerSubs_OrphanClient(broker, bc);
                }
                BrokerClient_Remove(broker, bc, 0); /* 正常断开 */
                return 0;
            }
            default:
                break;
        }
        /* WebSocket cleanup is handled by BrokerTransport */
    }

    /* Check keepalive timeout (MQTT spec 3.1.2.10: 1.5x keep alive) */
    if (bc->keep_alive_sec > 0) {
        WOLFMQTT_BROKER_TIME_T now = WOLFMQTT_BROKER_GET_TIME_S();
        if ((now - bc->last_rx) >
            (WOLFMQTT_BROKER_TIME_T)(bc->keep_alive_sec * 3 / 2)) {            
        #ifdef WOLFMQTT_V5
            BrokerSend_Disconnect(bc, MQTT_REASON_KEEP_ALIVE_TIMEOUT);
        #endif
            BrokerClient_PublishWill(broker, bc); /* abnormal disconnect */
            /* Session persistence: keep subs if clean_session=0 */
            if (bc->clean_session) {
                BrokerSubs_RemoveClient(broker, bc);
            }
            else {
                BrokerSubs_OrphanClient(broker, bc);
            }
            BrokerClient_Remove(broker, bc, -1);
            return 0;
        }
    }

    return activity;
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */
int MqttBroker_Init(MqttBroker* broker)
{
    int rc;
    MqttBrokerNet net;

    if (broker == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

#if !defined(WOLFMQTT_WOLFIP) && !defined(WOLFMQTT_BROKER_CUSTOM_NET)
    /* 初始化默认 POSIX 网络层 */
    rc = MqttBrokerNet_Init(&net);
    if (rc != MQTT_CODE_SUCCESS) {
        return rc;
    }
#else
    /* 自定义网络模式下，不支持自动初始化 */
    return MQTT_CODE_ERROR_BAD_ARG;
#endif

    /* 委托给 InitEx */
    return MqttBroker_InitEx(broker, &net);
}

int MqttBroker_InitEx(MqttBroker* broker, MqttBrokerNet* net)
{
    if (broker == NULL || net == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    XMEMSET(broker, 0, sizeof(*broker));
    XMEMCPY(&broker->net, net, sizeof(MqttBrokerNet));
    broker->listen_sock = BROKER_SOCKET_INVALID;
    broker->port = MQTT_DEFAULT_PORT;
#ifdef ENABLE_MQTT_TLS
    broker->listen_sock_tls = BROKER_SOCKET_INVALID;
    broker->port_tls = MQTT_SECURE_PORT;
#endif
#ifdef ENABLE_MQTT_WEBSOCKET
    broker->enable_ws = 1;         /* Enable WebSocket on unified HTTP port by default */
#endif
    broker->api_ctx = NULL;          /* API context allocated on start */
    broker->enable_http = 1;         /* Enable HTTP server by default */
    broker->http_port = MQTT_WS_PORT; /* Default unified HTTP/WebSocket port: 8080 */
    
    /* Set default HTTP Basic authentication credentials */
    XSTRNCPY(broker->http_username, "admin", sizeof(broker->http_username) - 1);
    broker->http_username[sizeof(broker->http_username) - 1] = '\0';
    XSTRNCPY(broker->http_password, "22182666", sizeof(broker->http_password) - 1);
    broker->http_password[sizeof(broker->http_password) - 1] = '\0';
    
    broker->running = 0;
    broker->log_level = LOG_LEVEL_INFO; /* Default to INFO level */
    broker->log = Log_DefaultCallback;  /* Set default log callback */
    broker->next_packet_id = 1;

    /* 初始化缓冲区大小默认值 */
    broker->rx_buf_sz = BROKER_RX_BUF_SZ;
    broker->tx_buf_sz = BROKER_TX_BUF_SZ;
    broker->timeout_ms = BROKER_TIMEOUT_MS;
    broker->listen_backlog = BROKER_LISTEN_BACKLOG;

    /* 初始化容量限制默认值 */
    broker->max_clients = BROKER_MAX_CLIENTS;
    broker->max_subs = BROKER_MAX_SUBS;
    broker->max_retained = BROKER_MAX_RETAINED;
    broker->max_pending_wills = BROKER_MAX_PENDING_WILLS;

    /* 初始化字符串长度限制默认值 */
    broker->max_client_id_len = BROKER_MAX_CLIENT_ID_LEN;
    broker->max_username_len = BROKER_MAX_USERNAME_LEN;
    broker->max_password_len = BROKER_MAX_PASSWORD_LEN;
    broker->max_filter_len = BROKER_MAX_FILTER_LEN;
    broker->max_topic_len = BROKER_MAX_TOPIC_LEN;
    broker->max_payload_len = BROKER_MAX_PAYLOAD_LEN;
    broker->max_will_payload_len = BROKER_MAX_WILL_PAYLOAD_LEN;

#ifdef WOLFMQTT_V5
    /* MQTT 5 默认值 */
    broker->max_packet_size = 0; /* no limit */
    broker->topic_alias_max = BROKER_MAX_TOPIC_ALIASES;
#endif

    /* 会话持久化默认值 */
    broker->default_session_expiry_interval = 180; /* 3 minutes */

    /* 统计设置 */
    broker->stats_interval = 20; /* 20 seconds */
    broker->enable_stats = 1;

    /* 静态文件目录默认值 */
    broker->static_dir = NULL;  /* NULL means use "./www" as default */

    /* 初始化统计数据 */
    broker->stats.start = WOLFMQTT_BROKER_GET_TIME_S();
    /* 设置 last_stats_time 为启动时间减去间隔，确保第一次立即发送 */
    broker->last_stats_time = broker->stats.start - broker->stats_interval;

#ifdef WOLFMQTT_BROKER_EPOLL
    /* Initialize epoll */
    broker->epoll_fd = -1;
    broker->epoll_events = NULL;
    broker->epoll_max_events = BROKER_EPOLL_MAX_EVENTS_DEFAULT;  /* Default value, can be overridden */
#endif

#if !defined(WOLFMQTT_WOLFIP) && !defined(WOLFMQTT_BROKER_CUSTOM_NET)
    /* For the default POSIX backend, the net callbacks expect ctx to be a
     * MqttBroker* for logging via WBLOG_*. If no context was provided,
     * default to using this broker instance to avoid NULL-dereference. */
    if (broker->net.ctx == NULL) {
        broker->net.ctx = broker;
    }
#endif

    return MQTT_CODE_SUCCESS;
}

/* 获取broker统计数据 */
int MqttBroker_GetStats(MqttBroker* broker, BrokerStats* stats)
{
    if (broker == NULL || stats == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    XMEMCPY(stats, &broker->stats, sizeof(BrokerStats));
    return MQTT_CODE_SUCCESS;
}

/* 重置broker统计数据（保留启动时间） */
int MqttBroker_ResetStats(MqttBroker* broker)
{
    if (broker == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    /* 保留启动时间，重置其他统计 */
    WOLFMQTT_BROKER_TIME_T start = broker->stats.start;
    XMEMSET(&broker->stats, 0, sizeof(BrokerStats));
    broker->stats.start = start;
    return MQTT_CODE_SUCCESS;
}

#ifdef WOLFMQTT_BROKER_EPOLL
/* Set epoll max events (must be called before MqttBroker_Start) */
int MqttBroker_SetEpollMaxEvents(MqttBroker* broker, int max_events)
{
    if (broker == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* Validate max_events range */
    if (max_events < 1 || max_events > 4096) {
        WBLOG_ERR(broker, "epoll max_events out of range (1-4096): %d", max_events);
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* Check if broker is already running */
    if (broker->running || broker->epoll_fd >= 0) {
        WBLOG_ERR(broker, "cannot set epoll max_events after broker started");
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    broker->epoll_max_events = max_events;
    WBLOG_INFO(broker, "epoll max_events set to %d", max_events);
    return MQTT_CODE_SUCCESS;
}
#endif

#ifdef WOLFMQTT_BROKER_COMMANDS
/* -------------------------------------------------------------------------- */
/* Broker Control Commands (Broker控制命令)                                    */
/* -------------------------------------------------------------------------- */

/* 解析命令字符串 */
static int BrokerCommand_Parse(const char* payload, int payload_len,
                                 char** cmd_out, char** arg_out)
{
    char* payload_copy = NULL;
    char* cmd = NULL;
    char* arg = NULL;
    char* space;
    int rc = MQTT_CODE_SUCCESS;

    if (payload == NULL || payload_len <= 0 || cmd_out == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    /* 复制 payload 以便修改 */
    payload_copy = (char*)WOLFMQTT_MALLOC(payload_len + 1);
    if (payload_copy == NULL) {
        return MQTT_CODE_ERROR_MEMORY;
    }
    XMEMCPY(payload_copy, payload, payload_len);
    payload_copy[payload_len] = '\0';

    /* 跳过前导空白 */
    cmd = payload_copy;
    while (*cmd == ' ' || *cmd == '\t' || *cmd == '\r' || *cmd == '\n') {
        cmd++;
    }

    /* 查找空格分隔命令和参数 */
    space = XSTRCHR(cmd, ' ');
    if (space != NULL) {
        *space = '\0';  /* 终止命令 */
        arg = space + 1;
        /* 跳过参数前导空白 */
        while (*arg == ' ' || *arg == '\t') {
            arg++;
        }
        /* 处理引号包围的参数 */
        if (*arg == '"' || *arg == '\'') {
            char quote = *arg;
            arg++;
            char* end = XSTRCHR(arg, quote);
            if (end != NULL) {
                *end = '\0';
            }
        }
        /* 如果参数为空，设置为 NULL */
        if (*arg == '\0') {
            arg = NULL;
        }
    }

    *cmd_out = cmd;
    if (arg_out != NULL) {
        *arg_out = arg;
    }

    return rc;
}

/* 断开所有客户端 */
static int BrokerCommand_DisconnectAll(MqttBroker* broker)
{
    int disconnected = 0;

    if (broker == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
 

#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        for (i = 0; i < broker->max_clients; i++) {
            BrokerClient* bc = &broker->clients[i];
            if (!bc->in_use) continue;

            /* 发送 DISCONNECT 数据包 */
            bc->tx_buf[0] = MQTT_PACKET_TYPE_SET(MQTT_PACKET_TYPE_DISCONNECT);
            bc->tx_buf[1] = 0;
            (void)MqttPacket_Write(&bc->client, bc->tx_buf, 2);

            /* 移除客户端 */
            BrokerClient_Remove(broker, bc, -1);
            disconnected++;
        }
    }
#else
    {
        BrokerClient* bc = broker->clients;
        while (bc) {
            BrokerClient* next = bc->next;
            if (bc->connected) {
                /* 发送 DISCONNECT 数据包 */
                bc->tx_buf[0] = MQTT_PACKET_TYPE_SET(MQTT_PACKET_TYPE_DISCONNECT);
                bc->tx_buf[1] = 0;
                (void)MqttPacket_Write(&bc->client, bc->tx_buf, 2);

                /* 移除客户端 */
                BrokerClient_Remove(broker, bc, -1);
                disconnected++;
            }
            bc = next;
        }
    }
#endif

    WBLOG_INFO(broker, "disconnected %d clients", disconnected);
    return MQTT_CODE_SUCCESS;
}

/* 处理 reset 命令 */
static int BrokerCommand_HandleReset(MqttBroker* broker,
                                       BrokerCommandResponse* resp)
{
    int rc;

    if (broker == NULL || resp == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
 

    /* 1. 断开所有客户端 */
    rc = BrokerCommand_DisconnectAll(broker);
    if (rc != MQTT_CODE_SUCCESS) {
        resp->code = BROKER_CMD_ERROR_FAILED;
        resp->message = "Failed to disconnect clients";
        return rc;
    }

#ifdef WOLFMQTT_BROKER_RETAINED
    /* 2. 清空保留消息 */
    BrokerRetained_FreeAll(broker);
#endif

#ifdef WOLFMQTT_BROKER_WILL
    /* 3. 清空待处理遗嘱 */
    BrokerPendingWill_FreeAll(broker);
#endif

    /* 4. 清空所有订阅（已在 BrokerClient_Remove 中处理） */

    /* 5. 重置统计数据 */
    MqttBroker_ResetStats(broker);

    resp->code = BROKER_CMD_SUCCESS;
    resp->message = "Broker reset successfully";
    return MQTT_CODE_SUCCESS;
}

/* 处理 kick 命令 */
static int BrokerCommand_HandleKick(MqttBroker* broker, const char* client_id,
                                      BrokerCommandResponse* resp)
{
    BrokerClient* target = NULL;

    if (broker == NULL || resp == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    /* 无参数：踢掉所有客户端 */
    if (client_id == NULL || *client_id == '\0') {        
        resp->code = BROKER_CMD_SUCCESS;
        resp->message = "All clients disconnected";
        return BrokerCommand_DisconnectAll(broker);
    }

    /* 有参数：踢掉指定客户端 */
    target = BrokerClient_FindByClientId(broker, client_id, NULL);
#ifdef WOLFMQTT_STATIC_MEMORY
    if (target == NULL || !target->in_use) {
#else
    if (target == NULL || !target->connected) {
#endif
        resp->code = BROKER_CMD_ERROR_NOT_FOUND;
        resp->message = "Client not found";
        resp->detail = client_id;
        return MQTT_CODE_ERROR_NOT_FOUND;
    }

    /* 发送 DISCONNECT 数据包 */
    target->tx_buf[0] = MQTT_PACKET_TYPE_SET(MQTT_PACKET_TYPE_DISCONNECT);
    target->tx_buf[1] = 0;
    (void)MqttPacket_Write(&target->client, target->tx_buf, 2);

    /* 移除客户端 */
    BrokerClient_Remove(broker, target, -1);

    resp->code = BROKER_CMD_SUCCESS;
    resp->message = "Client disconnected";
    return MQTT_CODE_SUCCESS;
}

/* 发送命令响应 */
static int BrokerCommand_SendResponse(MqttBroker* broker, BrokerClient* sender,
                                        const BrokerCommandResponse* resp)
{
    char json_buf[512];
    int json_len;
    char response_topic[256];
    const char* result_str;
    int rc;

    if (broker == NULL || resp == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    /* 确定结果字符串 */
    switch (resp->code) {
        case BROKER_CMD_SUCCESS:
            result_str = "success";
            break;
        case BROKER_CMD_ERROR_INVALID:
            result_str = "invalid_command";
            break;
        case BROKER_CMD_ERROR_ARGS:
            result_str = "invalid_args";
            break;
        case BROKER_CMD_ERROR_NOT_FOUND:
            result_str = "not_found";
            break;
        case BROKER_CMD_ERROR_DENIED:
            result_str = "denied";
            break;
        case BROKER_CMD_ERROR_FAILED:
            result_str = "failed";
            break;
        default:
            result_str = "unknown";
            break;
    }

    /* 构建 JSON 响应 */
    if (resp->detail != NULL) {
        json_len = XSNPRINTF(json_buf, sizeof(json_buf),
            "{\"command\":\"%s\",\"result\":\"%s\",\"message\":\"%s\",\"detail\":\"%s\"}",
            resp->command ? resp->command : "",
            result_str,
            resp->message ? resp->message : "",
            resp->detail);
    } else {
        json_len = XSNPRINTF(json_buf, sizeof(json_buf),
            "{\"command\":\"%s\",\"result\":\"%s\",\"message\":\"%s\"}",
            resp->command ? resp->command : "",
            result_str,
            resp->message ? resp->message : "");
    }

    if (json_len <= 0 || json_len >= (int)sizeof(json_buf)) {
        WBLOG_ERR(broker, "command response JSON encode failed");
        return MQTT_CODE_ERROR_OUT_OF_BUFFER;
    }

    /* 构建响应主题 */
    if (sender != NULL && BROKER_STR_VALID(sender->client_id)) {
        XSNPRINTF(response_topic, sizeof(response_topic),
            "$sys/broker/commands/response/%s", sender->client_id);
    } else {
        XSTRNCPY(response_topic, "$sys/broker/commands/response/unknown",
                 sizeof(response_topic));
    }

    /* 发送 PUBLISH 消息给发送者 */
    if (sender != NULL && sender->connected) {
        MqttPublish pub;
        int packet_len;

        XMEMSET(&pub, 0, sizeof(pub));
        pub.retain = 0;
        pub.qos = MQTT_QOS_0;
        pub.topic_name = response_topic;
        pub.topic_name_len = (word16)XSTRLEN(response_topic);

        /* 编码 PUBLISH 数据包 */
        packet_len = MqttEncode_Publish(sender->tx_buf,
            BROKER_CLIENT_TX_SZ(sender), &pub, 0);
        if (packet_len <= 0) {
            WBLOG_ERR(broker, "failed to encode command response rc=%d", packet_len);
            return packet_len;
        }

        /* 复制 payload */
        if (packet_len + json_len <= BROKER_CLIENT_TX_SZ(sender)) {
            XMEMCPY(sender->tx_buf + packet_len, json_buf, json_len);
            packet_len += json_len;

            /* 更新剩余长度字段 */
            {
                int remaining_len = json_len;
                int len_pos = 1;
                do {
                    sender->tx_buf[len_pos] = (remaining_len & 0x7F) | 0x80;
                    remaining_len >>= 7;
                    if (remaining_len == 0) {
                        sender->tx_buf[len_pos] &= 0x7F;
                        break;
                    }
                    len_pos++;
                } while (remaining_len > 0 && len_pos < 4);
            }

            /* 发送数据包 */
            rc = MqttPacket_Write(&sender->client, sender->tx_buf, packet_len);
            if (rc != MQTT_CODE_SUCCESS) {
                WBLOG_ERR(broker, "failed to send command response rc=%d", rc);
                return rc;
            }
            WBLOG_DBG(broker, "sent command response to '%s': %s",
                response_topic, json_buf);
        } else {
            WBLOG_ERR(broker, "command response too large");
            return MQTT_CODE_ERROR_OUT_OF_BUFFER;
        }
    } else {
        WBLOG_DBG(broker, "cannot send command response: sender disconnected");
    }

    return MQTT_CODE_SUCCESS;
}

/* 处理控制命令 */
static int BrokerCommand_Process(MqttBroker* broker, BrokerClient* sender,
                                  const char* payload, int payload_len)
{
    char* cmd = NULL;
    char* arg = NULL;
    BrokerCommandResponse resp;
    int rc = MQTT_CODE_SUCCESS;
    char* payload_copy = NULL;  /* 用于释放解析时分配的内存 */

    if (broker == NULL || payload == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    BROKER_INIT_STRUCT(resp);

    /* 解析命令 */
    rc = BrokerCommand_Parse(payload, payload_len, &cmd, &arg);
    if (rc != MQTT_CODE_SUCCESS) {
        WBLOG_ERR(broker, "failed to parse command rc=%d", rc);
        resp.code = BROKER_CMD_ERROR_FAILED;
        resp.message = "Failed to parse command";
        BrokerCommand_SendResponse(broker, sender, &resp);
        return rc;
    }

    /* 保存 payload_copy 指针以便释放 */
    payload_copy = cmd;

    /* 验证命令 */
    if (cmd == NULL || *cmd == '\0') {
        resp.code = BROKER_CMD_ERROR_INVALID;
        resp.message = "Empty command";
        BrokerCommand_SendResponse(broker, sender, &resp);
        if (payload_copy) WOLFMQTT_FREE(payload_copy);
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    resp.command = cmd;

    /* 分发命令 */
    if (XSTRCMP(cmd, "reset") == 0) {
        if (arg != NULL) {
            resp.code = BROKER_CMD_ERROR_ARGS;
            resp.message = "reset command takes no arguments";
        } else {
            rc = BrokerCommand_HandleReset(broker, &resp);
        }
    }
    else if (XSTRCMP(cmd, "kick") == 0) {
        rc = BrokerCommand_HandleKick(broker, arg, &resp);
    }
    else {
        resp.code = BROKER_CMD_ERROR_INVALID;
        resp.message = "Unknown command";
        resp.detail = cmd;
        rc = MQTT_CODE_ERROR_BAD_ARG;
    }

    /* 发送响应 */
    BrokerCommand_SendResponse(broker, sender, &resp);

    if (payload_copy) WOLFMQTT_FREE(payload_copy);
    return rc;
}

#endif /* WOLFMQTT_BROKER_COMMANDS */

/* 发送统计消息到 $sys/broker/stats 主题（保留消息 + 主动推送） */
static int MqttBroker_PublishStats(MqttBroker* broker)
{
    int rc = MQTT_CODE_SUCCESS;
    char json_buf[512];
    int json_len;
    WOLFMQTT_BROKER_TIME_T now, uptime;
    const char* stats_topic = "$sys/broker/stats";
    int subscribers_notified = 0;

    if (broker == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    /* 检查是否启用统计发送 */
    if (broker->stats_interval == 0) {
        return MQTT_CODE_SUCCESS;  /* 禁用，直接返回 */
    }

    /* 检查是否到达发送时间 */
    now = WOLFMQTT_BROKER_GET_TIME_S();
    if (now < broker->last_stats_time + broker->stats_interval) {
        return MQTT_CODE_SUCCESS;  /* 未到时间 */
    }

    /* 更新上次发送时间 */
    broker->last_stats_time = now;

    /* 计算运行时间 */
    uptime = (now >= broker->stats.start) ? (now - broker->stats.start) : 0;

    /* 构建 JSON 格式的统计数据 */
    json_len = XSNPRINTF(json_buf, sizeof(json_buf),
        "{\"conns\":%u,\"rx_msgs\":%u,\"tx_msgs\":%u,\"rx_bytes\":%u,\"tx_bytes\":%u,\"retained\":%u,\"subs\":%u,\"uptime\":%u}",
        (unsigned int)broker->stats.conns,
        (unsigned int)broker->stats.rx_msgs,
        (unsigned int)broker->stats.tx_msgs,
        (unsigned int)broker->stats.rx_bytes,
        (unsigned int)broker->stats.tx_bytes,
        (unsigned int)broker->stats.retained,
        (unsigned int)broker->stats.subs,
        (unsigned int)uptime);

    if (json_len <= 0 || json_len >= (int)sizeof(json_buf)) {
        WBLOG_ERR(broker, "stats JSON encode failed");
        return MQTT_CODE_ERROR_OUT_OF_BUFFER;
    }

#ifdef WOLFMQTT_BROKER_RETAINED
    /* 通过保留消息发布统计数据（供新订阅者使用） */
    rc = BrokerRetained_Store(broker, stats_topic,
        (const byte*)json_buf, (word32)json_len,
        MQTT_QOS_0, 0);  /* QoS 0, 无过期 */

    if (rc != MQTT_CODE_SUCCESS) {
        WBLOG_ERR(broker, "failed to store retained stats rc=%d", rc);
        return rc;
    }
#endif

    /* 主动向已订阅 $sys/broker/stats 的客户端推送 PUBLISH 消息 */
#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        for (i = 0; i < broker->max_subs; i++) {
            BrokerSub* sub = &broker->subs[i];
            if (!sub->in_use) continue;
#else
    {
        BrokerSub* sub = broker->subs;
        while (sub) {
            BrokerSub* next_sub = sub->next;
#endif
            if (sub->client != NULL &&
                sub->client->connected &&
                BROKER_STR_VALID(sub->filter) &&
                XSTRCMP(sub->filter, stats_topic) == 0) {

                MqttPublish pub;
                int pub_len;
                word16 packet_id = 0;

                /* 构建 PUBLISH 消息 */
                XMEMSET(&pub, 0, sizeof(pub));
                pub.retain = 1;  /* 保留标志 */
                pub.qos = MQTT_QOS_0;
                pub.topic_name = (char*)stats_topic;
                pub.topic_name_len = (word16)XSTRLEN(stats_topic);
                pub.buffer = (byte*)json_buf;
                pub.total_len = json_len;
#ifdef WOLFMQTT_V5
                pub.protocol_level = sub->client->protocol_level;
#endif

                /* 编码 PUBLISH 消息 */
                pub_len = MqttEncode_Publish(sub->client->tx_buf,
                    BROKER_CLIENT_TX_SZ(sub->client), &pub, packet_id);

                if (pub_len > 0) {
                    /* 发送给客户端 */
                    int write_rc = MqttPacket_Write(&sub->client->client,
                        sub->client->tx_buf, pub_len);
                    if (write_rc > 0) {
                        subscribers_notified++;
                    } else {
                        WBLOG_ERR(broker, "failed to send stats to client %s rc=%d",
                            BROKER_STR_VALID(sub->client->client_id) ? sub->client->client_id : "(unknown)",
                            write_rc);
                    }
                }
            }
#ifndef WOLFMQTT_STATIC_MEMORY
            sub = next_sub;
#endif
        }
    }
 
    return MQTT_CODE_SUCCESS;
}

int MqttBroker_Step(MqttBroker* broker)
{
    int activity = 0;
    int rc;

    if (broker == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    if (!broker->running) {
        return MQTT_CODE_SUCCESS;
    }

#ifdef WOLFMQTT_BROKER_EPOLL
    /* epoll-based event loop */
    {
        int nfds, i;

        /* Wait for events (1ms timeout to allow periodic tasks) */
        nfds = BrokerEpoll_Wait(broker, 1);
        if (nfds < 0 && nfds != MQTT_CODE_CONTINUE) {
            return nfds; /* Error */
        }

        /* Process ready sockets */
        for (i = 0; i < nfds; i++) {
            struct epoll_event* ev = &broker->epoll_events[i];
            BROKER_SOCKET_T sock = ev->data.fd;

            /* Check if it's a listener socket (new connection) */
#ifdef ENABLE_MQTT_TLS
            if (sock == broker->listen_sock || sock == broker->listen_sock_tls) {
                BROKER_SOCKET_T new_sock = BROKER_SOCKET_INVALID;
                int is_tls = (sock == broker->listen_sock_tls) ? 1 : 0;
#else
            if (sock == broker->listen_sock) {
                BROKER_SOCKET_T new_sock = BROKER_SOCKET_INVALID;
                int is_tls = 0;
#endif
                
                rc = broker->net.accept(broker->net.ctx, sock, &new_sock);
                if (rc == MQTT_CODE_SUCCESS && new_sock != BROKER_SOCKET_INVALID) {
                    if (BrokerClient_Add(broker, new_sock, is_tls) == NULL) {
                        WBLOG_ERR(broker,
                            "accept sock=%d rejected (alloc)",
                            (int)new_sock);
                        broker->net.close(broker->net.ctx, new_sock);
                    }
                    activity = 1;
                }
            }
#ifdef ENABLE_MQTT_WEBSOCKET
            else if (broker->api_ctx && sock == broker->api_ctx->http_listen_sock) {
                /* HTTP/WebSocket listener - delegate to API processor */
                if (broker->api_ctx) {
                    rc = MqttBrokerApi_Process(broker->api_ctx);
                    if (rc != MQTT_CODE_CONTINUE) {
                        activity = 1;
                    }
                }
            }
#endif
            else {
                /* It's a client socket - find and process it */
                BrokerClient* bc = NULL;
#ifdef WOLFMQTT_STATIC_MEMORY
                int j;
                for (j = 0; j < broker->max_clients; j++) {
                    if (broker->clients[j].in_use && broker->clients[j].sock == sock) {
                        bc = &broker->clients[j];
                        break;
                    }
                }
#else
                BrokerClient* cur = broker->clients;
                while (cur != NULL) {
                    if (cur->sock == sock) {
                        bc = cur;
                        break;
                    }
                    cur = cur->next;
                }
#endif

                if (bc != NULL) {
                    rc = BrokerClient_Process(broker, bc);
                    if (rc > 0) {
                        activity = 1;
                    }
                }
            }
        }
    }
#else
    /* select-based event loop (original implementation) */

    /* 1. Try to accept new connections (non-blocking) */

    /* Plain (non-TLS) listener */
    if (broker->listen_sock != BROKER_SOCKET_INVALID) {
        BROKER_SOCKET_T new_sock = BROKER_SOCKET_INVALID;
        rc = broker->net.accept(broker->net.ctx, broker->listen_sock,
            &new_sock);
        if (rc == MQTT_CODE_SUCCESS && new_sock != BROKER_SOCKET_INVALID) {
        #ifdef WOLFMQTT_POSIX_SOCKET
            /* Reject socket if >= FD_SETSIZE (would overflow fd_set) */
            if (new_sock >= FD_SETSIZE) {
                WBLOG_ERR(broker,
                    "accept sock=%d rejected (>= FD_SETSIZE)",
                    (int)new_sock);
                broker->net.close(broker->net.ctx, new_sock);
            }
            else
        #endif
            {                
                if (BrokerClient_Add(broker, new_sock, 0) == NULL) {
                    WBLOG_ERR(broker,
                        "accept sock=%d rejected (alloc)",
                        (int)new_sock);
                    broker->net.close(broker->net.ctx, new_sock);
                }
                activity = 1;
            }
        }
    }

#ifdef ENABLE_MQTT_TLS
    /* TLS listener */
    if (broker->listen_sock_tls != BROKER_SOCKET_INVALID) {
        BROKER_SOCKET_T new_sock = BROKER_SOCKET_INVALID;
        rc = broker->net.accept(broker->net.ctx, broker->listen_sock_tls,
            &new_sock);
        if (rc == MQTT_CODE_SUCCESS && new_sock != BROKER_SOCKET_INVALID) {
        #ifdef WOLFMQTT_POSIX_SOCKET
            /* Reject socket if >= FD_SETSIZE (would overflow fd_set) */
            if (new_sock >= FD_SETSIZE) {
                WBLOG_ERR(broker,
                    "accept sock=%d rejected (>= FD_SETSIZE)",
                    (int)new_sock);
                broker->net.close(broker->net.ctx, new_sock);
            }
            else
        #endif
            {
                
                if (BrokerClient_Add(broker, new_sock, 1) == NULL) {
                    WBLOG_ERR(broker,
                        "accept sock=%d rejected (alloc)",
                        (int)new_sock);
                    broker->net.close(broker->net.ctx, new_sock);
                }
                activity = 1;
            }
        }
    }
#endif /* ENABLE_MQTT_TLS */

    /* WebSocket handling is now integrated into HTTP API listener (unified port) */

    /* 2. Process each client */
#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        for (i = 0; i < broker->max_clients; i++) {
            BrokerClient* bc = &broker->clients[i];
            if (!bc->in_use) {
                continue;
            }
            rc = BrokerClient_Process(broker, bc);
            if (rc > 0) {
                activity = 1;
            }
        }
    }
#else
    {
        BrokerClient* bc = broker->clients;
        while (bc) {
            BrokerClient* next = bc->next;
            rc = BrokerClient_Process(broker, bc);
            if (rc > 0) {
                activity = 1;
            }
            /* BrokerClient_Process may remove another client (e.g. client ID
             * takeover), which could free the node that next points to.
             * Validate next is still in the linked list before dereferencing */
            if (next != NULL) {
                BrokerClient* v = broker->clients;
                while (v != NULL && v != next) {
                    v = v->next;
                }
                if (v == NULL) {
                    break; /* next was freed; remaining clients handled next step */
                }
            }
            bc = next;
        }
    }
#endif
#endif /* WOLFMQTT_BROKER_EPOLL */

    /* 3. Check for expired sessions (v5 Session Expiry Interval) */
#ifdef WOLFMQTT_V5
    if (BrokerSubs_CheckSessionExpiry(broker) > 0) {
        activity = 1;
    }
#endif

    /* 4. Process pending wills (v5 Will Delay Interval) */
    if (BrokerPendingWill_Process(broker) > 0) {
        activity = 1;
    }

    /* 5. Process HTTP API requests */
    if (broker->api_ctx != NULL) {
        rc = MqttBrokerApi_Process(broker->api_ctx);
        if (rc != MQTT_CODE_CONTINUE) {
            activity = 1;
        }
    }

    /* 6. 定期发送统计消息到 $sys/broker/stats */
    MqttBroker_PublishStats(broker);

    return activity ? MQTT_CODE_SUCCESS : MQTT_CODE_CONTINUE;
}

int MqttBroker_Start(MqttBroker* broker)
{
    int rc;

    if (broker == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    /* Print compile-time macro configuration */    
    PRINTF("Compile-time Macro Configuration:");
    PRINTF("=================================================");
#ifdef WOLFMQTT_BROKER_AUTH
    PRINTF("WOLFMQTT_BROKER_AUTH=true");
#else
    PRINTF("WOLFMQTT_BROKER_AUTH=false");
#endif
#ifdef WOLFMQTT_BROKER_NO_INSECURE
    PRINTF("WOLFMQTT_BROKER_NO_INSECURE=true");
#else
    PRINTF("WOLFMQTT_BROKER_NO_INSECURE=false");
#endif
#ifdef WOLFMQTT_V5
    PRINTF("WOLFMQTT_V5=true");
#else
    PRINTF("WOLFMQTT_V5=false");
#endif
#ifdef WOLFMQTT_NONBLOCK
    PRINTF("WOLFMQTT_NONBLOCK=true");
#else
    PRINTF("WOLFMQTT_NONBLOCK=false");
#endif
#ifdef WOLFMQTT_MULTITHREAD
    PRINTF("WOLFMQTT_MULTITHREAD=true");
#else
    PRINTF("WOLFMQTT_MULTITHREAD=false");
#endif
#ifdef ENABLE_MQTT_TLS
    PRINTF("ENABLE_MQTT_TLS=true");
#else
    PRINTF("ENABLE_MQTT_TLS=false");
#endif
#ifdef ENABLE_MQTT_WEBSOCKET
    PRINTF("ENABLE_MQTT_WEBSOCKET=true");
#else
    PRINTF("ENABLE_MQTT_WEBSOCKET=false");
#endif
#ifdef WOLFMQTT_BROKER_EPOLL
    PRINTF("WOLFMQTT_BROKER_EPOLL=true (epoll I/O multiplexing)");
#else
    PRINTF("WOLFMQTT_BROKER_EPOLL=false (select-based)");
#endif
    PRINTF("=================================================");

#ifdef ENABLE_MQTT_TLS
    /* Initialize TLS context if TLS is enabled */
    if (broker->use_tls) {
    #if !defined(WOLFMQTT_BROKER_CUSTOM_NET)
        rc = BrokerTls_Init(broker);
        if (rc != MQTT_CODE_SUCCESS) {
            WBLOG_ERR(broker, "TLS init failed rc=%d", rc);
            return rc;
        }
    #else
        if (broker->tls_ctx == NULL) {
            WBLOG_ERR(broker, "TLS ctx must be set before start");
            return MQTT_CODE_ERROR_BAD_ARG;
        }
    #endif
    }

    /* Start plain (non-TLS) listener */
  #ifndef WOLFMQTT_BROKER_NO_INSECURE
    if (!broker->use_tls || broker->port != broker->port_tls) {
        rc = broker->net.listen(broker->net.ctx, &broker->listen_sock,
            broker->port, BROKER_LISTEN_BACKLOG);
        if (rc != MQTT_CODE_SUCCESS) {
            WBLOG_ERR(broker, "listen (plain) failed rc=%d", rc);
            return rc;
        }
        WBLOG_INFO(broker, "listening on port %d (plain)",
            broker->port);
    }
    else if (broker->use_tls && broker->port == broker->port_tls) {
        WBLOG_INFO(broker,
            "plain port == TLS port (%d), TLS-only mode",
            broker->port_tls);
    }
  #endif /* !WOLFMQTT_BROKER_NO_INSECURE */

    /* Start TLS listener */
    if (broker->use_tls) {
        rc = broker->net.listen(broker->net.ctx, &broker->listen_sock_tls,
            broker->port_tls, BROKER_LISTEN_BACKLOG);
        if (rc != MQTT_CODE_SUCCESS) {
            WBLOG_ERR(broker, "listen (TLS) failed rc=%d", rc);
            return rc;
        }
        WBLOG_INFO(broker, "listening on port %d (TLS)",
            broker->port_tls);
    }
#else
    /* No TLS support compiled in: plain listener only */
    rc = broker->net.listen(broker->net.ctx, &broker->listen_sock,
        broker->port, BROKER_LISTEN_BACKLOG);
    if (rc != MQTT_CODE_SUCCESS) {
        WBLOG_ERR(broker, "listen failed rc=%d", rc);
        return rc;
    }
    WBLOG_INFO(broker, "listening on port %d (no TLS)", broker->port);
#endif

#ifdef WOLFMQTT_BROKER_AUTH
    if (broker->username || broker->password) {
        WBLOG_INFO(broker, "auth enabled user=%s",
            broker->username ? broker->username : "(null)");
    #ifdef ENABLE_MQTT_TLS
    #ifndef WOLFMQTT_BROKER_NO_INSECURE
        if (broker->use_tls &&
            broker->port != broker->port_tls) {
            WBLOG_ERR(broker,
                "WARNING: auth credentials exposed on plaintext "
                "port %d. Rebuild with ./configure --disable-broker-insecure "
                "for TLS-only",
                broker->port);
        }
    #endif
    #endif
    }
#endif  

    /* Ensure at least one listener is active */
    if (broker->listen_sock == BROKER_SOCKET_INVALID
#ifdef ENABLE_MQTT_TLS
        && broker->listen_sock_tls == BROKER_SOCKET_INVALID
#endif
    ) {
        WBLOG_ERR(broker, "no listeners configured");
        return MQTT_CODE_ERROR_BAD_ARG;
    }

#ifdef WOLFMQTT_BROKER_EPOLL
    /* Initialize epoll for I/O multiplexing */
    rc = BrokerEpoll_Init(broker);
    if (rc != MQTT_CODE_SUCCESS) {
        WBLOG_ERR(broker, "epoll init failed rc=%d", rc);
        return rc;
    }

    /* Add listener sockets to epoll */
    if (broker->listen_sock != BROKER_SOCKET_INVALID) {
        rc = BrokerEpoll_AddSocket(broker, broker->listen_sock, EPOLLIN);
        if (rc != MQTT_CODE_SUCCESS) {
            WBLOG_ERR(broker, "failed to add listen sock to epoll");
            return rc;
        }
    }
#ifdef ENABLE_MQTT_TLS
    if (broker->listen_sock_tls != BROKER_SOCKET_INVALID) {
        rc = BrokerEpoll_AddSocket(broker, broker->listen_sock_tls, EPOLLIN);
        if (rc != MQTT_CODE_SUCCESS) {
            WBLOG_ERR(broker, "failed to add TLS listen sock to epoll");
            return rc;
        }
    }
#endif
     
#endif

#ifdef ENABLE_MQTT_WEBSOCKET
    /* Start unified HTTP/WebSocket listener if enabled */
    if ((broker->enable_http || broker->enable_ws) && broker->http_port > 0) {
        /* Allocate API context if not already allocated */
        if (broker->api_ctx == NULL) {
            broker->api_ctx = (MqttBrokerApiContext*)WOLFMQTT_MALLOC(sizeof(MqttBrokerApiContext));
            if (broker->api_ctx == NULL) {
                WBLOG_ERR(broker, "Failed to allocate API context");
                return MQTT_CODE_ERROR_MEMORY;
            }
        }

        rc = MqttBrokerApi_Init(broker, broker->api_ctx, broker->http_port);
        if (rc != MQTT_CODE_SUCCESS) {
            WBLOG_ERR(broker, "Unified HTTP/WebSocket listen failed on port %d rc=%d",
                     broker->http_port, rc);
            WOLFMQTT_FREE(broker->api_ctx);
            broker->api_ctx = NULL;
            return rc;
        }
        WBLOG_INFO(broker, "listening on port %d (HTTP/WebSocket)", broker->http_port);
        
#ifdef WOLFMQTT_BROKER_EPOLL
        /* Add HTTP/WebSocket listener socket to epoll */
        if (broker->api_ctx && broker->api_ctx->http_listen_sock != BROKER_SOCKET_INVALID) {
            rc = BrokerEpoll_AddSocket(broker, broker->api_ctx->http_listen_sock, EPOLLIN);
            if (rc != MQTT_CODE_SUCCESS) {
                WBLOG_ERR(broker, "failed to add HTTP/WebSocket listen sock to epoll");
                /* Continue anyway - HTTP API will still work via polling */
            }  
        }
#endif
    }
#else
    /* Start HTTP-only listener if enabled */
    if (broker->enable_http && broker->http_port > 0) {
        /* Allocate API context if not already allocated */
        if (broker->api_ctx == NULL) {
            broker->api_ctx = (MqttBrokerApiContext*)WOLFMQTT_MALLOC(sizeof(MqttBrokerApiContext));
            if (broker->api_ctx == NULL) {
                WBLOG_ERR(broker, "Failed to allocate API context");
                return MQTT_CODE_ERROR_MEMORY;
            }
        }

        rc = MqttBrokerApi_Init(broker, broker->api_ctx, broker->http_port);
        if (rc != MQTT_CODE_SUCCESS) {
            WBLOG_ERR(broker, "HTTP listen failed on port %d rc=%d",
                     broker->http_port, rc);
            WOLFMQTT_FREE(broker->api_ctx);
            broker->api_ctx = NULL;
            return rc;
        }
        WBLOG_INFO(broker, "listening on port %d (HTTP)", broker->http_port);
        
#ifdef WOLFMQTT_BROKER_EPOLL
        /* Add HTTP listener socket to epoll */
        if (broker->api_ctx && broker->api_ctx->http_listen_sock != BROKER_SOCKET_INVALID) {
            rc = BrokerEpoll_AddSocket(broker, broker->api_ctx->http_listen_sock, EPOLLIN);
            if (rc != MQTT_CODE_SUCCESS) {
                WBLOG_ERR(broker, "failed to add HTTP listen sock to epoll");
            } else {
                WBLOG_INFO(broker, "HTTP socket added to epoll");
            }
        }
#endif
    }
#endif

    broker->running = 1;
    return MQTT_CODE_SUCCESS;
}

/* MqttBroker_StartWebSocket removed - WebSocket now uses unified HTTP port */

#ifdef ENABLE_MQTT_WEBSOCKET
/* Add WebSocket client after successful handshake (for unified port mode) */
int MqttBroker_AddWebSocketClient(MqttBroker* broker, BROKER_SOCKET_T sock)
{
    int rc;
    
    if (broker == NULL || sock == BROKER_SOCKET_INVALID) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* Add client to broker */
    BrokerClient* bc = BrokerClient_Add(broker, sock, 0);
    if (bc == NULL) {
        WBLOG_ERR(broker, "Failed to add WebSocket client on sock=%d", (int)sock);
        broker->net.close(broker->net.ctx, sock);
        return MQTT_CODE_ERROR_MEMORY;
    }
    
    /* Initialize WebSocket transport */
    rc = BrokerTransport_Init(bc, BROKER_TRANSPORT_WEBSOCKET, broker);
    if (rc != MQTT_CODE_SUCCESS) {
        WBLOG_ERR(broker, "Failed to init WebSocket transport on sock=%d: %d",
                  (int)sock, rc);
        BrokerClient_Remove(broker, bc, -1);
        return rc;
    }
    
    /* Mark WebSocket handshake as already done since it was completed in API handler */
    MqttWebSocketContext* ws_ctx = (MqttWebSocketContext*)bc->transport.context;
    if (ws_ctx) {
        ws_ctx->handshake_done = 1;
    }
    
    WBLOG_INFO(broker, "WebSocket client added successfully on sock=%d", (int)sock);
    return MQTT_CODE_SUCCESS;
}
#endif /* ENABLE_MQTT_WEBSOCKET */

int MqttBroker_Run(MqttBroker* broker)
{
    int rc;

    rc = MqttBroker_Start(broker);
    if (rc != MQTT_CODE_SUCCESS) {
        return rc;
    }

    while (broker->running) {
        rc = MqttBroker_Step(broker);
        if (rc == MQTT_CODE_CONTINUE) {
            /* Idle - sleep briefly to avoid busy-waiting */
            BROKER_SLEEP_MS(10);
        }
        else if (rc < 0 && rc != MQTT_CODE_CONTINUE) {
            break;
        }
    }

    return MQTT_CODE_SUCCESS;
}

int MqttBroker_Stop(MqttBroker* broker)
{
    if (broker == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    broker->running = 0;
    return MQTT_CODE_SUCCESS;
}

int MqttBroker_Free(MqttBroker* broker)
{
    if (broker == NULL) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    /* Disconnect and free all clients and subscriptions */
#ifdef WOLFMQTT_STATIC_MEMORY
    {
        int i;
        for (i = 0; i < broker->max_clients; i++) {
            if (broker->clients[i].in_use) {
                BrokerSubs_RemoveClient(broker, &broker->clients[i]);
                BrokerClient_Free(&broker->clients[i]);
            }
        }
    }
#else
    while (broker->clients) {
        BrokerSubs_RemoveClient(broker, broker->clients);
        BrokerClient_Remove(broker, broker->clients, -1);
    }
    /* Free any orphaned subs (e.g. from clean_session=0 clients) */
    while (broker->subs) {
        BrokerSub* next = broker->subs->next;
        if (broker->subs->filter) {
            WOLFMQTT_FREE(broker->subs->filter);
        }
        if (broker->subs->client_id) {
            WOLFMQTT_FREE(broker->subs->client_id);
        }
        WOLFMQTT_FREE(broker->subs);
        broker->subs = next;
    }
#endif

    /* Clean up pending wills and retained messages */
    BrokerPendingWill_FreeAll(broker);
    BrokerRetained_FreeAll(broker);

    /* Clean up HTTP API */
    if (broker->api_ctx != NULL) {
        MqttBrokerApi_Free(broker->api_ctx);
        WOLFMQTT_FREE(broker->api_ctx);
        broker->api_ctx = NULL;
    }

#ifdef ENABLE_MQTT_TLS
    if (broker->tls_ctx != NULL) {
    #if !defined(WOLFMQTT_BROKER_CUSTOM_NET)
        if (broker->tls_ctx_owned) {
            /* Context was created by BrokerTls_Init: full cleanup */
            BrokerTls_Free(broker);
        }
        else
    #endif
        {
            /* Application-provided TLS context: free ctx but skip
             * wolfSSL_Cleanup() since wolfSSL may be shared */
            wolfSSL_CTX_free(broker->tls_ctx);
            broker->tls_ctx = NULL;
        }
    }
#endif

    /* Close listen sockets */
#ifdef ENABLE_MQTT_WEBSOCKET
    /* WebSocket cleanup is handled by unified HTTP/WebSocket transport layer */
#endif

    if (broker->listen_sock != BROKER_SOCKET_INVALID) {
        broker->net.close(broker->net.ctx, broker->listen_sock);
        broker->listen_sock = BROKER_SOCKET_INVALID;
    }
#ifdef ENABLE_MQTT_TLS
    if (broker->listen_sock_tls != BROKER_SOCKET_INVALID) {
        broker->net.close(broker->net.ctx, broker->listen_sock_tls);
        broker->listen_sock_tls = BROKER_SOCKET_INVALID;
    }
#endif

#ifdef WOLFMQTT_BROKER_EPOLL
    /* Cleanup epoll resources */
    BrokerEpoll_Cleanup(broker);
#endif

    return MQTT_CODE_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* CLI wrapper                                                                 */
/* -------------------------------------------------------------------------- */
static void BrokerUsage(const char* prog)
{
    (void)prog; /* Suppress unused parameter warning */
    PRINTF("usage: %s [-p port] [-l level]"
#ifdef WOLFMQTT_BROKER_AUTH
           " [-u user] [-P pass]"
#endif
#ifdef ENABLE_MQTT_TLS
           " [-t] [-s port] [-V ver] [-c cert] [-K key] [-A ca]"
#endif
#ifdef ENABLE_MQTT_WEBSOCKET
           " [-w port]"
#endif
           , prog);
    PRINTF("  -p <port>   Plain port (default: %d)", MQTT_DEFAULT_PORT);
    PRINTF("  -l <level>  Log level: 0=debug, 1=info, 2=warn (default), 3=error, 4=fatal");
#ifdef ENABLE_MQTT_TLS
    PRINTF("  -t          Enable TLS support");
    PRINTF("  -s <port>   TLS port (default: %d)", MQTT_SECURE_PORT);
    PRINTF("  -V <ver>    TLS version: 12=TLS 1.2, 13=TLS 1.3 (default: auto)");
    PRINTF("  -c <file>   Server certificate file (PEM)");
    PRINTF("  -K <file>   Server private key file (PEM)");
    PRINTF("  -A <file>   CA certificate for mutual TLS (PEM)");
#endif
#ifdef ENABLE_MQTT_WEBSOCKET
    PRINTF("  -w <port>   WebSocket listen port (default: %d)", MQTT_WS_PORT);
#endif
    PRINTF("Features:"
#ifdef WOLFMQTT_BROKER_RETAINED
           " retained"
#endif
#ifdef WOLFMQTT_BROKER_WILL
           " will"
#endif
#ifdef WOLFMQTT_BROKER_WILDCARDS
           " wildcards"
#endif
#ifdef WOLFMQTT_BROKER_AUTH
           " auth"
#endif
#ifdef WOLFMQTT_BROKER_INSECURE
           " insecure"
#endif
#ifdef ENABLE_MQTT_TLS
           " tls"
#endif
#ifdef ENABLE_MQTT_WEBSOCKET
           " websocket"
#endif
           );
}

#if !defined(WOLFMQTT_WOLFIP) && !defined(WOLFMQTT_BROKER_CUSTOM_NET) && \
    !defined(NO_MAIN_DRIVER)
static MqttBroker* g_broker = NULL;
#include <signal.h>
static void broker_signal_handler(int signo)
{
    if (g_broker != NULL) {
        PRINTF("received signal %d, shutting down", signo);
        MqttBroker_Stop(g_broker);
    }
}
#endif

int wolfmqtt_broker(int argc, char** argv)
{
    int rc;
    MqttBroker broker;
    int i;

    /* Set stdout to unbuffered for immediate output */
#ifndef WOLFMQTT_NO_STDIO
    setvbuf(stdout, NULL, _IONBF, 0);
#endif

#if !defined(WOLFMQTT_WOLFIP) && !defined(WOLFMQTT_BROKER_CUSTOM_NET)
    /* 使用简化的 Init API，自动初始化默认网络层 */
    rc = MqttBroker_Init(&broker);
    if (rc != MQTT_CODE_SUCCESS) {
        return rc;
    }
#else
    /* wolfIP 和自定义网络模式需要使用 InitEx */
    #if defined(WOLFMQTT_WOLFIP)
        PRINTF("use MqttBrokerNet_wolfIP_Init() with MqttBroker_InitEx()");
    #else
        PRINTF("custom net requires MqttBroker_InitEx()");
    #endif
    return MQTT_CODE_ERROR_BAD_ARG;
#endif

    /* Parse command line arguments */
    for (i = 1; i < argc; i++) {
        if (XSTRCMP(argv[i], "-p") == 0 && i + 1 < argc) {
            broker.port = (word16)XATOI(argv[++i]);
        }
        else if (XSTRCMP(argv[i], "-l") == 0 && i + 1 < argc) {
            /* Log level: 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR, 4=FATAL */
            broker.log_level = (byte)XATOI(argv[++i]);
        }
#ifdef WOLFMQTT_BROKER_AUTH
        else if (XSTRCMP(argv[i], "-u") == 0 && i + 1 < argc) {
            broker.username = argv[++i];
        }
        else if (XSTRCMP(argv[i], "-P") == 0 && i + 1 < argc) {
            broker.password = argv[++i];
        }
#endif
#ifdef ENABLE_MQTT_TLS
        else if (XSTRCMP(argv[i], "-t") == 0) {
            broker.use_tls = 1;
        }
        else if (XSTRCMP(argv[i], "-s") == 0 && i + 1 < argc) {
            broker.port_tls = (word16)XATOI(argv[++i]);
        }
        else if (XSTRCMP(argv[i], "-V") == 0 && i + 1 < argc) {
            broker.tls_version = (byte)XATOI(argv[++i]);
        }
        else if (XSTRCMP(argv[i], "-c") == 0 && i + 1 < argc) {
            broker.tls_cert = argv[++i];
        }
        else if (XSTRCMP(argv[i], "-K") == 0 && i + 1 < argc) {
            broker.tls_key = argv[++i];
        }
        else if (XSTRCMP(argv[i], "-A") == 0 && i + 1 < argc) {
            broker.tls_ca = argv[++i];
        }
#endif
#ifdef ENABLE_MQTT_WEBSOCKET
        else if (XSTRCMP(argv[i], "-w") == 0 && i + 1 < argc) {
            broker.http_port = (word16)XATOI(argv[++i]);
            broker.enable_ws = 1;
        }
#endif
        else if (XSTRCMP(argv[i], "-h") == 0) {
            BrokerUsage(argv[0]);
            return 0;
        }
        else {
            BrokerUsage(argv[0]);
            return MQTT_CODE_ERROR_BAD_ARG;
        }
    }

#if !defined(WOLFMQTT_WOLFIP) && !defined(WOLFMQTT_BROKER_CUSTOM_NET) && \
    !defined(NO_MAIN_DRIVER)
    g_broker = &broker;
    signal(SIGINT, broker_signal_handler);
    signal(SIGTERM, broker_signal_handler);
#endif

    rc = MqttBroker_Run(&broker);

#if !defined(WOLFMQTT_WOLFIP) && !defined(WOLFMQTT_BROKER_CUSTOM_NET) && \
    !defined(NO_MAIN_DRIVER)
    g_broker = NULL;
#endif

    MqttBroker_Free(&broker);
    return rc;
}

#ifndef NO_MAIN_DRIVER
int main(int argc, char** argv)
{
    return wolfmqtt_broker(argc, argv);
}
#endif

#else /* WOLFMQTT_BROKER */
#ifndef NO_MAIN_DRIVER
int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    PRINTF("not built (configure with --enable-broker)");
    return 0;
}
#endif
#endif /* WOLFMQTT_BROKER */
