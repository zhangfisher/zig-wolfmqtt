/* mqtt_broker_api.c
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

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h> /* for offsetof */
#include <limits.h> /* for ULONG_MAX */

#ifdef WOLFMQTT_BROKER
#if defined(WOLFMQTT_WOLFIP)
    #include "wolfip.h"
#elif !defined(WOLFMQTT_BROKER_CUSTOM_NET)
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
#endif
#endif

#include "wolfmqtt/mqtt_broker.h"
#include "wolfmqtt/logger.h"
#include "wolfmqtt/mqtt_types.h"
#include "wolfmqtt/mqtt_packet.h"

/* API token length requirement */
#define MIN_API_TOKEN_LENGTH    12

/* API response logging macro to reduce code duplication */
#define LOG_API_RESPONSE(broker, status, method, path, query, client_ip) \
    BA_LOG_DBG(broker, "API Response %d %s %s%s%s to=%s", \
               status, method, path, ((query) && (query)[0]) ? "?" : "", \
               (query) ? (query) : "", client_ip)

/* Logging macros - same as mqtt_broker.c */
#ifdef WOLFMQTT_BROKER_LOG
    static inline void api_log(MqttBroker* b, LogLevel level, const char* format, ...) {
        if (b && b->log) {
            va_list args;
            va_start(args, format);
            b->log(level, format, args);
            va_end(args);
        }
    #ifdef WOLFMQTT_BROKER_DEBUG
        else {
            va_list args;
            va_start(args, format);
            vfprintf(stderr, format, args);
            va_end(args);
            fprintf(stderr, "\n");
        }
    #endif
        (void)level;
    }
    
    #define BA_LOG_DBG(b, ...)   api_log(b, LOG_LEVEL_DEBUG, __VA_ARGS__)
    #define BA_LOG_INFO(b, ...)  api_log(b, LOG_LEVEL_INFO, __VA_ARGS__)
    #define BA_LOG_WARN(b, ...)  api_log(b, LOG_LEVEL_WARN, __VA_ARGS__)
    #define BA_LOG_ERR(b, ...)   api_log(b, LOG_LEVEL_ERROR, __VA_ARGS__)
    #define BA_LOG_FATAL(b, ...) api_log(b, LOG_LEVEL_FATAL, __VA_ARGS__)
#else
    #define BA_LOG_DBG(b, ...)
    #define BA_LOG_INFO(b, ...)
    #define BA_LOG_WARN(b, ...)
    #define BA_LOG_ERR(b, ...)
    #define BA_LOG_FATAL(b, ...)
#endif

/* Helper function to find substring with length limit */
static char* strnstr(const char* haystack, const char* needle, size_t len) {
    size_t needle_len = strlen(needle);
    if (needle_len == 0) return (char*)haystack;
    if (needle_len > len) return NULL;
    
    for (size_t i = 0; i <= len - needle_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return (char*)(haystack + i);
        }
    }
    return NULL;
}

/* Helper function for case-insensitive comparison */
static int strnicmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)s1[i]) != tolower((unsigned char)s2[i])) {
            return tolower((unsigned char)s1[i]) - tolower((unsigned char)s2[i]);
        }
        if (s1[i] == '\0') break;
    }
    return 0;
}

/* Time abstraction - use same as mqtt_broker.c */
#ifndef WOLFMQTT_BROKER_GET_TIME_S
    #include <time.h>
    #define WOLFMQTT_BROKER_GET_TIME_S() ((unsigned long)time(NULL))
#endif

/* HTTP response codes */
#define HTTP_200_OK             "HTTP/1.1 200 OK\r\n"
#define HTTP_400_BAD_REQUEST    "HTTP/1.1 400 Bad Request\r\n"
#define HTTP_401_UNAUTHORIZED   "HTTP/1.1 401 Unauthorized\r\n"
#define HTTP_404_NOT_FOUND      "HTTP/1.1 404 Not Found\r\n"
#define HTTP_500_INTERNAL_ERROR "HTTP/1.1 500 Internal Server Error\r\n"

/* Content type */
#define CONTENT_TYPE_JSON       "Content-Type: application/json\r\n"
#define CONTENT_TYPE_TEXT       "Content-Type: text/plain\r\n"
#define CONNECTION_CLOSE        "Connection: close\r\n\r\n"

/* HTTP constants */
#define HTTP_MAX_HEADER_LEN     128
#define HTTP_MAX_METHOD_LEN     8
#define HTTP_MAX_PATH_LEN       256
#define HTTP_MAX_QUERY_LEN      128
#define HTTP_BUFFER_SIZE        8192

/* Helper function to send HTTP response */
static int http_send_response(MqttBroker* broker, BROKER_SOCKET_T sock, 
                              const char* status_line, const char* content_type,
                              const char* body, int body_len)
{
    char header_buf[256];
    int header_len;
    int total_sent = 0;
    int rc;

    if (!broker || sock == BROKER_SOCKET_INVALID) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    /* Build header */
    header_len = XSNPRINTF(header_buf, sizeof(header_buf),
                          "%s%s%s",
                          status_line,
                          content_type,
                          CONNECTION_CLOSE);

    if (header_len <= 0 || header_len >= (int)sizeof(header_buf)) {
        return MQTT_CODE_ERROR_OUT_OF_BUFFER;
    }

    /* Send header */
    rc = broker->net.write(broker->net.ctx, sock, (const byte*)header_buf, header_len, broker->timeout_ms);
    if (rc != header_len) {
        BA_LOG_ERR(broker, "Failed to send HTTP header: %d", rc);
        return MQTT_CODE_ERROR_NETWORK;
    }
    total_sent += rc;

    /* Send body if present */
    if (body && body_len > 0) {
        rc = broker->net.write(broker->net.ctx, sock, (const byte*)body, body_len, broker->timeout_ms);
        if (rc != body_len) {
            BA_LOG_ERR(broker, "Failed to send HTTP body: %d", rc);
            return MQTT_CODE_ERROR_NETWORK;
        }
        total_sent += rc;
    }

    return total_sent;
}

