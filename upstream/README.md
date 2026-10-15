# 固定 Muse Gadget SDK 上游

`sources.json` 只保存 Muse Gadget SDK 的来源、固定版本和归档校验值。Actell 是独立维护的协作应用，不作为本工程的补丁上游。本目录不存放完整 SDK 源码。

| 内容 | 位置 |
|---|---|
| 原始下载归档 | `local/upstream/archives/` |
| 干净上游检出 | `local/upstream/checkouts/` |
| 应用扩展后的工作副本 | `local/build/` |
| 本工程扩展 | `bridge/extensions/` |
| 对 SDK 的修改 | `bridge/patches/muse-gadget-sdk/` |

从清单 URL 获取固定 commit 后核对 `git rev-parse HEAD`；若使用已有归档，核对 `archive_sha256`。该哈希对应本机已有归档的精确字节，其他下载方式可能产生不同归档封装，不能据此声称同一个文件已验证。

在独立工作副本中进行补丁暂存和构建。Muse 的官方账户、配对、Noise 控制通道、硬件驱动和基础 UI 属于上游；本工程增加 Actell 命令组件、可选角色和无屏调试扩展。查看 [补丁说明](../bridge/patches/README.md) 与 [第三方许可](../THIRD_PARTY.md)。
