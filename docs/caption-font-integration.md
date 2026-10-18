# 共享字体读取与字幕集成

已实现默认关闭的共享资源字体接入，目标为 Waveshare 2.16 的 480×480 有屏 profile。代码、主机验证与目标构建不等于真机显示、内存水位或迁移验收。已重新构建独立字体固件、核对设备并完成完整 Flash 私有备份；经用户明确授权覆盖旧区域后已完成首刷，共享字体加载成功，持久数据在写入阶段通过字节比对，屏幕显示及运行性能仍待验收。用途核查见[分区规划](font-partition-plan.md)。当前设备 profile 为 Regular 16px/4bpp、17px 行高；旧大字号包回退原字体。详见[原版字号与布局](caption-font-native-size.md)。

## 设备实现

- **只读资源验证**：`actell_caption_font.c` 查找 `actell_assets`，只读映射，检查容器/reader ABI、目录边界、条目唯一性、资源格式及所有 SHA-256。目录上限 8KiB、条目上限 32；与主机打包器一致。只在 UI 初始化时验证一次，失败后不自动重试或写 Flash。
- **按字形解码**：`actell_font_core.c` 在映射区中查找 cmap、字形索引和度量，对当前字形执行有界位流/RLE 解码，直接输出到 LVGL 提供的 A8 draw buffer。不分配整库 bitmap、不复制全部字形描述符，解码行缓冲为 64 字节。常驻状态是字体元数据、LVGL 描述符和映射句柄；目录解析有临时堆开销，Flash 映射仍消耗虚拟地址/MMU 资源，实际板卡堆和时延未测。
- **缺字与文本**：同一字体覆盖中文、Latin 和标点。支持的码点保留原样，未覆盖字符、emoji 和非法 UTF-8 显式显示 `?`；tab/CR 归为空格，LF 保留。组合序列不做 NFC 合成，超出字集的组合符也显示 `?`。不启用其他授权不明 fallback 字体。
- **像素分页**：`actell_caption_layout.c` 使用二进制整数 advance，保留英文词边界、长词/URL 安全折行、CJK 关闭标点禁则并避免行尾开括号。按合法码点复制完整行；分页保持原来相邻页重叠一行的规则，原文 byte 进度映射到同一布局。输入扫描上限 8192 字节，输出仍受现有 400 字节字幕缓冲限制，字体补丁本身不扩大聊天缓存或网络消息；组合 TTS 启用时的缓存扩展由 TTS 补丁负责。
- **普通与缩小布局**：恢复上游原有三行普通回复、阅读区域计算和待机字幕/角色/音量条几何。布局格宽取原 unscii-16，实际混排仍按像素 advance 换行。顶部硬件按钮、图标、状态栏及 Avatar 网格适配保持现有实现。新资源和布局尚未进行本轮真机验收。
- **UI 接点**：薄补丁让 `caption_font()` 选择已验证字体，消息字体行高固定为原版 17px，保留原字幕高度计算；回复 label 使用显式像素换行结果，不再由 LVGL 二次自动换行。听读布局切换时原子发布像素宽与行数；原角色、音量/音频、会话与工具逻辑保留。
- **回退**：开关关闭、有屏 profile 不适用、旧分区表、缺失/损坏资源、格式不兼容或映射失败时，使用原字体、规范化和列数分页。保留原 Unifont 字库作为固件回退资源；新的大字库只存于共享包。无屏下 Kconfig 不允许启用此字体实现。
- **音频边界**：字体补丁不修改请求的 `output_modality`，SDK-only 字体构建沿用上游 `text` 请求与静音字幕节奏；合并后的可选[豆包 TTS](doubao-tts.md)可独立启用，将文字回复送往豆包合成。服务端回复形式与 iOS 播放行为尚未完成对照验收，不能仅凭客户端参数断言服务端不会生成语音。

逐步追加文本的合法 UTF-8 前缀、各原文字节进度均已检查输出行数和实际像素宽度。当前实现每次字幕页更新重新测量受限文本，不在 UI 每帧全量测量，也不分配整段布局数组；未保存历史页快照，末尾未完成英文词仍可能随追加回流。真实流式服务的观感和时延需设备验收，不能把前缀回归写成已经消除所有重排。

