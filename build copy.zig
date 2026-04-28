//! wolfMQTT Zig 构建配置 (Zig 0.16+)
//! 参考 CMakeLists.txt 的配置

const std = @import("std");

/// 构建配置选项
pub const BuildOptions = struct {
    tls: bool = false,
    mqtt_sn: bool = false,
    nonblock: bool = true,
    no_timeout: bool = false,
    error_strings: bool = true,
    stdincap: bool = true,
    v5: bool = true,
    discb: bool = true,
    mt: bool = true,
    curl: bool = false,
    property_cb: bool = true,
    debug_client: bool = false,
    static_link: bool = false,
    broker: bool = true, // 默认启用 broker
    broker_retained: bool = true,
    broker_will: bool = true,
    broker_wildcards: bool = true,
    broker_auth: bool = true,
    broker_log: bool = true,
    broker_insecure: bool = true,
    broker_debug: bool = true,
};

pub fn build(b: *std.Build) void {
    // 默认目标平台为 ARM Linux (arm-linux-gnueabihf)
    const target = b.standardTargetOptions(.{
        .default_target = .{
            .cpu_arch = .arm,
            .os_tag = .linux,
            .abi = .gnueabihf,
        },
    });
    const optimize = b.standardOptimizeOption(.{
        .preferred_optimize_mode = .ReleaseSmall,
    });

    // 解析构建选项
    const opts = parseBuildOptions(b);

    // 检查是否只构建特定组件
    const build_only = b.option([]const u8, "build-only", "Build only specified component (wolfmqtt|broker|all)");
    const should_build_all = build_only == null or std.mem.eql(u8, build_only.?, "all");
    const should_build_wolfmqtt = should_build_all or (build_only != null and std.mem.eql(u8, build_only.?, "wolfmqtt"));
    const should_build_broker = should_build_all or (build_only != null and std.mem.eql(u8, build_only.?, "broker"));

    var lib: ?*std.Build.Step.Compile = null;

    // 构建 wolfMQTT 静态库
    if (should_build_wolfmqtt) {
        // 如果默认构建且需要 broker，则库也需要启用 broker 宏
        var lib_opts = opts;
        if (build_only == null and should_build_broker) {
            lib_opts.broker = true;
        }

        lib = buildWolfmqttLib(b, target, optimize, lib_opts);

        // 设置自定义安装路径: zig-out/wolfmqtt/<arch>/<os>/<abi>
        const install_prefix = b.fmt("wolfmqtt/{s}/{s}/{s}", .{
            @tagName(target.result.cpu.arch),
            @tagName(target.result.os.tag),
            @tagName(target.result.abi),
        });

        const install_step = b.addInstallArtifact(lib.?, .{
            .dest_dir = .{ .override = .{ .custom = install_prefix } },
        });
        b.getInstallStep().dependOn(&install_step.step);

        // 生成构建摘要 README.md
        generateBuildReadme(b, "wolfmqtt", target.result, lib_opts);
    }

    // 构建 broker 可执行文件
    if (should_build_broker and opts.broker) {
        // 确保库已经构建
        if (lib == null) {
            lib = buildWolfmqttLib(b, target, optimize, opts);

            // 设置自定义安装路径: zig-out/wolfmqtt/<arch>/<os>/<abi>
            const install_prefix = b.fmt("wolfmqtt/{s}/{s}/{s}", .{
                @tagName(target.result.cpu.arch),
                @tagName(target.result.os.tag),
                @tagName(target.result.abi),
            });

            const install_step = b.addInstallArtifact(lib.?, .{
                .dest_dir = .{ .override = .{ .custom = install_prefix } },
            });
            b.getInstallStep().dependOn(&install_step.step);

            // 生成 wolfMQTT README.md
            generateBuildReadme(b, "wolfmqtt", target.result, opts);
        }
        buildBroker(b, lib.?, opts, target, optimize);
    }

    // 注意：demo 现在是独立构建项目，请使用 cd buildSchemas/demo && zig build

    // 添加常用构建步骤
    addCommonSteps(b);
}

