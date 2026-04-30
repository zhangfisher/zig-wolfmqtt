/* mqtt_topic_alias.h
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

/* MQTT 5.0 主题别名自动管理模块
 * 
 * 提供默认的主题别名自动映射机制，同时支持外部自定义策略
 */

#ifndef WOLFMQTT_TOPIC_ALIAS_H
#define WOLFMQTT_TOPIC_ALIAS_H

#ifdef __cplusplus
    extern "C" {
#endif

#include "wolfmqtt/mqtt_types.h"

#ifdef WOLFMQTT_V5

/* 前向声明 */
struct _MqttClient;

/* 主题别名条目 */
typedef struct MqttTopicAliasEntry {
    word16      alias;          /* 别名 ID (1-65535) */
    char*       topic_name;     /* 主题名称 */
    word16      topic_len;      /* 主题长度 */
    word32      last_used;      /* 最后使用时间戳（用于 LRU） */
    byte        active;         /* 是否激活 */
} MqttTopicAliasEntry;

/* 主题别名管理器配置 */
typedef struct MqttTopicAliasConfig {
    word16              max_aliases;        /* 最大别名数量 */
    byte                auto_assign;        /* 自动分配别名 */
    byte                use_lru;            /* 使用 LRU 淘汰策略 */
    word32              ttl_seconds;        /* 别名生存时间（0=永久） */
} MqttTopicAliasConfig;

/* 主题别名管理器 */
typedef struct MqttTopicAliasManager {
    MqttTopicAliasEntry*    entries;        /* 别名表 */
    word16                  max_entries;    /* 最大条目数 */
    word16                  next_alias;     /* 下一个可用别名 ID */
    word32                  timestamp;      /* 当前时间戳 */
    MqttTopicAliasConfig    config;         /* 配置 */
    
    /* 回调函数 - 允许外部自定义行为 */
    int (*on_alias_created)(word16 alias, const char* topic, void* ctx);
    int (*on_alias_removed)(word16 alias, const char* topic, void* ctx);
    void* callback_ctx;                     /* 回调上下文 */
} MqttTopicAliasManager;

/*
 * API 函数
 */

/*! \brief      初始化主题别名管理器
 *  \param      manager     指向管理器的指针
 *  \param      config      配置参数（可为 NULL 使用默认值）
 *  \param      buffer      预分配的内存缓冲区（可选）
 *  \param      buffer_size 缓冲区大小
 *  \return     MQTT_CODE_SUCCESS 或错误码
 */
WOLFMQTT_API int MqttTopicAlias_Init(
    MqttTopicAliasManager* manager,
    const MqttTopicAliasConfig* config,
    void* buffer,
    word32 buffer_size);

/*! \brief      清理主题别名管理器
 *  \param      manager     指向管理器的指针
 */
WOLFMQTT_API void MqttTopicAlias_DeInit(MqttTopicAliasManager* manager);

/*! \brief      注册或获取主题别名
 *  \details    如果主题已有别名，返回现有别名；否则创建新别名
 *  \param      manager     指向管理器的指针
 *  \param      topic_name  主题名称
 *  \param      topic_len   主题长度
 *  \param      alias_out   输出：分配的别名 ID
 *  \return     MQTT_CODE_SUCCESS 或错误码
 */
WOLFMQTT_API int MqttTopicAlias_Register(
    MqttTopicAliasManager* manager,
    const char* topic_name,
    word16 topic_len,
    word16* alias_out);

/*! \brief      根据别名查找主题名称
 *  \param      manager     指向管理器的指针
 *  \param      alias       别名 ID
 *  \param      topic_out   输出：主题名称指针
 *  \param      len_out     输出：主题长度
 *  \return     MQTT_CODE_SUCCESS 或未找到
 */
WOLFMQTT_API int MqttTopicAlias_Lookup(
    MqttTopicAliasManager* manager,
    word16 alias,
    const char** topic_out,
    word16* len_out);

/*! \brief      移除主题别名
 *  \param      manager     指向管理器的指针
 *  \param      alias       要移除的别名 ID
 *  \return     MQTT_CODE_SUCCESS 或错误码
 */
WOLFMQTT_API int MqttTopicAlias_Remove(
    MqttTopicAliasManager* manager,
    word16 alias);

/*! \brief      清空所有别名
 *  \param      manager     指向管理器的指针
 */
WOLFMQTT_API void MqttTopicAlias_Clear(MqttTopicAliasManager* manager);

/*! \brief      设置回调函数
 *  \param      manager             指向管理器的指针
 *  \param      on_created_cb       别名创建时的回调
 *  \param      on_removed_cb       别名移除时的回调
 *  \param      ctx                 回调上下文
 */
WOLFMQTT_API void MqttTopicAlias_SetCallbacks(
    MqttTopicAliasManager* manager,
    int (*on_created_cb)(word16, const char*, void*),
    int (*on_removed_cb)(word16, const char*, void*),
    void* ctx);

/*! \brief      更新管理器时间戳（用于 LRU 和 TTL）
 *  \param      manager     指向管理器的指针
 *  \param      timestamp   当前时间戳（秒）
 */
WOLFMQTT_API void MqttTopicAlias_UpdateTime(
    MqttTopicAliasManager* manager,
    word32 timestamp);

/*! \brief      清理过期的别名（基于 TTL）
 *  \param      manager     指向管理器的指针
 *  \return     清理的别名数量
 */
WOLFMQTT_API int MqttTopicAlias_CleanupExpired(MqttTopicAliasManager* manager);

/*! \brief      获取统计信息
 *  \param      manager         指向管理器的指针
 *  \param      active_count    输出：活跃别名数量
 *  \param      total_created   输出：总共创建的别名数
 */
WOLFMQTT_API void MqttTopicAlias_GetStats(
    MqttTopicAliasManager* manager,
    word16* active_count,
    word32* total_created);

/*
 * 便捷宏 - 简化常用操作
 */

/* 使用默认配置初始化 */
#define MqttTopicAlias_InitDefault(mgr) \
    MqttTopicAlias_Init((mgr), NULL, NULL, 0)

/* 检查别名是否有效 */
#define MqttTopicAlias_IsValid(alias) ((alias) > 0 && (alias) <= 0xFFFF)

#endif /* WOLFMQTT_V5 */

#ifdef __cplusplus
    } /* extern "C" */
#endif

#endif /* WOLFMQTT_TOPIC_ALIAS_H */
