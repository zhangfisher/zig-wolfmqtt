# 为 ARM Linux 编译 wslay WebSocket 库

## ❌ 当前问题

当前的 `libs/wslay/lib/libwslay.a` 是为 **x86_64 Windows** 编译的，不能用于 **ARM Linux** 交叉编译。

尝试编译时会看到错误：
```
ld.lld: warning: libwslay.a: archive member is neither ET_REL nor LLVM bitcode
```

## ✅ 解决方案

需要为 ARM Linux 目标重新编译 wslay 库。

### 方法 1：使用交叉编译工具链（推荐）

#### 步骤 1：安装 ARM 交叉编译工具链

**Ubuntu/Debian**:
```bash
sudo apt-get install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
```

**macOS (Homebrew)**:
```bash
brew install arm-linux-gnueabihf-binutils
brew install FiloSottile/musl-cross/musl-cross  # for musl libc
```

**Windows (WSL2)**:
在 WSL2 Ubuntu 中执行上面的 Ubuntu 命令。

#### 步骤 2：下载 wslay 源代码

```bash
cd /tmp
git clone https://github.com/tatsuhiro-t/wslay.git
cd wslay
```

#### 步骤 3：配置交叉编译

```bash
# 设置交叉编译工具链
export CC=arm-linux-gnueabihf-gcc
export CXX=arm-linux-gnueabihf-g++
export AR=arm-linux-gnueabihf-ar
export RANLIB=arm-linux-gnueabihf-ranlib

# 配置构建
./configure \
    --host=arm-linux-gnueabihf \
    --prefix=/tmp/wslay-install \
    --enable-static \
    --disable-shared \
    --disable-examples
```

#### 步骤 4：编译和安装

```bash
make -j$(nproc)
make install
```

#### 步骤 5：复制库文件到项目

```bash
# 回到 wolfMQTT 项目目录
cd /path/to/wolfMQTT

# 复制静态库
cp /tmp/wslay-install/lib/libwslay.a libs/wslay/lib/libwslay-arm-linux.a

# 复制头文件（如果需要）
cp -r /tmp/wslay-install/include/wslay libs/wslay/include/
```

#### 步骤 6：修改构建配置

编辑 `build/entries/broker.zig`，将：

```zig
broker.root_module.addObjectFile(b.path("libs/wslay/lib/libwslay.a"));
```

改为：

```zig
broker.root_module.addObjectFile(b.path("libs/wslay/lib/libwslay-arm-linux.a"));
```

或者重命名文件：
```bash
mv libs/wslay/lib/libwslay-arm-linux.a libs/wslay/lib/libwslay.a
```

---

### 方法 2：使用 Zig 编译（实验性）

Zig 可以作为交叉编译器使用，但 wslay 是 CMake/Autotools 项目，需要额外配置。

#### 步骤 1：创建 Zig 包装脚本

创建 `zig-cc.sh`:
```bash
#!/bin/bash
zig cc -target arm-linux-musleabihf "$@"
```

创建 `zig-cxx.sh`:
```bash
#!/bin/bash
zig c++ -target arm-linux-musleabihf "$@"
```

#### 步骤 2：使用 Zig 编译 wslay

```bash
cd /tmp/wslay
chmod +x zig-cc.sh zig-cxx.sh

export CC=/path/to/zig-cc.sh
export CXX=/path/to/zig-cxx.sh
export AR="zig ar"

./configure \
    --host=arm-linux \
    --prefix=/tmp/wslay-install-zig \
    --enable-static \
    --disable-shared

make -j$(nproc)
make install
```

---

### 方法 3：在 ARM 设备上直接编译

如果有 ARM Linux 设备（如 Raspberry Pi），可以直接在上面编译。

#### 步骤 1：在 ARM 设备上安装依赖

```bash
# Raspberry Pi OS / Ubuntu ARM
sudo apt-get install build-essential autoconf libtool
```

#### 步骤 2：克隆并编译

```bash
git clone https://github.com/tatsuhiro-t/wslay.git
cd wslay

./configure --enable-static --disable-shared --disable-examples
make -j$(nproc)
sudo make install
```