/// 添加模块定义
fn addModuleDefinitions(module: *std.Build.Module, opts: BuildOptions, target_info: std.Target) void {
    // 必需定义
    module.addCMacro("BUILDING_WOLFMQTT", "1");
    module.addCMacro("BUILDING_ZIG", "1");

    // 防止 vs_settings.h 被包含（它会强制启用 TLS）
    module.addCMacro("HAVE_CONFIG_H", "1");

    // 功能定义
    if (opts.tls) {
        module.addCMacro("ENABLE_MQTT_TLS", "1");
    }

    if (opts.mqtt_sn) {
        module.addCMacro("WOLFMQTT_SN", "1");
    }

    if (opts.nonblock) {
        module.addCMacro("WOLFMQTT_NONBLOCK", "1");
    }

    if (opts.no_timeout) {
        module.addCMacro("WOLFMQTT_NO_TIMEOUT", "1");
    }

    if (!opts.error_strings) {
        module.addCMacro("WOLFMQTT_NO_ERROR_STRINGS", "1");
    }

    if (!opts.stdincap) {
        module.addCMacro("WOLFMQTT_NO_STDIN_CAP", "1");
    }

    if (opts.v5) {
        module.addCMacro("WOLFMQTT_V5", "1");
    }

    if (opts.discb) {
        module.addCMacro("WOLFMQTT_DISCONNECT_CB", "1");
    }

    if (opts.mt) {
        module.addCMacro("WOLFMQTT_MULTITHREAD", "1");
    }

    if (opts.curl) {
        module.addCMacro("ENABLE_MQTT_CURL", "1");
    }

    if (opts.property_cb) {
        module.addCMacro("WOLFMQTT_PROPERTY_CB", "1");
    }

    if (opts.debug_client) {
        module.addCMacro("WOLFMQTT_DEBUG_CLIENT", "1");
    }

    if (opts.broker) {
        module.addCMacro("WOLFMQTT_BROKER", "1");

        if (!opts.broker_retained) {
            module.addCMacro("WOLFMQTT_BROKER_NO_RETAINED", "1");
        }
        if (!opts.broker_will) {
            module.addCMacro("WOLFMQTT_BROKER_NO_WILL", "1");
        }
        if (!opts.broker_wildcards) {
            module.addCMacro("WOLFMQTT_BROKER_NO_WILDCARDS", "1");
        }
        if (!opts.broker_auth) {
            module.addCMacro("WOLFMQTT_BROKER_NO_AUTH", "1");
        }
        if (!opts.broker_log) {
            module.addCMacro("WOLFMQTT_BROKER_NO_LOG", "1");
        }
        if (!opts.broker_insecure) {
            module.addCMacro("WOLFMQTT_BROKER_NO_INSECURE", "1");
        }
        if (opts.broker_debug) {
            module.addCMacro("WOLFMQTT_BROKER_DEBUG", "1");
        }
    }

    // Windows 特定定义
    if (target_info.os.tag == .windows) {
        module.addCMacro("_WINDLL", "1");
    }
}

/// 添加源文件到模块
fn addSourcesToModule(b: *std.Build, module: *std.Build.Module, opts: BuildOptions) void {
    // 添加核心源文件
    const core_sources = [_][]const u8{
        "src/mqtt_client.c",
        "src/mqtt_packet.c",
        "src/mqtt_socket.c",
    };

    for (core_sources) |src| {
        module.addCSourceFile(.{ .file = b.path(src) });
    }

    // 根据 MQTT-SN 选项添加相关源文件
    if (opts.mqtt_sn) {
        const sn_sources = [_][]const u8{
            "src/mqtt_sn_client.c",
            "src/mqtt_sn_packet.c",
        };
        for (sn_sources) |src| {
            module.addCSourceFile(.{ .file = b.path(src) });
        }
    }
}

