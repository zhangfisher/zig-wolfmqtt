# HTTP API功能默认启用配置

## 变更概述

HTTP API功能现在**默认启用**，不再需要通过编译开关显式启用。

## 变更详情

### 之前的行为

```bash
# 需要显式添加 -Dbroker-api=true 才能启用API
zig build broker -Dstatic-link=true -Dwebsocket=true -Dbroker-api=true --release=small
```

如果不添加 `-Dbroker-api=true`，API功能不会被编译进去。

### 现在的行为

```bash
# API功能自动包含，无需额外参数
zig build broker -Dstatic-link=true -Dwebsocket=true --release=small
```

API功能默认启用，除非显式使用 `-Dbroker-api=false` 禁用。

## 修改的文件

### 1. Zig构建系统

**文件**: `build/utils/options.zig`

```zig
// 之前
broker_api: bool = false, // HTTP API support (default disabled)
.broker_api = b.option(bool, "broker-api", "Enable Broker HTTP API support") orelse false,

// 现在
broker_api: bool = true, // HTTP API support (default enabled)
.broker_api = b.option(bool, "broker-api", "Enable Broker HTTP API support") orelse true,
```

### 2. CMake构建系统

**文件**: `CMakeLists.txt`

```cmake
# 之前
add_option(WOLFMQTT_BROKER_API
           "Enable broker HTTP API"
           "no" "yes;no")

# 现在
add_option(WOLFMQTT_BROKER_API
           "Enable broker HTTP API"
           "yes" "yes;no")
```

### 3. Autotools构建系统

**文件**: `configure.ac`

添加了新的配置选项（默认为yes）：

```autoconf
AC_ARG_ENABLE([broker-api],
[AS_HELP_STRING([--disable-broker-api],[Disable broker HTTP API support])],
[ ENABLED_BROKER_API=$enableval ],
[ ENABLED_BROKER_API=yes ]
)
if test "x$ENABLED_BROKER_API" = "xyes"
then
AM_CFLAGS="$AM_CFLAGS -DWOLFMQTT_BROKER_API"
fi

AM_CONDITIONAL([BUILD_BROKER_API], [test "x$ENABLED_BROKER_API" = "xyes"])
```

**文件**: `src/include.am`

```makefile
if BUILD_BROKER
bin_PROGRAMS += src/mqtt_broker
src_mqtt_broker_SOURCES      = src/mqtt_broker.c
# HTTP API support (enabled by default)
if BUILD_BROKER_API
src_mqtt_broker_SOURCES     += src/mqtt_broker_api.c
endif
...
endif
```

## 设计理念

### 为什么默认启用？

1. **简化使用**：用户不需要记住额外的编译参数
2. **功能完整性**：Broker提供完整的管理API是合理的默认行为
3. **一致性**：与其他Broker功能（如retained、will、wildcards等）保持一致，它们都是默认启用的
4. **易于管理**：HTTP API提供了便捷的Broker管理方式，应该是标准配置

### 如何禁用？

如果确实需要禁用API功能（例如为了减小二进制文件大小），可以使用：

#### Zig构建
```bash
zig build broker -Dbroker-api=false
```

#### CMake构建
```bash
cmake .. -DWOLFMQTT_BROKER_API=no
```

#### Autotools构建
```bash
./configure --disable-broker-api
```

## 影响范围

### 不受影响的场景

- ✅ 现有代码无需修改
- ✅ 已经使用 `-Dbroker-api=true` 的构建命令仍然有效（只是参数变为可选）
- ✅ API功能和行为完全不变

### 需要注意的场景

- ⚠️ 如果之前依赖API功能未启用来减小体积，现在需要显式禁用
- ⚠️ 文档和脚本中的 `-Dbroker-api=true` 参数可以移除（但仍可保留以保持兼容）

## 编译验证

### Zig构建测试

```bash
# 不带任何API参数（应该包含API）
zig build broker -Dstatic-link=true --release=small
ls -lh zig-out/broker/linux-arm/mqtt_broker-musleabihf
# 输出: 120,464 bytes (包含API功能)

# 显式禁用API
zig build broker -Dstatic-link=true -Dbroker-api=false --release=small
ls -lh zig-out/broker/linux-arm/mqtt_broker-musleabihf
# 输出: 更小的文件大小（不包含API功能）
```

### CMake构建测试

```bash
mkdir build && cd build

# 默认配置（应该包含API）
cmake ..
make
# WOLFMQTT_BROKER_API will be ON by default

# 显式禁用API
cmake .. -DWOLFMQTT_BROKER_API=no
make
```

### Autotools构建测试

```bash
./autogen.sh

# 默认配置（应该包含API）
./configure
make
# ENABLED_BROKER_API will be yes by default

# 显式禁用API
./configure --disable-broker-api
make
```

## 向后兼容性

### 完全兼容

- ✅ 旧的构建命令仍然有效
- ✅ 现有的API调用代码无需修改
- ✅ 所有API端点和功能保持不变

### 建议更新

虽然以下做法仍然有效，但建议更新为更简洁的形式：

```bash
# 旧写法（仍然有效，但冗余）
zig build broker -Dbroker-api=true

# 新写法（推荐）
zig build broker
```

## 文档更新

已更新以下文档以反映这一变化：

1. **API_URL_AND_LOGGING_UPDATE.md**
   - 更新了编译示例
   - 添加了默认启用的说明

2. **API_PUBLISH_KICK_IMPLEMENTATION.md**
   - 更新了编译和测试章节
   - 添加了重要说明部分

3. **本文件（API_DEFAULT_ENABLED.md）**
   - 详细说明了变更原因和影响

## 总结

✅ **HTTP API功能现在默认启用**  
✅ **简化了编译流程**  
✅ **保持了向后兼容性**  
✅ **提供了禁用选项以满足特殊需求**  

这一变更使得wolfMQTT Broker更加易用，同时保持了灵活性和兼容性。
