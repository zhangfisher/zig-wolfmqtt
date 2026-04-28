//! wolfMQTT Zig 示例程序 - MQTT 客户端实现
//!
//! 本示例演示如何使用 Zig 语言与 wolfMQTT C 库进行互操作，实现一个完整的 MQTT 客户端。
//! 参考 examples/pub-sub/mqtt-pub.c 的实现逻辑。
//!
//! 主要特性：
//! - 使用 Zig 0.16 构建系统和 C 翻译功能
//! - 通过 extern struct 实现与 C 结构体的二进制兼容
//! - 使用 Zig 0.16 std.Io 网络 API
//! - 完整的 MQTT 连接、发布、断开流程
//! - 支持 MQTT v5.0 协议
//!
//! 平台要求：Linux（需要 POSIX socket 支持）

const std = @import("std");
const wolfmqtt = @import("wolfmqtt"); // C 语言绑定，用于访问 wolfMQTT API

// ============================================================================
// 从 types 模块导入类型定义
// ============================================================================
const types = @import("types/types.zig");
const builder = @import("types/builder.zig");
const tls_types = @import("types/tls.zig");
const default_types = @import("types/default.zig");

// ============================================================================
// MQTT 连接配置常量
// ============================================================================

/// MQTT Broker 服务器地址
const BROKER_HOST = "192.168.116.222";
/// MQTT Broker 服务器端口（1883 为非加密 MQTT 默认端口）
const BROKER_PORT: u16 = 1883;
/// MQTT 客户端唯一标识符
const CLIENT_ID = "zig-mqtt-demo";
/// 订阅/发布的主题名称
const TOPIC = "voerka/test";
/// 测试消息内容
const MESSAGE = "Hello from Zig!";

/// MQTT 命令超时时间（毫秒）
const DEFAULT_CMD_TIMEOUT_MS = 30000;
/// MQTT 心跳保活间隔（秒），用于维持长连接
const DEFAULT_KEEP_ALIVE_SEC: u16 = 60;
/// 消息服务质量等级：0=最多一次，1=至少一次，2=恰好一次
const DEFAULT_MQTT_QOS = types.MqttQoS.qos_0;
/// 发送和接收缓冲区的最大大小（字节）
const MAX_BUFFER_SIZE = 1024;

/// 测试时发送的消息总数
const MESSAGE_COUNT = 10;
/// 每条消息之间的发送间隔（毫秒）
const SEND_INTERVAL_MS = 1000;

// ============================================================================
// 核心数据结构定义
// ============================================================================

/// 网络上下文结构体
///
/// 封装 Zig 0.16 std.Io.net 的网络连接。
/// 该结构体作为回调函数的用户数据传递给 wolfMQTT 库。
const NetworkContext = struct {
    /// TCP 流连接，使用 std.Io.net.Stream
    stream: ?std.Io.net.Stream = null,
    /// 指向 IoThreaded 实例的指针，用于获取 Io 实例
    io_threaded: *std.Io.Threaded,
    /// 读缓冲区，用于 Stream.Reader
    read_buffer: []u8,
    /// 写缓冲区，用于 Stream.Writer
    write_buffer: []u8,

    /// 初始化网络上下文
    ///
    /// 参数:
    ///     io_threaded_ptr - 指向 IoThreaded 实例的指针
    ///     read_buffer - 读缓冲区
    ///     write_buffer - 写缓冲区
    /// 返回:
    ///     新创建的 NetworkContext 实例
    fn init(io_threaded_ptr: *std.Io.Threaded, read_buffer: []u8, write_buffer: []u8) NetworkContext {
        return .{
            .io_threaded = io_threaded_ptr,
            .read_buffer = read_buffer,
            .write_buffer = write_buffer,
        };
    }

    /// 清理网络上下文，关闭连接
    ///
    /// 参数:
    ///     self - 指向 NetworkContext 的指针
    fn deinit(self: *NetworkContext) void {
        if (self.stream) |s| {
            const io = self.io_threaded.io();
            s.close(io);
            self.stream = null;
        }
    }

    /// 获取 Io 实例
    fn getIo(self: *const NetworkContext) std.Io {
        return self.io_threaded.io();
    }
};

