# 原版消息字号与布局

上游固定 SDK 使用 `lv_font_unscii_16`，其声明行高为 17px；中文 fallback 也是 16px 级别。当前共享字库默认改为 **Actell Device Sans Regular 16px/4bpp、17px 行高、3px baseline**。中文、英文与标点继续共用覆盖字集，不恢复 ASCII 转写，不移除中文。

## 字形与度量

Source Han Sans 的 16px 原始全字集会产生 31px 极值行高；纵排重复符号等少数字形超出普通行。仅改 em 字号不能恢复原版密度。

生成器对高于 17px 的位图按面积采样等比缩小并保持水平中心，其余越界位图仅垂直移入 -3..14 的基线范围；保留原 advance、码点与完整位图，不裁切。常用汉字不缩放。当前覆盖 29,137 个字形，其中 16 个缩小，547 个作垂直移位；移位统计可包含已缩小字形。逐字像素 oracle 对应最终处理后的栅格，不冒充未经处理的 FreeType 输出。

24/28px 候选仍可显式生成用于历史比较。设备读取器要求 em 16、行高 17、baseline 3；旧字号资源即使容器哈希正确也回退原字体，防止只更新应用后又被旧资源撑大布局。

## 布局范围

- 普通回复恢复原版三行；缩小阅读区域沿用上游算法。
- 消息区域格宽采用原 unscii-16 的 `M` 宽度，实际混排依然按新字体 advance 做像素分页，因此显示的字符数量取决于文本。
- 待机两行字幕高度、底部边距、角色及音量条的垂直位置恢复上游公式；移除为大字库增加的两行普通回复及底部补偿。
- 保留 Avatar 原生网格与显示布局分离、现有字号以外的角色行为，以及按键扩展的顶部图标位置、尺寸、颜色、无边框处理和硬件键行为。顶部 Montserrat 24px 属于硬件适配，不随消息字号改为 16px。
- 保留中文规范化、缺字问号、像素换行、分页隔离与资源损坏回退。

## 重新生成与验证

```sh
node bridge/extensions/muse-fonts/tools/generate.js
python3 bridge/extensions/muse-fonts/tools/pack.py build --font actell_device_sans_regular_16_4
python3 bridge/extensions/muse-fonts/tools/run-font-host.py
python3 bridge/extensions/muse-fonts/tools/run-resource-port.py --components-dir "$RESOLVED_COMPONENTS"
python3 bridge/extensions/muse-fonts/tools/check-stage.py --with-avatar --with-tts
python3 bridge/tools/dev.py test
```

生成文件仍只放在 `local/build/caption-fonts/`，新共享包位于 `local/build/read-only-resources/actell_device_sans_regular_16_4/resources.bin`，报告和预览位于 `local/validation/`。输入字体和转换器沿用固定版本与哈希，OFL/MIT 来源及授权不变。

当前资源包为 3,409,357 字节，字库 payload 为 3,149,540 字节。所有 29,137 字形经实际 C 解码器验证最终像素、度量和行内边界，ASan/UBSan 及分页检查通过；设备适配器的有效包、损坏包、旧大字号包等八类主机场景通过。补丁检查核对原版消息几何，并覆盖 Avatar、字体、TTS 与硬件 controls 组合。

ESP-IDF 6.0.1 的显示板公开配置完整构建通过，包含现有 Avatar 及顶部 controls 适配；应用为 3,084,288 字节，4MiB 槽余 1,110,016 字节。该编译 profile 不含真实账号或 TTS key，不代表联网播报验收。112 项主机回归及原生/前端检查通过。

这些是源码、生成资源、主机和目标构建验证结果，不能当作设备显示验收。后续设备更新需要同时部署新应用和新资源包；不能直接使用旧大字号资源，也不能使用通用完整 `flash_args` 覆盖配对或 OTA 状态。应用与资源写入须用户明确授权，只更新匹配分区，保留顶部硬件适配、配对、已有数据及回退依据。新生成的资源不会自动刷入。

## 设备启动核查

私密设备配置构建已完成，保留双向 TTS、顶部 controls 和 Avatar 外部 BSS。非活动应用槽及新字体资源写入/校验通过，旧资源全区备份校验通过；配对、产品数据、分区表及回退应用保留。45 秒启动观察确认共享字体 29,137 字形、17px 行高，普通回复三行、阅读十四行；Wi-Fi 与 Muse 会话连接，无 panic/watchdog 或电源控制初始化错误。该记录不代替屏幕观感、动态分页与交互播报验收。操作报告位于 `local/validation/caption-native-flash/`，原始日志和回退备份仅保留在本机私密目录。
