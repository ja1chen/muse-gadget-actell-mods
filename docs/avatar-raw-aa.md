# 离线 RAW 抗锯齿角色

默认 160 网格角色使用离线生成的圆润脸型、静态阴影和 coverage mask；仍按最近邻显示到 320 像素。头部有 65 档点头姿态和三个半像素位置变体，眼睛和动态椭圆采用量化几何，因此不宣称 AA 与程序式版本逐像素相同。双点头公式、稳定嘴型峰值观察和时间包络、字体、音频与按键链路保持原样。

## 来源与复现

来源为 `raw-aa-handoff`，沿用 Apache-2.0。权威代码位于 `bridge/extensions/avatar/src/`。`muse_pixel_geometry.c` 保留原稳定嘴型几何，供离线生成和程序式回退比较；`muse_pixel_aa_fallback.inc` 是复用图像缓冲的程序式实现。`muse_pixel_aa_assets.h` 只有格式视图与偏移，不包含大型像素数组。完整资源和生成中间数据均留在忽略的 `local/`。

raw 文件为 2,269,340 字节，SHA-256 为 `822ccdc0eff4dc3648ff6cb540796a7f811912c1c8284cd89d6647d234780794`，含 6,836 条记录。设备不执行超采样或解压；格式包含 64 字节 `ACTAA001` 头、RGB565/coverage 数据及索引。CRC 用于兼容性和损坏检测，不代替签名或可信部署流程。头部颜色已对黑底预混，改背景、形状、颜色或尺寸必须重新生成。

使用已按当前扩展暂存的固定 SDK 头文件，在安装 NumPy 的 Python 环境执行：

```sh
python3 bridge/extensions/avatar/tools/raw-aa-generate_assets.py \
  --include local/build/sdk-copy/esp32/components/muse
python3 bridge/extensions/avatar/tools/raw-aa-externalize.py
```

产物位于 `local/build/avatar-raw-aa/generator/`；将最终 raw 与来源清单中的哈希比较后，才可用于部署。生成器不会覆盖权威源码。`check-raw-aa.py --include ... --raw ...` 检查实际 C 的绑定、损坏拒绝、解绑重绑、AA 缺失计数、程序式回退像素及绑定态 ASan/UBSan。常规 `dev.py avatar` 在本机已有 `local/build/avatar-raw-aa/assets/avatar-aa.raw` 时附加这些检查；缺少 raw 时明确跳过绑定检查。

## 存储和启动

共享容器条目为 `avatar.aa` / `actell-avatar-aa-raw-v1`，沿用 `ACTRES01` ABI 1。启动时先检查两块图像数组首末地址都位于外部 PSRAM，再验证资源目录的哈希、条目数量、范围、重复 ID 和格式，仅 mmap AA 条目的子范围并执行 raw 绑定校验。映射在整个绘制期保持，不把约 2.16 MiB 素材复制为 const/EMBED 或整包堆内存。

绑定发生在 UI 启动、任何 decoder/draw 发布之前，不提供后台热替换。缺少资源、目录不兼容、mmap 或 CRC 失败时，保留相同 PSRAM 缓冲上的程序式稳定嘴型回退。PSRAM 配置缺失直接编译拒绝，运行时地址异常则停用角色绘制、输出有界黑帧，不改用内部大数组。64 网格版本不调用专属绑定接口。

默认 AA 的 framebuffer 和 head cache 各为 51,200 字节，合计 100 KiB，全部使用外部 BSS；另有少量内部控制变量和 scale 表。共享 Flash 映射仍占用 MMU 和控制资源，不能称零 RAM 成本。目标链接图、启动地址检查、录音并发与总帧耗时应分别验证，主机 render 耗时不能换算为 ESP32 FPS。AA 过渡色可能扩大 dirty/flush 范围。

## 保留现有资源的部署

只有明确获授权的设备更新才能写资源。`prepare-raw-resource.py --current ... --raw ... --output ...` 仅在本机生成更新计划：验证当前包，保留每个已有条目的偏移和字节，把 AA 追加到 64 KiB 对齐的空闲尾部，再生成新目录。追加前必须读回旧包末尾到新条目起点的对齐间隙，并通过 `--alignment-gap` 传入原样保留，不能假定设备上的间隙已经擦为 0xFF。资源不足、重复 AA 条目或未知资产哈希均拒绝；不改变分区表或两个 app 槽。

实际部署先读回并备份现有包，验证当前分区和完整资源，再写独占空闲扇区中的 AA 数据并回读，最后更新资源目录并验证整包；字体 payload 不擦写或搬移。目录更新本身不是掉电原子事务，必须保留旧目录以便受保护恢复。新目录仍被旧固件作为同 ABI 的共享容器读取，忽略新增条目。目录写入/校验失败时恢复旧目录并验证原包；资源成功后再写非活动 app、核验旧活动 app 和持久数据并切换启动。不能省略资源备份和校验，也不能把普通测试当作刷机授权。

部署后的视觉、真实语音口型及录音并发点头性能需要真机验收；启动和连接正常不能替代这些验收。
