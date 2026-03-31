# CLAUDE.md

本文件为 Claude Code (claude.ai/code) 在此仓库中工作时提供指导。

## 项目概述

**ESP32 甲醛监测仪 (CH2O-Monitor)** - 基于 ESP32 和 ZE08-CH2O 传感器的甲醛浓度监测系统，支持 WiFi 配网、Web 监控和数据上报。

## 技术栈

- **硬件：** ESP32 微控制器、ZE08-CH2O 甲醛传感器
- **框架：** Arduino for ESP32
- **构建系统：** PlatformIO
- **库：** WiFi.h、WebServer.h、DNSServer.h、Preferences.h、HardwareSerial.h（均为内置）

## 常用命令

```bash
# 编译
pio run                          # 编译项目

# 上传
pio run --target upload          # 上传固件

# 监控
pio device monitor               # 串口监控（115200 波特率）
pio run --target upload && pio device monitor  # 上传并监控

# 清理
pio run --target clean           # 清理构建产物
```

## 关键架构

**引脚配置：**
- ZE08-CH2O TX → GPIO 16 (RX2)
- ZE08-CH2O RX → GPIO 17 (TX2)
- ZE08-CH2O VCC → 5V
- ZE08-CH2O GND → GND

**WiFi 配网：**
- AP 热点："ESP32_CatShit"（密码：12345678）
- 强制门户配网
- 凭证保存在 Preferences 中（持久化存储）

**HTTP 端点：**
- `GET /` - 仪表板页面
- `GET /sensor` - 传感器数据（JSON）
- `GET /data` - 传感器数据（纯文本）
- `GET /config` - WiFi 配置页面
- `GET /scan` - 扫描 WiFi 网络（JSON）
- `GET /status` - WiFi 状态（JSON）

## 文件结构

```
src/
├── main.cpp              # 主程序入口
├── config.h              # 配置参数
├── sensors/
│   └── ZE08CH2OSensor.h # ZE08-CH2O 传感器驱动
├── wifi/
│   └── WiFiManager.h     # WiFi 配网管理
├── web/
│   └── WebServer.h       # Web 服务器
└── data/
    ├── DataReporter.h    # 数据上报模块
    └── TimeSync.h        # NTP 时间同步
```

## 重要配置

**src/config.h：**
- `ZE08_BAUDRATE = 9600` - ZE08-CH2O 串口波特率
- `REPORT_INTERVAL = 1800000` - 数据上报间隔（30分钟）
- `CPU_FREQ_MHZ = 80` - CPU 频率（节能模式）

**甲醛健康阈值（GB/T 18883-2002）：**
- 正常：≤0.08 mg/m³
- 注意：0.08-0.1 mg/m³
- 警告：0.1-0.3 mg/m³
- 警报：>0.3 mg/m³

**传感器预热时间：**
- ZE08-CH2O：3-5 分钟
