# v13 聆听渲染性能

## 实现与边界

本次接入 `Actell-Muse-v13-Listening-Performance-Fix` 的渲染器增量，来源及前后 SHA-256 记录在 [source manifest](../bridge/extensions/avatar/source-manifest.json)。原版 v13 参考实现仍保存在 `tests/reference_v13.c`。权威源码位于 `bridge/extensions/avatar/src/`；SDK 暂存副本不能代替工程源码。

点头期间精确姿态变化仍会重建头部缓存。优化遍历原 160×160 网格，先用平方距离判定确定的内部与外部，只有边界带执行原 `hypotf` 和平滑并集。一次并集最多降低距离 0.875，两耳顺序并集最多降低 1.75；阈值 -1.81/1.76 保留浮点余量。不改变缓存键、点头节奏/幅度、眼睛、耳部装饰、蓝点、手机动作、状态优先级、按钮和布局，也不新增常驻缓存、BSS 或堆分配。自动变量和目标栈峰值仍需测量。

`MUSE_AVATAR_EXACT_DISTANCE_PREFILTER` 默认 1；定义为 0 后走原精确距离公式。此开关与 `CONFIG_MUSE_AVATAR_V2` 不同，后者关闭后选择原 64×64 角色。

## 主机复现

```sh
python3 bridge/tools/dev.py avatar
python3 bridge/extensions/muse-fonts/tools/check-stage.py --with-avatar --with-tts
```

入口核验固定 SDK 归档，执行两个渲染分支的直接/暂存 ASan/UBSan 检查、原 v13 参考像素与 dirty 覆盖检查、ESP 接口 fake 编译以及补丁一致性/受保护回滚测试。LeakSanitizer 未启用。

新增 `check-listening-performance.py` 对同一当前源码分别编译预筛开/关：8,000 帧按 1ms 时间采样聆听，4,000 帧随机全状态/音量/happy 切换，比较完整 160×160 输出；另在 25fps、8 秒序列中统计头部 hypot 次数与缓存重建次数。计数插桩与计时分离，计时采用无插桩 O2 版本，预热后每组 2,000 帧、共 5 组。输出位于 `local/validation/avatar-listening/performance.json`。

本机复跑：12,000 帧完整输出全部一致，原参考检查 2,400 组与 dirty 覆盖 199 组通过；200 帧聆听序列中缓存重建均为 74 次，头部 hypot 调用由 6,721,280 降为 296,114（减少约 95.6%）。同次主机无插桩计时均值约 81.84 → 42.77 微秒/帧（约 1.91 倍），属于本机当次运行，交接包的其他主机计时不作为本机结果。

这些结果仅说明指定样本和主机环境；不能换算为 ESP32 FPS，不能证明所有编译器/浮点实现逐像素相同，也不代表实际 AMOLED 卡顿已消除。全网格遍历和原 dirty 逻辑仍在；实际瓶颈还可能是调度、PSRAM 访问或显示传输。

## 目标资源检查

使用公开合成 profile 的实际 Xtensa 编译命令，单独编译预筛关/开 renderer：目标对象的 text 增加 650 字节，data/BSS 大小均不变；`-fstack-usage` 报告 `muse_pixel_render` 的静态栈帧两者均为 208 字节。这是对象级编译证据，text 数字包含工具对该对象的分类，不等于最终固件净增量；静态栈帧不包含完整调用链/运行时高水位。报告保存在 `local/validation/avatar-listening/target/`。

当前优化版已通过 ESP-IDF 6.0.1 的 ESP32-S3 显示板完整构建，公开合成配置同时链接 avatar、字体、TTS 和 Actell。应用为 3,084,288 字节，4 MiB 槽余 1,110,016 字节；两个头像缓冲共 51,200 字节位于外部 BSS，scale map 为 1,024 字节。此产物没有真实账号配置，只用于目标编译验证，不能作为已配置设备发布固件或真机验收。链接证据位于 `local/validation/avatar-listening/linked-target.json`。

## 暂存、构建与回退