## 暂存与公开配置构建

先按 [补丁说明](../mods/patches/README.md) 准备固定 SDK 的本机副本及已有扩展，再运行：

```sh
python3 mods/extensions/muse-fonts/tools/stage.py local/build/sdk-copy --check
python3 mods/extensions/muse-fonts/tools/stage.py local/build/sdk-copy
```

stage 拒绝覆盖和重复应用，验证固定归档哈希与 UI 基线，仅复制 `src/`、`include/`、授权、CMake/Kconfig/依赖声明和资源分区 CSV；不复制下载字体或生成数据进 app 组件。可选开关为 `CONFIG_ACTELL_CAPTION_FONT=y`；默认关闭，仅允许 Waveshare 2.16 有屏 profile。

已激活 ESP-IDF 6.0.1、并准备好已解析依赖时，可使用独立公开配置 runner：

```sh
python3 mods/extensions/muse-fonts/tools/build.py \
  --sdk local/build/sdk-copy --components-dir "$RESOLVED_COMPONENTS" --mode on
# 相同参数分别使用 --mode off 与 --mode headless
```

runner 从公共 defaults 与空账号/配网配置构建，所有配置与输出都在 `local/build/caption-firmware/<模式>/`。`on` 使用追加资源区的 CSV；`off` 使用原有屏分区表；`headless` 使用原无屏配置和分区。生成的分区表二进制仅是待部署产物，不会改变设备。

字体分支独立测试时，可通过同一 runner 的 SDK-only 模式准备含 SDK token 的本机固件：

```sh
python3 mods/extensions/muse-fonts/tools/build.py \
  --sdk local/build/sdk-copy --components-dir "$RESOLVED_COMPONENTS" --mode on \
  --secrets "$MUSE_SECRETS_FILE"
```

此模式调用已审查的 `mods/tools/apply-secrets.py --sdk-token-only`，源文件只能由导入器读取，不复制源文件或回显值；即使输入同时含其他 provider 字段，也不把它们写入字体配置。输出单独放在 `local/build/caption-firmware/private-on/`，不覆盖公开构建；权限按 0600/0700 创建，日志不回显，失败时回到合成配置排错。代理不得查看或搜索该真实构建的配置、生成头、固件或日志内容。构建工具只报告状态和字节数，不触发账号连接、配对或刷机。

不带 `--with-tts` 的 SDK-only runner 继续用于隔离的字体测试，不导入 provider key 或启用 TTS；它不能用于验证合并后的语音功能。字体与 TTS 可共同暂存，顺序见[补丁说明](../mods/patches/README.md)，组合配置使用 [TTS 导入与构建步骤](doubao-tts.md)。真实 SDK token 只用于后续已授权的设备验收，不用于普通回归测试；五项 SDK-only 导入测试均使用合成配置，覆盖 provider 排除、开关保留、权限、拒绝与不回显。

字体与 TTS 均已暂存时，可用同一 runner 构建组合固件：

```sh
python3 mods/extensions/muse-fonts/tools/build.py \
  --sdk local/build/sdk-copy --components-dir "$RESOLVED_COMPONENTS" --mode on \
  --with-tts --secrets "$TTS_SECRETS_FILE"
```

