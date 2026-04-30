/* mqtt_topic_alias.c
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

/* MQTT 5.0 主题别名自动管理实现 */

#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#include "wolfmqtt/mqtt_topic_alias.h"

#ifdef WOLFMQTT_V5

#include <string.h>
#include <stdlib.h>

#ifndef WOLFMQTT_TOPIC_ALIAS_DEFAULT_MAX
#define WOLFMQTT_TOPIC_ALIAS_DEFAULT_MAX 10
#endif

/* 内部辅助函数 */

/* 查找主题是否已有别名 */
static int find_existing_alias(MqttTopicAliasManager* manager,
                                const char* topic_name,
                                word16 topic_len,
                                word16* alias_out)
{
    word16 i;
    
    for (i = 0; i < manager->max_entries; i++) {
        MqttTopicAliasEntry* entry = &manager->entries[i];
        
        if (entry->active && 
            entry->topic_len == topic_len &&
            strncmp(entry->topic_name, topic_name, topic_len) == 0) {
            
            /* 找到匹配的条目 */
            *alias_out = entry->alias;
            entry->last_used = manager->timestamp; /* 更新使用时间 */
            return 1;
        }
    }
    
    return 0; /* 未找到 */
}

/* 查找空闲条目 */
static int find_free_entry(MqttTopicAliasManager* manager)
{
    word16 i;
    
    for (i = 0; i < manager->max_entries; i++) {
        if (!manager->entries[i].active) {
            return i;
        }
    }
    
    return -1; /* 没有空闲条目 */
}

/* LRU 策略：找到最少使用的条目 */
static int find_lru_entry(MqttTopicAliasManager* manager)
{
    word16 i;
    int lru_index = 0;
    word32 min_time = 0xFFFFFFFF;
    
    for (i = 0; i < manager->max_entries; i++) {
        if (manager->entries[i].active && 
            manager->entries[i].last_used < min_time) {
            min_time = manager->entries[i].last_used;
            lru_index = i;
        }
    }
    
    return lru_index;
}

/* 复制主题名称 */
static char* duplicate_topic(const char* topic, word16 len)
{
    char* dup = (char*)malloc(len + 1);
    if (dup) {
        memcpy(dup, topic, len);
        dup[len] = '\0';
    }
    return dup;
}

/*
 * 公开 API 实现
 */

int MqttTopicAlias_Init(MqttTopicAliasManager* manager,
                         const MqttTopicAliasConfig* config,
                         void* buffer,
                         word32 buffer_size)
{
    if (!manager) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    memset(manager, 0, sizeof(MqttTopicAliasManager));
    
    /* 设置默认配置 */
    manager->config.max_aliases = WOLFMQTT_TOPIC_ALIAS_DEFAULT_MAX;
    manager->config.auto_assign = 1;
    manager->config.use_lru = 1;
    manager->config.ttl_seconds = 0; /* 永久 */
    
    /* 应用用户配置 */
    if (config) {
        if (config->max_aliases > 0) {
            manager->config.max_aliases = config->max_aliases;
        }
        manager->config.auto_assign = config->auto_assign;
        manager->config.use_lru = config->use_lru;
        manager->config.ttl_seconds = config->ttl_seconds;
    }
    
    /* 分配条目数组 */
    manager->max_entries = manager->config.max_aliases;
    
    if (buffer && buffer_size >= sizeof(MqttTopicAliasEntry) * manager->max_entries) {
        /* 使用提供的缓冲区 */
        manager->entries = (MqttTopicAliasEntry*)buffer;
    } else {
        /* 动态分配 */
        manager->entries = (MqttTopicAliasEntry*)malloc(
            sizeof(MqttTopicAliasEntry) * manager->max_entries);
        if (!manager->entries) {
            return MQTT_CODE_ERROR_MEMORY;
        }
    }
    
    memset(manager->entries, 0, 
           sizeof(MqttTopicAliasEntry) * manager->max_entries);
    
    manager->next_alias = 1; /* 别名从 1 开始 */
    manager->timestamp = 0;
    manager->callback_ctx = NULL;
    manager->on_alias_created = NULL;
    manager->on_alias_removed = NULL;
    
    return MQTT_CODE_SUCCESS;
}

