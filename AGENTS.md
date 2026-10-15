# 工程约定

## 目录与所有权

- `bridge/`：本项目工程链路的总目录。新增客户端、扩展、补丁、测试和开发工具优先归入其对应子目录，不在仓库根目录新增平级实现目录。
- `bridge/host/`：本工程 Python 客户端、loopback mock、网页及其测试。运行入口为 `python3 bridge/tools/dev.py`。
- `bridge/extensions/`：本工程维护的 gadget-actell、avatar 和 headless-console 扩展源码与通用配置；avatar 的 `preview/` 仅本机保留，整体忽略，不提交或上传。
- `bridge/patches/muse-gadget-sdk/`：仅保存对固定 Muse Gadget SDK 的差异。本工程的“通过补丁修改上游”只适用于 Muse Gadget SDK。
- `upstream/`：只保存 Muse Gadget SDK 的来源 URL、固定 commit、归档 SHA-256 与接入说明。SDK 完整源码、Git 历史、开发签名密钥和下载包均放在 `local/upstream/`。
- `docs/`：可公开的架构、协议和使用说明；`bridge/extensions/skills/`：本工程交付的 Muse 使用说明，作为扩展一起维护。
- `bridge/tools/`：可复用开发、检查和验证程序。测试代码应纳入 Git；测试产生的报告、日志和二进制不得混入源码。
- `local/`：唯一的本机数据目录，整体被 Git 忽略。不得使用 `git add -f` 绕过。
- 根目录保留 `README.md`、`README.zh-CN.md`、`AGENTS.md`、`LICENSE`、`THIRD_PARTY.md` 和 Git 配置文件；第三方来源与许可归属集中记录在 `THIRD_PARTY.md`。
- `README.md` 为英文说明，`README.zh-CN.md` 为简体中文说明；两者顶部相互链接。修改目录、开发命令、能力边界或分享规则时同步两种语言。

## 本机数据分类

| 路径 | 内容 |
|---|---|
| `local/credentials/` | token、配网邀请、连接信息、私钥、证书、配对身份、设备专用配置；秘密文件使用 0600 |
| `local/logs/<任务>/` | 构建、测试、串口、运行日志 |
| `local/validation/<任务>/` | JSON 报告、验收记录、设备/模拟器标识和人工观察 |
| `local/build/` | 修改后的 SDK 暂存副本、固件、临时编译目录与可重建产物 |
| `local/cache/` | Python 字节码、依赖及工具缓存 |
| `local/upstream/archives/` | 固定版本上游源码归档 |
| `local/upstream/checkouts/` | 保持干净的上游检出，用于核对基线 |
| `local/captures/` | 真实截图、录屏和用户内容 |
| `local/archive/` | 历史交付材料、迁移前副本与迁移映射 |

文档示例使用相对路径、环境变量和明确的占位符。不得提交真实主机名、个人绝对路径、设备 UUID/序列号、Apple 签名团队 ID、云端私有文件/Page ID、账号状态或用户聊天。公开仓库 URL、归属信息和必需的协议/Bundle ID 不按秘密删除；改动身份标识前检查兼容性。

公开且明确标注的合成测试向量可以保留。新增密钥/证书样本优先在测试临时目录生成；真实凭据不写入源码、文档、聊天和日志。`.gitignore` 只是文件过滤，不能替代内容审查。

## 上游与扩展修改

1. 以 `upstream/sources.json` 固定 Muse Gadget SDK 基线；先核验归档哈希/commit，再准备 `local/build/` 副本。不要直接开发或覆盖干净 SDK 检出。
2. 扩展源码的权威位置是 `bridge/extensions/`，SDK 差异的权威位置是 `bridge/patches/muse-gadget-sdk/`。暂存 SDK 中的改动必须回写对应扩展/补丁，不能只留在忽略目录。
3. Headless 补丁包含新增文件的副本；修改 `bridge/extensions/headless-console/` 时同步 `bridge/patches/muse-gadget-sdk/headless.patch`，验证应用后文件逐字节一致。
4. 补丁使用上游仓库相对路径；依次应用 headless（可选）、gadget hook/component、avatar。用干净固定基线验证 `git apply --check`，避免重复应用。
5. Actell 是独立协作应用，其实现和修改直接在 Actell 工程维护。本工程不生成、分发或重新应用 Actell 补丁；其他仓库也不套用 Muse SDK 的补丁流程。联调时核对对端实际源码、分支和未提交状态，不以历史补丁推断已提交或可运行。
6. 不复制整个 SDK 到可分享目录，不把 SDK 文件伪装成本工程原创。保留来源和许可证；本工程独立代码遵守根目录 LICENSE，外部应用保留各自许可。
7. SDK 版本、补丁路径或目录布局变化时，同步来源清单、入口脚本、测试、README、AGENTS.md 与 .gitignore。

## 开发与验证

- `python3 bridge/tools/dev.py test`：运行 Python/原生 C/Node 主机回归及 headless 观察器检查；日志写入 `local/logs/current/`。
- `python3 bridge/tools/dev.py avatar`：使用固定 SDK 归档的头文件和当前扩展源码，在临时目录重新暂存并运行 C sanitizer harness；报告写入 `local/validation/current/`。
- `python3 bridge/tools/dev.py console` / `mock`：只启动本机 loopback 模拟服务。
- 使用开发入口设置的 Python 缓存和临时目录；新增工具默认向 `local/` 输出。Avatar 新生成的预览和验证报告写入 `local/validation/avatar-preview/`，既有 `bridge/extensions/avatar/preview/` 保留本机且整体忽略，不纳入 Git 分享。
- 目录重构应运行受影响的测试、静态资源检查和补丁暂存检查。结果区分源码检查、主机测试、目标编译、真实服务和真机验收；旧记录不能当作当前测试通过。
- 保持 TLS 校验、HMAC、审批、截止时间、去重和禁止自动重放语义。普通测试不调用真实账号、设备或公网 API。

## Git 与分享

- 提交/打包前运行 `python3 bridge/tools/check-share.py`，检查 `git status --short` 和待提交差异，确认 `local/` 及秘密文件均被忽略。
- 只选择与任务相关的文件暂存；初始化 Git 不等于授权提交、推送或发布。
- 对外打包从已审查的 Git 提交生成 `git archive`，输出到 `local/`；不要递归压缩整个工作目录。
- 用户数据的删除、实际刷机、账号配对和外部发布按用户明确授权执行。常规本机整理与验证可直接完成，不额外增加确认步骤。
