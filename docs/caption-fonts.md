# 中文字幕字体实施与容量筛选

字体候选、实际 cmap 覆盖、授权与来源锁定、转换字形预览和目标字库对象编译均已完成。完整静态字库无法与程序一起容纳在现有 app 分区，因此采用外部共享的通用只读资源路线。[资源读取、按需解码与像素分页](caption-font-integration.md)已实现，默认关闭；[二进制资源包与实测预算](read-only-resources.md)已生成，首次迁移与启动核查已通过，显示及性能验收待完成。

## 已核对的工程基线

- 开始实施时仓库为 `main`，HEAD 为 `a66821b`，工作区干净。
- Muse SDK 仍固定 `812c46f9fe42b69d916613a9dc702cbac5ff7b54`；本次重新计算本机归档 SHA-256，与 [来源清单](../upstream/sources.json) 一致。没有升级 SDK 或覆盖既有扩展。
- 现有有屏暂存源码声明 LVGL 9.5.0，Waveshare 2.16 使用的 `partitions_muse.csv` 有两个 4MiB OTA app 分区。本节记录最初源码筛选基线；后续设备核查与部署见分区规划。
- `caption_font()` 已支持原有 CJK fallback，但另有字幕高度直接读取 unscii-16；`muse_chat_text.c` 按列宽换行。比例字体需要同时处理真实行高、像素分页、规范化和动画，不能只换字体指针。
- 历史本机 app.bin 为 3,018,752 字节；这不是本轮重建结果，不用它冒充当前固件验收。

## 可复现候选

源码位于 [muse-fonts 扩展](../bridge/extensions/muse-fonts/README.md)。原始字体、完整授权和转换器固定版本及哈希见 [manifest](../bridge/extensions/muse-fonts/manifest.json)。派生字库名称为 **Actell Device Sans**；原字体是 Adobe Source Han Sans SC 2.005R。

每个候选实际保留 29,137 个字形，指定范围内另有 1,134 个 cmap 缺字，完整列表写入本机候选 JSON。包括基本汉字和扩展 A，未按演示语料或词频裁剪；不承诺所有汉字、emoji 或组合字符。生成时启用压缩，显式关闭 kerning，保留比例 advance；后续布局必须采用相同度量规则。

字体资源仍按 OFL-1.1 分发，独立工具代码按根目录 Apache-2.0。本机生成目录随附原始完整版权与授权、来源 manifest 和各候选报告。授权依据为 [Adobe 固定版本 LICENSE](https://github.com/adobe-fonts/source-han-sans/blob/2.005R/LICENSE.txt)，使用边界参考 [SIL OFL FAQ](https://openfontlicense.org/ofl-faq/)。字体许可不代表其他角色、图像、音乐或视频素材已获许可。

## 本轮目标对象测量

使用既有 ESP32-S3 GCC 15.2.0 和 LVGL 9.5.0 头文件编译全部六个候选。下表为对象的可分配字体数据，**不是 C 源码文件大小，也不是最终固件 Flash 增量**。使用独立 LVGL 配置，开启大字库描述符及字体压缩；未更改实际固件配置。

| 候选 | 字体对象数据（字节） | 相对 4MiB app 分区 |
|---|---:|---|
| Regular 24px / 4bpp | 5,990,327 | 字库本身已超出 |
| Medium 24px / 4bpp | 6,211,813 | 字库本身已超出 |
| Regular 28px / 4bpp | 7,267,836 | 字库本身已超出 |
| Medium 28px / 4bpp | 7,510,308 | 字库本身已超出 |
| Regular 24px / 2bpp | 3,552,553 | 仅余 641,751 字节供程序及余量 |
| Medium 24px / 2bpp | 3,598,082 | 仅余 596,222 字节供程序及余量 |

筛选暂设 256KiB app 空余预算；2bpp 候选扣除此余量后仅剩约 0.32–0.36MiB 供程序。结合既有程序规模，不能把它当作可直接集成的方案。原有 Unifont 移除后的精确净增量仍须实际链接确认。

24px 候选输出 line_height 为 46px，28px 为 54px，包含所保留字符的极值。后续布局应使用真实字体度量，并检查是否需要经覆盖审查的度量策略；不能直接假设行高等于字号或裁掉字形。预览只展示 1:1 转换栅格，尚无原版 16px 同屏比较、LVGL 解码截图或 AMOLED 实拍。

## 产物与验证边界

- `local/build/caption-fonts/`：六份生成 C、ESP32-S3 字库对象、来源及授权副本。
- `local/validation/caption-fonts/`：候选 PNG、覆盖和生成哈希报告、`summary.json`、`budget.json`。
- `local/logs/caption-fonts/`：字库对象编译输出。

生成资源未纳入 Git。工具不访问凭据、账号或设备。本页表格属于最初字体对象筛选；后续完整目标构建、真实字形解码与像素分页验证见[集成说明](caption-font-integration.md)。尚未进行真机 RAM/PSRAM/帧耗时测量；后续首次部署与启动核查不代表整机验收。

本轮现有主机回归通过：64 项 Python/原生检查、Node 静态与 DOM 检查、角色事件顺序检查及 headless 观察器 ASan/UBSan 检查。首次执行因沙箱禁止绑定 loopback 端口失败，获准开放本机测试端口后整套重跑通过。分享检查为 0 项发现，`git diff --check` 通过。新增实现的专项验证另见集成说明。

## 后续接入

保留既有数据的追加分区布局、迁移顺序、资源更新与恢复边界已整理为 [字体分区规划](font-partition-plan.md)。已完成表结构校验，并在单独授权后完成首次设备迁移；详细边界见分区规划。

采用独立通用只读资源分区及映射，以保留完整字集和双 OTA。首次部署已核查实际 Flash 尾部并验证字体加载；资源更新、OTA 字体版本配套及回滚仍需专项验收。既有分区未移动，仅追加共享资源区。

上述软件接入和专项验证已落实，当前范围与结果见[集成说明](caption-font-integration.md)。字体字号/字重仍为候选，最终选择和实机验收待完成；设置 UI 翻译不在本轮范围。