void MqttTopicAlias_DeInit(MqttTopicAliasManager* manager)
{
    word16 i;
    
    if (!manager || !manager->entries) {
        return;
    }
    
    /* 释放所有主题名称 */
    for (i = 0; i < manager->max_entries; i++) {
        if (manager->entries[i].topic_name) {
            free(manager->entries[i].topic_name);
            manager->entries[i].topic_name = NULL;
        }
    }
    
    /* 如果 entries 是动态分配的，释放它 */
    /* 注意：这里假设如果不是外部提供的缓冲区，就是动态分配的 */
    /* 实际使用中可能需要一个标志来区分 */
    free(manager->entries);
    manager->entries = NULL;
    
    memset(manager, 0, sizeof(MqttTopicAliasManager));
}

int MqttTopicAlias_Register(MqttTopicAliasManager* manager,
                             const char* topic_name,
                             word16 topic_len,
                             word16* alias_out)
{
    int existing;
    word16 alias;
    
    if (!manager || !manager->entries || !topic_name || !alias_out) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    /* 检查主题是否已有别名 */
    existing = find_existing_alias(manager, topic_name, topic_len, &alias);
    if (existing) {
        *alias_out = alias;
        return MQTT_CODE_SUCCESS;
    }
    
    /* 需要创建新别名 */
    int entry_index;
    
    if (manager->config.auto_assign) {
        /* 自动分配模式 */
        
        /* 查找空闲条目 */
        entry_index = find_free_entry(manager);
        
        /* 如果没有空闲条目，使用 LRU 淘汰 */
        if (entry_index < 0) {
            if (manager->config.use_lru) {
                entry_index = find_lru_entry(manager);
                
                /* 移除旧别名 */
                MqttTopicAliasEntry* old_entry = &manager->entries[entry_index];
                if (manager->on_alias_removed) {
                    manager->on_alias_removed(old_entry->alias, 
                                             old_entry->topic_name,
                                             manager->callback_ctx);
                }
                
                free(old_entry->topic_name);
                old_entry->topic_name = NULL;
                old_entry->active = 0;
            } else {
                return MQTT_CODE_ERROR_BUFFER; /* 表已满 */
            }
        }
        
        /* 分配新别名 ID */
        alias = manager->next_alias++;
        if (manager->next_alias > 0xFFFF) {
            manager->next_alias = 1; /* 回绕 */
        }
        
        /* 填充条目 */
        MqttTopicAliasEntry* entry = &manager->entries[entry_index];
        entry->alias = alias;
        entry->topic_name = duplicate_topic(topic_name, topic_len);
        if (!entry->topic_name) {
            return MQTT_CODE_ERROR_MEMORY;
        }
        entry->topic_len = topic_len;
        entry->last_used = manager->timestamp;
        entry->active = 1;
        
        /* 调用创建回调 */
        if (manager->on_alias_created) {
            manager->on_alias_created(alias, topic_name, manager->callback_ctx);
        }
        
        *alias_out = alias;
        return MQTT_CODE_SUCCESS;
        
    } else {
        /* 手动分配模式 - 返回错误，让应用层决定 */
        return MQTT_CODE_ERROR_NOT_IMPLEMENTED;
    }
}

int MqttTopicAlias_Lookup(MqttTopicAliasManager* manager,
                           word16 alias,
                           const char** topic_out,
                           word16* len_out)
{
    word16 i;
    
    if (!manager || !manager->entries || !alias || !topic_out || !len_out) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    for (i = 0; i < manager->max_entries; i++) {
        MqttTopicAliasEntry* entry = &manager->entries[i];
        
        if (entry->active && entry->alias == alias) {
            *topic_out = entry->topic_name;
            *len_out = entry->topic_len;
            entry->last_used = manager->timestamp; /* 更新使用时间 */
            return MQTT_CODE_SUCCESS;
        }
    }
    
    return MQTT_CODE_ERROR_NOT_FOUND;
}

