# 通用只读资源包

本轮已实现可复现的主机打包与校验工具，生成六种候选的真实二进制字体和资源包；没有选定最终字号字重；Regular 24px/4bpp 候选包已部署至设备并通过读回校验。[只读映射、按需字体解码和像素分页](caption-font-integration.md)已实现并有主机及目标构建证据；首次迁移与启动核查已通过，升级/回滚和显示验收仍待完成。

## 格式与共享约定

`ACTRES01` 容器使用小端 64 字节头：8 字节 magic，四个 uint32（容器主版本、目录长度、条目数量、包总长度），32 字节目录 SHA-256，8 字节置零保留字段。主版本与 reader ABI 均为 1。规范化 UTF-8 JSON 目录紧随头部，必须装入首个 64KiB 块，目录最多 8KiB、32 条资源。

目录记录 `reader_abi`、候选 profile 和 entries。每项包含不带主机路径的逻辑 ID、格式版本标识、包内绝对 offset、length、payload SHA-256。资源起点均按 64KiB 对齐；空隙填充 0xFF。包不填充至分区全长。当前字体条目为 `caption.font`，格式 `lv-font-conv-bin-1.5.3`；另附完整 OFL、来源 manifest 和候选转换/覆盖报告。格式可容纳图片、动画等其他条目，不代表设备已实现这些资源的解码器。

两个 app 槽位共享同一只读资源包，应用只能读取。部署工具才负责更新包，普通启动不写、不格式化资源区。每个应用需要声明可接受的 reader ABI、必需的逻辑 ID 与格式；不匹配则回退内置原字体。主机 `verify --require` 已检查这份契约，设备端已实现相同的版本、边界与哈希检查；主机测试不代表 OTA 回滚已通过。哈希提供完整性，不替代既有认证与签名机制。

字体内部采用固定转换器的标准二进制格式，区别于 C 数组。无 kerning 时二进制格式使用整数像素 advance；后续分页要以二进制实际度量为准，不能复用原 C 字库的 1/16 像素 advance 假设。已核对 LVGL 9.5.0 现成加载器会分配并复制整个 bitmap；调用 from-buffer 也不意味着零拷贝。本项目已实现有界逐字形解码并在只读映射中访问字体，无整库 bitmap 分配；主机精确像素比对通过，实际板卡内存峰值与时延仍待测。

## 实际体积与预算

规划通用分区为 `actell_assets`，地址 `0x830000`、容量 `0x7D0000`（7.8125MiB），利用原表尾部可规划空间，保留原有分区和前面的 56KiB 对齐空隙。这里不再把 7MiB 固定划给字体，也不在尾部另留任意大小的空区：其他资源共享这块容量。此布局已按设备原表核查并完成首次部署；尾部旧资源的处理与备份边界见分区规划。

每个包仅含一份候选字体。字体升级余量暂按字体二进制大小的 10%、至少 256KiB，再向上按 64KiB 对齐；是可调预算，不是预先填入包中的垃圾数据或物理子分区。先扣除包的对齐占用、来源与授权，再扣余量，剩余容量可安排图片、动画等。

| 候选 | 字体二进制（字节） | 当前包（字节） | 字体升级余量 | 其他资源可用预算 |
|---|---:|---:|---:|---:|
| Regular 24px / 4bpp | 5,748,896 | 5,965,063 | 576KiB | **1.50MiB** |
| Medium 24px / 4bpp | 5,970,272 | 6,227,207 | 640KiB | **1.1875MiB** |
| Regular 28px / 4bpp | 7,026,432 | 7,275,783 | 704KiB | 128KiB |
| Medium 28px / 4bpp | 7,269,068 | 7,472,391 | 768KiB | **不足 128KiB** |
| Regular 24px / 2bpp | 3,311,016 | 3,540,231 | 384KiB | 4.00MiB |
| Medium 24px / 2bpp | 3,356,636 | 3,605,767 | 384KiB | 3.9375MiB |

28px Medium 的包本身能装入，但预留字体升级余量后超预算，不能判为满足本次规划。加入新资源时还要计算新增目录和条目对齐空间；表中的“其他资源预算”不等于任意零碎文件实际内容总和。字符范围未缩减，29,137 字形及 1,134 个 cmap 空缺沿用候选报告。

## 复现与验证

从仓库根运行（字体输入与依赖准备见 [扩展说明](../bridge/extensions/muse-fonts/README.md)）：

```sh
node bridge/extensions/muse-fonts/tools/generate.js
python3 bridge/extensions/muse-fonts/tools/pack.py build \
  --font actell_device_sans_regular_24_4
python3 bridge/extensions/muse-fonts/tools/pack.py verify \
  local/build/read-only-resources/actell_device_sans_regular_24_4/resources.bin \
  --require caption.font lv-font-conv-bin-1.5.3
python3 bridge/tools/dev.py test
```

`build` 支持追加 `--asset ID FORMAT FILE`，生成通用目录。仅添加已审查的公开资源及其必要来源/授权；不得打入凭据或用户内容。新增媒体的格式与许可由对应资源单独声明，字体 OFL 不覆盖其他媒体。

本机产物：

- `local/build/caption-fonts/*.font`：标准二进制候选。
- `local/build/read-only-resources/<候选>/resources.bin`：实际资源包；旁附目录和可直接阅读的授权。
- `local/validation/read-only-resources/<候选>.json`：包 SHA-256、实测字节数、余量与未验证事项。

七项容器测试覆盖共享资源和确定性输出、数据/目录损坏、截断与追加数据、伪造目录后的越界/重叠、重复 ID、未知版本、缺失/不兼容的必需资源，以及字体表和 glyph 偏移越界。所有候选包均已解包核对各条目的哈希、目录边界、字体表及字形索引，并验证重打包字节完全一致。统一回归另含四项真实 C 核心检查及原有 64 项主机测试，Node 与观察器检查也通过。分享检查为 0 项发现。逐字形解码和设备适配主机验证、三种完整目标构建详见[集成说明](caption-font-integration.md)，尚未真机验收。

迁移顺序、旧数据保留和共享资源更新边界见 [分区调整规划](font-partition-plan.md)。读取层、逐字形解码、SDK hook 和像素分页的软件实现已完成，已在单独授权后完成首次部署与启动核查，后续继续真机显示、性能与回滚验证。