/// MQTT 客户端实例（使用 extern struct 正确定义）
// MqttClient 缓冲区（C opaque 类型，使用足够大的字节缓冲区）
// 使用 16 字节对齐以确保 ARM 架构上的正确性
var mqtt_client_buf: [1024]u8 align(16) = undefined;
/// MQTT 连接参数缓冲区（使用字节缓冲区 + MqttStructBuilder）
var mqtt_connect_buf: [256]u8 align(8) = undefined;
/// MQTT 发布消息缓冲区
var mqtt_publish_buf: [512]u8 align(8) = undefined;
/// MQTT 断开连接缓冲区
var mqtt_disconnect_buf: [128]u8 align(8) = undefined;

/// 发送缓冲区指针（动态分配，用于传递给 C API）
var tx_buf: ?[*]u8 = null;
/// 接收缓冲区指针（动态分配，用于传递给 C API）
var rx_buf: ?[*]u8 = null;
/// 发送缓冲区切片（用于内存释放）
var tx_buf_slice: ?[]u8 = null;
/// 接收缓冲区切片（用于内存释放）
var rx_buf_slice: ?[]u8 = null;
/// 网络上下文实例，管理 TCP 连接
var net_ctx: NetworkContext = undefined;
/// std.Io.Threaded 实例，用于所有网络 I/O 操作
var io_threaded: std.Io.Threaded = undefined;
/// Stream 读写缓冲区（8KB）
var stream_read_buf: [8192]u8 = undefined;
var stream_write_buf: [8192]u8 = undefined;
/// MQTT 数据包 ID 计数器（1-65535 循环）
var packet_id: u16 = 1;
/// MQTT 网络回调接口实例（使用 types/default.zig 中的默认类型）
var mqtt_net: default_types.MqttNetStruct = .{};

// ============================================================================
// 辅助工具函数
// ============================================================================

/// 获取下一个 MQTT 数据包 ID
///
/// MQTT 协议要求 QoS > 0 的消息必须有唯一的 packet_id。
/// 此函数生成递增的 ID，从 1 到 65535 循环。
///
/// 返回:
///     当前的数据包 ID（调用后自动递增）
inline fn getPacketId() u16 {
    const id = packet_id; // 保存当前 ID
    packet_id +%= 1; // 递增（溢出时自动回绕）
    if (packet_id == 0) packet_id = 1; // 跳过 0（无效 ID）
    return id;
}

// ============================================================================
// 网络层回调函数实现（使用 Zig 0.16 std.Io 网络 API）
// ============================================================================
//
// 这些函数由 wolfMQTT 库在需要网络操作时调用。
// 我们使用 Zig 0.16 的 std.os.linux 系统调用实现底层 TCP 通信。

/// 建立 TCP 连接到 MQTT Broker
///
/// 参数:
///     net_ctx_ptr - 网络上下文指针（指向 NetworkContext）
///     host - 服务器主机名或 IP 地址（C 字符串）
///     port - 服务器端口号
///     timeout_ms - 超时时间（毫秒，当前未使用）
///
/// 返回:
///     0 表示成功，负数表示错误码
fn networkConnect(net_ctx_ptr: ?*anyopaque, host: [*c]const u8, port: u16, timeout_ms: c_int) callconv(.c) c_int {
    _ = timeout_ms; // 暂时忽略超时参数
    const ctx: *NetworkContext = @ptrCast(@alignCast(net_ctx_ptr.?));

    std.log.info("networkConnect 被调用: host={s}, port={d}", .{ std.mem.span(host), port });

    // 如果已有活动连接，先关闭它
    if (ctx.stream) |s| {
        const io = ctx.getIo();
        s.close(io);
        ctx.stream = null;
    }

    // 解析主机名和端口
    const host_str = std.mem.span(host);
    std.log.info("开始解析地址: {s}", .{host_str});

    // 使用 std.Io.net.IpAddress.parse 解析地址
    const address = std.Io.net.IpAddress.parse(host_str, port) catch |err| {
        std.log.err("IP 地址解析失败: {s}, 错误: {}", .{ host_str, err });
        return @as(c_int, @bitCast(wolfmqtt.MQTT_CODE_ERROR_NETWORK));
    };

    std.log.info("地址解析成功，准备连接...", .{});

    // 使用 std.Io.net.IpAddress.connect 建立 TCP 连接
    const io = ctx.getIo();

    std.log.info("Io 实例: userdata={*}, vtable={*}", .{ io.userdata, io.vtable });

    const stream = std.Io.net.IpAddress.connect(
        &address,
        io,
        .{
            .mode = .stream, // Socket.Mode.stream
            .protocol = null, // null 表示使用默认协议（TCP）
            .timeout = .none, // 无超时
        },
    ) catch |err| {
        std.log.err("连接服务器失败: {s}:{d}, 错误: {}", .{ host_str, port, err });
        return @as(c_int, @bitCast(wolfmqtt.MQTT_CODE_ERROR_NETWORK));
    };

    ctx.stream = stream; // 保存成功的连接
    std.log.info("✓ 已连接到 MQTT Broker: {s}:{d}", .{ host_str, port });
    return 0; // 成功
}