int MqttTopicAlias_Remove(MqttTopicAliasManager* manager, word16 alias)
{
    word16 i;
    
    if (!manager || !manager->entries || !alias) {
        return MQTT_CODE_ERROR_BAD_ARG;
    }
    
    for (i = 0; i < manager->max_entries; i++) {
        MqttTopicAliasEntry* entry = &manager->entries[i];
        
        if (entry->active && entry->alias == alias) {
            /* 调用移除回调 */
            if (manager->on_alias_removed) {
                manager->on_alias_removed(alias, entry->topic_name, 
                                         manager->callback_ctx);
            }
            
            free(entry->topic_name);
            entry->topic_name = NULL;
            entry->active = 0;
            
            return MQTT_CODE_SUCCESS;
        }
    }
    
    return MQTT_CODE_ERROR_NOT_FOUND;
}

void MqttTopicAlias_Clear(MqttTopicAliasManager* manager)
{
    word16 i;
    
    if (!manager || !manager->entries) {
        return;
    }
    
    for (i = 0; i < manager->max_entries; i++) {
        MqttTopicAliasEntry* entry = &manager->entries[i];
        
        if (entry->active) {
            /* 调用移除回调 */
            if (manager->on_alias_removed) {
                manager->on_alias_removed(entry->alias, entry->topic_name,
                                         manager->callback_ctx);
            }
            
            free(entry->topic_name);
            entry->topic_name = NULL;
            entry->active = 0;
        }
    }
    
    manager->next_alias = 1;
}

void MqttTopicAlias_SetCallbacks(MqttTopicAliasManager* manager,
                                  int (*on_created_cb)(word16, const char*, void*),
                                  int (*on_removed_cb)(word16, const char*, void*),
                                  void* ctx)
{
    if (manager) {
        manager->on_alias_created = on_created_cb;
        manager->on_alias_removed = on_removed_cb;
        manager->callback_ctx = ctx;
    }
}

void MqttTopicAlias_UpdateTime(MqttTopicAliasManager* manager, word32 timestamp)
{
    if (manager) {
        manager->timestamp = timestamp;
    }
}

int MqttTopicAlias_CleanupExpired(MqttTopicAliasManager* manager)
{
    int cleaned = 0;
    word16 i;
    
    if (!manager || !manager->entries || manager->config.ttl_seconds == 0) {
        return 0; /* TTL 未启用 */
    }
    
    for (i = 0; i < manager->max_entries; i++) {
        MqttTopicAliasEntry* entry = &manager->entries[i];
        
        if (entry->active) {
            word32 age = manager->timestamp - entry->last_used;
            
            if (age > manager->config.ttl_seconds) {
                /* 过期了，移除 */
                if (manager->on_alias_removed) {
                    manager->on_alias_removed(entry->alias, entry->topic_name,
                                             manager->callback_ctx);
                }
                
                free(entry->topic_name);
                entry->topic_name = NULL;
                entry->active = 0;
                cleaned++;
            }
        }
    }
    
    return cleaned;
}

void MqttTopicAlias_GetStats(MqttTopicAliasManager* manager,
                              word16* active_count,
                              word32* total_created)
{
    word16 count = 0;
    word16 i;
    
    if (!manager || !manager->entries) {
        if (active_count) *active_count = 0;
        if (total_created) *total_created = 0;
        return;
    }
    
    for (i = 0; i < manager->max_entries; i++) {
        if (manager->entries[i].active) {
            count++;
        }
    }
    
    if (active_count) *active_count = count;
    if (total_created) *total_created = manager->next_alias - 1;
}

#endif /* WOLFMQTT_V5 */
