//! wolfMQTT Zig 构建配置 (Zig 0.16+)
//! 参考 CMakeLists.txt 的配置
//! 本文件作为构建协调入口，实际的构建逻辑在 build/ 目录中

const std = @import("std");
const options_mod = @import("build/utils/options.zig");
const wolfmqtt_entry = @import("build/entries/wolfmqtt.zig");
const broker_entry = @import("build/entries/broker.zig");
const common_steps = @import("build/utils/common_steps.zig");
const readme_mod = @import("build/utils/readme.zig");

pub fn build(b: *std.Build) void {
    // 默认目标平台为 ARM Linux (arm-linux-gnueabihf)
    const target = b.standardTargetOptions(.{
        .default_target = .{
            .cpu_arch = .arm,
            .os_tag = .linux,
            .abi = .gnueabihf,
        },
    });

    // 默认优化模式：ReleaseSmall（优化体积）
    // 用户可通过 --release=fast 或 --release=safe 覆盖
    const optimize = b.standardOptimizeOption(.{
        .preferred_optimize_mode = .ReleaseSmall,
    });

    // 解析构建选项
    const opts = options_mod.parseBuildOptions(b);

    // 确定要执行的命令
    const command_str = b.option([]const u8, "command", "Build command (all, wolfmqtt, broker)") orelse "all";
    const command = std.meta.stringToEnum(enum { all, wolfmqtt, broker }, command_str) orelse .all;

    // 构建 wolfMQTT 静态库（始终构建，用于 broker 依赖）
    const lib = wolfmqtt_entry.build(b, target, optimize, opts);

    // 设置 wolfmqtt 安装路径和安装步骤
    const wolfmqtt_install_prefix = b.fmt("wolfmqtt/{s}-{s}", .{
        @tagName(target.result.os.tag),
        @tagName(target.result.cpu.arch),
    });
    const wolfmqtt_install_step = b.addInstallArtifact(lib, .{
        .dest_dir = .{ .override = .{ .custom = wolfmqtt_install_prefix } },
    });
    // 默认 install step 依赖 wolfmqtt
    b.getInstallStep().dependOn(&wolfmqtt_install_step.step);

    // 创建 wolfmqtt 独立构建步骤
    const wolfmqtt_step = b.step("wolfmqtt", "Build and install wolfmqtt library");
    wolfmqtt_step.dependOn(&wolfmqtt_install_step.step);

    // 根据命令选择构建内容
    switch (command) {
        .all => {
            // 构建 broker 可执行文件（如果启用）
            if (opts.broker) {
                broker_entry.build(b, lib, opts, target, optimize);
            }

            // 生成构建摘要
            readme_mod.generateBuildReadme(b, "wolfmqtt", target.result, opts);
            if (opts.broker) {
                readme_mod.generateBuildReadme(b, "broker", target.result, opts);
            }
        },
        .wolfmqtt => {
            // 仅构建 wolfmqtt 库
            readme_mod.generateBuildReadme(b, "wolfmqtt", target.result, opts);
        },
        .broker => {
            // 仅构建 broker
            if (opts.broker) {
                broker_entry.build(b, lib, opts, target, optimize);
                readme_mod.generateBuildReadme(b, "broker", target.result, opts);
            } else {
                std.debug.print("Warning: broker is disabled in options\n", .{});
            }
        },
    }

    // 注意：demo 现在是独立构建项目，请使用 cd buildSchemas/demo && zig build

    // 添加常用构建步骤
    common_steps.addCommonSteps(b);
}
