# 工程约定

## 目录与所有权

- `bridge/`：本项目工程链路的总目录。新增客户端、扩展、补丁、测试和开发工具优先归入其对应子目录，不在仓库根目录新增平级实现目录。
- `bridge/host/`：本工程 Python 客户端、loopback mock、网页及其测试。运行入口为 `python3 bridge/tools/dev.py`。
- `bridge/extensions/`：本工程维护的 gadget-actell、avatar、headless-console 和 muse-fonts 扩展源码与通用配置；avatar 的 `preview/` 仅本机保留，整体忽略，不提交或上传。
- `bridge/patches/muse-gadget-sdk/`：仅保存对固定 Muse Gadget SDK 的差异。本工程的“通过补丁修改上游”只适用于 Muse Gadget SDK。
- `upstream/`：只保存 Muse Gadget SDK 的来源 URL、固定 commit、归档 SHA-256 与接入说明。SDK 完整源码、Git 历史、开发签名密钥和下载包均放在 `local/upstream/`。
- `docs/`：可公开的架构、协议和使用说明；`bridge/extensions/skills/`：本工程交付的 Muse 使用说明，作为扩展一起维护。
- `bridge/tools/`：可复用开发、检查和验证程序。测试代码应纳入 Git；测试产生的报告、日志和二进制不得混入源码。
- `local/`：工程内唯一的本机数据目录，整体被 Git 忽略。用户指定的秘密配置源文件可以放在工程外，不得复制进工程。不得使用 `git add -f` 绕过。
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

### 秘密配置的工具边界

- 秘密配置使用 `sdkconfig` 风格 KV，由用户指定路径；代理不得直接读取、搜索、展示或复制秘密文件。只通过经审查的导入工具写入指定 `local/build/` 内的配置，工具只报告状态，不回显值。
- 含真实凭据的生成配置、头文件、固件和可能回显凭据的日志同样不得直接查看或搜索。必要的构建/刷机工具可在授权范围内使用这些文件，输出保持本机私密；排错使用独立合成配置，不回显真实日志。
- 主机测试使用合成输入；不得用用户真实秘密验证导入工具。字体独立构建只导入 SDK token，不引入其他功能的凭据或实现。

## 上游与扩展修改

- 以 `upstream/sources.json` 固定 Muse Gadget SDK 基线；先核验归档哈希/commit，再准备 `local/build/` 副本。不要直接开发或覆盖干净 SDK 检出。
- 扩展源码的权威位置是 `bridge/extensions/`，SDK 差异的权威位置是 `bridge/patches/muse-gadget-sdk/`。暂存 SDK 中的改动必须回写对应扩展/补丁，不能只留在忽略目录。
- Headless 补丁包含新增文件的副本；修改 `bridge/extensions/headless-console/` 时同步 `bridge/patches/muse-gadget-sdk/headless.patch`，验证应用后文件逐字节一致。
- 补丁使用上游仓库相对路径；依次应用 headless（可选）、gadget hook/component、avatar（可选）、caption font hook/component（可选）。用干净固定基线验证 `git apply --check`，避免重复应用。
- Actell 是独立协作应用，其实现和修改直接在 Actell 工程维护。本工程不生成、分发或重新应用 Actell 补丁；其他仓库也不套用 Muse SDK 的补丁流程。联调时核对对端实际源码、分支和未提交状态，不以历史补丁推断已提交或可运行。
- 不复制整个 SDK 到可分享目录，不把 SDK 文件伪装成本工程原创。保留来源和许可证；本工程独立代码遵守根目录 LICENSE，外部应用保留各自许可。
- SDK 版本、补丁路径或目录布局变化时，同步来源清单、入口脚本、测试、README、AGENTS.md 与 .gitignore。

## 两份差异索引的持续维护

后续涉及 Muse Gadget SDK 的开发，必须随实现同步维护以下两份索引；已有内容增量更新，不能只在聊天、提交日志或 `local/` 中记录。

- `docs/muse-sdk-changes.md`：按类别概括本项目对 Muse Gadget SDK 本身的改动，让读者快速了解相对上游有哪些适配。
- `docs/muse-sdk-extensions.md`：按类别概括本项目为 Muse Gadget SDK 增加的功能，让读者快速了解新增了哪些能力。

维护时遵循以下要求：

- 保持简短，每类用简短说明概括改了什么或增加了什么，可引用组件、补丁、协议和使用文档。
- 不展开逐文件、逐函数改动，不罗列配置参数、命令步骤、协议字段或测试明细；这些内容留在对应详细文档中。
- 不写具体日期，不按时间追加开发或验收日志；索引反映当前差异，不充当 changelog。
- 使用无编号的分类条目，避免顺序编号与全局计数；修改相关类别即可，减少不同分支维护时的冲突。
- 一项功能同时涉及 SDK 修改与扩展实现时，两份索引同步更新并按需相互引用，避免重复展开。
- 必要的能力限制用简短说明保留，不把已实现或主机测试写成真机验收通过；详细验证记录放在对应文档或 `local/`。
- 沿用本文件的数据与分享规则，不写入真实凭据或设备信息。完成相关任务前检查索引及引用是否仍准确，涉及 README 描述时同步中英文版本。

## 开发与验证

- `python3 bridge/tools/dev.py test`：运行 Python/原生 C/Node 主机回归及 headless 观察器检查；日志写入 `local/logs/current/`。
- `python3 bridge/tools/dev.py avatar`：使用固定 SDK 归档的头文件和当前扩展源码，在临时目录重新暂存并运行 C sanitizer harness；报告写入 `local/validation/current/`。
- `python3 bridge/tools/dev.py console` / `mock`：只启动本机 loopback 模拟服务。
- 使用开发入口设置的 Python 缓存和临时目录；新增工具默认向 `local/` 输出。Avatar 新生成的预览和验证报告写入 `local/validation/avatar-preview/`，既有 `bridge/extensions/avatar/preview/` 保留本机且整体忽略，不纳入 Git 分享。
- 目录重构应运行受影响的测试、静态资源检查和补丁暂存检查。结果区分源码检查、主机测试、目标编译、真实服务和真机验收；旧记录不能当作当前测试通过。
- 保持 TLS 校验、HMAC、审批、截止时间、去重和禁止自动重放语义。普通测试不调用真实账号、设备或公网 API。
- 字体与通用资源的生成数据、oracle、资源包和固件保留在 `local/`；扩展维护来源与授权、字集、只读读取器、布局和生成/验证工具。资源缺失或不兼容时回退原字体，不自动写 Flash、格式化、清空 NVS 或重置配对。资源分区 CSV 必须保留所有原有条目，设备迁移独立授权。

## Git 与分享

- 提交/打包前运行 `python3 bridge/tools/check-share.py`，检查 `git status --short` 和待提交差异，确认 `local/` 及秘密文件均被忽略。
- 只选择与任务相关的文件暂存；初始化 Git 不等于授权提交、推送或发布。
- 对外打包从已审查的 Git 提交生成 `git archive`，输出到 `local/`；不要递归压缩整个工作目录。
- 用户数据的删除、实际刷机、账号配对和外部发布按用户明确授权执行。常规本机整理与验证可直接完成，不额外增加确认步骤。
