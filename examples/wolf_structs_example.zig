// wolf_structs 使用示例
// 演示如何使用动态生成的 MQTT 结构体

const std = @import("std");
const ws = @import("wolf_structs.zig");

pub fn main() !void {
    // =========================================================================
    // 定义全局编译选项（通常在整个项目中保持一致）
    // =========================================================================
    const compile_opts = ws.WolfMqttCompileOptions{
        .enable_tls = true,
        .enable_sn = false,
        .enable_v5 = true,
        .enable_disconnect_cb = true,
        .enable_property_cb = true,
        .enable_multithread = true,
        .enable_curl = false,
        .enable_nonblock_debug = false,
    };

    // =========================================================================
    // 示例 1: 使用 buildMqttClientStruct
    // =========================================================================

    // 先创建依赖的结构类型
    const NetType = ws.buildMqttNetStruct(compile_opts);
    const TlsType = ws.buildMqttTlsStruct(compile_opts);
    const PkReadType = ws.buildMqttPkReadStruct(compile_opts);
    const PubRespType = ws.buildMqttPublishRespStruct(compile_opts);

    // 使用自定义配置创建客户端
    const MyClient = ws.buildMqttClientStruct(
        compile_opts,
        NetType,
        TlsType,
        PkReadType,
        PubRespType,
    );
    var my_client = MyClient{};
    my_client.init();
    std.debug.print("自定义客户端大小: {} 字节\n", .{MyClient.size()});

    // 使用默认配置
    const default_opts = ws.WolfMqttCompileOptions.default();
    const DefaultClient = ws.buildMqttClientStruct(
        default_opts,
        ws.buildMqttNetStruct(default_opts),
        ws.buildMqttTlsStruct(default_opts),
        ws.buildMqttPkReadStruct(default_opts),
        ws.buildMqttPublishRespStruct(default_opts),
    );
    var default_client = DefaultClient{};
    default_client.init();

    // 使用完整配置
    const full_opts = ws.WolfMqttCompileOptions.full();
    const FullClient = ws.buildMqttClientStruct(
        full_opts,
        ws.buildMqttNetStruct(full_opts),
        ws.buildMqttTlsStruct(full_opts),
        ws.buildMqttPkReadStruct(full_opts),
        ws.buildMqttPublishRespStruct(full_opts),
    );
    var full_client = FullClient{};
    full_client.init();
    std.debug.print("完整客户端大小: {} 字节\n", .{FullClient.size()});

    // =========================================================================
    // 示例 2: 使用 buildMqttNetStruct
    // =========================================================================

    const MyNet = ws.buildMqttNetStruct(compile_opts);
    var my_net = MyNet{};
    my_net.init();

    // =========================================================================
    // 示例 3: 使用 buildMqttMessageStruct
    // =========================================================================

    // 先创建依赖的类型
    const MsgPubRespType = ws.buildMqttPublishRespStruct(compile_opts);

    // 创建消息结构
    const MyMsg = ws.buildMqttMessageStruct(compile_opts, MsgPubRespType);
    var my_msg = MyMsg{};
    my_msg.init();

    // =========================================================================
    // 示例 4: 使用 buildMqttConnectStruct
    // =========================================================================

    const MyConnect = ws.buildMqttConnectStruct(compile_opts);
    var connect = MyConnect{};
    connect.init();
    connect.keep_alive_sec = 60;
    connect.clean_session = 1;

    // =========================================================================
    // 示例 5: 使用 buildMqttPublishRespStruct
    // =========================================================================

    const PubResp = ws.buildMqttPublishRespStruct(compile_opts);
    var pub_resp = PubResp{};
    pub_resp.init();
    pub_resp.packet_id = 1;

    // =========================================================================
    // 示例 6: 使用 buildMqttSubscribeStruct
    // =========================================================================

    const Sub = ws.buildMqttSubscribeStruct(compile_opts);
    var sub = Sub{};
    sub.init();
    sub.packet_id = 2;

    // =========================================================================
    // 示例 7: 使用 buildMqttDisconnectStruct
    // =========================================================================

    const Disc = ws.buildMqttDisconnectStruct(compile_opts);
    var disc = Disc{};
    disc.init();

    // =========================================================================
    // 示例 8: 使用 buildMqttAuthStruct（仅v5.0）
    // =========================================================================

    const Auth = ws.buildMqttAuthStruct(compile_opts);
    var auth = Auth{};
    auth.init();

    // =========================================================================
    // 示例 9: 使用 MqttPendRespStruct（多线程模式）
    // =========================================================================

    var pend_resp = ws.MqttPendRespStruct{};
    pend_resp.init();
    pend_resp.packet_id = 3;

    std.debug.print("\n所有结构体初始化成功！\n", .{});
}

test "dynamic structs initialization" {
    // 使用统一的编译选项
    const opts = ws.WolfMqttCompileOptions{
        .enable_tls = true,
        .enable_v5 = true,
        .enable_multithread = true,
    };

    // 先创建依赖的类型
    const NetType1 = ws.buildMqttNetStruct(opts);
    const TlsType1 = ws.buildMqttTlsStruct(opts);
    const PkReadType1 = ws.buildMqttPkReadStruct(opts);
    const PubRespType1 = ws.buildMqttPublishRespStruct(opts);

    // 测试客户端结构
    const Client1 = ws.buildMqttClientStruct(
        opts,
        NetType1,
        TlsType1,
        PkReadType1,
        PubRespType1,
    );
    var c1 = Client1{};
    c1.init();

    // 测试不同配置的客户端
    const minimal_opts = ws.WolfMqttCompileOptions.minimal();
    const Client2 = ws.buildMqttClientStruct(
        minimal_opts,
        ws.buildMqttNetStruct(minimal_opts),
        ws.buildMqttTlsStruct(minimal_opts),
        ws.buildMqttPkReadStruct(minimal_opts),
        ws.buildMqttPublishRespStruct(minimal_opts),
    );
    var c2 = Client2{};
    c2.init();

    // 测试消息结构
    const MsgPubRespType = ws.buildMqttPublishRespStruct(opts);
    const Msg1 = ws.buildMqttMessageStruct(opts, MsgPubRespType);
    var m1 = Msg1{};
    m1.init();

    try std.testing.expect(minimal_opts.enable_tls == false);
    try std.testing.expect(opts.enable_tls == true);
}