/* Helper function to validate API token */
static int validate_api_token(MqttBrokerApiContext* api_ctx, const char* auth_header)
{
    if (!api_ctx || !auth_header) {
        return 0;
    }

    /* If no token configured, allow access */
    if (api_ctx->api_token[0] == '\0') {
        return 1;
    }

    /* Check if Authorization header starts with "Basic " */
    if (XSTRNCMP(auth_header, "Basic ", 6) != 0) {
        return 0;
    }

    /* Compare the token after "Basic " prefix */
    if (XSTRCMP(auth_header + 6, api_ctx->api_token) == 0) {
        return 1;
    }

    return 0;
}

/* Parse HTTP request line and headers */
static int parse_http_request(MqttBroker* broker, byte* buffer, int len,
                             char* method, int method_max,
                             char* path, int path_max,
                             char* query, int query_max,
                             char* auth_header, int auth_max)
{
    char* line_start = (char*)buffer;
    char* line_end;
    int remaining = len;
    
    if (!buffer || len <= 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    /* Initialize output parameters */
    if (method) method[0] = '\0';
    if (path) path[0] = '\0';
    if (query) query[0] = '\0';
    if (auth_header) auth_header[0] = '\0';

    /* Find end of request line (first \r\n) */
    line_end = strnstr(line_start, "\r\n", remaining);
    if (!line_end) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }

    /* Parse request line: METHOD PATH HTTP/x.x */
    {
        char* space1 = XSTRCHR(line_start, ' ');
        char* space2;
        
        if (!space1 || space1 >= line_end) {
            return MQTT_CODE_ERROR_BAD_ARG;
        }
        
        /* Extract method */
        if (method) {
            int method_len = (int)(space1 - line_start);
            if (method_len >= method_max) method_len = method_max - 1;
            XMEMCPY(method, line_start, method_len);
            method[method_len] = '\0';
        }
        
        /* Find path and query */
        space2 = XSTRCHR(space1 + 1, ' ');
        if (!space2 || space2 >= line_end) {
            return MQTT_CODE_ERROR_BAD_ARG;
        }
        
        /* Extract path */
        if (path) {
            char* question_mark = XSTRCHR(space1 + 1, '?');
            int path_len;
            
            if (question_mark && question_mark < space2) {
                path_len = (int)(question_mark - (space1 + 1));
                if (path_len >= path_max) path_len = path_max - 1;
                XMEMCPY(path, space1 + 1, path_len);
                path[path_len] = '\0';
                
                /* Extract query string */
                if (query) {
                    int query_len = (int)(space2 - (question_mark + 1));
                    if (query_len >= query_max) query_len = query_max - 1;
                    XMEMCPY(query, question_mark + 1, query_len);
                    query[query_len] = '\0';
                }
            } else {
                path_len = (int)(space2 - (space1 + 1));
                if (path_len >= path_max) path_len = path_max - 1;
                XMEMCPY(path, space1 + 1, path_len);
                path[path_len] = '\0';
            }
        }
    }

    /* Parse headers */
    line_start = line_end + 2; /* Skip \r\n */
    remaining = len - (int)(line_start - (char*)buffer);
    
    while (remaining > 2) { /* At least \r\n left */
        line_end = strnstr(line_start, "\r\n", remaining);
        if (!line_end) {
            break; /* End of headers */
        }
        
        if (line_end == line_start) {
            break; /* Empty line indicates end of headers */
        }
        
        /* Check for Authorization header */
        if (auth_header && strnicmp(line_start, "Authorization:", 14) == 0) {
            char* value_start = line_start + 14;
            /* Skip leading spaces */
            while (*value_start == ' ' && value_start < line_end) {
                value_start++;
            }
            
            int auth_len = (int)(line_end - value_start);
            if (auth_len >= auth_max) auth_len = auth_max - 1;
            XMEMCPY(auth_header, value_start, auth_len);
            auth_header[auth_len] = '\0';
        }
        
        /* Move to next line */
        line_start = line_end + 2;
        remaining = len - (int)(line_start - (char*)buffer);
    }

    return MQTT_CODE_SUCCESS;
}

/* Forward declaration for BrokerHandle_PublishMessage */
extern int BrokerHandle_PublishMessage(MqttBroker* broker, MqttPublish* pub_msg);

/* Handle GET /stats endpoint */
static int handle_get_stats(MqttBrokerApiContext* api_ctx, BROKER_SOCKET_T sock)
{
    MqttBroker* broker = api_ctx->broker;
    char response_body[512];
    int body_len;
    BrokerStats stats;
    
    if (!broker) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* Get current stats */
    if (MqttBroker_GetStats(broker, &stats) != MQTT_CODE_SUCCESS) {
        return http_send_response(broker, sock, HTTP_500_INTERNAL_ERROR, 
                                 CONTENT_TYPE_JSON, "{\"error\":\"Failed to get stats\"}", 27);
    }
    
    /* Format JSON response */
    body_len = XSNPRINTF(response_body, sizeof(response_body),
                        "{"
                        "\"conns\":%u,"
                        "\"rx_msgs\":%u,"
                        "\"tx_msgs\":%u,"
                        "\"rx_bytes\":%u,"
                        "\"tx_bytes\":%u,"
                        "\"retained\":%u,"
                        "\"subs\":%u,"
                        "\"uptime_seconds\":%lu"
                        "}",
                        stats.conns,
                        stats.rx_msgs,
                        stats.tx_msgs,
                        stats.rx_bytes,
                        stats.tx_bytes,
                        stats.retained,
                        stats.subs,
                        (unsigned long)(WOLFMQTT_BROKER_GET_TIME_S() - stats.start));
    
    return http_send_response(broker, sock, HTTP_200_OK, CONTENT_TYPE_JSON, 
                             response_body, body_len);
}