/// 链接外部库
fn linkExternalLibraries(b: *std.Build, lib: *std.Build.Step.Compile, opts: BuildOptions, target_info: std.Target) void {
    // TLS 支持
    if (opts.tls) {
        const wolfssl_path = b.option(
            []const u8,
            "wolfssl-path",
            "wolfSSL installation path (contains include and lib directories)",
        ) orelse "/usr/local";

        const wolfssl_include = std.fmt.allocPrint(
            b.allocator,
            "{s}/include",
            .{wolfssl_path},
        ) catch @panic("Memory allocation failed");

        const wolfssl_lib = std.fmt.allocPrint(
            b.allocator,
            "{s}/lib",
            .{wolfssl_path},
        ) catch @panic("Memory allocation failed");

        lib.root_module.addIncludePath(b.path(wolfssl_include));
        lib.root_module.addLibraryPath(b.path(wolfssl_lib));
        lib.root_module.linkSystemLibrary("wolfssl", .{});
    }

    // 多线程支持
    if (opts.mt) {
        if (target_info.os.tag == .windows) {
            // Windows 不需要额外链接
        } else if (target_info.os.tag == .linux or
            target_info.os.tag == .macos or
            target_info.os.tag == .freebsd)
        {
            lib.root_module.linkSystemLibrary("pthread", .{});
        }
    }

    // Curl 支持
    if (opts.curl) {
        lib.root_module.linkSystemLibrary("curl", .{});
    }
}

/// 构建 wolfMQTT 静态库
fn buildWolfmqttLib(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    opts: BuildOptions,
) *std.Build.Step.Compile {
    // 创建根模块
    const root_module = b.createModule(.{
        .target = target,
        .optimize = optimize,
    });

    // 添加包含路径
    root_module.addIncludePath(b.path("wolfmqtt"));
    root_module.addIncludePath(b.path("."));

    // 添加编译定义
    addModuleDefinitions(root_module, opts, target.result);

    // 添加源文件到模块
    addSourcesToModule(b, root_module, opts);

    // 创建 wolfmqtt 静态库
    const lib = b.addLibrary(.{
        .linkage = .static,
        .name = "wolfmqtt",
        .root_module = root_module,
    });

    // 链接 C 标准库
    root_module.link_libc = true;

    // 链接外部库
    linkExternalLibraries(b, lib, opts, target.result);

    return lib;
}

/// 构建 broker 可执行文件
fn buildBroker(
    b: *std.Build,
    lib: *std.Build.Step.Compile,
    opts: BuildOptions,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
) void {
    // 确定最终的目标
    var final_target_query: std.Target.Query = .{
        .cpu_arch = target.result.cpu.arch,
        .os_tag = target.result.os.tag,
        .abi = target.result.abi,
    };

    // 如果启用静态链接，使用 musleabihf
    if (opts.static_link and target.result.os.tag == .linux) {
        final_target_query.abi = .musleabihf;
    }

    const resolved_target = b.resolveTargetQuery(final_target_query);

    const broker_root = b.createModule(.{
        .target = resolved_target,
        .optimize = optimize,
    });

    broker_root.addIncludePath(b.path("wolfmqtt"));
    broker_root.addIncludePath(b.path("."));
    broker_root.addCSourceFile(.{ .file = b.path("src/mqtt_broker.c") });

    addModuleDefinitions(broker_root, opts, target.result);

    const broker = b.addExecutable(.{
        .name = "mqtt_broker",
        .root_module = broker_root,
        .linkage = if (opts.static_link) .static else null,
    });

    broker.root_module.linkLibrary(lib);
    broker.root_module.link_libc = true;

    // Windows: link ws2_32.lib for socket functions
    if (target.result.os.tag == .windows) {
        broker.root_module.linkSystemLibrary("ws2_32", .{});
    }

    if (opts.mt) {
        if (target.result.os.tag == .linux or
            target.result.os.tag == .macos or
            target.result.os.tag == .freebsd)
        {
            broker.root_module.linkSystemLibrary("pthread", .{});
        }
    }

    // 设置自定义安装路径: zig-out/broker/<arch>/<os>/<abi>
    const install_prefix = b.fmt("broker/{s}/{s}/{s}", .{
        @tagName(target.result.cpu.arch),
        @tagName(target.result.os.tag),
        @tagName(target.result.abi),
    });

    const install_step = b.addInstallArtifact(broker, .{
        .dest_dir = .{ .override = .{ .custom = install_prefix } },
    });
    b.getInstallStep().dependOn(&install_step.step);

    // 生成构建摘要 README.md
    generateBuildReadme(b, "broker", target.result, opts);
}