新建已核验固定基线的 `local/build/` SDK 副本，按 [补丁说明](../bridge/patches/README.md) 依次暂存 gadget、avatar、字体及可选 TTS，再使用 [avatar 构建入口](../bridge/extensions/avatar/README.md#memory-and-build-validation)。显示板 profile 不变，不转用 headless。

对已安装原 v13 的 SDK，普通 `stage.py` 的重复运行只确认安装记录与当前文件一致，并不会升级 renderer。优先从干净固定基线重建暂存副本，避免绕过安装哈希直接覆盖。交接包的 `update_v13.py` 仅适用于其严格匹配的既有安装，需先核对全部安装记录及后续字体/UI 改动；工程未引入或默认执行该外部更新脚本。不得强制覆盖不匹配的 SDK 或混用 journal。

仅关闭本次预筛，针对公开合成配置的隔离构建，在已激活 ESP-IDF 环境中使用同一个构建目录重新配置并构建：

```sh
idf.py -C local/build/sdk-copy/esp32 \
  -B "$PWD/local/build/avatar-v13/v13" \
  -DCMAKE_C_FLAGS=-DMUSE_AVATAR_EXACT_DISTANCE_PREFILTER=0 reconfigure build
```

该命令应在现有公开 v13 构建完成后使用，保留该目录缓存中的 SDKCONFIG/组件设置。如已有其他 C flags，应保留并追加该定义。恢复时改为 `=1` 后重新配置/构建。不要直接展示私密配置的构建输出；私密构建继续使用不回显凭据的工程工具。关闭整个 v13 则在配置中显式关闭 `CONFIG_MUSE_AVATAR_V2` 并重建。回到旧 renderer 时应从 Git 历史恢复源码并重建干净暂存副本，不直接改已安装文件使 journal 失效。

构建不刷机。真实刷机需用户明确授权，使用既有显示板流程，保留数据分区、资源、配对与回退固件。

## 真机 A/B 验收

保持板卡、CPU 频率、PSRAM/BSS 放置、LVGL 配置、显示尺寸、亮度、输入 level 和刷新周期一致，分别构建预筛关/开版本。预热并覆盖多轮完整 8 秒动画，同时采集 idle、thinking、listening。

- `muse_pixel_render` 前后计 render_us；失效区域计算前后计 dirty_us，记录 changed cells、矩形数量与面积。
- 在真实 flush 提交及 DMA 完成/`lv_display_flush_ready` 处配对记录，统计每帧累计传输及最后完成时间；提交返回不能当作屏幕完成。
- 记录实际 tick/frame 间隔、内部 RAM/PSRAM 最低余量和栈高水位。使用预分配统计及低频汇总 p50/p95/max，避免逐帧串口输出干扰。
- 同时间轴录像核对两次点头、眼睛、耳朵、蓝点、状态切换、待机与手机动作，并验证 64×64 回退及语音/Actell 并发。

只有测得 flush 占比高时再评估有限分段的失效策略，依据 LVGL 实际最终矩形及 DMA 耗时，不能仅凭逻辑像素数量判断收益。本次没有加入设备计时钩子或逐帧调试日志。优化版私密固件已完成非活动应用槽刷写与回读校验，原有资源、持久数据及回退槽保留；45 秒启动观察中字体与 Muse 会话就绪，无 panic/watchdog。交互后用户反馈聆听已无明显卡顿；后续被动采集未记录到新的复位、panic、看门狗或 brownout，并记录到一次 TTS 成功输出。该结论属于当前设备本轮的主观流畅度反馈与运行日志，不等于目标帧耗时或并发压力测试通过；显示传输计时、栈高水位、长期稳定性和完整回退仍待专项验收。

## 串口复位与崩溃区分

交互验证初始曾采集到 `rst:0x15 (USB_UART_CHIP_RESET)`，没有 panic、断言、回溯、看门狗或 brownout 记录；位置与串口查询/采集切换相符。该记录指向 USB UART 复位，不足以归因于 renderer 崩溃。期间没有再次刷写，重启前后加载的应用槽及 ELF 标识一致；后续一次 TTS 请求成功。

后续交互观察使用保持既有串口配置的只读采集：不发命令、不切换 DTR/RTS。需要查询设备状态时，不在流畅度观察中反复打开会操纵控制线的串口工具。串口切换导致的重启必须与设备自主复位分开记录；没有采集覆盖的时段也不能据此排除其他故障。
