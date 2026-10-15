# 本项目工程链路

```text
bridge/
├── host/                 # Python 客户端、loopback mock、网页、主机测试
├── extensions/
│   ├── gadget-actell/    # 独立 ESP-IDF Actell 命令组件
│   ├── doubao-tts/       # 可选豆包回复语音组件与主机验证
│   ├── avatar/           # 原创角色与可选手机动作
│   ├── headless-console/ # 无屏文字控制台
│   └── skills/           # Muse 扩展使用说明
├── patches/              # 仅对 Muse Gadget SDK 的修改
└── tools/                # 开发入口、分享检查与可复用验证程序
```

所有示例命令默认在仓库根目录执行：

```sh
python3 bridge/tools/dev.py test
python3 bridge/tools/dev.py console --port 8766
python3 bridge/tools/check-share.py
# 创建私密占位配置，由用户在本机编辑：
python3 bridge/tools/apply-secrets.py --secrets "$HOME/.config/muse-actell/device-secrets.conf" --init
```

扩展源码和补丁在这里维护；上游版本见 [来源清单](../upstream/sources.json)，应用顺序见 [补丁说明](patches/README.md)。生成的日志、缓存、构建与验证结果统一写入根目录 `local/`，不要写进本目录。

豆包 TTS 的配置格式、工具导入与构建方式见 [TTS 说明](../docs/doubao-tts.md)。代理不直接读取秘密配置。

Actell 在其独立工程中直接维护和验证，不通过本目录提供补丁。`patches/muse-gadget-sdk/actell-hook.patch` 是 Muse SDK 侧接入 Actell 的接点。

项目许可见根目录 [LICENSE](../LICENSE)，第三方来源见 [THIRD_PARTY.md](../THIRD_PARTY.md)。