/// 添加常用构建步骤
fn addCommonSteps(b: *std.Build) void {
    // 格式化步骤
    const fmt_step = b.step("fmt", "Format Zig source code");
    const fmt = b.addFmt(.{
        .paths = &.{"build.zig"},
        .check = false,
    });
    fmt_step.dependOn(&fmt.step);

    // 检查格式化步骤
    const fmt_check_step = b.step("fmt-check", "Check code formatting");
    const fmt_check = b.addFmt(.{
        .paths = &.{"build.zig"},
        .check = true,
    });
    fmt_check_step.dependOn(&fmt_check.step);
}

/// 解析命令行构建选项
fn parseBuildOptions(b: *std.Build) BuildOptions {
    return BuildOptions{
        .tls = b.option(bool, "tls", "Enable TLS support (requires wolfSSL)") orelse false,
        .mqtt_sn = b.option(bool, "mqtt-sn", "Enable MQTT-SN support") orelse false,
        .nonblock = b.option(bool, "nonblock", "Enable non-blocking support") orelse true,
        .no_timeout = b.option(bool, "no-timeout", "Disable timeout support") orelse false,
        .error_strings = b.option(bool, "error-strings", "Enable error strings") orelse true,
        .stdincap = b.option(bool, "stdincap", "Enable STDIN capture") orelse true,
        .v5 = b.option(bool, "v5", "Enable MQTT v5.0 support") orelse true,
        .discb = b.option(bool, "discb", "Enable disconnect callback") orelse true,
        .mt = b.option(bool, "mt", "Enable multi-threading support") orelse true,
        .curl = b.option(bool, "curl", "Enable curl easy socket backend") orelse false,
        .property_cb = b.option(bool, "property-cb", "Enable property callback (MQTT v5.0)") orelse true,
        .debug_client = b.option(bool, "debug-client", "Enable debug client logging") orelse false,
        .static_link = b.option(bool, "static-link", "Enable static linking for Linux targets") orelse false,
        .broker = b.option(bool, "broker", "Enable Broker support") orelse true, // 默认启用
        .broker_retained = b.option(bool, "broker-retained", "Enable Broker retained message support") orelse true,
        .broker_will = b.option(bool, "broker-will", "Enable Broker Last Will support") orelse true,
        .broker_wildcards = b.option(bool, "broker-wildcards", "Enable Broker wildcard topic matching") orelse true,
        .broker_auth = b.option(bool, "broker-auth", "Enable Broker username/password authentication") orelse true,
        .broker_log = b.option(bool, "broker-log", "Enable Broker logging") orelse true,
        .broker_insecure = b.option(bool, "broker-insecure", "Enable Broker plaintext listener") orelse true,
        .broker_debug = b.option(bool, "broker-debug", "Enable Broker detailed debug logging") orelse true,
    };
}

