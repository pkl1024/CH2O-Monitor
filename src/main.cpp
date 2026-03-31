/**
 * @file main.cpp
 * @brief ESP32 甲醛监测仪 - 节能版本
 *
 * 功能特性:
 * - ZE08-CH2O 甲醛传感器监测
 * - WiFi 配网功能
 * - Web 配置与监控界面
 * - 数据上报至服务器
 * - 低功耗节能模式
 */

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "sensors/ZE08CH2OSensor.h"
#include "wifi/WiFiManager.h"
#include "web/WebServer.h"
#include "data/TimeSync.h"
#include "data/DataReporter.h"

// ============== 全局对象 ==============
#ifndef DISABLE_SERIAL
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#endif

ZE08CH2OSensor ch2oSensor;
WiFiManager wifiManager;
SensorWebServer webServer(ch2oSensor, wifiManager);
TimeSync timeSync;
DataReporter dataReporter;

// ============== 全局状态 ==============
unsigned long last_wifi_check = 0;
unsigned long last_wifi_health_check = 0;
unsigned long last_millis = 0;

// WiFi健康检查间隔（5分钟）
const unsigned long WIFI_HEALTH_CHECK_INTERVAL = 300000;

// ============== 前置声明 ==============
void initPowerSaving();
void updateSensors();
void updateWiFi();
void updateDataReporting();
void updateWebServer();

/**
 * @brief 初始化节能模式
 */
void initPowerSaving() {
    // 降低CPU频率到80MHz
    setCpuFrequencyMhz(CPU_FREQ_MHZ);

    // 关闭板载LED (GPIO2)
#ifdef DISABLE_LED
    pinMode(2, OUTPUT);
    digitalWrite(2, LOW);
#endif

    // 启用WiFi Modem Sleep模式
    WiFi.setSleep(WIFI_PS_MIN_MODEM);
}

/**
 * @brief 初始化函数 - 启动时运行一次
 */
void setup() {
    // 1. 初始化节能模式（最先执行）
    initPowerSaving();

    // 2. 初始化串口（如果启用）
#ifndef DISABLE_SERIAL
    Serial.begin(SERIAL_BAUDRATE);
    delay(100);
    DEBUG_PRINTLN("\n\n========== ESP32 甲醛监测仪启动（节能版） ==========");
#endif

    // 3. 初始化甲醛传感器
    ch2oSensor.begin();

    // 4. 初始化WiFi管理器
    wifiManager.begin();

    // 5. 初始化Web服务器
    dataReporter.setWiFiManager(&wifiManager);
    webServer.setDataReporter(dataReporter);
    webServer.setTimeSync(timeSync);
    webServer.begin();

    // 6. 初始化时间同步
    timeSync.begin();

    // 7. 初始化数据上报
    dataReporter.begin();

#ifndef DISABLE_SERIAL
    DEBUG_PRINTLN("初始化完成");
#endif
}

/**
 * @brief 主循环函数 - 持续运行
 */
void loop() {
    unsigned long currentMillis = millis();

    // 1. 更新传感器数据
    updateSensors();

    // 2. 更新WiFi连接状态
    updateWiFi();

    // 3. 更新时间同步
    timeSync.update();

    // 4. 更新数据上报
    updateDataReporting();

    // 5. 更新Web服务器
    updateWebServer();

    // 6. 节能延迟
    delay(MAIN_LOOP_DELAY);
}

/**
 * @brief 更新传感器数据
 */
void updateSensors() {
    ch2oSensor.update();
}

/**
 * @brief 更新WiFi连接
 */
void updateWiFi() {
    static unsigned long last_wifi_check = 0;
    unsigned long currentMillis = millis();

    wifiManager.update();
    wifiManager.processDNS();

    // 定期检查WiFi连接状态
    if (currentMillis - last_wifi_check >= WIFI_CHECK_INTERVAL) {
        last_wifi_check = currentMillis;

        if (wifiManager.isConnected() && !timeSync.isSynced()) {
            timeSync.syncNow();
        }
    }

    // 定期进行WiFi健康检查
    if (currentMillis - last_wifi_health_check >= WIFI_HEALTH_CHECK_INTERVAL) {
        last_wifi_health_check = currentMillis;

        if (wifiManager.isConnected()) {
            DEBUG_PRINTLN("[WiFi] 执行定期健康检查...");
            if (!wifiManager.ensureWiFiHealth()) {
                DEBUG_PRINTLN("[WiFi] 健康检查发现问题，尝试重置连接...");
                wifiManager.resetWiFiConnection();
            } else {
                DEBUG_PRINTLN("[WiFi] 健康检查通过");
            }
        }
    }
}

/**
 * @brief 更新数据上报
 */
void updateDataReporting() {
    int wifi_rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
    dataReporter.update(ch2oSensor, wifi_rssi);
}

/**
 * @brief 更新Web服务器
 */
void updateWebServer() {
    webServer.update();
}
