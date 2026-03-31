/**
 * @file WiFiManager.h
 * @brief WiFi 配网管理模块 - 节能版本(动态功率调整)
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "../config.h"

#ifndef DISABLE_SERIAL
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#endif

// 动态功率调整配置 (ESP32可用功率等级: 2/5/11/13/15/17/19 dBm)
const unsigned long POWER_ADJUST_INTERVAL = 30000; // 每30秒调整一次功率
const int8_t RSSI_THRESHOLD_HIGH = -50;   // RSSI > -50: 信号很强，用最低功率
const int8_t RSSI_THRESHOLD_GOOD = -65;   // -65 < RSSI <= -50: 信号良好，低功率
const int8_t RSSI_THRESHOLD_OK = -75;     // -75 < RSSI <= -65: 信号一般，中等功率
                                             // RSSI <= -75: 信号弱，提高功率

/**
 * @brief WiFi 状态回调函数类型
 */
using WiFiStatusCallback = std::function<void(bool connected)>;

/**
 * @brief WiFi 配网管理器类
 */
class WiFiManager {
public:
    /**
     * @brief 构造函数
     */
    WiFiManager()
        : ap_active_(false), wifi_connected_(false), ap_start_time_(0),
          last_power_adjust_time_(0), current_tx_power_(WIFI_POWER_11dBm),
          status_callback_(nullptr) {}

    /**
     * @brief 初始化 WiFi 管理器
     */
    void begin() {
        preferences_.begin("wifi-config", false);
        loadWiFiConfig();

        // 启用Wi-Fi省电模式 (Modem Sleep)
        WiFi.setSleep(WIFI_PS_MIN_MODEM);

        startAP();
        delay(1000);

        // 尝试连接WiFi(如果有保存的配置)
        if (saved_ssid_.length() > 0) {
            DEBUG_PRINTLN("发现已保存的WiFi配置，尝试连接...");
            connectToWiFi();
        } else {
            DEBUG_PRINTLN("未找到WiFi配置，请通过配网页面配置");
        }
    }

    /**
     * @brief 更新 WiFi 状态（应该在 loop 中调用）
     */
    void update() {
        unsigned long current_time = millis();

        checkWiFiStatus(current_time);
        checkAPTimeout(current_time);
        adjustPowerDynamically(current_time);
    }

    /**
     * @brief 处理 DNS 请求（用于强制门户）
     */
    void processDNS() {
        if (ap_active_) {
            dns_server_.processNextRequest();
        }
    }

    // ============== WiFi 配置操作 ==============

    /**
     * @brief 连接到指定 WiFi 网络
     * @param ssid WiFi 名称
     * @param password WiFi 密码
     * @return true 连接成功
     */
    bool connectWiFi(const String& ssid, const String& password) {
        DEBUG_PRINTLN("尝试连接到: " + ssid);

        saveWiFiConfig(ssid, password);

        WiFi.mode(WIFI_AP_STA);
        WiFi.begin(ssid.c_str(), password.c_str());

        // 等待连接(最多15秒)
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 30) {
            delay(500);
            attempts++;
        }

