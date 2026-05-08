# Broker 配置项 - 快速参考卡

## API端点
```
POST /api/configs
Content-Type: application/x-www-form-urlencoded
Authorization: Basic <token>
```

## 32个可配置项速查表

### 🔧 功能开关 (4个)
| 配置项 | 值 | 默认 |
|--------|-----|------|
| `enable_stats` | true/false | 1 |
| `enable_api` | true/false | 1 |
| `use_tls` | true/false | 0 |
| `use_ws` | true/false | 0 |

### 📊 日志监控 (2个)
| 配置项 | 范围 | 默认 |
|--------|------|------|
| `log_level` | 0-5 | 1 |
| `stats_interval` | 0-3600秒 | 20 |

### 🌐 网络端口 (6个)
| 配置项 | 范围 | 默认 |
|--------|------|------|
| `port` | 1-65535 | 1883 |
| `api_port` | 1-65535 | 8081 |
| `port_tls` | 1-65535 | 8883 |
| `port_ws` | 1-65535 | 8080 |
| `timeout_ms` | 100-60000 | 1000 |
| `listen_backlog` | 1-1024 | 128 |

### 💾 缓冲区 (2个)
| 配置项 | 范围 | 默认 |
|--------|------|------|
| `rx_buf_sz` | 256-65535 | 4096 |
| `tx_buf_sz` | 256-65535 | 4096 |

### 👥 容量限制 (4个)
| 配置项 | 范围 | 默认 |
|--------|------|------|
| `max_clients` | 0-1000 | 0 |
| `max_subs` | 0-10000 | 0 |
| `max_retained` | 0-1000 | 0 |
| `max_pending_wills` | 0-100 | 0 |

*注：0=无限制*

### 🔐 认证安全 (3个)
| 配置项 | 要求 | 默认 |
|--------|------|------|
| `username` | 任意字符串 | NULL |
| `password` | 任意字符串 | NULL |
| `api_token` | ≥12字符 | "" |

### ⚙️ MQTT 5 (2个)
| 配置项 | 范围 | 默认 |
|--------|------|------|
| `max_packet_size` | 0-268435455 | 0 |
| `topic_alias_max` | 0-65535 | 0 |

### 🔄 会话持久化 (1个)
| 配置项 | 范围 | 默认 |
|--------|------|------|
| `default_session_expiry_interval` | 0-4294967295秒 | 0 |

### 📝 负载限制 (2个)
| 配置项 | 范围 | 默认 |
|--------|------|------|
| `max_payload_len` | 0-65535 | 4096 |
| `max_will_payload_len` | 0-65535 | 256 |

### 🔒 TLS配置 (1个)
| 配置项 | 值 | 默认 |
|--------|-----|------|
| `tls_version` | 0/12/13 | 0 |

---

## 常用命令示例

### 基础配置
```bash
# 调整日志和统计
curl -X POST -H "Authorization: Basic token" \
  -d "log_level=2&stats_interval=60" \
  http://localhost:8081/api/configs

# 设置容量限制
curl -X POST -H "Authorization: Basic token" \
  -d "max_clients=100&max_subs=500" \
  http://localhost:8081/api/configs
```

### 性能优化
```bash
# 大缓冲区配置
curl -X POST -H "Authorization: Basic token" \
  -d "rx_buf_sz=8192&tx_buf_sz=8192&timeout_ms=5000" \
  http://localhost:8081/api/configs
```

### 安全加固
```bash
# 启用认证
curl -X POST -H "Authorization: Basic token" \
  -d "username=admin&password=pass123&api_token=StrongToken123" \
  http://localhost:8081/api/configs
```

### 批量配置
```bash
# 生产环境一键配置
curl -X POST -H "Authorization: Basic token" \
  -d "log_level=2&stats_interval=60&timeout_ms=5000&max_clients=100&max_subs=500&rx_buf_sz=8192&tx_buf_sz=8192" \
  http://localhost:8081/api/configs
```

---

## 响应格式

### 成功
```json
{"status":"success","message":"Updated 5 configuration(s)","updated":5}
```

### 部分成功
```json
{"status":"partial","message":"Updated 3 configuration(s) with errors","updated":3,"errors":"..."}
```

### 失败
```json
{"status":"error","message":"Failed to update configurations","errors":"..."}
```

---

## 重要提示

⚠️ **立即生效**: log_level, stats_interval, max_*, timeout_ms, buffers  
⚠️ **需重启**: port, api_port, use_tls, use_ws, listen_backlog  
⚠️ **内存计算**: 总内存 ≈ (rx + tx) × max_clients  
⚠️ **静态模式**: 受编译时数组大小限制  
⚠️ **0表示无限制**: max_* 字段设为0表示不主动限制  

---

## 完整文档

查看 `API_CONFIGS_COMPLETE_REFERENCE.md` 获取详细说明和更多示例。
