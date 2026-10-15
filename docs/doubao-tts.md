# 豆包回复语音

可选扩展把 Muse **语音会话中已完成的回复**提交给豆包，边接收边通过原有扬声器链路播放。控制台文字会话仍只返回文字。源码位于 `bridge/extensions/doubao-tts/`，SDK 接点维护于 `bridge/patches/muse-gadget-sdk/doubao-tts.patch`；不依赖 Linux 播放进程或 RTC SDK。

## 秘密配置

秘密文件由用户指定，**推荐放在工程目录外**。每次必须通过 `--secrets` 显式传路径，工具没有默认工程内路径，也不将源文件复制进工程。以下环境变量仅保存路径，不保存凭据；工具可以在该路径创建权限为 0600 的占位文件，已有文件不会覆盖：

```sh
export TTS_SECRETS_FILE="$HOME/.config/muse-actell/device-secrets.conf"
python3 bridge/tools/apply-secrets.py --secrets "$TTS_SECRETS_FILE" --init
```

用户自行在本机编辑。代理不得直接读取、搜索或展示该文件；只有导入工具读取内容。格式如下，所有值均为占位示例：

```ini
CONFIG_GADGET_SDK_TOKEN="REPLACE_WITH_GADGET_SDK_TOKEN"
CONFIG_DOUBAO_TTS_API_KEY="REPLACE_WITH_DOUBAO_API_KEY"
```

- `CONFIG_GADGET_SDK_TOKEN`：Muse Gadgets SDK token，前缀为 `mgst_`，用于官方账户/配对链路；必填。
- `CONFIG_DOUBAO_TTS_API_KEY`：豆包语音控制台的新版 API Key。非空时自动启用 TTS；省略或设为 `""` 时自动关闭，并清空生成配置中的旧 key。首版不实现旧版 AppId/AccessToken 鉴权。

资源 ID `seed-tts-2.0`、音色 `zh_female_vv_uranus_bigtts` 固定在组件实现中，不接受秘密文件覆盖。`CONFIG_DOUBAO_TTS` 根据 API key 自动推导，不需要也不允许在秘密文件中填写。旧生成配置中的资源/音色字段会在导入时移除。

字符串使用双引号，支持空行及整行 `#` 注释。不使用 `export`、行尾注释或 shell 展开，工具不会执行文件中的命令。音频格式、采样率和服务地址也由实现固定，不需要填写。

Wi-Fi 和 Muse 账户通过现有官方配网/配对流程建立；Actell 连接邀请仍通过其独立的可信 USB 配置流程导入，不需要放进这个文件。

导入到指定构建的实际 `sdkconfig`，同时替换已有缓存值：

```sh
python3 bridge/tools/apply-secrets.py \
  --secrets "$TTS_SECRETS_FILE" \
  --sdkconfig local/build/sdk-copy/esp32/build-tts/sdkconfig
# 如果使用另一个本机秘密文件，只传路径：
python3 bridge/tools/apply-secrets.py \
  --secrets /path/outside/project/another-device.conf \
  --sdkconfig local/build/sdk-copy/esp32/build-tts/sdkconfig
```

工具允许输入位于用户指定的任意路径，支持绝对路径、相对路径及 `~`；输出仍限制在 `local/build/`。输入文件必须属于当前用户且为普通文件，拒绝文件符号链接、重复 KV 字段、未知字段、占位值及控制字符；写入时原子替换并设置 0600，不打印配置值或解析异常原文。它不执行 shell、不联网、不刷机。外部文件仍受操作系统和运行环境的访问权限约束。

SDK 配置、生成头文件及固件都会包含编译期凭据，因此这些产物同样属于本机秘密数据。代理不得直接查看真实配置的生成文件或可能回显值的日志。主机验证使用单独的合成凭据；真实配置的构建、刷机及启动验证记录只保留在 `local/`，不能当作云端 TTS 播报验收。

分享前可通过同一工具比对实际凭据，检查 Git 可见文件及暂存区，只输出脱敏后的匹配位置，不输出凭据或匹配行；不会扫描忽略的产物或任意历史：

```sh
python3 bridge/tools/apply-secrets.py --secrets "$TTS_SECRETS_FILE" --audit-share
```

## 暂存与构建

先按 `upstream/sources.json` 核验归档哈希，解压到新的 `local/build/sdk-copy/`，保留干净基线。扩展应用顺序如下；TTS 为显示板可选组件，不在 headless 文字模式启用：

```sh
python3 bridge/extensions/gadget-actell/tools/stage.py local/build/sdk-copy --check
python3 bridge/extensions/gadget-actell/tools/stage.py local/build/sdk-copy
python3 bridge/extensions/doubao-tts/tools/stage.py local/build/sdk-copy --check
python3 bridge/extensions/doubao-tts/tools/stage.py local/build/sdk-copy
python3 bridge/extensions/avatar/tools/stage.py --sdk-root local/build/sdk-copy
python3 bridge/tools/apply-secrets.py \
  --secrets "$TTS_SECRETS_FILE" \
  --sdkconfig local/build/sdk-copy/esp32/build-tts/sdkconfig
```