/* Handle GET /clients endpoint */
static int handle_get_clients(MqttBrokerApiContext* api_ctx, BROKER_SOCKET_T sock)
{
    MqttBroker* broker = api_ctx->broker;
    char response_body[2048];
    int body_len = 0;
    int first = 1;
    
    if (!broker) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    body_len += XSNPRINTF(response_body + body_len, sizeof(response_body) - body_len, "[");
    
    {
        BrokerClient* bc = broker->clients;
        while (bc) {
            if (bc->connected) {
                if (!first) {
                    body_len += XSNPRINTF(response_body + body_len, sizeof(response_body) - body_len, ",");
                }
                body_len += XSNPRINTF(response_body + body_len, sizeof(response_body) - body_len,
                                    "{\"id\":\"%s\",\"ip\":\"%s\"}",
                                    bc->client_id ? bc->client_id : "",
                                    bc->client_ip);
                first = 0;
            }
            bc = bc->next;
        }
    }
    
    body_len += XSNPRINTF(response_body + body_len, sizeof(response_body) - body_len, "]");
    
    return http_send_response(broker, sock, HTTP_200_OK, CONTENT_TYPE_JSON, 
                             response_body, body_len);
}

/* Handle GET /topics/... endpoint */
static int handle_get_topics(MqttBrokerApiContext* api_ctx, BROKER_SOCKET_T sock, const char* topic_path)
{
    MqttBroker* broker = api_ctx->broker;
    char response_body[4096];
    int body_len = 0;
    int first_sub = 1;
    
    if (!broker || !topic_path) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    body_len += XSNPRINTF(response_body + body_len, sizeof(response_body) - body_len, 
                         "{\"topic\":\"%s\",\"subscribers\":[", topic_path);
    
    /* Find subscriptions matching this topic */
    {
        BrokerSub* sub = broker->subs;
        while (sub) {
            if (sub->client && XSTRCMP(sub->filter, topic_path) == 0) {
                if (!first_sub) {
                    body_len += XSNPRINTF(response_body + body_len, sizeof(response_body) - body_len, ",");
                }
                body_len += XSNPRINTF(response_body + body_len, sizeof(response_body) - body_len,
                                    "{\"client_id\":\"%s\",\"qos\":%d}",
                                    sub->client_id ? sub->client_id : "",
                                    sub->qos);
                first_sub = 0;
            }
            sub = sub->next;
        }
    }
    
    body_len += XSNPRINTF(response_body + body_len, sizeof(response_body) - body_len, "]}");
    
    return http_send_response(broker, sock, HTTP_200_OK, CONTENT_TYPE_JSON, 
                             response_body, body_len);
}

/* Handle GET /options endpoint */
static int handle_get_options(MqttBrokerApiContext* api_ctx, BROKER_SOCKET_T sock)
{
    MqttBroker* broker = api_ctx->broker;
    char response_body[1024];
    int body_len;
    
    if (!broker) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* Format options as JSON - exclude function pointers/callbacks */
    body_len = XSNPRINTF(response_body, sizeof(response_body),
                        "{"
                        "\"port\":%u,"
                        "\"rx_buf_sz\":%u,"
                        "\"tx_buf_sz\":%u,"
                        "\"timeout_ms\":%u,"
                        "\"listen_backlog\":%u,"
                        "\"max_clients\":%u,"
                        "\"max_subs\":%u,"
                        "\"max_retained\":%u,"
                        "\"max_pending_wills\":%u,"
                        "\"max_client_id_len\":%u,"
                        "\"max_username_len\":%u,"
                        "\"max_password_len\":%u,"
                        "\"max_filter_len\":%u,"
                        "\"max_topic_len\":%u,"
                        "\"max_payload_len\":%u,"
                        "\"max_will_payload_len\":%u,"
                        "\"max_packet_size\":%u,"
                        "\"topic_alias_max\":%u,"
                        "\"default_session_expiry_interval\":%u,"
                        "\"stats_interval\":%u,"
                        "\"log_level\":%u,"
                        "\"enable_stats\":%u"
                        "}",
                        broker->port,
                        broker->rx_buf_sz,
                        broker->tx_buf_sz,
                        broker->timeout_ms,
                        broker->listen_backlog,
                        broker->max_clients,
                        broker->max_subs,
                        broker->max_retained,
                        broker->max_pending_wills,
                        broker->max_client_id_len,
                        broker->max_username_len,
                        broker->max_password_len,
                        broker->max_filter_len,
                        broker->max_topic_len,
                        broker->max_payload_len,
                        broker->max_will_payload_len,
                        broker->max_packet_size,
                        broker->topic_alias_max,
                        broker->default_session_expiry_interval,
                        broker->stats_interval,
                        broker->log_level,
                        broker->enable_stats);
    
    return http_send_response(broker, sock, HTTP_200_OK, CONTENT_TYPE_JSON, 
                             response_body, body_len);
}