`--with-tts` 默认双向；切回 HTTP 添加 `--tts-transport http`，输出隔离到 `private-fonts-tts-http/`，接口宏会在构建后核验。不要仅修改输出 sdkconfig 后再次运行 runner，因为配置种子会重建。详细步骤见 [TTS 接口切换](doubao-tts.md#编译期选择接口)。

`--with-tts` 要求普通导入工具确认 TTS 已启用，输出隔离到 `local/build/caption-firmware/private-fonts-tts/`。构建成功后由目标编译器只检查字体、TTS 和板卡开关，不回显配置内容；构建目录统一收紧权限。该模式仍不刷机，不改分区或资源，也不发送合成请求。

**不要将 IDF 输出的通用 `flash` / `flash_args` 当作保留数据的迁移命令。** 通用产物包含初始化 otadata，可能重置 OTA 状态。本轮 runner 只执行 build；真正迁移须核对设备、备份、生成定点写入计划并独立授权。详见 [保留数据的迁移规划](font-partition-plan.md)。

## 验证记录

当前原版字号资源的验证见[16px 实施说明](caption-font-native-size.md)；下列六候选及首次部署数据属于历史 24/28px 方案，不代表本轮设备已更新。

- 六份候选各 29,137 字形，共 **174,822 字形**；有界 C 解析/解码输出与转换器原始量化像素逐字哈希及度量完全一致。
- ASan/UBSan：完整候选字形解码、截断位流、非法表头/边界、各输出容量、非法 UTF-8 与页请求。
- 实际设备适配源码在主机分区/密码库接口替身下历史通过 7 类场景：有效包映射与 LVGL 回调、无分区、哈希损坏、未知 ABI、错误字体格式、重算包哈希后的非法字体结构、重算目录哈希后的越界偏移。使用真实 SHA-256，替身不提供 NVS、网络或 Flash 写 API；该结果属于主机验证。
- 固定归档的最小原始文件集按 headless → gadget → caption 补丁依次 `git apply --check` 并应用；暂存文件与权威扩展逐字节一致，重复暂存被拒绝。
- 原有字幕分页测试 7 项通过；工程回归包含原有 64 项测试、新增 16 项打包、真实 C 核心与合成配置导入测试，以及 Node、观察器检查。
- 首次设备部署使用 Regular 24px/4bpp 候选包，资源和应用读回校验通过；切换前持久数据与引导区域逐字节保留。新 OTA 槽启动、29,137 字形共享字体加载与 45 秒无崩溃观察通过，连接状态报告仅保存在本机验证目录。未触发测试播报，未作屏幕可读性或分页验收；报告位于 `local/validation/caption-flash/`。
- 当前目标构建结果记录于 `local/validation/caption-fonts/firmware-*.json`，最终链接符号和节区差异记录于同目录的 `firmware-matrix.json`。真机显示、触控、音频链路回归、RAM/PSRAM/最大连续块、MMU 余量和帧耗时仍未验收。

| 目标构建 | app.bin 字节 | app 分区剩余 | 新读取/解码实现 |
|---|---:|---:|---|
| Waveshare 2.16 开启 | 3,018,752 | 1,175,552 | 已编译并链接 |
| Waveshare 2.16 关闭 | 3,018,752 | 1,175,552 | 未编译、未链接 |
| 无屏 | 1,445,888 | 651,264 | 未编译、未链接 |

字体与 TTS 组合构建已通过，app 为 3,084,288 字节，4MiB 槽位余 1,110,016 字节；目标编译器确认共享字体、TTS 和目标板卡开关均已启用。该构建使用受保护的本机配置，未在普通主机回归中调用云端 TTS。组合固件已刷入非活动槽并校验，切换后共享字体加载成功，普通两行、阅读五行，45 秒观察无崩溃或看门狗异常；资源包与分区表未重写，持久数据在写入阶段逐字节保留。未主动发起合成；实际播报、并行内存和动画仍需单独验收。

开启相对关闭的 ELF `.flash.text` 增加 6,572 字节，`.flash.rodata` 增加 544 字节；静态 DRAM data/bss 共增加 112 字节，IRAM text 无增量。签名镜像对齐后 app.bin 大小相同，不能据此宣称代码增量为零。该节区差异不包含启动时 cJSON、密码库或绘制缓存的动态内存。已从实际编译命令与 ELF 符号确认开关和无屏排除关系，新大字库数组未链接进任何 app。开启构建的新分区表与关闭构建相比，原有条目逐项完全一致，仅追加资源区。

复现深入主机验证：

```sh
node mods/extensions/muse-fonts/tools/generate.js
python3 mods/extensions/muse-fonts/tools/run-font-host.py
python3 mods/extensions/muse-fonts/tools/run-resource-port.py --components-dir "$RESOLVED_COMPONENTS"
python3 mods/extensions/muse-fonts/tools/check-stage.py
# 验证字体与 TTS 同时暂存的补丁顺序及源码一致性：
python3 mods/extensions/muse-fonts/tools/check-stage.py --with-tts
python3 mods/tools/dev.py test
```

`run-resource-port.py` 的 SHA 替身当前使用 macOS CommonCrypto；其他平台仍可运行独立 C 核心和包测试。生成 oracle、测试日志、故障向量和二进制全部保留在 `local/`。
