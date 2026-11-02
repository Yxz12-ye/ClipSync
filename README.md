# ClipSync

ClipSync 是 Windows 纯文本剪贴板同步工具，对接中州鹅剪贴板同步中的「自定义」类型（SyncClipboard 协议）。两端连接同一服务、使用相同账号即可互相同步。

## 工作方式

- 使用 HTTP Basic Authentication，GET / PUT 服务地址下的 `SyncClipboard.json`。
- 发送 `Type: "Text"`、`Text`、`Hash`、`HasData: false`、`DataName: ""`、`Size`；`Hash` 为 UTF-8 文本的 SHA-256 小写十六进制，`Size` 为 UTF-8 字节数，与中州鹅一致。
- 启动时优先接收远端文本；仅当远端返回 HTTP 404 时上传本机当前文本。
- 拉取失败、JSON 无效或远端为图片/文件时，保留本机剪贴板；常驻运行时继续重试。`--once` 同步失败时返回非零退出码。
- 按 `pollIntervalMs` 轮询，使用 ETag 和文本哈希避免重复处理及回传。

当前只同步文本（包括空文本），不下载图片、文件或同步历史记录。服务端必须支持对应 JSON 文件的 GET / PUT；使用 WebDAV 存储时请预先创建父目录。

## 配置

复制 `clipsync.example.json` 为 `clipsync.json`，填写与中州鹅「自定义」页面一致的服务地址及账号：

```json
{
  "serverUrl": "https://sync.example.com/",
  "username": "your-user",
  "password": "your-password",
  "pollIntervalMs": 3000
}
```

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `serverUrl` | 是 | 服务根地址或工作目录，例如 `https://example.com/dav/clipboard/`；也可直接填写完整的 `SyncClipboard.json` URL |
| `username` | 否 | 与中州鹅一致的 Basic 认证用户名 |
| `password` | 否 | 与中州鹅一致的密码 |
| `pollIntervalMs` | 否 | 拉取间隔，默认 3000 毫秒，最小 250 毫秒 |

迁移旧配置时，删除 `davUrl` / `remotePath`，将完整工作目录填入 `serverUrl`。例如原地址 `https://example.com/dav/`、目录 `clipsync`，若继续使用该目录，则两端地址都填 `https://example.com/dav/clipsync/`。程序读写该目录下的 `SyncClipboard.json`，不再使用原来的 `clipboard/current.json`，也不自动导入旧内容。

> `clipsync.json` 含有密码时不要提交到 Git 或分享给他人。建议使用 HTTPS。

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
4. 在“常规”中选择合适的运行账户。请选择“只在用户登录时运行”，确保程序能访问当前桌面会话的剪贴板。

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

协议集成测试需要 Python 3，使用本地模拟 HTTP 服务和内存剪贴板，不修改系统剪贴板：

```powershell
cmake --preset win-msvc-ninja-release -DENABLE_PROTOCOL_TESTING=ON
cmake --build --preset win-msvc-ninja-release
ctest --test-dir build/win-msvc-ninja-release --output-on-failure
```

## 故障排查

- **无法打开配置文件**：检查计划任务的“起始于”，或使用 `--config` 指定绝对路径。
- **HTTP 401/403**：检查两端用户名、密码和服务权限。
- **HTTP 404/409**：确认 `serverUrl` 与中州鹅一致；如果使用 WebDAV，请预先创建工作目录并允许 GET / PUT。
- **看不到同步变化**：确认中州鹅选用“自定义”类型、各设备使用同一个服务地址和账号，并检查 `pollIntervalMs`。
- **仍有任务栏终端图标**：确认“程序或脚本”直接指向 `ClipSync.exe`，没有经过 `cmd.exe`、PowerShell 或 `.bat`。

## 许可证

项目当前未声明单独的开源许可证。未经项目维护者许可，请勿将其作为第三方产品分发。