        return (WiFi.status() == WL_CONNECTED);
    }

    /**
     * @brief 删除保存的 WiFi 配置
     */
    void deleteWiFiConfig() {
        preferences_.remove("ssid");
        preferences_.remove("password");
        saved_ssid_ = "";
        saved_password_ = "";
        DEBUG_PRINTLN("WiFi配置已删除");

        WiFi.disconnect();
        wifi_connected_ = false;
        startAP();
    }

    /**
     * @brief 扫描 WiFi 网络
     * @return JSON 格式的扫描结果
     */
    String scanNetworks() {
        DEBUG_PRINTLN("=== 收到/scan请求 ===");
        unsigned long scanStartTime = millis();

        // 确保WiFi处于正确的模式
        WiFiMode_t currentMode = WiFi.getMode();
        DEBUG_PRINTLN("当前WiFi模式: " + String(currentMode));

        // 强制设置为AP+STA模式以确保扫描正常工作
        WiFi.mode(WIFI_MODE_APSTA);
        delay(300);

        // 彻底断开所有WiFi连接以避免干扰
        DEBUG_PRINTLN("断开所有WiFi连接以进行干净的扫描");
        WiFi.disconnect(true);
        WiFi.enableSTA(true);
        delay(300);

        // 确保没有正在进行的连接尝试
        if (WiFi.status() != WL_DISCONNECTED) {
            DEBUG_PRINTLN("等待WiFi完全断开...");
            unsigned long disconnectStart = millis();
            while (WiFi.status() != WL_DISCONNECTED && (millis() - disconnectStart) < 3000) {
                delay(100);
            }
        }

        // 额外的稳定性措施
        WiFi.scanDelete();
        delay(200);

        DEBUG_PRINTLN("开始扫描WiFi网络...");

        // 使用多轮重试机制提高稳定性
        int n = WIFI_SCAN_FAILED;
        const int MAX_RETRIES = 3;

        for (int retry = 0; retry < MAX_RETRIES && n == WIFI_SCAN_FAILED; retry++) {
            if (retry > 0) {
                DEBUG_PRINTLN("第" + String(retry + 1) + "次重试扫描...");
                WiFi.scanDelete();
                delay(500);
            }

            // 先清理之前的扫描结果
            WiFi.scanDelete();
            delay(100);

            // 使用同步扫描确保稳定性
            n = WiFi.scanNetworks(false, true);

            // 如果扫描失败，添加额外的延迟
            if (n == WIFI_SCAN_FAILED) {
                DEBUG_PRINTLN("扫描失败，等待后重试...");
                delay(1000);
            }
        }

        unsigned long startTime = millis();
        DEBUG_PRINTLN("扫描完成，找到 " + String(n) + " 个网络");

        // 检查扫描结果
        if (n == WIFI_SCAN_FAILED) {
            DEBUG_PRINTLN("WiFi扫描最终失败，经过" + String(MAX_RETRIES) + "次重试");
            WiFi.scanDelete();
            return "{\"networks\":[],\"error\":\"WiFi扫描失败，已重试" + String(MAX_RETRIES) + "次\"}";
        }

        // 打印扫描结果
        for (int i = 0; i < n; i++) {
            DEBUG_PRINT(String(i+1) + ": " + WiFi.SSID(i) + " (" + WiFi.RSSI(i) + " dBm)\n");
        }

        String json = "{\"networks\":[";
        for (int i = 0; i < n; i++) {
            if (i > 0) json += ",";
            json += "{";

            // 处理SSID中的特殊字符，进行JSON转义
            String ssid = WiFi.SSID(i);
            ssid.replace("\\", "\\\\");
            ssid.replace("\"", "\\\"");
            ssid.replace("\n", "\\n");
            ssid.replace("\r", "\\r");
            ssid.replace("\t", "\\t");

            json += "\"ssid\":\"" + ssid + "\",";
            json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
            json += "\"encryption\":" + String(WiFi.encryptionType(i));
            json += "}";
        }
        json += "]}";

        DEBUG_PRINTLN("返回JSON: " + json);

        // 打印扫描统计信息
        DEBUG_PRINTLN("=== 扫描统计 ===");
        DEBUG_PRINTLN("总耗时: " + String(millis() - scanStartTime) + " ms");
        DEBUG_PRINTLN("扫描耗时: " + String(millis() - startTime) + " ms");
        DEBUG_PRINTLN("找到网络数: " + String(n));

        // 清理扫描结果
        WiFi.scanDelete();

        return json;
    }

    // ============== 状态获取 ==============

    bool isConnected() const { return wifi_connected_; }
    bool isAPActive() const { return ap_active_; }
    String getSavedSSID() const { return saved_ssid_; }
    String getSavedPassword() const { return saved_password_; }
    String getLocalIP() const { return WiFi.localIP().toString(); }
    String getSSID() const { return WiFi.SSID(); }
    int32_t getRSSI() const { return WiFi.RSSI(); }

    /**
     * @brief 获取状态 JSON (包含发射功率信息)
     */
    String getStatusJSON() const {
        String json = "{";
        json += "\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
        if (WiFi.status() == WL_CONNECTED) {
            json += ",\"ssid\":\"" + WiFi.SSID() + "\"";
            json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
            json += ",\"rssi\":" + String(WiFi.RSSI());
            json += ",\"tx_power_dbm\":" + String(getPowerLevel_dBm(current_tx_power_));
        }
        json += "}";
        return json;
    }

    /**
     * @brief 设置状态回调
     */
    void setStatusCallback(WiFiStatusCallback callback) {
        status_callback_ = callback;
    }

    // ============== 数据上报辅助功能 ==============

    /**
     * @brief 为数据上报准备WiFi（唤醒、提升功率）
     * 应在上报前调用，上报完成后调用endDataReport()恢复
     */
    void beginDataReport() {
        if (!wifi_connected_) return;

        DEBUG_PRINTLN("[WiFi] 为数据上报唤醒WiFi...");

        // 1. 禁用WiFi休眠，确保连接活跃
        WiFi.setSleep(WIFI_PS_NONE);
        delay(50);

        // 2. 临时提升发射功率到最高，保证连接稳定性
        if (current_tx_power_ < WIFI_POWER_19dBm) {
            previous_tx_power_ = current_tx_power_;
            current_tx_power_ = WIFI_POWER_19dBm;
            WiFi.setTxPower(current_tx_power_);
            DEBUG_PRINTLN("[WiFi] 临时提升发射功率至 19dBm");
        }

        // 3. 发送空操作保持连接活跃
        WiFi.status();
        delay(50);
    }

    /**
     * @brief 数据上报完成后恢复WiFi节能设置
     */
    void endDataReport() {
        if (!wifi_connected_) return;

        // 恢复之前的功率设置
        if (previous_tx_power_ != WIFI_POWER_19dBm) {
            current_tx_power_ = previous_tx_power_;
            WiFi.setTxPower(current_tx_power_);
            DEBUG_PRINT("[WiFi] 恢复发射功率至 ");
            DEBUG_PRINT(getPowerLevel_dBm(current_tx_power_));
            DEBUG_PRINTLN(" dBm");
        }

        // 重新启用WiFi休眠
        WiFi.setSleep(WIFI_PS_MIN_MODEM);
        DEBUG_PRINTLN("[WiFi] 已恢复WiFi节能模式");
    }

    /**
     * @brief 强制检查并恢复WiFi连接
     * @return true WiFi状态正常
     */
    bool ensureWiFiHealth() {
        if (!wifi_connected_) {
            DEBUG_PRINTLN("[WiFi] WiFi未连接，尝试恢复...");
            return false;
        }

        // 检查WiFi状态是否真的正常
        wl_status_t status = WiFi.status();
        if (status != WL_CONNECTED) {
            DEBUG_PRINT("[WiFi] WiFi状态异常: ");
            DEBUG_PRINTLN(status);
            return false;
        }

        // 尝试获取IP确认连接有效
        IPAddress ip = WiFi.localIP();
        if (ip == INADDR_NONE || ip == IPAddress(0, 0, 0, 0)) {
            DEBUG_PRINTLN("[WiFi] IP地址无效");
            return false;
        }

        return true;
    }

    /**
     * @brief 重置WiFi连接（用于顽固故障）
     */
    void resetWiFiConnection() {
        DEBUG_PRINTLN("[WiFi] 执行WiFi连接重置...");

        if (saved_ssid_.length() > 0) {
            WiFi.disconnect(true);
            delay(500);

            WiFi.mode(WIFI_AP_STA);
            delay(100);

            WiFi.begin(saved_ssid_.c_str(), saved_password_.c_str());
            DEBUG_PRINTLN("[WiFi] 已重新发起连接");
        }
    }

