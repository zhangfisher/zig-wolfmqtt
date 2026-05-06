# wolfMQTT 库体积优化 - 快速参考

## 🚀 一键优化命令

### ⚠️ 重要提示

**build.zig 默认配置** (590 KB) vs **--release 标志** (46 KB)
- `zig build` → 590 KB（仅编译器优化，保留调试信息）
- `zig build --release=small` → 46 KB（完整发布优化，⭐推荐）

### 最小体积 (46 KB) ⭐强烈推荐
```bash
zig build wolfmqtt --release=small
```

### 平衡配置 (46 KB + 功能裁剪)
```bash
zig build wolfmqtt --release=small \
  -Derror-strings=false \
  -Dv5=false \
  -Dmt=false
```

### 完整功能 (590 KB) - 仅用于开发
```bash
zig build wolfmqtt
```

---

## 📊 体积对比

| 配置 | 大小 | 减少 | 适用场景 |
|------|------|------|---------|
| 默认 (build.zig) | 590 KB | - | 开发阶段 |
| **--release=small** | **46 KB** | **92%** | ⭐ **生产部署** |
| --release=fast | 46 KB | 92% | 性能敏感 |
| --release=safe | 46 KB | 92% | 安全检查 |

---

## 🔑 关键选项

| 选项 | 节省空间 | 说明 |
|------|---------|------|
| `-Derror-strings=false` | ~10 KB | 禁用错误消息文本 |
| `-Dv5=false` | ~100 KB | 禁用 MQTT v5.0 |
| `-Dmt=false` | ~20 KB | 禁用多线程支持 |
| `-Dproperty-cb=false` | ~10 KB | 禁用属性回调 |
| `-Ddiscb=false` | ~5 KB | 禁用断开回调 |

---

## 💡 快速决策

**问：我的设备 Flash 只有 256KB？**
→ 使用最小配置 (314 KB 可能需要裁剪更多)

**问：我需要 MQTT v5.0 但不需要多线程？**
→ 使用平衡配置

**问：我在开发阶段，需要调试信息？**
→ 使用完整配置

**问：我部署到树莓派等 Linux 设备？**
→ 使用平衡配置或完整配置

---

## 📖 详细文档

- `LIB_SIZE_ANALYSIS.md` - 详细分析报告
- `LIB_SIZE_OPTIMIZATION.md` - 完整优化指南
