# 主机客户端

`mods/host/muse_client/` 是 Python 3.10+ 标准库客户端。使用 exact-body HMAC、带证书校验的 HTTPS、有限大小的 PNG/JPEG 获取和明确启用的 loopback mock。它不提供 Muse VM Home Link 接入或模型图像注入。

在根目录设置 `PYTHONPATH=mods/host`，或从 `mods/host/` 目录导入模块。先运行 `python3 mods/tools/dev.py mock`，然后可以在只针对本机 mock 的脚本中使用：

```python
import time
from pathlib import Path
from muse_client import Client, DirectHTTPTransport
from muse_client.mock_server import TEST_SECRET

client = Client("test-client", TEST_SECRET,
    DirectHTTPTransport("http://127.0.0.1:8765", test_only_loopback_http=True))
deadline = int(time.time() * 1000) + 60000
client.open_run("demo-run", deadline)
image = client.call("demo-run", "screen-1", deadline, "screenshot", {})["result"]
output = Path("local/captures/demo-screen.png")
output.parent.mkdir(parents=True, exist_ok=True)
client.save_image("demo-run", image["image_id"], image["generation"], output)
client.rpc("runs.cancel", {"run_id": "demo-run"})
```

该示例在根目录执行，截图为合成 1×1 图片；输出文件以 0600 创建，拒绝覆盖已有文件。测试 secret 是公开测试向量，不可用于真实服务。

## 导入连接信息

Actell 明确分享的连接信息包含客户端秘密，只存放在 `local/credentials/` 的 0600 文件中。证书指纹用于人工核对，不能替代 TLS 证书链/主机名校验。

```python
from pathlib import Path
from muse_client.connection_info import client_from_connection_info

client = client_from_connection_info(
    Path("local/credentials/connection-info.json").read_bytes(),
    ca_file="local/credentials/verified-public-ca.pem",
)
```

系统信任的服务器可省略 `ca_file`；这里需要的是已验证的 CA 公共证书，不是私钥或 PKCS#12。构造客户端不发起请求。实际使用前需要有效的本机配置、网络可达性和 Actell 端授权。

## 行为约束

- 所有读写均鉴权，真实端点要求 HTTPS；禁止重定向、环境代理和证书校验绕过。
- 写入超时或未知结果不自动重试。查询原 run/call ID，`not_found` 不能证明之前未执行。
- 图片验证覆盖容器元数据、大小、哈希、PNG CRC 或 JPEG 尺寸；使用前仍需成功解码。
- 保存图片不代表 Muse 模型已看到图片。主机 mock 不具备真实手机审批、持久化去重或设备执行能力。

见 [V1 协议](protocol/v1.md) 与 [设备 V2 协议](protocol/gadget-v2.md)。