/* Handle POST publish/<topic> endpoint */
static int handle_post_publish(MqttBrokerApiContext* api_ctx, BROKER_SOCKET_T sock, 
                              const char* topic, const char* query_string, 
                              const char* body, int body_len)
{
    MqttBroker* broker = api_ctx->broker;
    char response_body[256];
    int resp_len;
    MqttPublish pub_msg;
    int rc;
    MqttQoS qos = MQTT_QOS_0;
    byte retain = 0;
    byte* decoded_buffer = NULL;
    int decoded_len = 0;
    
    if (!broker || !topic) {
        return http_send_response(broker, sock, HTTP_400_BAD_REQUEST, 
                                 CONTENT_TYPE_JSON, "{\"error\":\"Invalid parameters\"}", 27);
    }
    
    /* Parse query parameters for qos and retain */
    if (query_string) {
        char* qos_str = strstr(query_string, "qos=");
        char* retain_str = strstr(query_string, "retain=");
        
        if (qos_str) {
            int qos_val = XATOI(qos_str + 4);
            if (qos_val >= 0 && qos_val <= 2) {
                qos = (MqttQoS)qos_val;
            }
        }
        
        if (retain_str) {
            if (XSTRNCMP(retain_str + 7, "true", 4) == 0 || 
                XSTRNCMP(retain_str + 7, "1", 1) == 0) {
                retain = 1;
            }
        }
    }
    
    /* Prepare publish message */
    XMEMSET(&pub_msg, 0, sizeof(pub_msg));
    pub_msg.topic_name = (char*)topic;
    pub_msg.topic_name_len = (word16)XSTRLEN(topic);
    pub_msg.qos = qos;
    pub_msg.retain = retain;
    
    /* Set payload - trim whitespace from both ends */
    if (body && body_len > 0) {
        const char* start = body;
        const char* end = body + body_len - 1;
        
        /* Trim leading whitespace (spaces, tabs, newlines, carriage returns) */
        while (start <= end && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) {
            start++;
        }
        
        /* Trim trailing whitespace */
        while (end >= start && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
            end--;
        }
        
        if (start <= end) {
            int trimmed_len = (int)(end - start + 1);
            
            /* Check if body starts with "base64:" prefix for binary data */
            if (trimmed_len > 7 && XSTRNCMP(start, "base64:", 7) == 0) {
                /* Extract base64 content */
                const char* b64_data = start + 7;
                int b64_len = trimmed_len - 7;
                
                /* Calculate decoded size and allocate buffer */
                int max_decoded_size = (b64_len * 3) / 4 + 4;
                decoded_buffer = (byte*)WOLFMQTT_MALLOC(max_decoded_size);
                
                if (decoded_buffer) {
                    /* Decode base64 to binary */
                    extern int ws_base64_decode(const char* src, int src_len, byte* dst, int dst_max);
                    decoded_len = ws_base64_decode(b64_data, b64_len, decoded_buffer, max_decoded_size);
                    
                    if (decoded_len > 0) {
                        /* Use decoded binary data */
                        pub_msg.buffer = decoded_buffer;
                        pub_msg.total_len = (word16)decoded_len;
                    } else {
                        /* Decoding failed */
                        if (decoded_buffer) WOLFMQTT_FREE(decoded_buffer);
                        return http_send_response(broker, sock, HTTP_400_BAD_REQUEST, 
                                                 CONTENT_TYPE_JSON, "{\"error\":\"Invalid base64 encoding\"}", 33);
                    }
                } else {
                    return http_send_response(broker, sock, HTTP_500_INTERNAL_ERROR, 
                                             CONTENT_TYPE_JSON, "{\"error\":\"Memory allocation failed\"}", 31);
                }
            } else {
                /* Regular text payload */
                pub_msg.buffer = (byte*)start;
                pub_msg.total_len = (word16)trimmed_len;
            }
        } else {
            pub_msg.buffer = NULL;
            pub_msg.total_len = 0;
        }
    } else {
        pub_msg.buffer = NULL;
        pub_msg.total_len = 0;
    }
    
    /* Publish to all subscribers using the broker's publish function */
    rc = BrokerPublish_Message(broker, topic, pub_msg.buffer, pub_msg.total_len, qos, retain);
    
    /* Free decoded buffer if allocated */
    if (decoded_buffer) {
        WOLFMQTT_FREE(decoded_buffer);
    }
    
    if (rc == MQTT_CODE_SUCCESS) {
        resp_len = XSNPRINTF(response_body, sizeof(response_body), 
                            "{\"status\":\"success\",\"message\":\"Published to topic '%s'\"}", topic);
        return http_send_response(broker, sock, HTTP_200_OK, CONTENT_TYPE_JSON, 
                                 response_body, resp_len);
    } else {
        resp_len = XSNPRINTF(response_body, sizeof(response_body), 
                            "{\"error\":\"Failed to publish message\",\"code\":%d}", rc);
        return http_send_response(broker, sock, HTTP_500_INTERNAL_ERROR, CONTENT_TYPE_JSON, 
                                 response_body, resp_len);
    }
}

/* Configuration value types */
typedef enum {
    CONFIG_TYPE_UINT,      /* Unsigned integer with range check */
    CONFIG_TYPE_BOOL,      /* Boolean (true/false/1/0) */
    CONFIG_TYPE_STRING     /* String with length validation */
} ConfigValueType;

/* Field size for type-safe casting */
typedef enum {
    FIELD_SIZE_BYTE = 1,
    FIELD_SIZE_WORD16 = 2,
    FIELD_SIZE_WORD32 = 4
} FieldSize;

/* Configuration item descriptor */
typedef struct {
    const char* key;                    /* Configuration key name */
    ConfigValueType type;               /* Value type */
    size_t offset;                      /* Offset in MqttBroker struct */
    FieldSize field_size;               /* Target field size for type casting */
    union {
        struct {
            unsigned long min_val;      /* For UINT: minimum value */
            unsigned long max_val;      /* For UINT: maximum value */
        } uint_range;
        struct {
            size_t min_len;             /* For STRING: minimum length */
            size_t max_len;             /* For STRING: maximum length */
        } str_range;
    } constraints;
} ConfigItemDescriptor;

/* Helper macro to calculate field offset */
#define CONFIG_OFFSET(field) offsetof(MqttBroker, field)