/// 从网络读取数据
///
/// 参数:
///     net_ctx_ptr - 网络上下文指针
///     buf - 接收缓冲区指针
///     buf_len - 期望读取的字节数
///     timeout_ms - 超时时间（毫秒，当前未使用）
///
/// 返回:
///     实际读取的字节数（正数），或错误码（负数）
fn networkRead(net_ctx_ptr: ?*anyopaque, buf: [*c]u8, buf_len: c_int, timeout_ms: c_int) callconv(.c) c_int {
    _ = timeout_ms;
    const ctx: *NetworkContext = @ptrCast(@alignCast(net_ctx_ptr.?));

    if (ctx.stream == null) {
        return @as(c_int, @bitCast(wolfmqtt.MQTT_CODE_ERROR_NETWORK));
    }

    // 使用 Stream.Reader 读取数据
    const stream = ctx.stream.?;
    const io = ctx.getIo();
    var stream_reader = stream.reader(io, ctx.read_buffer);
    var reader_interface = &stream_reader.interface;

    const dest_slice = buf[0..@intCast(buf_len)];
    const bytes_read = reader_interface.readSliceShort(dest_slice) catch {
        return @as(c_int, @bitCast(wolfmqtt.MQTT_CODE_ERROR_NETWORK));
    };

    return @intCast(bytes_read);
}

/// 向网络写入数据
///
/// 参数:
///     net_ctx_ptr - 网络上下文指针
///     buf - 发送缓冲区指针
///     buf_len - 要写入的字节数
///     timeout_ms - 超时时间（毫秒，当前未使用）
///
/// 返回:
///     实际写入的字节数（正数），或错误码（负数）
fn networkWrite(net_ctx_ptr: ?*anyopaque, buf: [*c]const u8, buf_len: c_int, timeout_ms: c_int) callconv(.c) c_int {
    _ = timeout_ms;
    const ctx: *NetworkContext = @ptrCast(@alignCast(net_ctx_ptr.?));

    if (ctx.stream == null) {
        return @as(c_int, @bitCast(wolfmqtt.MQTT_CODE_ERROR_NETWORK));
    }

    // 使用 Stream.Writer 写入数据
    const stream = ctx.stream.?;
    const io = ctx.getIo();
    var stream_writer = stream.writer(io, ctx.write_buffer);
    var writer_interface = &stream_writer.interface;

    const src_slice = buf[0..@intCast(buf_len)];
    const bytes_written = writer_interface.write(src_slice) catch {
        return @as(c_int, @bitCast(wolfmqtt.MQTT_CODE_ERROR_NETWORK));
    };

    return @intCast(bytes_written);
}

/// 断开 TCP 连接
///
/// 参数:
///     net_ctx_ptr - 网络上下文指针
///
/// 返回:
///     0 表示成功，负数表示错误码
fn networkDisconnect(net_ctx_ptr: ?*anyopaque) callconv(.c) c_int {
    const ctx: *NetworkContext = @ptrCast(@alignCast(net_ctx_ptr.?));

    if (ctx.stream) |s| {
        const io = ctx.getIo();
        s.close(io);
        ctx.stream = null;
        std.log.info("✓ 已断开与 MQTT Broker 的连接", .{});
    }

    return 0;
}

