# ClipSync

ClipSync 是一个基于 WebDAV 的 Windows 剪贴板同步工具。它会把本机剪贴板文本同步到远端 JSON 文件，并定期拉取远端内容更新本机剪贴板，适合在多台 Windows 设备之间同步纯文本。

## 工作方式

程序启动时先读取远端剪贴板：

- 远端存在内容时，将远端文本写入本机剪贴板。
- 远端没有内容时，将本机当前剪贴板上传到远端。
- 运行期间按 `pollIntervalMs` 定期检查远端变化。
- 使用文本哈希和 WebDAV `ETag` 避免重复同步和循环更新。

当前版本只同步文本，不同步图片、文件或富文本格式。

## 配置

复制 `clipsync.example.json` 为 `clipsync.json`，然后填写 WebDAV 信息：

```json
{
  "davUrl": "https://dav.example.com/dav/",
  "remotePath": "clipsync",
  "username": "your-user",
  "password": "your-password",
  "pollIntervalMs": 3000
}
```

字段说明：

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `davUrl` | 是 | WebDAV 服务地址，例如 `https://example.com/dav/` |
| `remotePath` | 否 | 远端工作目录；目录不存在时程序会尝试创建 |
| `username` | 否 | HTTP Basic Authentication 用户名 |
| `password` | 否 | HTTP Basic Authentication 密码 |
| `pollIntervalMs` | 否 | 拉取间隔，默认 3000 毫秒，最小 250 毫秒 |

程序会在该目录下使用 `clipboard/current.json` 保存同步内容。请确保 WebDAV 账号具有读取、写入和创建目录的权限。

> `clipsync.json` 含有密码时不要提交到 Git 或分享给他人。建议使用 HTTPS，并为同步服务创建权限受限的专用账号。

## 命令行

```text
ClipSync.exe [options]
```

| 参数 | 说明 |
| --- | --- |
| `-h`, `--help` | 显示帮助 |
| `-v`, `--version` | 显示版本 |
| `--config <path>` | 指定配置文件，默认 `clipsync.json` |
| `--once` | 执行一次启动同步后退出 |
| `--hidden` | 隐藏 Windows 控制台窗口，适用于计划任务 |

示例：

```powershell
.\ClipSync.exe --config clipsync.json
.\ClipSync.exe --config clipsync.json --hidden
.\ClipSync.exe --config clipsync.json --once
```

## Windows 计划任务

在任务计划程序的“操作”中配置：

1. **程序或脚本**：直接选择 `ClipSync.exe` 的完整路径，不要填写 `cmd.exe`、`powershell.exe` 或批处理文件。
2. **添加参数**：

   ```text
   --config clipsync.json --hidden
   ```

3. **起始于**：填写 `clipsync.json` 所在目录，例如 `D:\\Apps\\ClipSync`。
4. 在“常规”中选择合适的运行账户。需要无人登录时，选择“无论用户是否登录都运行”。

`--hidden` 只控制 ClipSync 自身创建的控制台窗口。如果通过脚本或命令解释器启动，外层的终端窗口仍可能显示；因此应让计划任务直接启动 `.exe`。

## 构建

依赖：

- CMake 3.16 或更高版本
- C++17 编译器（MSVC、Clang 或 GCC）
- Ninja（推荐）
- Windows 构建需要 libcurl 和 nlohmann-json，可通过 vcpkg 安装

PowerShell：

```powershell
.\build.ps1 -BuildType Release
```

也可以直接使用 CMake 预设：

```powershell
cmake --preset win-msvc-ninja-release
cmake --build --preset win-msvc-ninja-release
```

生成的程序位于对应构建目录的 `bin` 子目录中。

## 故障排查

- **无法打开配置文件**：检查计划任务的“起始于”，或使用 `--config` 指定绝对路径。
- **HTTP 401/403**：检查用户名、密码和 WebDAV 权限。
- **HTTP 404/409**：确认 `davUrl` 和 `remotePath` 正确，并确认账号允许创建目录。
- **看不到同步变化**：确认多台设备使用同一个远端目录，并检查 `pollIntervalMs`。
- **仍有任务栏终端图标**：确认“程序或脚本”直接指向 `ClipSync.exe`，没有经过 `cmd.exe`、PowerShell 或 `.bat`。

## 许可证

项目当前未声明单独的开源许可证。未经项目维护者许可，请勿将其作为第三方产品分发。
