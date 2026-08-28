# TWI3 环境传感器开发与验收记录

> 更新时间：2026-08-10

## 1. 总线与接线

环境传感器固定使用 R528/T113-S3 的 TWI3：

| J3 引脚 | SoC 引脚 | 功能 |
|---:|---|---|
| 11 | PE6 | TWI3-SCK / SCL，mux 4 |
| 10 | PE7 | TWI3-SDA / SDA，mux 4 |
| 1 或 17 | — | 3.3V |
| 9、14 或 20 | — | GND |

PE6、PE7 由现有 Allwinner TWI/GPIO HAL 配置，不直接写 `PE_CFG0`。TWI3 注册为 `/dev/i2c3`，目标频率 100 kHz；TWI2 仍用于 FT5x06，频率 400 kHz。

三块模块并联：

| 传感器 | 7 位地址 |
|---|---:|
| SHT40 | `0x44` |
| GY-302/BH1750 | 优先 `0x23`，兼容 `0x5c` |
| SGP30 | `0x58` |

## 2. 软件结构

```text
envmon_i2c.[ch]          统一 /dev/i2c3 I2CIOC_TRANSFER 封装
envmon_sht40.[ch]        温湿度、CRC8、定点换算
envmon_bh1750.[ch]       地址回退、连续高分辨率照度
envmon_sgp30.[ch]        eCO2/TVOC、CRC8、预热
environment_service.[ch] 单线程 1 Hz 调度、状态和恢复
routine_model.[ch]       真实/演示/陈旧数据合并
routine_ui.[ch]          空气质量卡和状态 badge
```

采集线程只打开一次 `/dev/i2c3`，循环内不动态分配内存。每笔失败最多额外重试两次；一个传感器失败不会终止其他传感器或 UI。相同错误只在状态变化时记录，恢复时再记录一次。

## 3. 协议摘要

### SHT40

- 写 `0xfd`，产生 STOP；
- 等待至少 10 ms；
- 独立读取 6 字节；
- 温度和湿度词分别做 CRC8（init `0xff`、poly `0x31`）；
- 温度保存为 °C×100，湿度保存为 %RH×100；
- CRC 错误丢弃本次数据并保留旧值。

### BH1750

- 向 `0x23` 发送 `0x01`，失败后尝试 `0x5c`；
- 初始化时发送 `0x10` 连续高分辨率模式；
- 首次等待至少 180 ms；
- 后续直接读取 2 字节，保存 lux×10；
- 连续 3 个周期失败后才重新初始化。

### SGP30

- 初始化写 `0x20 0x03`，等待至少 10 ms；
- 使用单调时钟绝对周期，每秒写一次 `0x20 0x08`；
- STOP 后等待至少 12 ms，再读取 6 字节；
- eCO2 和 TVOC 分别校验 CRC；
- 初始化后 15 秒标记 WARMUP，仍维持约 1 Hz 调用；
- 连续 3 次失败后才恢复初始化。

## 4. 当前固件

```text
nuttx/nuttx.bin
  size:   6,982,552 bytes
  sha256: 596d693eaba2f1a8e1764ef0f2bf9bd935eee1b2a19ecd832fa2986eeb3aa29c

rtos_nuttx_r528s3-dshanpi_uart0_256Mnand.img
  size:   23,114,752 bytes
  sha256: 1519c575947374eecc706ee5a3204b8c153816d4bbd067d042ce64f8ce9c93ff
```

2026-08-10 触摸坐标修复版本构建和链接成功，NAND 镜像中的 `nsh.fex` 与 `nuttx.bin` 哈希一致。打包日志包含 `Dragon execute image.cfg SUCCESS` 和 `pack finish`；板级 `data/res` 缺失后回退公共资源，因而 shell 返回非零，但镜像已按 `docs/编译指南.md` 的五项规则完成校验。

## 5. 当前验收状态

| 项目 | 状态 | 说明 |
|---|---|---|
| 固件编译、链接和打包 | ✅ 通过 | 新模块均已进入 map |
| 新固件烧录启动 | ✅ 通过 | 用户确认可以正常显示 |
| 中文 UI 布局 | ✅ 通过 | 传感器版界面正常显示 |
| `/dev/i2c3` 通信 | ✅ 通过 | bus 3 上三个目标地址均可扫描 |
| SHT40 `0x44` 应答 | ✅ 通过 | i2ctool 扫描得到 `0x44` |
| BH1750 `0x23/0x5c` 应答 | ✅ 通过 | 当前模块为 `0x23`，`0x5c` 无应答符合地址脚配置 |
| SGP30 `0x58` 应答 | ✅ 通过 | i2ctool 扫描得到 `0x58` |
| 实时数值和动态变化 | ✅ 通过 | 屏幕实时显示，环境变化时数值随之变化 |
| 单模块拔插和恢复 | ✅ 通过 | 拔出后变“过期”且停止变化，其他模块正常；插回后无需重启应用或开发板即可恢复 |
| 故障日志限速 | ✅ 通过 | 相同故障不会持续刷屏 |
| 3 次限时运行冒烟测试 | ✅ 通过 | `ps` 前后一致；used 834444→797380 B，nused 均为 918 |
| 冷启动和长时间压力测试 | ⏸️ 本轮延期 | 10 次冷启动及 2–8 小时测试不执行，不标为通过 |

上述结论只覆盖用户实际提供的真机结果。软件频率配置不能替代逻辑分析仪波形结论；3 次短测也不能替代长期压力测试。

## 6. 已执行命令与后续验收

已执行地址扫描：

```text
nsh> i2c dev -b 3 -f 100000 -z 44 44  # 0x44 应答
nsh> i2c dev -b 3 -f 100000 -z 23 23  # 0x23 应答
nsh> i2c dev -b 3 -f 100000 -z 5c 5c  # 无应答，符合当前 BH1750 地址配置
nsh> i2c dev -b 3 -f 100000 -z 58 58  # 0x58 应答
```

新触摸交互固件烧录后继续执行：

```text
nsh> routine_mgr --selftest
nsh> routine_mgr
nsh> routine_mgr --english
nsh> routine_mgr --duration 30
```

预期启动信息包括：

```text
envmon: opened /dev/i2c3 at 100000 Hz
envmon: SHT40 LIVE
envmon: BH1750 LIVE address=0x23
envmon: SGP30 WARMUP
envmon: SGP30 LIVE
```

若某块模块未连接，只应看到该模块的状态变化，UI 和其他模块不应退出或持续刷相同错误。新的传感器详情弹框还应在不关闭的情况下从“实时”变为“过期”，插回后自动恢复；该项等待新固件真机验收。