private:
    wifi_power_t previous_tx_power_ = WIFI_POWER_11dBm;  // 保存之前的功率设置
    /**
     * @brief 根据WiFi信号强度动态调整发射功率
     */
    void adjustPowerDynamically(unsigned long current_time) {
        // 只在WiFi已连接且非AP模式时调整
        if (!wifi_connected_ || ap_active_) {
            return;
        }

        // 按固定间隔调整
        if (current_time - last_power_adjust_time_ < POWER_ADJUST_INTERVAL) {
            return;
        }

        last_power_adjust_time_ = current_time;

        int32_t rssi = WiFi.RSSI();
        wifi_power_t new_power;

        // 根据RSSI选择合适的发射功率
        if (rssi > RSSI_THRESHOLD_HIGH) {
            // 信号很强 (-50dBm以上)，用最低功率
            new_power = WIFI_POWER_5dBm;
        } else if (rssi > RSSI_THRESHOLD_GOOD) {
            // 信号良好 (-65 ~ -50dBm)，用低功率
            new_power = WIFI_POWER_11dBm;
        } else if (rssi > RSSI_THRESHOLD_OK) {
            // 信号一般 (-75 ~ -65dBm)，用中等功率
            new_power = WIFI_POWER_13dBm;
        } else {
            // 信号弱 (<= -75dBm)，提高功率保证连接
            new_power = WIFI_POWER_17dBm;
        }

        // 只有功率等级变化时才设置
        if (new_power != current_tx_power_) {
            current_tx_power_ = new_power;
            WiFi.setTxPower(current_tx_power_);

            DEBUG_PRINT("[WiFi] 动态调整发射功率: RSSI=");
            DEBUG_PRINT(rssi);
            DEBUG_PRINT(" dBm → 功率=");
            DEBUG_PRINT(getPowerLevel_dBm(current_tx_power_));
            DEBUG_PRINTLN(" dBm");
        }
    }

    /**
     * @brief 将功率等级转换为dBm数值
     */
    int8_t getPowerLevel_dBm(wifi_power_t power) const {
        switch (power) {
            case WIFI_POWER_2dBm: return 2;
            case WIFI_POWER_5dBm: return 5;
            case WIFI_POWER_11dBm: return 11;
            case WIFI_POWER_13dBm: return 13;
            case WIFI_POWER_15dBm: return 15;
            case WIFI_POWER_17dBm: return 17;
            case WIFI_POWER_19dBm: return 19;
            default: return 11;
        }
    }

    /**
     * @brief 启动 AP 热点
     */
    void startAP() {
        if (ap_active_) return;

        DEBUG_PRINTLN("启动AP热点...");
        WiFi.mode(WIFI_AP_STA);

        // 配置AP
        IPAddress local_IP(192, 168, 4, 1);
        IPAddress gateway(192, 168, 4, 1);
        IPAddress subnet(255, 255, 255, 0);
        WiFi.softAPConfig(local_IP, gateway, subnet);

        // AP模式用中等功率保证配网距离
        WiFi.setTxPower(WIFI_POWER_11dBm);
        current_tx_power_ = WIFI_POWER_11dBm;

        // 启动AP - 开放热点(无密码)
        bool result = WiFi.softAP(AP_SSID, NULL, AP_CHANNEL);
        DEBUG_PRINTLN("softAP启动结果: " + String(result ? "成功" : "失败"));

        // 启动DNS服务器用于强制门户
        dns_server_.start(53, "*", local_IP);
        DEBUG_PRINTLN("DNS服务器已启动，捕获所有域名请求");

        ap_active_ = true;
        ap_start_time_ = millis();

        DEBUG_PRINTLN("AP热点已启动");
        DEBUG_PRINTLN("SSID: " + String(AP_SSID));
        DEBUG_PRINTLN("密码: 无(开放热点)");
        DEBUG_PRINTLN("AP IP: " + WiFi.softAPIP().toString());
        DEBUG_PRINTLN("请连接到此热点进行配网");
        DEBUG_PRINTLN("访问: http://192.168.4.1 或 http://192.168.4.1/config");
    }

    /**
     * @brief 停止 AP 热点
     */
    void stopAP() {
        if (!ap_active_) return;

        DEBUG_PRINTLN("关闭AP热点...");
        dns_server_.stop();
        WiFi.softAPdisconnect(true);

        // 切换到仅STA模式以节能
        if (wifi_connected_) {
            WiFi.mode(WIFI_STA);
            // 切换模式后立即调整功率
            adjustPowerDynamically(millis());
        }

        ap_active_ = false;
        DEBUG_PRINTLN("AP热点已关闭");
    }

    /**
     * @brief 检查AP超时并关闭
     */
    void checkAPTimeout(unsigned long current_time) {
        if (ap_active_ && wifi_connected_) {
            // WiFi已连接，AP超时后关闭
            if (current_time - ap_start_time_ >= AP_TIMEOUT) {
                DEBUG_PRINTLN("AP超时，自动关闭以节能");
                stopAP();
            }
        }
    }

    /**
     * @brief 加载 WiFi 配置
     */
    void loadWiFiConfig() {
        saved_ssid_ = preferences_.getString("ssid", "");
        saved_password_ = preferences_.getString("password", "");

        if (saved_ssid_.length() > 0) {
            DEBUG_PRINTLN("加载WiFi配置:");
            DEBUG_PRINTLN("SSID: " + saved_ssid_);
        } else {
            DEBUG_PRINTLN("没有保存的WiFi配置");
        }
    }

    /**
     * @brief 保存 WiFi 配置
     */
    void saveWiFiConfig(const String& ssid, const String& password) {
        preferences_.putString("ssid", ssid);
        preferences_.putString("password", password);
        saved_ssid_ = ssid;
        saved_password_ = password;
        DEBUG_PRINTLN("WiFi配置已保存");
    }

    /**
     * @brief 连接到已保存的 WiFi
     */
    void connectToWiFi() {
        if (saved_ssid_.length() == 0) {
            DEBUG_PRINTLN("没有WiFi配置");
            return;
        }

        DEBUG_PRINTLN("连接到WiFi: " + saved_ssid_);
        WiFi.mode(WIFI_AP_STA);
        WiFi.begin(saved_ssid_.c_str(), saved_password_.c_str());
        DEBUG_PRINTLN("正在后台尝试连接...");
    }

    /**
     * @brief 检查 WiFi 状态
     */
    void checkWiFiStatus(unsigned long current_time) {
        wl_status_t wifi_status = WiFi.status();
        if (wifi_status == WL_CONNECTED) {
            if (!wifi_connected_) {
                wifi_connected_ = true;
                DEBUG_PRINTLN("WiFi已连接!");
                DEBUG_PRINT("IP地址: ");
                DEBUG_PRINTLN(WiFi.localIP());

                if (status_callback_) {
                    status_callback_(true);
                }

                DEBUG_PRINTLN("AP热点将在 " + String(AP_TIMEOUT / 1000) + " 秒后自动关闭");

                // 连接成功后立即调整功率
                adjustPowerDynamically(current_time);
            }
        } else {
            if (wifi_connected_) {
                wifi_connected_ = false;
                DEBUG_PRINTLN("WiFi连接断开");

                if (status_callback_) {
                    status_callback_(false);
                }

                // 重新开启AP以便重新配置
                if (!ap_active_) {
                    startAP();
                }

                // 尝试重新连接
                if (saved_ssid_.length() > 0) {
                    connectToWiFi();
                }
            }
        }
    }

    DNSServer dns_server_;
    Preferences preferences_;

    String saved_ssid_;
    String saved_password_;
    bool ap_active_;
    bool wifi_connected_;
    unsigned long ap_start_time_;
    unsigned long last_power_adjust_time_;
    wifi_power_t current_tx_power_;

    WiFiStatusCallback status_callback_;
};

#endif // WIFI_MANAGER_H
