# 根路径映射修复

## 问题描述

访问 `http://192.168.116.217:8081/` 时出现以下错误：

```
[WARN ] Static file request rejected (unsafe path): /
[DEBUG] API Response 404 GET / to=192.168.116.218
```

**原因**: 
1. `is_safe_path()` 函数拒绝了以 `/` 开头的路径
2. 根路径 `/` 没有自动映射到 `index.html`

## 解决方案

### 1. 修改路径安全检查

只拒绝目录遍历（`..`），允许其他所有路径：

```c
static int is_safe_path(const char* path) {
    if (!path) return 0;
    
    /* Allow root path */
    if (XSTRCMP(path, "/") == 0)
        return 1;
    
    /* Only reject directory traversal attempts (..) */
    if (strstr(path, "..") != NULL)
        return 0;
    
    return 1;
}
```

**关键改变**：
- ❌ 之前：拒绝所有以 `/` 开头的路径
- ✅ 现在：只拒绝包含 `..` 的路径

### 2. 根路径映射到 index.html

在 `handle_static_file()` 函数中添加特殊处理：

```c
/* Handle root path - map to index.html */
const char* effective_path = url_path;
char root_index_path[512];
if (XSTRCMP(url_path, "/") == 0) {
    header_len = XSNPRINTF(root_index_path, sizeof(root_index_path), 
                          "%s/index.html", static_dir);
    effective_path = root_index_path;
    BA_LOG_DBG(broker, "Root path detected, mapping to: %s", root_index_path);
} else {
    /* Build full file path */
    header_len = XSNPRINTF(full_path, sizeof(full_path), 
                          "%s/%s", static_dir, url_path);
    effective_path = full_path;
}
```

## 路径处理逻辑

```
请求路径              处理方式                    实际文件
─────────────────────────────────────────────────────────
/                   → www/index.html           (根路径映射)
/etc/passwd         → www/etc/passwd           (允许，但文件不存在)
/about/             → www/about/index.html     (目录映射)
/style.css          → www/style.css            (普通文件)
/images/logo.png    → www/images/logo.png      (普通文件)
../etc/passwd       → 400 Bad Request          (拒绝，目录遍历)
/../../etc/passwd   → 400 Bad Request          (拒绝，目录遍历)
```

## 安全考虑

### 允许的路径
- ✅ `/` - 根路径（映射到 index.html）
- ✅ `/etc/passwd` - 会映射到 `www/etc/passwd`（文件通常不存在）
- ✅ `about/` - 相对路径目录
- ✅ `css/style.css` - 相对路径文件
- ✅ `images/logo.png` - 嵌套目录文件

### 拒绝的路径
- ❌ `../secret.txt` - 目录遍历
- ❌ `../../etc/passwd` - 多层目录遍历
- ❌ `about/../config` - 包含 `..` 的任何路径

## 测试验证

### 测试根路径
```bash
curl -u admin:22182666 http://192.168.116.217:8081/
# 应该返回 www/index.html 的内容
```

### 测试目录路径
```bash
curl -u admin:22182666 http://192.168.116.217:8081/about/
# 应该返回 www/about/index.html 的内容
```

### 测试普通文件
```bash
curl -u admin:22182666 http://192.168.116.217:8081/style.css
# 应该返回 www/style.css 的内容
```

### 测试不安全路径
```bash
# 目录遍历 - 应该被拒绝
curl -u admin:22182666 http://192.168.116.217:8081/../etc/passwd
# 返回: 400 Bad Request

curl -u admin:22182666 http://192.168.116.217:8081/../../etc/passwd
# 返回: 400 Bad Request

# 包含 .. 的路径 - 应该被拒绝
curl -u admin:22182666 http://192.168.116.217:8081/about/../config
# 返回: 400 Bad Request

# 以 / 开头的路径 - 允许（会映射到 www/ 下）
curl -u admin:22182666 http://192.168.116.217:8081/etc/passwd
# 返回: 404 Not Found (因为 www/etc/passwd 文件不存在)
# 这是安全的，因为实际访问的是 www/etc/passwd，不是系统的 /etc/passwd
```

## 日志输出

### 成功访问根路径
```
[DEBUG] Root path detected, mapping to: www/index.html
[DEBUG] Static file served: / (1234 bytes, total_sent=1234)
```

### 成功访问目录
```
[DEBUG] Directory detected, mapping to: www/about/index.html
[DEBUG] Static file served: /about/ (2048 bytes, total_sent=2048)
```

### 拒绝不安全路径
```
[WARN] Static file request rejected (unsafe path): ../etc/passwd
```

## 相关文件

- 修改文件: `src/mqtt_broker_api.c`
  - `is_safe_path()` 函数
  - `handle_static_file()` 函数
- 相关文档: `docs/WWW_AUTHENTICATE_FIX.md`

## 总结

✅ 根路径 `/` 现在可以正常访问  
✅ 自动映射到 `static_dir/index.html`  
✅ 只拒绝目录遍历（`..`），允许其他所有路径  
✅ `/etc/passwd` 等路径会安全地映射到 `www/etc/passwd`  
✅ 符合 Web 服务器标准行为  
✅ 改善了用户体验  
