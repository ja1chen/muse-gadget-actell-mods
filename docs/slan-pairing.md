# Simple LAN 配对（SLAN/1）

设备端使用现有 mDNS、HTTPS 和 Actell worker 完成申请、领取及 hello 确认。认证前必须等待本次启动的 SNTP 成功校时，不能仅凭 RTC 年份判断；请求时间在发现网络地址后、签名前采样。手机保留原有 ±60 秒时间窗。手机必须实现同一[共享契约](protocol/slan-v1.md)，SHA256 为 `d1ecce424490d4f8e7ea3dc927619f04b7c3fe8b9b522ee2dcacd286c23cee75`。本工程维护 Muse 端；共享发现字段已与 Actell 同步。

## 使用流程与边界

在 iPhone 开启“允许 Muse 连接”，待服务实际运行后点“添加 Muse”；设备设置打开 **Actell → Pair with Actell**，自动向唯一开放配对窗口的 Actell 发送申请，再在 iPhone 上选择并允许这台 Muse；ESP32 不显示手机候选列表。新申请和最终确认统一使用名称 **Muse Gadget**。手机批准后设备自动领取、保存并发送认证 hello，无需用户再次确认。设备分别显示等待批准、领取、保存和最终确认状态，收到成功 hello 且 `pairing_state=active` 后显示连接成功。配对成功默认开启截图、应用列表及控制权限，可在 Actell 分别关闭；重连不能重新打开用户已关闭的权限，已开放的 Muse 操作无需逐次确认，仍受权限、期限、停止和不重放规则约束。全部关闭后的 `pending_authorization` 不代表配对失败。

首次配对采用可信 Wi-Fi 上的 TOFU，**不抵御同局域网主动中间人**。名称只供选择，不是身份核验。后续只接受保存的精确 DER pin，不能按 TXT 或领取响应更换证书。不开 ESP HTTPS server，不使用 BLE、二维码或手工证书交付。官方 Muse 账号、Wi-Fi、BLE、聊天与 OTA 的实现保留。

当前发现适配使用 `_actell-muse._tcp` 的 `pair=1` 选择候选，并要求 TXT `host` 提供稳定 TLS `.local` 主机名；SRV hostname 只作为网络定位，不直接当证书身份。缺失、重复或非法 `host` 时提示升级。这是两端统一的发现字段补全；共享契约已同步更新本端文件与 hash，对端应使用同一份契约。稳定 host 不要求可解析；重连继续使用保存的 host/pin，广播身份提示不匹配时拒绝候选，不覆盖原记录。仅接受 Wi-Fi STA 上的 RFC1918 IPv4，进一步限制到当前 Wi-Fi 子网，排除本机、网络及广播地址；套接字绑定 STA 接口，不走其它接口/代理。多个合法配对窗口同时开放时不发送申请，提示只在目标 iPhone 保留添加窗口；手机未开服务或添加窗口时提示到手机开启。地址改变可以重新查找同 host/port；已暂存或保存的证书不能改变。跨子网、IPv6-only 及公网路径不支持。

无有效时间时启动既有 SNTP 并显示时间未同步，证书时间与 ACTELL1 时间窗不放宽；不承诺离线冷启动或 iOS 后台常驻。

## 精简配对记录

NVS 命名空间为 `actell`，唯一支持的配对键为 `peer_v3`。这是新的存储格式，不会自动迁移旧配对；Actell 的线上 SLAN/1 和 HMAC 契约不变。

| 内容 | 字节数 |
|---|---|
| 头部：`SLP3`、编码版本 1、pending/active 状态、总长度和三个字符串长度 | 14 |
| 认证 secret | 32 |
| 证书 SHA-256 指纹（二进制） | 32 |
| origin、client ID、PEM | 各字段实际字节数，无 NUL 或填充 |

