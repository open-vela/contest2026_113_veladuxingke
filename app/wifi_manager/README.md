# contest Wi-Fi manager

该应用替换 R528 vendor 示例 `wifi_manager`，builtin 名称仍为 `wifi_manager`，因此现有 `/data/xiaozhi.sh` 无需修改。

主要差异：

- `/data/wifi.cfg` 不存在时静默等待，不重复打印错误。
- 非法配置按状态限速提示，所有失败分支均休眠。
- 不使用 `system()` 拼接 SSID/密码，避免 shell 注入。
- 不记录密码。
- 通过 WAPI/netlib API 直接关联并获取 DHCP 地址。
- 连接失败采用 5–60 秒封顶的指数退避。

配置格式：

```text
SSID="My Network"
PASSWORD="12345678"
```

也接受无引号格式。SSID 长度为 1–32 字节，WPA2 密码为 8–63 字节。

主机解析器测试：

```bash
gcc -std=c11 -Wall -Wextra -Werror \
  -Iapp/wifi_manager \
  app/wifi_manager/wifi_config.c \
  app/wifi_manager/tests/wifi_config_test.c \
  -o /tmp/wifi_config_test
/tmp/wifi_config_test
```
