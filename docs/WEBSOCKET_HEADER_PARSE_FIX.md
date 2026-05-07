# WebSocket 头部解析修复 - 按行搜索

## ❌ 问题描述

WebSocket 握手时，`ws_find_header` 函数无法正确找到 "Upgrade:" 头部，导致握手失败。

**错误日志**：
```
[DEBUG] ws_find_header: found 'Upgrade:' at offset 113
[DEBUG] ws_find_header: not followed by ':' (char=' ')
[DEBUG] Parse request: Upgrade header not found
[ERROR] WebSocket handshake failed on sock=5 rc=-8
```

---

## 🔍 根本原因

### 原始实现的问题

原始的 `ws_find_header` 使用 `strstr` 在整个 HTTP 请求字符串中搜索字段名：

```c
while ((pos = strstr(pos, field_name)) != NULL) {
    if (pos[field_len] == ':') {
        // 提取值
    }
    pos++;
}
```

**问题场景**：

HTTP 请求内容：
```
GET /mqtt HTTP/1.1
Sec-WebSocket-Version: 13
Sec-WebSocket-Key: vQiXFTgjcgrdbv96EBpKWg==
Connection: Upgrade
Upgrade: websocket
Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits
```

当搜索 "Upgrade:" 时：
1. `strstr` 首先找到 "Connection: **Upgrade**" 中的 "Upgrade"（offset 113）
2. 检查 `pos[field_len]`（即 "Upgrade" 后面的字符）
3. 发现是空格 `' '` 而不是冒号 `':'`
4. 继续搜索，但已经错过了真正的 "Upgrade: websocket" 行

**根本原因**：`strstr` 会匹配任何位置的子串，包括其他头部字段的**值**中包含的文本。

---

## ✅ 解决方案

### 按行搜索

修改 `ws_find_header` 函数，逐行解析 HTTP 头部，确保只匹配**行首**的字段名：

```c
static const char* ws_find_header(const char* headers, 
                                   const char* field_name,
                                   char* value_buf,
                                   word32 value_buf_size)
{
    const char* line_start = headers;
    const char* line_end;
    word32 field_len = (word32)strlen(field_name);
    
    /* 按行搜索 */
    while ((line_end = strstr(line_start, "\r\n")) != NULL || 
           (line_end = strstr(line_start, "\n")) != NULL) {
        word32 line_len = (word32)(line_end - line_start);
        
        /* 检查该行是否以 field_name 开头 */
        if (line_len > field_len && 
            XSTRNCMP(line_start, field_name, field_len) == 0 &&
            line_start[field_len] == ':') {
            
            const char* value_start = line_start + field_len + 1;
            
            /* 跳过空格 */
            while (*value_start == ' ' || *value_start == '\t') {
                value_start++;
            }
            
            word32 value_len = (word32)(line_end - value_start);
            if (value_len >= value_buf_size) {
                value_len = value_buf_size - 1;
            }
            XMEMCPY(value_buf, value_start, value_len);
            value_buf[value_len] = '\0';
            
            return value_buf;
        }
        
        /* 移动到下一行 */
        line_start = line_end + 1;
        if (*line_start == '\n') {
            line_start++;  /* 处理 \r\n */
        }
    }
    
    return NULL;
}
```

---

## 📊 修复前后对比

### 修复前（❌ 错误）

```
搜索 "Upgrade:"
  ↓
strstr 找到 "Connection: Upgrade" 中的 "Upgrade"
  ↓
检查下一个字符 → 是空格 ' '
  ↓
不是冒号，继续搜索
  ↓
错过真正的 "Upgrade: websocket" 行
  ↓
返回 NULL → 握手失败 💥
```

### 修复后（✅ 正确）

```
按行解析：
  Line 1: "GET /mqtt HTTP/1.1" → 不匹配
  Line 2: "Sec-WebSocket-Version: 13" → 不匹配
  Line 3: "Sec-WebSocket-Key: ..." → 不匹配
  Line 4: "Connection: Upgrade" → 不匹配（不以 "Upgrade:" 开头）
  Line 5: "Upgrade: websocket" → ✅ 匹配！
    - 行首是 "Upgrade:"
    - 提取值 "websocket"
    - 返回成功 ✅
```

---

## 🎯 关键改进

### 1. 行级别匹配

