# Muse Gadget SDK 扩展功能索引

按类别概括本项目新增的能力；SDK 侧适配见 [改动索引](muse-sdk-changes.md)。配置、协议和实现细节通过下列引用查阅。

- **Actell 手机工具**：让 Muse 通过设备的 Wi-Fi HTTPS/HMAC 调用 Actell，复用其授权、审批、运行管理和工具执行队列。见 [组件说明](../bridge/extensions/gadget-actell/README.md)。
- **可信 USB 配置**：提供物理按键控制的开发配置入口，用于导入 Actell 连接邀请，默认关闭。见 [初始配置说明](../bridge/extensions/gadget-actell/README.md#trusted-one-time-bootstrap)。
- **实验图片回传**：提供受限缩略图回传，默认关闭；真实 Muse 模型图像输入仍需验证。见 [设备 V2 协议](protocol/gadget-v2.md)。
- **Pebble 角色与动作**：提供原创角色和基础状态动画，可选显示手机读写活动；动作反馈独立于工具执行。见 [角色说明](../bridge/extensions/avatar/README.md) 与 [可选活动反馈](optional-avatar-events.md)。
- **无屏文字交互**：提供 Muse 文字提交、取消、状态及回复事件浏览；可选网页仅用于隔离可信开发网络。见 [无屏控制台](headless-console.md)。
- **主机开发与使用说明**：提供 Python 客户端、loopback mock、网页原型和回归工具，以及 Muse 调用扩展的 skill。见 [主机客户端](host-client.md)、[开发入口](../bridge/README.md) 与 [Muse skill](../bridge/extensions/skills/gadget-actell-remote/SKILL.md)。

主机 mock 不操作真实手机；源码和主机测试不代表真机端到端验收。各项能力的前提与限制以对应详细说明为准。