// ============================================================================
// MQTT 高层操作函数
// ============================================================================

/// 初始化 MQTT 客户端
///
/// 分配内存缓冲区，配置网络回调，并初始化 wolfMQTT 客户端结构体。
///
/// 参数:
///     allocator - 内存分配器，用于动态分配收发缓冲区
///
/// 错误:
///     error.OutOfMemory - 内存分配失败
///     error.MqttInitFailed - wolfMQTT 初始化失败
fn mqttInit(allocator: std.mem.Allocator) !void {
    std.log.info("初始化 MQTT 客户端...", .{});

    // 初始化 std.Io.Threaded 实例（不返回错误）
    io_threaded = std.Io.Threaded.init(allocator, .{});

    // 验证 Io 实例是否正确初始化
    const test_io = io_threaded.io();
    std.log.info("Io 实例已初始化，userdata: {*}", .{test_io.userdata});

    // 初始化网络上下文（包含 io_threaded 指针和读写缓冲区）
    net_ctx = NetworkContext.init(&io_threaded, &stream_read_buf, &stream_write_buf);

    // 动态分配发送和接收缓冲区
    const tx_slice = allocator.alloc(u8, MAX_BUFFER_SIZE) catch return error.OutOfMemory;
    const rx_slice = allocator.alloc(u8, MAX_BUFFER_SIZE) catch return error.OutOfMemory;
    tx_buf_slice = tx_slice;
    rx_buf_slice = rx_slice;
    tx_buf = @ptrCast(tx_slice);
    rx_buf = @ptrCast(rx_slice);

    // 配置 MqttNet 网络回调接口（字段顺序必须与 C MqttNet 一致）
    mqtt_net = .{
        .context = &net_ctx, // 必须是第一个字段
        .connect = networkConnect, // TCP 连接回调
        .read = networkRead, // 数据读取回调
        .write = networkWrite, // 数据写入回调
        .disconnect = networkDisconnect, // 断开连接回调
    };

    // 调用 wolfMQTT C API 初始化客户端
    const rc = wolfmqtt.MqttClient_Init(
        @ptrCast(&mqtt_client_buf),
        @ptrCast(&mqtt_net), // 传入网络回调接口
        null, // 消息回调（本例不使用）
        tx_buf, // 发送缓冲区
        MAX_BUFFER_SIZE, // 发送缓冲区大小
        rx_buf, // 接收缓冲区
        MAX_BUFFER_SIZE, // 接收缓冲区大小
        DEFAULT_CMD_TIMEOUT_MS, // 命令超时时间
    );

    // 检查初始化结果
    if (rc != wolfmqtt.MQTT_CODE_SUCCESS) {
        std.log.err("MQTT 客户端初始化失败: {s} ({d})", .{
            wolfmqtt.MqttClient_ReturnCodeToString(rc),
            rc,
        });
        return error.MqttInitFailed;
    }

    std.log.info("✓ MQTT 客户端初始化完成", .{});
}

