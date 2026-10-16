# Muse × Actell Bridge

[English](README.md) | 简体中文

Muse Gadget SDK 的独立 Actell 扩展工程，面向 Waveshare ESP32-S3-Touch-AMOLED-2.16，并提供可选无屏配置。Muse 负责规划，设备端组件通过 HTTPS/HMAC 调用 Actell，复用 Actell 的授权、审批和执行队列。

## 目录

| 目录 | 内容 |
|---|---|
| `bridge/` | 本项目工程链路：host、extensions（含 skills）、patches、tools |
| `upstream/` | Muse Gadget SDK 的 URL、固定 commit、归档哈希；不含完整 SDK 源码 |
| `docs/` | 协议、架构和使用说明 |
| `local/` | 本机资料，整体忽略，不纳入 Git 分享 |

工程内部结构见 [bridge/README.md](bridge/README.md)，约定见 [AGENTS.md](AGENTS.md)。`LICENSE` 与 [THIRD_PARTY.md](THIRD_PARTY.md) 放在根目录，集中说明项目许可和第三方来源。本机日志、验证报告、缓存、构建、上游归档、凭据和真实截图分别放在 `local/` 的对应子目录。历史材料已保留于该目录，旧报告内的路径只代表当时环境。Avatar 的 `bridge/extensions/avatar/preview/` 也整体忽略，仅保留本机，不提交或上传。

## 本机开发

需要 Python 3.10+；完整主机测试还需要 Node.js 和 C 编译器。Python 客户端只使用标准库。

```sh
python3 bridge/tools/dev.py test
python3 bridge/tools/dev.py console --port 8766
# 或仅启动协议 mock：
python3 bridge/tools/dev.py mock
```

网页使用 `http://127.0.0.1:8766`。这些入口使用合成截图和模拟工具，不会操作手机；自然语言接口明确报告尚未接入真实 Muse 会话。测试会自行创建并关闭临时 loopback 服务。日志写入 `local/logs/current/`，Python 缓存和临时构建分别写入 `local/cache/` 与 `local/build/`。

其他 Python 使用方式：从 `bridge/host/` 运行模块，或设置 `PYTHONPATH=bridge/host`。客户端 API 示例见 [主机客户端](docs/host-client.md)。

TTS 接口配置检查在 ESP-IDF Python 环境运行：`python3 bridge/tools/validation/run-tts-kconfig.py`；worker 与目标构建验证见 [TTS 说明](docs/doubao-tts.md)。

## 上游与本工程的边界

- **上游**：官方账户/配对、Noise 控制通道、板卡驱动和基础 Muse UI；固定版本见 [来源清单](upstream/sources.json)。完整源码与开发/测试密钥仅保存在 `local/upstream/`。
- **本工程扩展**：[gadget-actell](bridge/extensions/gadget-actell/README.md)、[avatar](bridge/extensions/avatar/README.md)、[headless console](docs/headless-console.md) 及可选[豆包回复语音](docs/doubao-tts.md)。源码在 `bridge/extensions/` 维护，暂存 SDK 用于构建验证。TTS 默认使用校验证书的双向 WebSocket，可通过关闭 `CONFIG_DOUBAO_TTS_BIDIRECTIONAL` 编译切换至 HTTP 流式实现，复用现有 PCM 播放；云端及真机播放需单独验收。
- **SDK 修改**：[patches](bridge/patches/README.md) 仅保存 Muse Gadget SDK 的接点和适配补丁。Headless 新文件在扩展目录与补丁中保持同步。
- **Actell 应用**：独立工程维护连接、授权与手机执行实现；在 Actell 工程直接开发和验证。本工程不提供 Actell 安装补丁，历史 V1 补丁不再用于当前联调。
- **主机原型**：`bridge/host/` 用于协议和 UI 回归；当前设备组件直接使用 Wi-Fi HTTPS，无需实现历史原型提出的 Muse VM Home Link transport。

角色默认使用 160×160 v13，可通过编译开关回退原 64×64 版本；头部距离预筛减少聆听渲染计算，采样像素输出保持一致，见[性能说明](docs/avatar-listening-performance.md)；暂存保留 320 像素布局并兼容字幕字体/TTS。主机逐像素检查及目标构建不代表真机视觉或并发性能验收，见[角色说明](bridge/extensions/avatar/README.md)。

