# Waveshare 2.16 独立实体按键

固定基线：`812c46f9fe42b69d916613a9dc702cbac5ff7b54`。仅作用于 Waveshare ESP32-S3-Touch-AMOLED-2.16；不验证或升级其他上游快照。

正面看屏幕、USB 朝下：左 BOOT 切换扬声器输出，中 PWR 短按休眠/唤醒、硬件 LONG 请求软件关机，右 KEY3 保持 Talk 与原配对确认。扬声器静音不关闭麦克风。左键在熄屏时首次只唤醒；中键唤醒的同次按压不会再次休眠或软件关机。左键双击是两次扬声器切换，不再进入 BLE 设置。

屏幕左/中/右图标只显示状态和实体键位置，不提供点击动作。设置页 Speaker 开关保留；从主屏向左滑进入 Settings → Bluetooth → Phone setup 开关手机设置，未配对时同样有菜单。设置页不显示浮动实体键图标。Power off 页面使用 PWR 名称。

## 文件与接入

扩展的权威源码是 `include/waveshare_216_controls.h`，SDK 差异集中在 [controls 补丁](../../patches/muse-gadget-sdk/waveshare-216-controls.patch)。[manifest.json](manifest.json) 记录固定归档与目标头文件；不保存 SDK 完整副本、凭据或固件。

准备已核验的固定 SDK 副本于 `local/build/sdk-copy`。沿用[既有顺序](../../patches/README.md)应用所需扩展，**本扩展最后应用**，尤其应在 Avatar 和 caption font 之后：字体暂存器只接受固定上游 UI 或经过核验的 Avatar UI。不重新暂存或覆盖现有集成；若已有不兼容源码，检查器停止并报告。

```sh
python3 bridge/extensions/waveshare-216-controls/tools/stage.py local/build/sdk-copy --check
python3 bridge/extensions/waveshare-216-controls/tools/stage.py local/build/sdk-copy
```

脚本只允许修改本仓库 `local/build/` 中的副本。先核验归档 SHA-256，再逐字节核对全部受影响源码；接受固定基线，以及按权威安装器重建的 Avatar v13、caption font 及两者组合的明确变体；不以本地安装日志替代源码比对。其他扩展接点保持原样。重复运行显示 Already applied，源码或扩展头文件发生未知变化时拒绝继续。`--check` 不修改目标源码。应用记录保存于 `local/validation/waveshare-216-controls/`，仅包含基线和源码哈希，不读取配置或凭据。

显示版配置顺序：`sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-waveshare-s3-216` 后追加本扩展 `overlays/sdkconfig.controls` 和所需已有扩展 overlay。使用独立显示版 build，禁止套用 headless 配置。新 overlay 启用原生 Montserrat 24 字体；遗漏时本板编译会明确报错。启用 TTS 的构建须保留普通秘密配置导入方式，不能使用 SDK-token-only 模式。

## 电源事件与错误规则