/* Configuration items table - defines all configurable broker options */
static const ConfigItemDescriptor config_items[] = {
    /* Basic settings */
    {"log_level", CONFIG_TYPE_UINT, CONFIG_OFFSET(log_level), FIELD_SIZE_BYTE, {.uint_range = {0, 5}}},
    {"stats_interval", CONFIG_TYPE_UINT, CONFIG_OFFSET(stats_interval), FIELD_SIZE_WORD32, {.uint_range = {1, 3600}}},
    {"enable_stats", CONFIG_TYPE_BOOL, CONFIG_OFFSET(enable_stats), FIELD_SIZE_BYTE, {0}},
    {"timeout_ms", CONFIG_TYPE_UINT, CONFIG_OFFSET(timeout_ms), FIELD_SIZE_WORD16, {.uint_range = {100, 60000}}},
    {"max_clients", CONFIG_TYPE_UINT, CONFIG_OFFSET(max_clients), FIELD_SIZE_WORD16, {.uint_range = {1, 1000}}},
    {"rx_buf_sz", CONFIG_TYPE_UINT, CONFIG_OFFSET(rx_buf_sz), FIELD_SIZE_WORD16, {.uint_range = {256, 65535}}},
    {"tx_buf_sz", CONFIG_TYPE_UINT, CONFIG_OFFSET(tx_buf_sz), FIELD_SIZE_WORD16, {.uint_range = {256, 65535}}},
    {"listen_backlog", CONFIG_TYPE_UINT, CONFIG_OFFSET(listen_backlog), FIELD_SIZE_WORD16, {.uint_range = {1, 1024}}},
    {"enable_api", CONFIG_TYPE_BOOL, CONFIG_OFFSET(enable_api), FIELD_SIZE_BYTE, {0}},
    
#ifdef ENABLE_MQTT_TLS
    /* TLS settings */
    {"use_tls", CONFIG_TYPE_BOOL, CONFIG_OFFSET(use_tls), FIELD_SIZE_BYTE, {0}},
    {"port_tls", CONFIG_TYPE_UINT, CONFIG_OFFSET(port_tls), FIELD_SIZE_WORD16, {.uint_range = {1, 65535}}},
    {"tls_version", CONFIG_TYPE_UINT, CONFIG_OFFSET(tls_version), FIELD_SIZE_BYTE, {.uint_range = {0, 13}}},
#endif
    
    /* WebSocket settings */
    {"use_ws", CONFIG_TYPE_BOOL, CONFIG_OFFSET(use_ws), FIELD_SIZE_BYTE, {0}},
    {"port_ws", CONFIG_TYPE_UINT, CONFIG_OFFSET(port_ws), FIELD_SIZE_WORD16, {.uint_range = {1, 65535}}},
    
    /* Network settings */
    {"port", CONFIG_TYPE_UINT, CONFIG_OFFSET(port), FIELD_SIZE_WORD16, {.uint_range = {1, 65535}}},
    {"api_port", CONFIG_TYPE_UINT, CONFIG_OFFSET(api_port), FIELD_SIZE_WORD16, {.uint_range = {1, 65535}}},
    
    /* Capacity limits */
    {"max_subs", CONFIG_TYPE_UINT, CONFIG_OFFSET(max_subs), FIELD_SIZE_WORD16, {.uint_range = {0, 10000}}},
    {"max_retained", CONFIG_TYPE_UINT, CONFIG_OFFSET(max_retained), FIELD_SIZE_WORD16, {.uint_range = {0, 1000}}},
    {"max_pending_wills", CONFIG_TYPE_UINT, CONFIG_OFFSET(max_pending_wills), FIELD_SIZE_WORD16, {.uint_range = {0, 100}}},
    {"max_payload_len", CONFIG_TYPE_UINT, CONFIG_OFFSET(max_payload_len), FIELD_SIZE_WORD16, {.uint_range = {0, 65535}}},
    {"max_will_payload_len", CONFIG_TYPE_UINT, CONFIG_OFFSET(max_will_payload_len), FIELD_SIZE_WORD16, {.uint_range = {0, 65535}}},
    
#ifdef WOLFMQTT_V5
    /* MQTT v5 settings */
    {"max_packet_size", CONFIG_TYPE_UINT, CONFIG_OFFSET(max_packet_size), FIELD_SIZE_WORD32, {.uint_range = {0, 268435455}}},
    {"topic_alias_max", CONFIG_TYPE_UINT, CONFIG_OFFSET(topic_alias_max), FIELD_SIZE_WORD16, {.uint_range = {0, 65535}}},
#endif
    
    /* Session settings */
    {"default_session_expiry_interval", CONFIG_TYPE_UINT, CONFIG_OFFSET(default_session_expiry_interval), FIELD_SIZE_WORD32, {.uint_range = {0, ULONG_MAX}}},
    
    /* Authentication settings - strings */
    {"username", CONFIG_TYPE_STRING, CONFIG_OFFSET(username), FIELD_SIZE_BYTE, {.str_range = {0, 128}}},
    {"password", CONFIG_TYPE_STRING, CONFIG_OFFSET(password), FIELD_SIZE_BYTE, {.str_range = {0, 128}}},
    
    /* API settings */
    {"api_token", CONFIG_TYPE_STRING, CONFIG_OFFSET(api_token), FIELD_SIZE_BYTE, {.str_range = {12, sizeof(((MqttBroker*)0)->api_token) - 1}}},
    
    {NULL, 0, 0, 0, {0}} /* Sentinel */
};

/* Find config item by key */
static const ConfigItemDescriptor* find_config_item(const char* key) {
    for (int i = 0; config_items[i].key != NULL; i++) {
        if (XSTRCMP(config_items[i].key, key) == 0) {
            return &config_items[i];
        }
    }
    return NULL;
}

/* Apply configuration value based on type and field size */
static int apply_config_value(MqttBroker* broker, const ConfigItemDescriptor* item, const char* value) {
    byte* target = (byte*)broker + item->offset;
    
    switch (item->type) {
        case CONFIG_TYPE_UINT: {
            unsigned long val = (unsigned long)XATOI(value);
            
            /* Range check */
            if (val < item->constraints.uint_range.min_val || 
                val > item->constraints.uint_range.max_val) {
                return MQTT_CODE_ERROR_BAD_ARG;
            }
            
            /* Write value based on field size */
            switch (item->field_size) {
                case FIELD_SIZE_BYTE:
                    *(byte*)target = (byte)val;
                    break;
                case FIELD_SIZE_WORD16:
                    *(word16*)target = (word16)val;
                    break;
                case FIELD_SIZE_WORD32:
                    /* Handle both word32 and unsigned int (both are 4 bytes on most platforms) */
                    if (item->offset == CONFIG_OFFSET(stats_interval)) {
                        *(unsigned int*)target = (unsigned int)val;
                    } else {
                        *(word32*)target = (word32)val;
                    }
                    break;
                default:
                    return MQTT_CODE_ERROR_BAD_ARG;
            }
            
            return MQTT_CODE_SUCCESS;
        }
        
        case CONFIG_TYPE_BOOL: {
            byte bool_val;
            if (XSTRCMP(value, "true") == 0 || XSTRCMP(value, "1") == 0) {
                bool_val = 1;
            } else if (XSTRCMP(value, "false") == 0 || XSTRCMP(value, "0") == 0) {
                bool_val = 0;
            } else {
                return MQTT_CODE_ERROR_BAD_ARG;
            }
            *(byte*)target = bool_val;
            return MQTT_CODE_SUCCESS;
        }
        
        case CONFIG_TYPE_STRING: {
            size_t val_len = XSTRLEN(value);
            
            /* Length validation */
            if (val_len < item->constraints.str_range.min_len ||
                val_len > item->constraints.str_range.max_len) {
                return MQTT_CODE_ERROR_BAD_ARG;
            }
            
            /* Special handling for pointer fields (username/password) */
            if (item->offset == CONFIG_OFFSET(username) ||
                item->offset == CONFIG_OFFSET(password)) {
                /* These are char* pointers, set to value or NULL */
                char** ptr_target = (char**)target;
                *ptr_target = (value[0] != '\0') ? (char*)value : NULL;
            }
            /* For fixed-size arrays (api_token) */
            else {
                XSTRNCPY((char*)target, value, item->constraints.str_range.max_len);
                ((char*)target)[item->constraints.str_range.max_len] = '\0';
            }
            
            return MQTT_CODE_SUCCESS;
        }
        
        default:
            return MQTT_CODE_ERROR_BAD_ARG;
    }
}