开始构建前，在 `local/build/` 准备固定 SDK 的独立副本，按补丁说明应用并暂存扩展。不要混入 Actell 原有 USB-C/NCM 转发固件。目标编译、真实 Muse 账户、USB 配置、手机执行和模型图像输入均需分别验证，主机测试不能替代真机验收。

秘密配置由用户指定路径，推荐放在工程目录外。`--secrets` 为必填参数，不设置默认工程内路径。用 `python3 bridge/tools/apply-secrets.py --secrets /path/outside/project/device-secrets.conf --init` 创建 0600 占位文件，再由用户本机编辑；仅允许工具读取，代理不得直接查看、展示或将源文件复制进工程。通过 `python3 bridge/tools/apply-secrets.py --secrets /path/outside/project/device-secrets.conf --sdkconfig local/build/sdk-copy/esp32/build-tts/sdkconfig` 导入，工具不回显值。文件仅填写 `CONFIG_GADGET_SDK_TOKEN` 和可选的 `CONFIG_DOUBAO_TTS_API_KEY`；API key 非空时自动启用 TTS，资源与音色在组件中固定。字段与构建步骤见 [TTS 配置说明](docs/doubao-tts.md)。 `--sdk-token-only` 仅用于独立字体构建，会移除 provider 配置；准备 TTS 或字体/TTS 组合固件时不使用该模式。

可选 [Waveshare 2.16 按键扩展](bridge/extensions/waveshare-216-controls/README.md) 将左 BOOT、中 PWR、右 KEY3 分别用于扬声器、休眠/唤醒与硬件长按关机、Talk；保留触屏设置。获授权的正式构建可在字体/TTS runner 追加 `--with-controls`，并使用扩展提供的非活动 app 槽更新工具。该扩展在字体等既有扩展之后最后应用，显示版 overlay 启用原生 24px 电源图标。`dev.py test` 中的按键回归需要 `local/upstream/archives/` 下已核验的固定 SDK 归档。基础按键行为和图标位置已获实机确认；长按时序、唤醒功耗、实际音频及完整显示验收仍待完成。

## 协议与资料

- [Muse Gadget SDK 改动索引](docs/muse-sdk-changes.md)：按类别简述 SDK 适配，引用详细说明。
- [Muse Gadget SDK 扩展功能索引](docs/muse-sdk-extensions.md)：按类别简述新增能力，引用详细说明。
- [Gadget V2](docs/protocol/gadget-v2.md) / [主机 V1](docs/protocol/v1.md)
- [可选角色动作](docs/optional-avatar-events.md)
- [Muse skill](bridge/extensions/skills/gadget-actell-remote/SKILL.md)
- [来源与许可](THIRD_PARTY.md)

两份索引随实现同步维护，按类别简述差异，不写日期、不使用编号；实现、配置和验证细节保留在链接的详细文档中。

本工程独立代码遵守根目录 Apache-2.0 LICENSE；Actell 作为独立应用保留其自身 GPL-3.0 许可。公开合成鉴权向量属于测试数据；工程内的真实 token、配对信息和私钥存放在 `local/credentials/`，用户指定的秘密配置源文件可保留在工程外。

实验性[字幕字体与共享资源](docs/caption-font-integration.md)提供默认关闭的读取器、有界逐字形解码、像素分页、原字体回退和公开配置固件构建。[资源包](docs/read-only-resources.md)中的 Actell Device Sans 候选来自固定 Adobe 字体，字体单独保留 OFL-1.1 授权，生成数据留在 `local/`。字体构建入口也支持 `--mode on --with-tts --secrets` 生成受保护的组合固件，需先暂存两个扩展。资源布局保留原有数据分区与双 OTA 槽位；已完成首次资源部署和固件启动核查；最终字体选型、显示/性能验收及回滚验证仍待完成。 添加 `--tts-transport http` 可构建原 HTTP 单向接口，输出隔离到 `private-fonts-tts-http/`；省略时仍默认双向。

## Git 分享前

```sh
python3 bridge/tools/check-share.py
git status --short
```

检查器扫描已跟踪及未忽略文件，只报告路径和问题类型，不打印凭据；它不替代人工审查。对外归档从已审查的 Git 提交生成，输出到 `local/`，不要压缩整个工作目录。

需要比对实际凭据时，运行 `python3 bridge/tools/apply-secrets.py --secrets /path/outside/project/device-secrets.conf --audit-share`。工具检查 Git 可见文件及暂存内容，仅报告脱敏后的匹配位置，不回显值或匹配行。