- **之前**: 在整个字符串中搜索子串
- **现在**: 逐行检查，只匹配行首的字段名

### 2. 避免误匹配

- **之前**: 可能匹配到头部值中包含的文本
- **现在**: 只匹配完整的头部字段名（后面紧跟冒号）

### 3. 兼容性

- 支持 `\r\n` 换行符（标准 HTTP）
- 支持 `\n` 换行符（某些客户端）

---

## 📝 测试验证

### 预期日志输出

```
[INFO ] New WebSocket connection on sock=5
[INFO] WebSocket received 263 bytes for handshake on sock=5
[DEBUG] HTTP Request (first 200 bytes):
GET /mqtt HTTP/1.1
Sec-WebSocket-Version: 13
Sec-WebSocket-Key: vQiXFTgjcgrdbv96EBpKWg==
Connection: Upgrade
Upgrade: websocket
Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits

[DEBUG] Parse request: GET method OK
[DEBUG] ws_find_header: searching for 'Upgrade:'
[DEBUG] ws_find_header: found in line Upgrade: websocket, value = 'websocket'
[DEBUG] Parse request: Upgrade header OK (websocket)
[DEBUG] ws_find_header: searching for 'Connection:'
[DEBUG] ws_find_header: found in line Connection: Upgrade, value = 'Upgrade'
[DEBUG] Parse request: Connection header OK (Upgrade)
[DEBUG] ws_find_header: searching for 'Sec-WebSocket-Key:'
[DEBUG] ws_find_header: found in line Sec-WebSocket-Key: vQiXFTgjcgrdbv96EBpKWg==, value = 'vQiXFTgjcgrdbv96EBpKWg=='
[DEBUG] Parse request: Sec-WebSocket-Key found: 'vQiXFTgjcgrdbv96EBpKWg==' (len=24)
[DEBUG] Parse request: SUCCESS
[DEBUG] WebSocket handshake: request parsed successfully
[DEBUG] WebSocket handshake: response built successfully (129 bytes)
[INFO ] WebSocket handshake completed on sock=5 (129 bytes sent)
```

---

## 🚀 部署步骤

1. **编译**

```bash
zig build broker -Dstatic-link=true -Dwebsocket=true --release=small
```

2. **部署到 ARM 设备**

```bash
scp zig-out/broker/linux-arm/mqtt_broker-musleabihf user@arm-device:/usr/local/bin/
```

3. **运行并测试**

```bash
ssh user@arm-device
./mqtt_broker-musleabihf
```

4. **从客户端连接**

使用浏览器、Node.js 或其他 MQTT over WebSocket 客户端连接。

---

## 💡 经验教训

### HTTP 头部解析的最佳实践

1. **按行解析**: HTTP 头部是行格式，应该逐行处理
2. **精确匹配**: 字段名必须在行首，后面紧跟冒号
3. **大小写处理**: HTTP 头部字段名不区分大小写（RFC 7230）
   - 当前实现使用 `XSTRNCMP`（区分大小写）
   - 如果需要更严格的兼容性，可以改为不区分大小写的比较

### 常见陷阱

❌ **不要使用 `strstr` 搜索整个消息**
- 可能匹配到值中的文本
- 无法保证字段名的完整性

✅ **应该按行解析**
- 每行一个头部字段
- 字段名在行首
- 字段名和值之间用冒号分隔

---

## 📖 相关 RFC

- [RFC 6455 - The WebSocket Protocol](https://tools.ietf.org/html/rfc6455)
- [RFC 7230 - HTTP/1.1 Message Syntax and Routing](https://tools.ietf.org/html/rfc7230#section-3.2)

根据 RFC 7230 Section 3.2：
> Each header field consists of a case-insensitive field name followed by a colon (":"), optional leading whitespace, the field value, and optional trailing whitespace.

---

## 📅 修复时间

**日期**: 2026年5月6日  
**问题**: WebSocket 握手头部解析失败  
**根因**: `strstr` 误匹配到其他行的值  
**方案**: 按行搜索，精确匹配行首字段名  
**状态**: ✅ 已修复并编译成功

---

**总结**: 通过按行解析 HTTP 头部，避免了 `strstr` 的误匹配问题，确保准确找到所需的头部字段。这是 HTTP 协议解析的标准做法。