/* Handle POST config/<option> endpoint */
/* Handle POST /api/configs endpoint - batch config update */
static int handle_post_configs(MqttBrokerApiContext* api_ctx, BROKER_SOCKET_T sock, 
                               const char* body, int body_len)
{
    MqttBroker* broker = api_ctx->broker;
    char response_body[512];
    int resp_len;
    int rc = MQTT_CODE_SUCCESS;
    int updated_count = 0;
    char errors[1024];
    int error_len = 0;
    
    if (!broker) {
        BA_LOG_ERR(broker, "Config update failed: broker not available");
        return http_send_response(broker, sock, HTTP_500_INTERNAL_ERROR, 
                                 CONTENT_TYPE_JSON, "{\"error\":\"Broker not available\"}", 27);
    }
    
    if (!body || body_len == 0) {
        BA_LOG_ERR(broker, "Config update failed: empty body");
        return http_send_response(broker, sock, HTTP_400_BAD_REQUEST, 
                                 CONTENT_TYPE_JSON, "{\"error\":\"Request body is required\"}", 32);
    }
    
    /* Parse body in URL-encoded format: option1=value1&option2=value2 */
    {
        char body_copy[1024];
        int copy_len = (body_len < (int)sizeof(body_copy) - 1) ? body_len : (int)sizeof(body_copy) - 1;
        XMEMCPY(body_copy, body, copy_len);
        body_copy[copy_len] = '\0';
        
        /* Parse key=value pairs */
        char* token = body_copy;
        char* next_token;
        
        while ((token = strtok_r(token, "&", &next_token)) != NULL) {
            char* equals = strchr(token, '=');
            if (equals) {
                *equals = '\0';
                char* key = token;
                char* value = equals + 1;
                
                /* Trim whitespace */
                while (*key == ' ' || *key == '\t') key++;
                while (*value == ' ' || *value == '\t') value++;
                
                /* Find config item and apply */
                const ConfigItemDescriptor* item = find_config_item(key);
                if (item) {
                    int apply_rc = apply_config_value(broker, item, value);
                    if (apply_rc == MQTT_CODE_SUCCESS) {
                        updated_count++;
                    } else {
                        error_len += XSNPRINTF(errors + error_len, sizeof(errors) - error_len, 
                                             "%s=%s invalid; ", key, value);
                        rc = MQTT_CODE_ERROR_BAD_ARG;
                    }
                } else {
                    error_len += XSNPRINTF(errors + error_len, sizeof(errors) - error_len, 
                                         "unknown option '%s'; ", key);
                    rc = MQTT_CODE_ERROR_BAD_ARG;
                }
            }
            
            token = next_token;
        }
    }
    
    /* Build response */
    if (updated_count > 0 && rc == MQTT_CODE_SUCCESS) {
        resp_len = XSNPRINTF(response_body, sizeof(response_body), 
                            "{\"status\":\"success\",\"message\":\"Updated %d configuration(s)\",\"updated\":%d}", 
                            updated_count, updated_count);
        return http_send_response(broker, sock, HTTP_200_OK, CONTENT_TYPE_JSON, 
                                 response_body, resp_len);
    } else if (updated_count > 0 && rc != MQTT_CODE_SUCCESS) {
        /* Partial success - some updated, some failed */
        if (error_len > 0 && error_len < (int)sizeof(errors)) {
            errors[error_len - 2] = '\0'; /* Remove trailing "; " */
        }
        resp_len = XSNPRINTF(response_body, sizeof(response_body), 
                            "{\"status\":\"partial\",\"message\":\"Updated %d configuration(s) with errors\",\"updated\":%d,\"errors\":\"%s\"}", 
                            updated_count, updated_count, errors);
        return http_send_response(broker, sock, HTTP_200_OK, CONTENT_TYPE_JSON, 
                                 response_body, resp_len);
    } else {
        /* All failed */
        if (error_len > 0 && error_len < (int)sizeof(errors)) {
            errors[error_len - 2] = '\0'; /* Remove trailing "; " */
        }
        resp_len = XSNPRINTF(response_body, sizeof(response_body), 
                            "{\"status\":\"error\",\"message\":\"Failed to update configurations\",\"errors\":\"%s\"}", 
                            errors);
        return http_send_response(broker, sock, HTTP_400_BAD_REQUEST, CONTENT_TYPE_JSON, 
                                 response_body, resp_len);
    }
}

