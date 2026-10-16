# Muse Gadget SDK 修改

本目录只维护 Muse Gadget SDK 补丁，基于 `upstream/sources.json` 中的固定 commit，在 SDK 仓库根目录应用；补丁文件内部路径保持 SDK 布局。Actell 的实现直接在其独立工程维护，不使用本目录的补丁流程。

| 补丁 | 内容 | 维护位置 |
|---|---|---|
| `muse-gadget-sdk/headless.patch` | 无屏构建入口、文字事件和开发 HTTP 控制台 | 新增文件同时保留于 `bridge/extensions/headless-console/`，需同步 |
| `muse-gadget-sdk/actell-hook.patch` | 官方命令注册/调用、会话、USB setup 和组件依赖接点 | 本补丁；独立组件位于 `bridge/extensions/gadget-actell/` |
| `muse-gadget-sdk/doubao-tts.patch` | 可选豆包回复语音、PCM 输出和取消接点 | 本补丁；独立组件位于 `bridge/extensions/doubao-tts/` |
| `muse-gadget-sdk/waveshare-216-controls.patch` | 仅 2.16 独立按键、可靠 PMU 事件和顶部状态图标 | 本补丁；状态机位于 `bridge/extensions/waveshare-216-controls/` |
| `muse-gadget-sdk/caption-font.patch` | 可选共享字体、实际行高与像素分页接点 | 本补丁；独立实现位于 `bridge/extensions/muse-fonts/` |

`actell-hook.patch` 修改的是 Muse SDK 的注册与调用接点，名称中的 Actell 表示集成对象；它不是针对 Actell 工程的补丁。

Muse 工作副本放在 `local/build/`。若使用 headless，先检查并应用 `headless.patch`，再运行 `bridge/extensions/gadget-actell/tools/stage.py`，按需暂存 avatar，按需运行 `bridge/extensions/muse-fonts/tools/stage.py` 接入共享资源，再按需运行 `bridge/extensions/doubao-tts/tools/stage.py` 接入显示板 TTS。Avatar 使用官方 override 入口，不需要修改上游 CMake。无屏文字控制台不启用 TTS。

```sh
git -C local/build/sdk-copy apply --check "$PWD/bridge/patches/muse-gadget-sdk/headless.patch"
git -C local/build/sdk-copy apply "$PWD/bridge/patches/muse-gadget-sdk/headless.patch"
python3 bridge/extensions/gadget-actell/tools/stage.py local/build/sdk-copy --check
python3 bridge/extensions/gadget-actell/tools/stage.py local/build/sdk-copy
python3 bridge/extensions/avatar/tools/stage.py --sdk-root local/build/sdk-copy
python3 bridge/extensions/muse-fonts/tools/stage.py local/build/sdk-copy --check
python3 bridge/extensions/muse-fonts/tools/stage.py local/build/sdk-copy
python3 bridge/extensions/doubao-tts/tools/stage.py local/build/sdk-copy --check
python3 bridge/extensions/doubao-tts/tools/stage.py local/build/sdk-copy
```

应用前需存在新的、完整的固定 SDK 副本；脚本拒绝覆盖已有组件/角色。删除暂存副本不会丢失这里保存的扩展与补丁。

显示版按需在全部既有扩展之后，最后运行 `python3 bridge/extensions/waveshare-216-controls/tools/stage.py local/build/sdk-copy --check`，再去掉 `--check` 应用。该扩展支持重复检查和安全反向应用，保持固定基线；配置、事件合并规则及实物验收边界见 [2.16 controls](../extensions/waveshare-216-controls/README.md)。不要将显示版 overlay 用于 headless。
