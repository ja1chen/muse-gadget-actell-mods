# Muse Gadget SDK 改动索引

按类别概括本项目相对上游 SDK 的适配。固定基线见 [来源清单](../upstream/sources.json)，新增能力见 [扩展功能索引](muse-sdk-extensions.md)。

- **Actell 接入**：将独立 Actell 组件接入官方命令注册、调用和会话生命周期，增加可选 USB 初始配置入口；保留官方账户、配对和控制通道。见 [补丁说明](../bridge/patches/README.md) 与 [组件说明](../bridge/extensions/gadget-actell/README.md)。
- **无屏适配**：复用 Muse 文字会话，调整无屏构建与启动流程，增加文字事件观察和可选开发网页。见 [无屏控制台](headless-console.md)。
- **角色覆盖**：使用上游已有覆盖入口替换默认角色，接入可选手机读写动作；无需额外修改上游构建机制。见 [角色说明](../bridge/extensions/avatar/README.md)。
- **回复语音接点**：在会话的回复、播放及取消流程接入可选豆包 TTS，复用现有 PCM 输出和字幕；关闭扩展或扬声器时保留字幕节奏。见 [TTS 说明](doubao-tts.md)。
- **配置适配**：提供桥接启停、无屏、板卡 USB 配置和中日韩字幕字体的配置组合，通过不回显值的工具导入及审计本机秘密配置，并根据豆包 API key 自动启停 TTS。见 [组件配置](../bridge/extensions/gadget-actell/README.md#combining-existing-overlays) 与 [TTS 配置](doubao-tts.md)。

补丁应用与扩展暂存方式统一见 [补丁说明](../bridge/patches/README.md)。本项目只维护 Muse SDK 侧适配，Actell 应用在其独立工程维护。