/* Handle POST reset endpoint */
static int handle_post_reset(MqttBrokerApiContext* api_ctx, BROKER_SOCKET_T sock)
{
    MqttBroker* broker = api_ctx->broker;
    char response_body[128];
    int resp_len;
    
    if (!broker) {
        return http_send_response(broker, sock, HTTP_500_INTERNAL_ERROR, 
                                 CONTENT_TYPE_JSON, "{\"error\":\"Broker not available\"}", 27);
    }
    
    /* Reset broker statistics */
    MqttBroker_ResetStats(broker);
    
    resp_len = XSNPRINTF(response_body, sizeof(response_body), 
                        "{\"status\":\"success\",\"message\":\"Broker reset completed\"}");
    
    return http_send_response(broker, sock, HTTP_200_OK, CONTENT_TYPE_JSON, 
                             response_body, resp_len);
}

/* Handle POST kick/<client_identifier> endpoint */
static int handle_post_kick(MqttBrokerApiContext* api_ctx, BROKER_SOCKET_T sock, 
                           const char* client_identifier)
{
    MqttBroker* broker = api_ctx->broker;
    char response_body[256];
    int resp_len;
    int rc;
    
    if (!broker || !client_identifier) {
        return http_send_response(broker, sock, HTTP_400_BAD_REQUEST, 
                                 CONTENT_TYPE_JSON, "{\"error\":\"Invalid client identifier\"}", 33);
    }
    
    /* Kick the specified client using the broker's kick function */
    rc = BrokerKick_Client(broker, client_identifier);
    
    if (rc == MQTT_CODE_SUCCESS) {
        resp_len = XSNPRINTF(response_body, sizeof(response_body), 
                            "{\"status\":\"success\",\"message\":\"Client '%s' kicked\"}", client_identifier);
        return http_send_response(broker, sock, HTTP_200_OK, CONTENT_TYPE_JSON, 
                                 response_body, resp_len);
    } else if (rc == MQTT_CODE_ERROR_NOT_FOUND) {
        resp_len = XSNPRINTF(response_body, sizeof(response_body), 
                            "{\"error\":\"Client '%s' not found\"}", client_identifier);
        return http_send_response(broker, sock, HTTP_404_NOT_FOUND, CONTENT_TYPE_JSON, 
                                 response_body, resp_len);
    } else {
        resp_len = XSNPRINTF(response_body, sizeof(response_body), 
                            "{\"error\":\"Failed to kick client: %d\"}", rc);
        return http_send_response(broker, sock, HTTP_500_INTERNAL_ERROR, CONTENT_TYPE_JSON, 
                                 response_body, resp_len);
    }
}

/* Main HTTP request handler */
static int handle_http_request(MqttBrokerApiContext* api_ctx, BROKER_SOCKET_T sock, 
                              byte* buffer, int len)
{
    char method[HTTP_MAX_METHOD_LEN];
    char path[HTTP_MAX_PATH_LEN];
    char query[HTTP_MAX_QUERY_LEN];
    char auth_header[HTTP_MAX_HEADER_LEN];
    char* body_start;
    int body_len = 0;
    int rc;
    
    if (!api_ctx || !buffer || len <= 0) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* Get client IP address */
    char client_ip[64] = "unknown";
#ifdef WOLFMQTT_BROKER
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    if (getpeername(sock, (struct sockaddr*)&addr, &addr_len) == 0) {
        inet_ntop(AF_INET, &addr.sin_addr, client_ip, sizeof(client_ip));
    }
#endif
    
    /* Parse HTTP request */
    rc = parse_http_request(api_ctx->broker, buffer, len,
                           method, sizeof(method),
                           path, sizeof(path),
                           query, sizeof(query),
                           auth_header, sizeof(auth_header));
    
    if (rc != MQTT_CODE_SUCCESS) {
        LOG_API_RESPONSE(api_ctx->broker, 400, method, path, query, client_ip);
        return http_send_response(api_ctx->broker, sock, HTTP_400_BAD_REQUEST, 
                                 CONTENT_TYPE_TEXT, "Bad Request", 11);
    }
    
    /* Log request details at DEBUG level */
    BA_LOG_DBG(api_ctx->broker, "API Request %s %s%s%s from=%s",
              method, path, (query[0] ? "?" : ""), query, client_ip);
    
    /* Validate API token if configured */
    if (!validate_api_token(api_ctx, auth_header)) {
        LOG_API_RESPONSE(api_ctx->broker, 401, method, path, query, client_ip);
        return http_send_response(api_ctx->broker, sock, HTTP_401_UNAUTHORIZED, 
                                 CONTENT_TYPE_TEXT, "Unauthorized", 12);
    }
    
    /* Find body start (after double CRLF) */
    body_start = strnstr((char*)buffer, "\r\n\r\n", len);
    if (body_start) {
        body_start += 4; /* Skip \r\n\r\n */
        body_len = len - (int)(body_start - (char*)buffer);
    }
    
    /* Route request based on method and path */
    if (XSTRCMP(method, "GET") == 0) {
        if (XSTRCMP(path, "/api/stats") == 0) {
            rc = handle_get_stats(api_ctx, sock);
            LOG_API_RESPONSE(api_ctx->broker, 200, method, path, "", client_ip);
            return rc;
        } else if (XSTRCMP(path, "/api/clients") == 0) {
            rc = handle_get_clients(api_ctx, sock);
            LOG_API_RESPONSE(api_ctx->broker, 200, method, path, "", client_ip);
            return rc;
        } else if (XSTRNCMP(path, "/api/topics/", 12) == 0) {
            rc = handle_get_topics(api_ctx, sock, path + 12);
            LOG_API_RESPONSE(api_ctx->broker, 200, method, path, "", client_ip);
            return rc;
        } else if (XSTRCMP(path, "/api/configs") == 0) {
            rc = handle_get_options(api_ctx, sock);
            LOG_API_RESPONSE(api_ctx->broker, 200, method, path, "", client_ip);
            return rc;
        } else {
            LOG_API_RESPONSE(api_ctx->broker, 404, method, path, query, client_ip);
            return http_send_response(api_ctx->broker, sock, HTTP_404_NOT_FOUND, 
                                     CONTENT_TYPE_TEXT, "Not Found", 9);
        }
    } else if (XSTRCMP(method, "POST") == 0) {
        if (XSTRNCMP(path, "/api/publish/", 13) == 0) {
            rc = handle_post_publish(api_ctx, sock, path + 13, query, body_start, body_len);
            LOG_API_RESPONSE(api_ctx->broker, 200, method, path, "", client_ip);
            return rc;
        } else if (XSTRCMP(path, "/api/configs") == 0) {
            rc = handle_post_configs(api_ctx, sock, body_start, body_len);
            LOG_API_RESPONSE(api_ctx->broker, 200, method, path, "", client_ip);
            return rc;
        } else if (XSTRCMP(path, "/api/reset") == 0) {
            rc = handle_post_reset(api_ctx, sock);
            LOG_API_RESPONSE(api_ctx->broker, 200, method, path, "", client_ip);
            return rc;
        } else if (XSTRNCMP(path, "/api/kick/", 10) == 0) {
            rc = handle_post_kick(api_ctx, sock, path + 10);
            LOG_API_RESPONSE(api_ctx->broker, 200, method, path, "", client_ip);
            return rc;
        } else {
            LOG_API_RESPONSE(api_ctx->broker, 404, method, path, query, client_ip);
            return http_send_response(api_ctx->broker, sock, HTTP_404_NOT_FOUND, 
                                     CONTENT_TYPE_TEXT, "Not Found", 9);
        }
    } else {
        LOG_API_RESPONSE(api_ctx->broker, 400, method, path, query, client_ip);
        return http_send_response(api_ctx->broker, sock, HTTP_400_BAD_REQUEST, 
                                 CONTENT_TYPE_TEXT, "Method Not Allowed", 18);
    }
}