/// 生成构建摘要 README.md
fn generateBuildReadme(b: *std.Build, name: []const u8, target: std.Target, opts: BuildOptions) void {
    const readme_content = b.fmt(
        \\# {s} Build Summary
        \\
        \\## Target Platform
        \\- **Architecture**: {s}
        \\- **OS**: {s}
        \\- **ABI**: {s}
        \\
        \\## Compile-time Macros
        \\
        \\| Macro | Status |
        \\|-------|--------|
        \\| BUILDING_WOLFMQTT | ✅ Enabled |
        \\| BUILDING_ZIG | ✅ Enabled |
        \\| HAVE_CONFIG_H | ✅ Enabled |
        \\| ENABLE_MQTT_TLS | {s} |
        \\| WOLFMQTT_SN | {s} |
        \\| WOLFMQTT_NONBLOCK | {s} |
        \\| WOLFMQTT_NO_TIMEOUT | {s} |
        \\| WOLFMQTT_NO_ERROR_STRINGS | {s} |
        \\| WOLFMQTT_NO_STDIN_CAP | {s} |
        \\| WOLFMQTT_V5 | {s} |
        \\| WOLFMQTT_DISCONNECT_CB | {s} |
        \\| WOLFMQTT_MULTITHREAD | {s} |
        \\| ENABLE_MQTT_CURL | {s} |
        \\| WOLFMQTT_PROPERTY_CB | {s} |
        \\| WOLFMQTT_DEBUG_CLIENT | {s} |
        \\| WOLFMQTT_BROKER | {s} |
        \\| WOLFMQTT_BROKER_NO_RETAINED | {s} |
        \\| WOLFMQTT_BROKER_NO_WILL | {s} |
        \\| WOLFMQTT_BROKER_NO_WILDCARDS | {s} |
        \\| WOLFMQTT_BROKER_NO_AUTH | {s} |
        \\| WOLFMQTT_BROKER_NO_LOG | {s} |
        \\| WOLFMQTT_BROKER_NO_INSECURE | {s} |
        \\| WOLFMQTT_BROKER_DEBUG | {s} |
        \\
        \\---
        \\*Generated by Zig build system*
        \\
    ,
        .{
            name,
            @tagName(target.cpu.arch),
            @tagName(target.os.tag),
            @tagName(target.abi),
            if (opts.tls) "✅ Enabled" else "❌ Disabled",
            if (opts.mqtt_sn) "✅ Enabled" else "❌ Disabled",
            if (opts.nonblock) "✅ Enabled" else "❌ Disabled",
            if (opts.no_timeout) "✅ Enabled" else "❌ Disabled",
            if (!opts.error_strings) "✅ Enabled" else "❌ Disabled",
            if (!opts.stdincap) "✅ Enabled" else "❌ Disabled",
            if (opts.v5) "✅ Enabled" else "❌ Disabled",
            if (opts.discb) "✅ Enabled" else "❌ Disabled",
            if (opts.mt) "✅ Enabled" else "❌ Disabled",
            if (opts.curl) "✅ Enabled" else "❌ Disabled",
            if (opts.property_cb) "✅ Enabled" else "❌ Disabled",
            if (opts.debug_client) "✅ Enabled" else "❌ Disabled",
            if (opts.broker) "✅ Enabled" else "❌ Disabled",
            if (opts.broker and !opts.broker_retained) "✅ Enabled" else "❌ Disabled",
            if (opts.broker and !opts.broker_will) "✅ Enabled" else "❌ Disabled",
            if (opts.broker and !opts.broker_wildcards) "✅ Enabled" else "❌ Disabled",
            if (opts.broker and !opts.broker_auth) "✅ Enabled" else "❌ Disabled",
            if (opts.broker and !opts.broker_log) "✅ Enabled" else "❌ Disabled",
            if (opts.broker and !opts.broker_insecure) "✅ Enabled" else "❌ Disabled",
            if (opts.broker and opts.broker_debug) "✅ Enabled" else "❌ Disabled",
        },
    );

    // 写入 README.md 文件
    const output_dir = b.fmt("{s}/{s}/{s}/{s}", .{
        name,
        @tagName(target.cpu.arch),
        @tagName(target.os.tag),
        @tagName(target.abi),
    });

    const readme_path = b.fmt("{s}/README.md", .{output_dir});

    // 写入文件并安装（addInstallFile 会自动创建目录）
    const write_step = b.addWriteFiles();
    const readme_file = write_step.add(readme_path, readme_content);
    const install_readme = b.addInstallFile(readme_file, readme_path);
    b.getInstallStep().dependOn(&install_readme.step);
}