所有长度使用大端 16 位整数，总记录长度必须精确匹配，且不超过 4,572 字节。加载时拒绝未知版本或状态、越界长度、嵌入 NUL 和证书／指纹不匹配，并清空失败输出。常规证书无需占满原来预留的 4,096 字节。记录仍采用单个 blob 替换，只有 pending 转为 active 时写入确认状态；普通重连不改写配对。

## 实现

- `actell_slan.c`：≤512 字节请求和 ≤4096 字节响应的严格平面 JSON schema；检查 UTF-8、重复/未知键、类型、UUID、无 padding canonical base64url、名称、host、port、pin 与期限。原始申请 body、request_id、claim_token 在本次申请内不变。
- `actell_pairing.c`：通过原有单 worker（栈配置由 8 KiB 增至 12 KiB）串行处理扫描、选择、申请、轮询、保存、ACK、重试与删除；无新增常驻任务。claim_token 与正式 secret 分离，轮询不写 NVS。正常轮询间隔 2 秒，网络失败退避 2/4/8 秒，HTTP 超时上限 10 秒。失败、拒绝、取消与过期不显示成功。
- `actell_discovery.c` / `actell_lan_transport.c` / `actell_leaf.c`：首次连接在该客户端自己的 Mbed TLS 配置挂载校验 callback，保持 VERIFY_REQUIRED，独立验证单张自签非 CA、P-256、服务器用途、DNS SAN、有效期及自签名，并将握手原始 DER 转 PEM、计算 pin。后续 request/claim 均精确比较同一 pin。验证成功前不发送请求材料，不附公共 CA bundle，不修改全局 CA store 或其它官方客户端。拒绝 redirect、cookie、压缩、chunked、缺失/重复关键响应头和过大 body；要求 JSON、Content-Length、no-store。
- `actell_provision.c`：仅支持精简 `peer_v3` blob：独立 magic、版本、精确总长度与 pending_ack/active 标记，地址、客户端标识和 PEM 证书按实际长度保存，指纹使用 32 字节。无旧格式读取、迁移或回退。领取后保存 pending，再发正式 secret 签名的 hello；收到 active 才持久改为 active。普通 RPC loader 拒绝 pending。
- `actell_bridge.c`：每次 hello 重新生成 RPC request_id，并沿用每请求新 nonce/时间/ACTELL1 HMAC；丢 ACK 后仅重试 hello，不重新领取、生成 key 或重放旧动作。设置页在配对相关操作下方提供持久保存的 Auto-connect 开关，缺少偏好时默认开启以保持原行为。开启后启动及每 30 秒使用已保存身份发送只读 hello；校时成功会唤醒同一 worker，对尚未连接的保存配对立即尝试一次 hello，无需等待下一轮轮询。关闭自动连接、取消或认证明确拒绝后的暂停不会被校时唤醒绕过，已连接时不重复发 hello；pending 收到确认后保存 active，网络失败保留材料继续尝试。自动或手动连接被明确拒绝后暂停后台重试，保留配对与重连入口，由用户检查后手动 Reconnect；关闭仅停止后台重连，手动重连和工具调用仍可用。偏好保存失败恢复原开关状态，独立 NVS key 不重写配对。active 探测和手动重连成功均不重写配对 NVS，仅 pending 收到确认后保存 active，不重放业务。旧 USB 配置继续可用且默认关闭。
- `actell-hook.patch`：新增设置页与存储/网络初始化后的 worker 启动接点；页面离开时清理缓存指针，重进重建。复用既有命令/会话 hook，保留 generation、图片和活动反馈保护。取消只停止本次连接并保留已保存记录；网络、认证、存储读取失败均不自动删除配对，只有显式 Forget pairing 删除。取消/删除立即阻断本次传输；删除仅擦除当前配对 key 并提交，不写删除标记，不读取旧记录。NVS 写失败不宣称持久成功。