/* Accept and process API connections */
static int api_accept_connection(MqttBrokerApiContext* api_ctx)
{
    MqttBroker* broker = api_ctx->broker;
    BROKER_SOCKET_T new_sock = BROKER_SOCKET_INVALID;
    int rc;
    byte recv_buffer[HTTP_BUFFER_SIZE];
    int bytes_read;
    
    if (!broker || api_ctx->api_listen_sock == BROKER_SOCKET_INVALID) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* Accept new connection */
    rc = broker->net.accept(broker->net.ctx, api_ctx->api_listen_sock, &new_sock);
    if (rc != MQTT_CODE_SUCCESS || new_sock == BROKER_SOCKET_INVALID) {
        return MQTT_CODE_CONTINUE; /* No connection available */
    }
    
    /* Read HTTP request */
    bytes_read = broker->net.read(broker->net.ctx, new_sock, recv_buffer, sizeof(recv_buffer), broker->timeout_ms);
    if (bytes_read <= 0) {
        BA_LOG_ERR(broker, "Failed to read HTTP request: %d", bytes_read);
        broker->net.close(broker->net.ctx, new_sock);
        return MQTT_CODE_ERROR_NETWORK;
    }
    
    /* Process HTTP request */
    rc = handle_http_request(api_ctx, new_sock, recv_buffer, bytes_read);
    
    /* Close connection */
    broker->net.close(broker->net.ctx, new_sock);
    
    return (rc >= 0) ? MQTT_CODE_SUCCESS : rc;
}

/* Initialize API context */
int MqttBrokerApi_Init(MqttBroker* broker, MqttBrokerApiContext* api_ctx, word16 port)
{
    if (!broker || !api_ctx) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    XMEMSET(api_ctx, 0, sizeof(MqttBrokerApiContext));
    api_ctx->broker = broker;
    api_ctx->api_port = port;
    api_ctx->use_api = 1;
    
    /* Copy API token from broker configuration */
    if (broker && broker->api_token[0] != '\0') {
        XSTRNCPY(api_ctx->api_token, broker->api_token, sizeof(api_ctx->api_token) - 1);
        api_ctx->api_token[sizeof(api_ctx->api_token) - 1] = '\0';
    } else {
        /* Use default token if broker doesn't have one configured */
        XSTRNCPY(api_ctx->api_token, "22182666", sizeof(api_ctx->api_token) - 1);
        api_ctx->api_token[sizeof(api_ctx->api_token) - 1] = '\0';
    }
    
    /* Start listening on API port */
    if (broker->net.listen) {
        int rc = broker->net.listen(broker->net.ctx, &api_ctx->api_listen_sock, port, 5);
        if (rc != MQTT_CODE_SUCCESS) {
            BA_LOG_ERR(broker, "Failed to start API listener on port %u: %d", port, rc);
            api_ctx->use_api = 0;
            return rc;
        }
    } else {
        api_ctx->use_api = 0;
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    return MQTT_CODE_SUCCESS;
}

/* Set API token for authentication */
int MqttBrokerApi_SetToken(MqttBrokerApiContext* api_ctx, const char* token)
{
    if (!api_ctx || !token) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* Validate token length */
    if (XSTRLEN(token) < MIN_API_TOKEN_LENGTH) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* Copy token */
    XSTRNCPY(api_ctx->api_token, token, sizeof(api_ctx->api_token) - 1);
    api_ctx->api_token[sizeof(api_ctx->api_token) - 1] = '\0';
    
    BA_LOG_INFO(api_ctx->broker, "API token set (length: %zu)", XSTRLEN(token));
    
    return MQTT_CODE_SUCCESS;
}

/* Process API events (call from main broker loop) */
int MqttBrokerApi_Process(MqttBrokerApiContext* api_ctx)
{
    if (!api_ctx || !api_ctx->use_api) {
        return MQTT_CODE_SUCCESS;
    }
    
    return api_accept_connection(api_ctx);
}

/* Cleanup API resources */
void MqttBrokerApi_Free(MqttBrokerApiContext* api_ctx)
{
    if (!api_ctx) {
        return;
    }
    
    if (api_ctx->api_listen_sock != BROKER_SOCKET_INVALID && api_ctx->broker) {
        api_ctx->broker->net.close(api_ctx->broker->net.ctx, api_ctx->api_listen_sock);
        api_ctx->api_listen_sock = BROKER_SOCKET_INVALID;
    }
    
    XMEMSET(api_ctx, 0, sizeof(MqttBrokerApiContext));
}
