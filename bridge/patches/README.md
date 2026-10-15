# Muse Gadget SDK 修改

本目录只维护 Muse Gadget SDK 补丁，基于 `upstream/sources.json` 中的固定 commit，在 SDK 仓库根目录应用；补丁文件内部路径保持 SDK 布局。Actell 的实现直接在其独立工程维护，不使用本目录的补丁流程。

| 补丁 | 内容 | 维护位置 |
|---|---|---|
| `muse-gadget-sdk/headless.patch` | 无屏构建入口、文字事件和开发 HTTP 控制台 | 新增文件同时保留于 `bridge/extensions/headless-console/`，需同步 |
| `muse-gadget-sdk/actell-hook.patch` | 官方命令注册/调用、会话、USB setup 和组件依赖接点 | 本补丁；独立组件位于 `bridge/extensions/gadget-actell/` |
| `muse-gadget-sdk/doubao-tts.patch` | 可选豆包回复语音、PCM 输出和取消接点 | 本补丁；独立组件位于 `bridge/extensions/doubao-tts/` |

`actell-hook.patch` 修改的是 Muse SDK 的注册与调用接点，名称中的 Actell 表示集成对象；它不是针对 Actell 工程的补丁。

Muse 工作副本放在 `local/build/`。若使用 headless，先检查并应用 `headless.patch`，再运行 `bridge/extensions/gadget-actell/tools/stage.py`；显示板需要语音时再运行 `bridge/extensions/doubao-tts/tools/stage.py`，最后按需运行 `bridge/extensions/avatar/tools/stage.py`。Avatar 使用官方 override 入口，不需要修改上游 CMake。无屏文字控制台不启用 TTS。

```sh
git -C local/build/sdk-copy apply --check "$PWD/bridge/patches/muse-gadget-sdk/headless.patch"
git -C local/build/sdk-copy apply "$PWD/bridge/patches/muse-gadget-sdk/headless.patch"
python3 bridge/extensions/gadget-actell/tools/stage.py local/build/sdk-copy --check
python3 bridge/extensions/gadget-actell/tools/stage.py local/build/sdk-copy
python3 bridge/extensions/avatar/tools/stage.py --sdk-root local/build/sdk-copy
```

应用前需存在新的、完整的固定 SDK 副本；脚本拒绝覆盖已有组件/角色。删除暂存副本不会丢失这里保存的扩展与补丁。