- AXP2101 [数据手册](https://files.waveshare.com/wiki/common/X-power-AXP2101_SWcharge_V1.0.pdf) REG27 `[5:4]=01` 选择 1.5 秒 IRQLEVEL。仅此板调用新接口设置并回读验证，保留 ONLEVEL 与 SDK 已配置的 10 秒 OFFLEVEL；不更改电源轨策略。实际时间仍待实物测量。
- 不读取不存在的“当前按键电平”，不使用 PRESS 后的软件计时判断长按。只有已确认、已成功清除锁存的硬件 LONG 能触发关机。
- 启动排空锁存后，只由新的 PRESS 建立操作资格；孤立 LONG、RELEASE、CLICK 均忽略，因此开机时继续按住不会触发 Talk 或软件关机，已松手时下一次新 PRESS 不被吞掉。
- 同次 PRESS+RELEASE 合并为一次完整短按。CLICK 不产生额外动作。已有按压中又出现 PRESS（包含前次释放与新按下混合）时，无法恢复顺序，保守取消该事务，不虚构第二次操作。
- 同一轮按键使用处理 Talk 前的睡眠状态，避免两键/三键同时唤醒造成意外静音或立即回睡；有效关机优先于同轮左键切换。
- LONG 和 RELEASE 同次到达时，先发 LONG、下个输入循环再发 RELEASE，适配现有共享处理器的先释放顺序；成功或失败的软件关机均无短按尾动作。原处理器在关机前释放正在进行的 Talk。
- 新的 POWER_CANCEL 只取消不可信事务，不产生休眠/关机。读失败或清锁存失败不交付事件；进入隔离期，丢弃后续非空锁存，直到成功读取并确认空锁存才接受新 PRESS。恢复期间的按压可能被保守忽略；不会自动重放。GPIO Talk 与 Speaker 独立工作。
- 启动初始化/门槛回读失败时，停用软件 PWR 控制并输出不含秘密的错误诊断；硬件 PWR 仍独立工作，软件恢复需重启。运行期 I2C 故障按上述空锁存规则恢复。
- PMU 按单调时钟轮询，正常间隔至少 20 ms；本板休眠等待最多 50 ms，Wi-Fi nap 也遵循该上限。RTOS 定时唤醒后再进行 I2C，不宣称未接线的 PMU IRQ 能直接唤醒 ESP32。50 ms 仅为等待上限，不是实测响应时间；I2C、调度和显示恢复会增加延迟和耗电。

## 图标

三图标位于顶边 y=6，左/右 x=-113/+113，中电源 x=0。电源使用原生 `lv_font_montserrat_24` 和 28×28 标签容器，底部 y=33，到 y=40 状态行保留 6px 空白；其他板仍用原 28px/36×36 规格。圆环绘制在顶部三个图标周围明确裁出留白，不以覆盖黑色背景代替预留。Speaker hint 不注册交互，`touch && !rim_controls` 保证本板不创建旧喇叭控件、回调或扩展热区；设置页触控保持可用。

## 验证与验收边界

```sh
python3 bridge/tools/dev.py test
```

主机回归要求 `local/upstream/archives/` 中已有 manifest 指定的固定归档，不自动下载或跳过。覆盖固定源码补丁检查、重复应用、冲突拒绝、可逆性，以及 headless、Actell、Avatar、caption font、TTS 接点叠加后回滚仍逐字节一致，并拒绝组合 UI 的未知改动；C ASan/UBSan 编译真实电源输入处理器与状态机，覆盖短按、唤醒吞键、LONG/RELEASE 同批、启动消隐、错误/重放、交错 Talk/Speaker 和 83,521 组四步事件/错误序列；生产 PMU 源码以 fake I2C 验证读写错误、门槛回读、10 秒/开机门槛保留和旧接口兼容。测试只使用合成输入。

验收分开记录源码检查、主机测试、目标编译和实物。目标编译与本机结果见 `local/validation/waveshare-216-controls/`；主机测试不代表硬件通过。尚需实物验证：1.5 秒实际门槛、开机持续按住/快速松手、短按捕获率、熄屏及 Wi-Fi nap 在电池/USB 下的最坏唤醒延迟和功耗、独立受控的约 10 秒硬关机与再次开机、真实 TTS 静音/恢复、配对确认和 BLE 菜单交互，以及头像、中文长回复、设置/休眠/关机页、圆环边缘和低亮度图标可读性。这些未完成项保持待验收；不清 NVS、不重配对。

基础设备验证已完成：正式字体/TTS/controls 组合构建后更新非活动 app 槽并切换启动；镜像与启动选择校验通过，持久数据和现有资源保留。新槽启动、字库加载与 45 秒无 panic/watchdog 观察通过，未出现 PWR 初始化或锁存错误。用户实机确认顶部三图标、左键两次切换扬声器、中键短按休眠/唤醒、右键 Talk 按住/松开全部正常。这些结论不包含长按门槛、独立硬关机、功耗、完整触屏/显示覆盖或云端 TTS 播放验收。分层报告见 `local/validation/waveshare-216-controls/report.json`。

## 已授权设备更新

正式设备构建在既有字体/TTS runner 后追加 `--with-controls`，输出到独立 `local/build/caption-firmware/private-fonts-tts-controls/`。先应用本扩展，再在已激活 ESP-IDF 的环境中执行：

```sh
python3 bridge/extensions/muse-fonts/tools/build.py \
  --sdk local/build/sdk-copy --components-dir local/build/sdk-copy/esp32/managed_components \
  --mode on --with-tts --with-controls --secrets "$TTS_SECRETS_FILE"
```

该模式通过 `apply-secrets.py` 普通导入，保留 TTS；不读取或回显真实生成配置和日志。编译器验证字体/TTS、板型与原生 24px 开关。真实构建失败时只报告失败状态，回到合成配置排错。

设备已具有匹配的双 app 槽和共享字库时，可在用户明确授权刷机后使用：

```sh
python3 bridge/extensions/waveshare-216-controls/tools/flash.py \
  --build local/build/caption-firmware/private-fonts-tts-controls \
  --resources local/build/read-only-resources/actell_device_sans_regular_24_4/resources.bin \
  --check
# 核对计划后，去掉 --check 写入。
```

工具要求唯一连接的串口设备通过运行态板型、ESP32-S3 安全状态、16 MB Flash、实际分区表、OTA 选择和现有资源镜像校验；不匹配即停止。写入非活动 app 槽，校验成功后才更新另一 OTA 选择扇区，再验证引导区域、分区表、NVS、PHY 和 product 数据逐字节保留。当前启动槽不在写入范围内，保留用于后续显式切回；不将此流程视为自动健康回滚。`--check` 不写 Flash，完成后复位运行；正式写入后留在下载模式，运行 `python3 bridge/extensions/waveshare-216-controls/tools/boot.py` 完成复位和 45 秒脱敏启动观察；`--review-only` 复核既有私密启动日志，不再次复位。私密备份与原始输出保存于 `local/credentials/`、`local/logs/`，仅输出脱敏计划和校验结果。不会改写字库、清空 NVS 或执行初始配对。

## 回滚

```sh
python3 bridge/extensions/waveshare-216-controls/tools/stage.py local/build/sdk-copy --reverse --check
python3 bridge/extensions/waveshare-216-controls/tools/stage.py local/build/sdk-copy --reverse
```

回滚先核对当前源码仍为本扩展产物，反向应用补丁并删除完全匹配的扩展头文件，保留字体及其他集成。后续未知修改导致检查失败时应人工合并，禁止整树 reset。移除 overlay 后可恢复原显示版配置；已有生成 sdkconfig 不由暂存器修改。固件回退须另行授权，使用板型与分区匹配的已知固件。
