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

## 上游与本工程的边界

- **上游**：官方账户/配对、Noise 控制通道、板卡驱动和基础 Muse UI；固定版本见 [来源清单](upstream/sources.json)。完整源码与开发/测试密钥仅保存在 `local/upstream/`。
- **本工程扩展**：[gadget-actell](bridge/extensions/gadget-actell/README.md)、[avatar](bridge/extensions/avatar/README.md)、[headless console](docs/headless-console.md)。源码在 `bridge/extensions/` 维护，暂存 SDK 仅用于构建验证。
- **SDK 修改**：[patches](bridge/patches/README.md) 仅保存 Muse Gadget SDK 的接点和适配补丁。Headless 新文件在扩展目录与补丁中保持同步。
- **Actell 应用**：独立工程维护连接、授权与手机执行实现；在 Actell 工程直接开发和验证。本工程不提供 Actell 安装补丁，历史 V1 补丁不再用于当前联调。
- **主机原型**：`bridge/host/` 用于协议和 UI 回归；当前设备组件直接使用 Wi-Fi HTTPS，无需实现历史原型提出的 Muse VM Home Link transport。

开始构建前，在 `local/build/` 准备固定 SDK 的独立副本，按补丁说明应用并暂存扩展。不要混入 Actell 原有 USB-C/NCM 转发固件。目标编译、真实 Muse 账户、USB 配置、手机执行和模型图像输入均需分别验证，主机测试不能替代真机验收。

## 协议与资料

- [Muse Gadget SDK 改动索引](docs/muse-sdk-changes.md)：按类别简述 SDK 适配，引用详细说明。
- [Muse Gadget SDK 扩展功能索引](docs/muse-sdk-extensions.md)：按类别简述新增能力，引用详细说明。
- [Gadget V2](docs/protocol/gadget-v2.md) / [主机 V1](docs/protocol/v1.md)
- [可选角色动作](docs/optional-avatar-events.md)
- [Muse skill](bridge/extensions/skills/gadget-actell-remote/SKILL.md)
- [来源与许可](THIRD_PARTY.md)

两份索引随实现同步维护，按类别简述差异，不写日期、不使用编号；实现、配置和验证细节保留在链接的详细文档中。

本工程独立代码遵守根目录 Apache-2.0 LICENSE；Actell 作为独立应用保留其自身 GPL-3.0 许可。公开合成鉴权向量属于测试数据，真实 token、配对信息和私钥只能存放在 `local/credentials/`。

实验性[字幕字体与共享资源](docs/caption-font-integration.md)提供默认关闭的读取器、有界逐字形解码、像素分页、原字体回退和公开配置固件构建。[资源包](docs/read-only-resources.md)中的 Actell Device Sans 候选来自固定 Adobe 字体，字体单独保留 OFL-1.1 授权，生成数据留在 `local/`。资源布局保留原有数据分区与双 OTA 槽位；已完成首次资源部署和固件启动核查；最终字体选型、显示/性能验收及回滚验证仍待完成。

## Git 分享前

```sh
python3 bridge/tools/check-share.py
git status --short
```

检查器扫描已跟踪及未忽略文件，只报告路径和问题类型，不打印凭据；它不替代人工审查。对外归档从已审查的 Git 提交生成，输出到 `local/`，不要压缩整个工作目录。