/// 建立 MQTT 协议连接
///
/// 先建立 TCP 网络连接，再发送 MQTT CONNECT 包并等待 CONNACK 响应。
///
/// 错误:
///     error.MqttConnectFailed - 连接失败（网络错误或 Broker 拒绝）
fn mqttConnect() !void {
    // 步骤 1: 建立 TCP 网络连接
    std.log.info("正在连接到 MQTT Broker: {s}:{d}...", .{ BROKER_HOST, BROKER_PORT });

    // 将 Zig 字符串转换为 C 字符串指针
    const host_cstr: [*c]const u8 = BROKER_HOST;

    const net_rc = wolfmqtt.MqttClient_NetConnect(
        @ptrCast(&mqtt_client_buf),
        host_cstr,
        BROKER_PORT,
        @intCast(DEFAULT_CMD_TIMEOUT_MS),
        0, // use_tls: 0 = 不使用 TLS
        null, // tls_cb: null = 无 TLS 回调
    );
    if (net_rc != wolfmqtt.MQTT_CODE_SUCCESS) {
        std.log.err("TCP 网络连接失败: {s} ({d})", .{
            wolfmqtt.MqttClient_ReturnCodeToString(net_rc),
            net_rc,
        });
        return error.MqttConnectFailed;
    }
    std.log.info("✓ TCP 网络连接成功", .{});

    // 步骤 2: 配置 MQTT 连接参数
    // 使用 builder 创建 MqttConnect 结构体类型
    const MqttConnectType = builder.buildMqttConnectStruct(
        types.MqttCompileOptions.default(),
        default_types.MqttMessageStruct,
        default_types.MqttConnectAckStruct,
    );

    // 创建并初始化连接结构体实例
    var connect_instance: MqttConnectType = undefined;
    connect_instance.init();

    // 设置连接参数
    connect_instance.keep_alive_sec = DEFAULT_KEEP_ALIVE_SEC;
    connect_instance.clean_session = 1;
    connect_instance.client_id = CLIENT_ID;
    connect_instance.protocol_level = 5; // MQTT v5.0

    // 步骤 3: 发送 MQTT CONNECT 包
    std.log.info("MQTT 握手中...", .{});
    const rc = wolfmqtt.MqttClient_Connect(@ptrCast(&mqtt_client_buf), @ptrCast(&connect_instance));
    if (rc != wolfmqtt.MQTT_CODE_SUCCESS) {
        std.log.err("MQTT 连接失败: {s} ({d})", .{
            wolfmqtt.MqttClient_ReturnCodeToString(rc),
            rc,
        });
        return error.MqttConnectFailed;
    }

    std.log.info("✓ MQTT 连接成功! ClientID: {s}", .{CLIENT_ID});
}

/// 发布 MQTT 消息到指定主题
///
/// 将消息发送到 Broker，支持 QoS 0/1/2。对于大消息会自动分片传输。
///
/// 参数:
///     msg - 要发送的消息内容（Zig 切片）
///
/// 错误:
///     error.MqttPublishFailed - 发布失败（网络错误或 Broker 拒绝）
fn mqttPublish(msg: []const u8) !void {
    // 使用 builder 创建 MqttMessage 结构体类型
    const MqttMessageType = builder.buildMqttMessageStruct(
        types.MqttCompileOptions.default(),
        builder.buildMqttPublishRespStruct(types.MqttCompileOptions.default()),
    );

    // 创建并初始化消息结构体实例
    var msg_instance: MqttMessageType = undefined;
    msg_instance.init();

    // 设置消息参数
    msg_instance.packet_id = getPacketId();
    msg_instance.qos = DEFAULT_MQTT_QOS;
    msg_instance.topic_name = TOPIC;
    msg_instance.topic_name_len = @intCast(TOPIC.len);
    msg_instance.buffer = @constCast(msg.ptr);
    msg_instance.buffer_len = @intCast(msg.len);
    msg_instance.total_len = @intCast(msg.len);

    // 调用 wolfMQTT C API 发布消息
    var rc: c_int = wolfmqtt.MqttClient_Publish(@ptrCast(&mqtt_client_buf), @ptrCast(&msg_instance));
    // 处理分片传输（MQTT_CODE_PUB_CONTINUE 表示还有更多数据要发送）
    while (rc == wolfmqtt.MQTT_CODE_PUB_CONTINUE) {
        rc = wolfmqtt.MqttClient_Publish(@ptrCast(&mqtt_client_buf), @ptrCast(&msg_instance));
    }

    // 检查最终结果
    if (rc != wolfmqtt.MQTT_CODE_SUCCESS) {
        std.log.err("MQTT 发布失败: {s} ({d})", .{
            wolfmqtt.MqttClient_ReturnCodeToString(rc),
            rc,
        });
        return error.MqttPublishFailed;
    }

    std.log.info("✓ 已发布 [{s}] QoS{d}: {s}", .{ TOPIC, @intFromEnum(DEFAULT_MQTT_QOS), msg });
}