#### 步骤 3：复制库文件

```bash
# 从 ARM 设备复制到开发机器
scp pi@raspberrypi:/usr/local/lib/libwslay.a ./libs/wslay/lib/
scp -r pi@raspberrypi:/usr/local/include/wslay ./libs/wslay/include/
```

---

## 🔍 验证库架构

编译完成后，验证库是否适用于 ARM：

```bash
# Linux/macOS
file libs/wslay/lib/libwslay.a

# 期望输出:
# libwslay.a: current ar archive
# 或者
# libwslay.a: ELF 32-bit LSB relocatable, ARM, EABI5 version 1 ...
```

如果看到：
```
libwslay.a: MS-DOS executable (PE)
```
说明仍然是 x86_64 Windows 版本，需要重新编译。

---

## 📝 临时解决方案

如果暂时不需要 WebSocket 功能，可以：

### 方案 A：编译不带 WebSocket 的 Broker

```bash
zig build broker -Dstatic-link=true --release=small
```

Broker 仍然支持：
- ✅ TCP (端口 1883)
- ✅ TLS (端口 8883，如果启用)
- ❌ WebSocket (需要 wslay 库)

### 方案 B：在本机测试 WebSocket

在 x86_64 Linux/macOS 上编译和测试：

```bash
# Linux/macOS
zig build broker -Dwebsocket=true --release=small

# 运行
./zig-out/broker/linux-x86_64/mqtt_broker
```

---

## 🎯 完整的编译流程示例

```bash
# 1. 准备环境
sudo apt-get install gcc-arm-linux-gnueabihf

# 2. 下载 wslay
cd /tmp
git clone https://github.com/tatsuhiro-t/wslay.git
cd wslay

# 3. 交叉编译
export CC=arm-linux-gnueabihf-gcc
export AR=arm-linux-gnueabihf-ar
./configure --host=arm-linux-gnueabihf --prefix=/tmp/wslay-arm --enable-static --disable-shared
make -j4
make install

# 4. 复制到 wolfMQTT 项目
cd /path/to/wolfMQTT
cp /tmp/wslay-arm/lib/libwslay.a libs/wslay/lib/libwslay-arm-linux.a

# 5. 编译 Broker
zig build broker -Dstatic-link=true -Dwebsocket=true --release=small

# 6. 部署到 ARM 设备
scp zig-out/broker/linux-arm/mqtt_broker-musleabihf user@arm-device:/usr/local/bin/

# 7. 在 ARM 设备上运行
ssh user@arm-device
./mqtt_broker-musleabihf -p 1883 -w 8080
```

---

## 📊 文件大小对比

| 平台 | 库文件 | 大小 |
|------|--------|------|
| x86_64 Windows | libwslay.a | ~20 KB |
| ARM Linux | libwslay.a | ~15-25 KB |
| x86_64 Linux | libwslay.a | ~18 KB |

---

## ⚠️ 注意事项

1. **ABI 兼容性**：确保交叉编译工具链的 ABI 与目标设备匹配
   - `gnueabihf` = hard-float (Raspberry Pi 3/4)
   - `gnueabi` = soft-float (老设备)
   - `musleabihf` = musl libc (Alpine Linux)

2. **依赖库**：wslay 没有外部依赖，编译相对简单

3. **优化选项**：可以使用 `-O2` 或 `-Os` 优化
   ```bash
   export CFLAGS="-O2 -march=armv7-a"
   ```

4. **调试信息**：如果需要调试，添加 `-g` 标志
   ```bash
   export CFLAGS="-g -O0"
   ```

---

## 📖 相关资源

- [wslay GitHub](https://github.com/tatsuhiro-t/wslay)
- [Zig 交叉编译文档](https://zig.guide/build-system/cross-compilation/)
- [ARM 交叉编译指南](https://developer.arm.com/documentation/102429/0100/Cross-compilation)

---

**总结**：要为 ARM Linux 启用 WebSocket 支持，需要使用交叉编译工具链重新编译 wslay 库。临时方案是编译不带 WebSocket 的 Broker，或者在 x86_64 平台上测试。