NVS API 以单个 blob 替换和 `nvs_commit` 为持久化边界；实际掉电/损坏仍需硬件测试。本轮未主动配置 NVS/flash at-rest 加密，也未核实实际设备的保护状态；有物理读取能力的人可能提取保存的正式 secret。没有烧 eFuse、改变 secure boot、删除官方账号/Wi-Fi/bond 或读取真实秘密配置。

## 构建与验证

先按[补丁说明](../mods/patches/README.md)在 `local/build/` 准备固定 SDK，运行组件 stage。目标为 Waveshare ESP32-S3-Touch-AMOLED-2.16、ESP-IDF v6.0.1。公开构建用 `overlays/sdkconfig.slan-public` 最后覆盖，空 token/Wi-Fi 且不签名，不应拿它替换已配真实账号的固件。示例在已配置 IDF 的 shell 内运行：

```sh
PROJECT_ROOT="$PWD"
cd local/build/sdk-copy/esp32
idf.py -B "$PROJECT_ROOT/local/build/slan-public" -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG="$PROJECT_ROOT/local/build/slan-public/sdkconfig" \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-waveshare-s3-216;$PROJECT_ROOT/mods/extensions/gadget-actell/overlays/sdkconfig.slan-public" build
```

关闭扩展用独立构建目录和最后覆盖 `CONFIG_ACTELL_BRIDGE=n` 的公开配置；不要复用含真实配置的构建目录。真实凭据只能通过工程的受保护导入工具处理。

```sh
python3 mods/tools/dev.py test
python3 mods/tools/check-share.py
```

主机测试执行真实 C codec、真实 worker 与真实 NVS 适配代码，并用 fake HTTP/NVS/clock 注入拒绝、过期、错 token、pin 替换、保存失败、丢 ACK、重启、撤销和删除等情形；ASan/UBSan 包括 20,000 组畸形输入。NVS 模型中的证书/PSA 是合成适配器，不冒充真实密码验证。dummy 原始字节与契约 hash 纳入测试。

独立原生密码检查执行**实际固件叶证书校验函数与同版本 Mbed TLS 源码**，验证正常 P-256、相同 pin，以及错误 SAN、无 SAN、过期/未生效、CA、错误用途、P-384、RSA、错误自签名、证书 bundle 与同名换叶拒绝；loopback TLS 验证 VERIFY_REQUIRED + 独立 callback 模式和服务器私钥持有。它不替代 ESP 网络适配或真实 iPhone 验收：

```sh
"$IDF_PYTHON_ENV_PATH/bin/python" mods/extensions/gadget-actell/tools/check-native-leaf.py \
  --mbedtls-root "$IDF_PATH/components/mbedtls/mbedtls" \
  --build-dir local/build/slan-mbedtls-host --prepare
```

该检查需要 `cryptography`；`--prepare` 自动构建原生 Mbed TLS 动态库。ESP fork 的原生构建关闭文件生成/包安装，并在本机兼容 include 目录映射其旧 private header 路径和空 sdkconfig；不改动 SDK 源码，测试产物只放 `local/`。临时合成私钥仅用于本机 TLS server，以 0600 保存并在检查后删除。

完整公开配置的扩展开/关目标构建、回归与原生验证结果记录在 `local/validation/slan/`，日志在 `local/logs/slan/`；最终验收使用独立固定源码目录 `local/build/slan-final-stage/`，固件留在 `local/build/slan-final-on/` 和 `local/build/slan-final-off/`，对应日志为 `final-build-on.log` / `final-build-off.log`；早期共享构建目录的结果不替代本次固定源码验收。运行时 heap/stack、水位、UI 实际显示、官方功能共存、实际 NVS 掉电以及 iPhone request/claim/hello 联调未测。这些历史构建记录不能替代当前真机验收；后续刷机、启动和真实调用结果分别保存在对应任务的本机验证记录中。

## 废弃方案清理与回滚