启用 ESP-IDF 6.0.1 后，在该 SDK 的 `esp32/` 目录执行构建。`SDKCONFIG` 必须指向工具写入的文件；扩展自身不替换板卡配置：

```sh
umask 077
mkdir -p ../../../logs/tts
tts_build_status=0
idf.py -B build-tts -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG="$PWD/build-tts/sdkconfig" \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-waveshare-s3-216" \
  reconfigure build > ../../../logs/tts/private-build.log 2>&1 || tts_build_status=$?
chmod -R go-rwx build-tts
printf 'Build exit status: %s\n' "$tts_build_status"
```

按需要在 defaults 末尾追加本工程的桥接、CJK 字体和板卡 USB overlay，使用其相对路径或环境变量。真实配置构建的完整输出只保存在 `local/logs/`，不要展示可能含凭据的错误上下文。部分构建工具会显式设置文件权限，不能只依赖 `umask`；成功或失败后均需收紧整个构建目录，保留当前用户所需的执行权限。构建成功不等于已刷机，实际刷机需用户明确授权。

## 播放与资源边界

- 通过固定的 `https://openspeech.bytedance.com/api/v3/tts/unidirectional` 请求流式合成，强制服务器证书校验，拒绝重定向，不向自定义地址发送凭据。
- 启用播报时，待朗读的回复文本会发送到豆包云端合成；请求不包含 Muse Gadget SDK token、配网信息或 Actell 配对邀请。API key 仅用于豆包请求鉴权。
- 输出为 PCM16、小端、单声道、16 kHz，按 32 KB/s 播放，无 MP3 解码或重采样。需在真实云端调用时核对返回格式。
- 单一网络工作任务读取响应；JSON 对象可跨 HTTP 分包，也可在一次读取中连续出现。解析后分块解码 base64，保留跨音频块的半个 PCM 样本。
- 预留 128 KiB PSRAM PCM 环形缓冲、128 KiB JSON 记录缓冲及 8 KiB 工作任务栈。JSON 解析还有临时分配，不能将这些固定缓冲之和当作峰值内存。复用 SDK 现有 64 KB 输出缓冲；字幕文本上限扩大至每条 8191 UTF-8 字节，8 条最多增加约 56 KiB PSRAM。原有 MP3 缓冲仍由上游分配，暂不移除。
- PCM 缓冲满时等待消费，以 TCP 背压限制接收；单个 JSON 对象超过 128 KiB 时终止，不丢弃中间音频继续播放。单段请求最长 120 秒，网络无数据超时 15 秒，单次阻塞 I/O 超时 5 秒。
- 回复按最多 1023 UTF-8 字节分段，尽量在句末切分，按顺序合成；这可能产生段间停顿。不实现文本增量输入、跨段韵律保持或离线合成。超过字幕总容量时明确报错，不静默播报被截断的文本。
- 新会话/取消通过代次失效旧任务和旧 PCM；本地取消立即生效，阻塞中的网络任务在 I/O 超时后退出。每段只提交一次，认证、网络、格式或超时失败均不自动重放，显示 `TTS UNAVAILABLE`。已经发出的云端请求可能已计费。
- 回复开始前已关闭扬声器，或未启用扩展时，跳过 TTS 并沿用上游静音字幕节奏。播报中途静音仅停止出声，当前回复的合成与后续分段仍可能继续；音量为零也不会跳过 TTS。字幕仍是基于播放帧数估算，未接入豆包字级时间戳。

SDK 补丁还将原有 MP3 自检的重采样状态显式零初始化，修复目标编译器的未初始化检查；不改变自检的正常播放流程。

## 验证

```sh
python3 bridge/tools/dev.py test
python3 bridge/tools/validation/run-tts-worker.py --sdk-root local/build/sdk-copy
python3 bridge/tools/check-share.py
```

主机回归使用合成数据验证碎片 JSON、base64、边界和私密配置导入。独立 worker harness 需要暂存 SDK 已解析的 cJSON 源码，用模拟 HTTPS 验证跨块样本连续性、错误/不完整响应、取消、新代次隔离及缓冲背压；它不建立网络连接。两类原生 harness 均启用 ASan/UBSan。构建验证可使用合成配置；使用真实配置的构建输出保持本机私密。云端鉴权、首声延迟、内部 RAM 最低余量、屏幕动画并行运行、断网/打断和长回复播放需真实设备单独验收；TTS 组件日志仅输出数值指标及固定状态，不输出文本或云端错误正文。

官方接口参考：[HTTP 单向流式](https://docs.volcengine.com/docs/DoubaoVoice/unidirectional-streaming-text-to-speech-http?lang=zh)、[旧版 V3 HTTP 流式说明](https://docs.volcengine.com/docs/DoubaoVoice/HTTPChunkedSSEUnidirectionalStreaming-V3?lang=zh)、[ESP-IDF HTTP Client](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/api-reference/protocols/esp_http_client.html)。
