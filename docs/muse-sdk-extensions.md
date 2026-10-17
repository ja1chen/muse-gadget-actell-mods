# Muse Gadget SDK 扩展功能索引

按类别概括本项目新增的能力；SDK 侧适配见 [改动索引](muse-sdk-changes.md)。配置、协议和实现细节通过下列引用查阅。

- **2.16 独立按键**：增加左键扬声器、中键休眠/唤醒与长按关机、右键 Talk，以及仅显示状态的三个无框图标；沿用触屏 Speaker 与手机 BLE 设置入口，提供固定基线及 Avatar/字体组合检查、幂等应用和安全 app 槽更新；已通过启动与基础按键/图标人工核查，完整真机验收待完成。见 [按键扩展](../bridge/extensions/waveshare-216-controls/README.md)。
- **Actell 手机工具**：让 Muse 通过设备的 Wi-Fi HTTPS/HMAC 调用 Actell，复用其授权、审批、运行管理和工具执行队列。见 [组件说明](../bridge/extensions/gadget-actell/README.md)。
- **Simple LAN 配对**：发现阶段分离网络地址与固定 TLS 身份名，支持同身份换 IP，旧版广播明确拒绝；设备页面区分首次配对与重连，唯一配对窗口自动申请、由手机允许，多窗口歧义不扇出；显示批准、领取与最终确认阶段，新申请默认名称为 Muse Gadget，旧待完成配对保留原名称；增加发现、手机允许后的凭据领取、pending_ack/active 保存与 hello 重试；首次使用可信局域网 TOFU，后续固定证书，无 BLE/扫码/设备端 server。主机与目标构建不代表真机联调通过。工具注册携带手机用途、参数、调用示例和审批语义。设置首页显示认证连接状态，支持位于配对操作下方、持久保存的自动重连开关；重连失败与取消保留配对，仅显式移除删除；认证请求须经本次启动的网络校时，发现后再取签名时间，避免 RTC 偏差导致拒绝；校时成功唤醒自动重连，不绕过用户关闭、取消或拒绝后的暂停。结果接收支持有界的大 JSON 并区分限额与网络故障，串口阶段诊断覆盖 worker 与控制结果队列，不记录参数或身份；完成重复请求同步回应，工作槽回收与独立写请求去重保护并存。见 [配对说明](slan-pairing.md)。
- **可信 USB 配置**：提供物理按键控制的开发配置入口，用于导入 Actell 连接邀请，默认关闭。见 [初始配置说明](../bridge/extensions/gadget-actell/README.md#trusted-one-time-bootstrap)。
- **实验图片回传**：提供受限缩略图回传，默认关闭；真实 Muse 模型图像输入仍需验证。见 [设备 V2 协议](protocol/gadget-v2.md)。
- **Pebble 角色与动作**：提供默认启用的 v13 高清角色、九种状态/动作与原 64 像素编译回退，保留精确缓存，头部距离预筛降低聆听重建成本，采样像素输出一致；可选手机活动独立于工具执行，已完成刷写与启动核查，实机观察无明显聆听卡顿，目标计时与并发性能待验收。见 [角色说明](../bridge/extensions/avatar/README.md) 与 [可选活动反馈](optional-avatar-events.md)。
- **无屏文字交互**：提供 Muse 文字提交、取消、状态及回复事件浏览；可选网页仅用于隔离可信开发网络。见 [无屏控制台](headless-console.md)。
- **字体与通用资源**：提供统一中西文字族的可复现生成、覆盖核查、预览与通用资源包，以及默认关闭的只读映射、按需解码和像素分页，普通两行回复与独立缩小阅读布局；已完成资源部署和启动核查，真机显示及运行性能待验收。见 [字体集成](caption-font-integration.md) 与 [共享资源包](read-only-resources.md)。
- **豆包回复播报**：增加默认双向 WebSocket、可通过构建参数切换且产物隔离的 HTTP 流式 TTS、PCM 缓冲背压、PSRAM 任务栈、长回复分段、取消隔离及不含正文的分阶段诊断，固定合成资源与音色，按 API key 是否配置自动启停；秘密配置通过工具导入和审计，长回复、打断和并行性能仍需实机验收。见 [TTS 说明](doubao-tts.md)。
- **主机开发与使用说明**：提供 Python 客户端、loopback mock、网页原型和回归工具，以及 Muse 调用扩展的 skill。见 [主机客户端](host-client.md)、[开发入口](../bridge/README.md) 与 [Muse skill](../bridge/extensions/skills/gadget-actell-remote/SKILL.md)。

主机 mock 不操作真实手机；源码和主机测试不代表真机端到端验收。各项能力的前提与限制以对应详细说明为准。