本任务清理了上一轮新建且未提交的 `ambp/` 核心、AMBP fixture/测试/oracle、BLE 构建失败门禁、AMBP 契约/Gate 文档和对应 README/索引条目。当前工程没有本任务的 QR UI 或临时 ESP HTTPS server；因此未做相关删除。官方 BLE、安全栈、通用 TLS/NVS、现有 RPC/审批、字体、TTS 和动画实现保留；历史本机日志仍在忽略的 `local/`，不再作为新方案验收依据。

回滚源码时只还原本任务的组件、接点、文档和测试，重新从固定 SDK 暂存；不要 hard reset 或覆盖其它修改。旧固件不能读取精简配对记录；旧配对需按明确授权单独清理并重新批准，正式固件不附带迁移或启动清理逻辑。删除新记录使用显式移除连接，不能恢复旧凭据快照或清官方 NVS。设备回滚须先检查实际固件和存储状态，不自动清空或恢复历史配对数据。

对端同步点：契约 hash、两份 dummy JSON、TXT `host`（稳定 SAN 主机名，与 SRV locator 分开）、`/v1/pair/request`、`/v1/pair/claim` 与 hello 的 `pairing_state=active`。pending_ack 的 scopes 为空；首次激活默认开放三项权限，之后尊重用户修改。手机服务关闭/重启后 pending key 撤销需由 Actell 实现和联合验证。

## Bonjour 身份名补全

Bonjour TXT host 使用手机现有证书的稳定 DNS SAN，并与 claim.host 一致。
Muse 用 SRV 地址/端口联网，用 host 校验证书并发送 SNI，不解析该稳定名。
首次暂存 host/pin，领取必须完全匹配；已保存身份不能被后续广播覆盖。
DHCP 地址和 SRV 主机名改变仍可发现同一身份。新配对缺失、重复或非法 host
显示版本不兼容；保留严格 hostname 校验。生产发现代码拆至 actell_discovery.c，
以合成 mDNS/Wi-Fi 测试 SRV/SAN 不同、IP 变化、换 host/pin 和旧版广播。
不增加操作或设备端服务，不代表真机联调验收。

发现补全验证：10 项 SLAN 测试通过，其中生产发现代码在 ASan/UBSan 下验证不同
SRV/SAN 名、同身份换 IP、不可被广播替换 host/pin、旧版广播和 claim host 不一致。
完整开发回归 88 + 16 + 7 项 Python 测试及 Node/观察器检查通过；16 项原生
叶证书/TLS 检查通过，loopback 服务端断言实际收到稳定 SAN 名的 SNI。
扩展开启与关闭的 ESP32-S3 公开配置完整构建均成功。日志为
`local/logs/slan/host-clarification-*`。未安装、刷机或执行真实 LAN 联调。

## 设备上的配对与日常使用

首次使用选择 `Pair with Actell`，在 iPhone 的 Actell 中开启 Muse 连接、选择 Add Muse，设备自动发现唯一开放窗口并发送申请，在手机上允许。ESP32 不再要求选择手机或显示 Bonjour 服务名列表；多个窗口同时开放时停止并提示缩小到目标手机。已配对后隐藏首次配对入口。

已保存配对会自动重连。`Reconnect` 只检查并恢复已有连接，不重新申请授权；收到手机允许但尚未完成确认时显示 `Complete pairing`。日常使用保持 Actell 打开、两端处于同一 Wi-Fi，返回 Muse 主界面使用。`Forget pairing` 用于移除本机保存的连接，权限仍在 iPhone 中管理。

设置首页的 Actell 行使用电话图标，右侧显示未配对、配对中、连接中、待完成配对、已连接或离线状态，与 Muse 行采用相同布局。连接状态来自已保存身份的认证 hello 探测；保存配对本身不代表在线。探测失败保留配对，后续成功可恢复显示已连接。此状态仅反映 ESP32 与 Actell 的连接，不代表 Actell 执行路由或 `runs.open` 准入就绪。