/// 断开 MQTT 连接
///
/// 向 Broker 发送 DISCONNECT 包并关闭 TCP 连接。
fn mqttDisconnect() void {
    std.log.info("MQTT 断开连接...", .{});

    // 使用 builder 创建 MqttDisconnect 结构体类型
    const MqttDisconnectType = builder.buildMqttDisconnectStruct(types.MqttCompileOptions.default());

    // 创建并初始化断开结构体实例
    var disconnect_instance: MqttDisconnectType = undefined;
    disconnect_instance.init();

    // 设置断开原因码（正常断开）
    if (types.MqttCompileOptions.default().enable_v5) {
        disconnect_instance.reason_code = types.MqttReasonCodes.NORMAL_DISCONNECTION;
        disconnect_instance.protocol_level = 5; // MQTT v5.0
    }

    // 调用 wolfMQTT C API 断开连接
    _ = wolfmqtt.MqttClient_Disconnect_ex(@ptrCast(&mqtt_client_buf), @ptrCast(&disconnect_instance));
    std.log.info("✓ MQTT 已断开连接", .{});
}

/// 清理资源
///
/// 关闭网络连接，释放动态分配的内存缓冲区，清理 IO 实例。
///
/// 参数:
///     allocator - 内存分配器，用于释放缓冲区
fn cleanup(allocator: std.mem.Allocator) void {
    // 关闭 TCP 连接并清理网络上下文
    net_ctx.deinit();

    // 释放发送缓冲区
    if (tx_buf_slice) |slice| {
        allocator.free(slice);
        tx_buf_slice = null;
        tx_buf = null;
    }
    // 释放接收缓冲区
    if (rx_buf_slice) |slice| {
        allocator.free(slice);
        rx_buf_slice = null;
        rx_buf = null;
    }

    // 清理 std.Io.Threaded 实例
    io_threaded.deinit();
}

// ============================================================================
// 主程序入口
// ============================================================================

/// 程序主函数
///
/// 执行完整的 MQTT 客户端生命周期：
/// 1. 初始化 MQTT 客户端和网络层
/// 2. 连接到 MQTT Broker
/// 3. 循环发送测试消息
/// 4. 断开连接并清理资源
///
/// 错误:
///     任何阶段的错误都会导致程序退出
pub fn main() !void {
    // 使用页面分配器（简单场景下足够）
    const allocator = std.heap.page_allocator;

    // 打印程序信息
    std.log.info("========================================", .{});
    std.log.info("  wolfMQTT Zig 示例程序", .{});
    std.log.info("  参考: examples/pub-sub/mqtt-pub.c", .{});
    std.log.info("  技术栈: Zig 0.16 + std.Io.net ", .{});
    std.log.info("========================================", .{});
    std.log.info("", .{});

    // 步骤 1: 初始化 MQTT 客户端
    try mqttInit(allocator);
    defer cleanup(allocator); // 确保退出时清理资源
    defer wolfmqtt.MqttClient_DeInit(@ptrCast(&mqtt_client_buf)); // 确保反初始化 MQTT 客户端

    // 步骤 2: 建立 MQTT 连接（内部会调用 networkConnect 建立 TCP 连接）
    try mqttConnect();
    defer mqttDisconnect(); // 确保退出时断开连接

    // 打印发送计划
    std.log.info("", .{});
    std.log.info("开始发送 {d} 条消息到主题: {s}", .{ MESSAGE_COUNT, TOPIC });
    std.log.info("发送间隔: {d} ms", .{SEND_INTERVAL_MS});
    std.log.info("", .{});

    // 步骤 3: 循环发送测试消息
    var msg_buf: [256]u8 = undefined; // 消息格式化缓冲区
    var i: u32 = 0;
    while (i < MESSAGE_COUNT) : (i += 1) {
        // 格式化消息内容
        const msg = try std.fmt.bufPrintZ(&msg_buf, "{s} - 消息 #{d}", .{ MESSAGE, i + 1 });
        try mqttPublish(msg); // 发布消息

        // 在最后一条消息前添加延迟
        if (i < MESSAGE_COUNT - 1) {
            _ = wolfmqtt.usleep(@intCast(SEND_INTERVAL_MS * 1000)); // 微秒级延迟
        }
    }

    // 打印完成信息
    std.log.info("", .{});
    std.log.info("✅ 完成! 共发送 {d} 条消息", .{MESSAGE_COUNT});
    std.log.info("========================================", .{});
}
