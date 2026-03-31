/**
 * @file DataReporter.h
 * @brief 数据上报模块 - 负责将甲醛传感器数据上报到Java服务器（节能版）
 *
 * 功能特性:
 * - 高频率采样数据缓存
 * - 批量数据上报
 * - 失败自动重试
 * - 数据确认后才丢弃
 * - WiFi状态健康检查和自动恢复
 */

#ifndef DATA_REPORTER_H
#define DATA_REPORTER_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include "../config.h"
#include "../sensors/ZE08CH2OSensor.h"

// 前置声明WiFiManager
class WiFiManager;

#ifndef DISABLE_SERIAL
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#endif

/**
 * @brief 单个传感器样本数据结构（节能版 - 仅甲醛）
 */
struct SensorSample {
    unsigned long timestamp_ms;      // 采样时间戳(ms)
    char timestamp_str[24];          // 格式化时间字符串

    // 甲醛数据
    bool ch2o_valid;
    float ch2o_ppb;
    float ch2o_ppm;
    float ch2o_mgm3;

    // WiFi信号
    int wifi_rssi;

    SensorSample()
        : timestamp_ms(0), ch2o_valid(false), ch2o_ppb(0), ch2o_ppm(0), ch2o_mgm3(0), wifi_rssi(0) {
        memset(timestamp_str, 0, sizeof(timestamp_str));
    }
};

/**
 * @brief 数据上报类
 */
class DataReporter {
public:
    /**
     * @brief 构造函数
     */
    DataReporter()
        : preferences_(), server_url_(""),
          last_sample_time_(0), last_report_attempt_(0),
          report_success_count_(0), report_fail_count_(0),
          sample_head_(0), sample_tail_(0), sample_count_(0),
          is_uploading_(false), wifi_manager_(nullptr) {}

    /**
     * @brief 设置WiFiManager引用（用于WiFi唤醒和健康检查）
     */
    void setWiFiManager(WiFiManager* wifi_manager) {
        wifi_manager_ = wifi_manager;
    }

    /**
     * @brief 初始化
     */
    void begin() {
        preferences_.begin("data_reporter", false);
        loadConfig();
        DEBUG_PRINTLN("数据上报模块初始化完成");
    }

    /**
     * @brief 设置完整上报URL
     */
    void setServerUrl(const String& url) {
        server_url_ = url;
        saveConfig();
    }

    /**
     * @brief 获取上报URL
     */
    String getServerUrl() const { return server_url_; }

    /**
     * @brief 检查是否已配置
     */
    bool isConfigured() const { return server_url_.length() > 0; }

    /**
     * @brief 获取缓存样本数量
     */
    int getSampleCount() const { return sample_count_; }

    /**
     * @brief 获取最大缓存容量
     */
    int getMaxSamples() const { return MAX_CACHED_SAMPLES; }

    /**
     * @brief 更新上报状态（应在loop中调用）
     */
    void update(ZE08CH2OSensor& ch2o, int wifi_rssi) {
        unsigned long current_time = millis();

        // 1. 采集样本数据
        if (current_time - last_sample_time_ >= CACHE_SAMPLE_INTERVAL) {
            last_sample_time_ = current_time;
            collectSample(ch2o, wifi_rssi);
        }

        // 2. 尝试上报数据
        if (sample_count_ > 0 && !is_uploading_) {
            bool should_report = false;

            // 判断是否需要上报
            if (current_time - last_report_attempt_ >= REPORT_INTERVAL) {
                should_report = true;
            } else if (sample_count_ >= MAX_CACHED_SAMPLES - 10) {
                // 缓存快满了，提前上报
                DEBUG_PRINTLN("[上报] 缓存快满，提前上报");
                should_report = true;
            } else if (report_fail_count_ > 0 && current_time - last_report_attempt_ >= RETRY_INTERVAL) {
                // 之前失败了，按重试间隔尝试
                DEBUG_PRINTLN("[上报] 重试上次失败的上报");
                should_report = true;
            }

            if (should_report && WiFi.status() == WL_CONNECTED && isConfigured()) {
                last_report_attempt_ = current_time;
                reportBatch();
            }
        }
    }

    /**
     * @brief 立即上报所有缓存数据（用于测试）
     */
    bool reportNow() {
        last_error_message_ = "";

        if (!isConfigured()) {
            last_error_message_ = "未配置上报地址";
            return false;
        }
        if (WiFi.status() != WL_CONNECTED) {
            last_error_message_ = "WiFi未连接";
            return false;
        }
        if (sample_count_ == 0) {
            last_error_message_ = "没有缓存数据可上报";
            return false;
        }
        if (is_uploading_) {
            last_error_message_ = "正在上传中，请稍后再试";
            return false;
        }
        return reportBatch();
    }

    /**
     * @brief 获取最后一次错误信息
     */
    String getLastErrorMessage() const { return last_error_message_; }

    /**
     * @brief 获取上报状态JSON
     */
    String getStatusJSON() const {
        String json = "{";
        json += "\"configured\":" + String(isConfigured() ? "true" : "false") + ",";
        if (isConfigured()) {
            json += "\"server_url\":\"" + server_url_ + "\",";
        }
        json += "\"success_count\":" + String(report_success_count_) + ",";
        json += "\"fail_count\":" + String(report_fail_count_) + ",";
        json += "\"cached_samples\":" + String(sample_count_) + ",";
        json += "\"max_samples\":" + String(MAX_CACHED_SAMPLES) + ",";
        json += "\"uploading\":" + String(is_uploading_ ? "true" : "false") + ",";
        json += "\"last_error\":\"" + last_error_message_ + "\"";
        json += "}";
        return json;
    }

private:
    Preferences preferences_;
    String server_url_;
    unsigned long last_sample_time_;
    unsigned long last_report_attempt_;
    unsigned long report_success_count_;
    unsigned long report_fail_count_;

    // 环形缓冲区
    SensorSample samples_[MAX_CACHED_SAMPLES];
    int sample_head_;      // 写入位置
    int sample_tail_;      // 读取位置
    int sample_count_;     // 当前样本数
    bool is_uploading_;    // 是否正在上传
    String last_error_message_;  // 最后一次错误信息

    WiFiManager* wifi_manager_;  // WiFi管理器引用

    /**
     * @brief 保存配置到Preferences
     */
    void saveConfig() {
        preferences_.putString("url", server_url_);
        DEBUG_PRINTLN("[上报] 配置已保存");
    }

    /**
     * @brief 从Preferences加载配置
     */
    void loadConfig() {
        server_url_ = preferences_.getString("url", "");
        if (isConfigured()) {
            DEBUG_PRINT("[上报] 已加载配置: ");
            DEBUG_PRINTLN(server_url_);
        }
    }

    /**
     * @brief 获取格式化时间字符串
     */
    void getFormattedTime(char* buffer, size_t buffer_size) {
        time_t now;
        struct tm timeinfo;
        if (!getLocalTime(&timeinfo)) {
            strncpy(buffer, "1970-01-01 00:00:00", buffer_size);
            return;
        }
        strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", &timeinfo);
    }

    /**
     * @brief 采集一个样本并加入缓存
     */
    void collectSample(ZE08CH2OSensor& ch2o, int wifi_rssi) {
        const CH2OData& ch2o_data = ch2o.getData();

        SensorSample& sample = samples_[sample_head_];
        sample.timestamp_ms = millis();
        getFormattedTime(sample.timestamp_str, sizeof(sample.timestamp_str));

        sample.ch2o_valid = ch2o_data.valid;
        sample.ch2o_ppb = ch2o_data.concentration_ppb;
        sample.ch2o_ppm = ch2o_data.concentration_ppm;
        sample.ch2o_mgm3 = ch2o_data.concentration_mg;

        sample.wifi_rssi = wifi_rssi;

        // 更新环形缓冲区指针
        sample_head_ = (sample_head_ + 1) % MAX_CACHED_SAMPLES;

        if (sample_count_ < MAX_CACHED_SAMPLES) {
            sample_count_++;
        } else {
            // 缓冲区已满，覆盖最旧的数据
            sample_tail_ = (sample_tail_ + 1) % MAX_CACHED_SAMPLES;
            DEBUG_PRINTLN("[上报] 缓存已满，覆盖旧数据");
        }

        DEBUG_PRINT("[上报] 已采集样本 #");
        DEBUG_PRINT(sample_count_);
        DEBUG_PRINT("/");
        DEBUG_PRINTLN(MAX_CACHED_SAMPLES);
    }

    /**
     * @brief 生成批量上报JSON数据
     */
    String getBatchJSON(int count) {
        String json = "{";
        json += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
        json += "\"batch_size\":" + String(count) + ",";
        json += "\"samples\":[";

        int idx = sample_tail_;
        for (int i = 0; i < count; i++) {
            if (i > 0) json += ",";

            const SensorSample& sample = samples_[idx];

            json += "{";
            json += "\"timestamp\":\"" + String(sample.timestamp_str) + "\",";
            json += "\"uptime_ms\":" + String(sample.timestamp_ms) + ",";

            json += "\"ch2o\":{";
            json += "\"valid\":" + String(sample.ch2o_valid ? "true" : "false") + ",";
            json += "\"concentration_ppb\":" + String(sample.ch2o_ppb, 2) + ",";
            json += "\"concentration_ppm\":" + String(sample.ch2o_ppm, 4) + ",";
            json += "\"concentration_mgm3\":" + String(sample.ch2o_mgm3, 4);
            json += "},";

            json += "\"wifi_rssi\":" + String(sample.wifi_rssi);
            json += "}";

            idx = (idx + 1) % MAX_CACHED_SAMPLES;
        }

        json += "]}";
        return json;
    }

    /**
     * @brief 批量上报数据
     */
    bool reportBatch() {
        if (sample_count_ == 0) {
            return true;
        }

        last_error_message_ = "";
        is_uploading_ = true;
        int upload_count = sample_count_;

        DEBUG_PRINT("[上报] 开始批量上报 ");
        DEBUG_PRINT(upload_count);
        DEBUG_PRINTLN(" 个样本...");

        // 生成JSON数据（提前生成，避免在连接后占用时间）
        String payload = getBatchJSON(upload_count);
        DEBUG_PRINT("[上报] 数据大小: ");
        DEBUG_PRINT(payload.length());
        DEBUG_PRINTLN(" 字节");

        bool success = false;
        const int MAX_RETRIES = 3;  // 增加重试次数到3次
        bool wifi_prepared = false;

        for (int retry = 0; retry <= MAX_RETRIES && !success; retry++) {
            if (retry > 0) {
                DEBUG_PRINT("[上报] 即时重试 ");
                DEBUG_PRINT(retry);
                DEBUG_PRINT("/");
                DEBUG_PRINTLN(MAX_RETRIES);

                // 对于第二次及以后的重试，尝试WiFi恢复
                if (retry >= 2 && wifi_manager_ != nullptr) {
                    DEBUG_PRINTLN("[上报] 尝试重置WiFi连接...");
                    wifi_manager_->resetWiFiConnection();
                    delay(3000);  // 等待WiFi重新连接
                } else {
                    delay(800);  // 稍微延长重试间隔
                }
            }

            // 确保WiFi处于活动状态
            if (WiFi.status() != WL_CONNECTED) {
                DEBUG_PRINTLN("[上报] WiFi未连接，跳过本次上报");
                last_error_message_ = "WiFi未连接";
                break;
            }

            // 准备WiFi（唤醒+提升功率）
            if (!wifi_prepared && wifi_manager_ != nullptr) {
                wifi_manager_->beginDataReport();
                wifi_prepared = true;
            }

            // 额外的WiFi健康检查
            if (wifi_manager_ != nullptr && !wifi_manager_->ensureWiFiHealth()) {
                DEBUG_PRINTLN("[上报] WiFi健康检查失败，跳过本次上报");
                last_error_message_ = "WiFi健康检查失败";
                break;
            }

            HTTPClient http;
            WiFiClientSecure* secure_client = nullptr;
            WiFiClient* plain_client = nullptr;

            bool is_https = server_url_.startsWith("https://");
            if (is_https) {
                secure_client = new WiFiClientSecure();
                secure_client->setInsecure();
                secure_client->setTimeout(15);  // 稍微延长超时
                http.begin(*secure_client, server_url_);
                DEBUG_PRINTLN("[上报] 使用HTTPS连接");
            } else {
                plain_client = new WiFiClient();
                plain_client->setTimeout(15);  // 稍微延长超时
                http.begin(*plain_client, server_url_);
                DEBUG_PRINTLN("[上报] 使用HTTP连接");
            }

            http.addHeader("Content-Type", "application/json");
            http.setTimeout(35000);  // 批量上传超时35秒
            http.setConnectTimeout(12000);  // 连接超时12秒

            int http_code = http.POST(payload);

            if (http_code == HTTP_CODE_OK) {
                String response = http.getString();
                DEBUG_PRINT("[上报] 响应: ");
                DEBUG_PRINTLN(response);

                // 解析响应，检查是否有错误
                if (response.indexOf("\"success\":false") >= 0) {
                    // 提取错误消息
                    int msgStart = response.indexOf("\"message\":\"");
                    if (msgStart >= 0) {
                        msgStart += 11;
                        int msgEnd = response.indexOf("\"", msgStart);
                        if (msgEnd > msgStart) {
                            last_error_message_ = response.substring(msgStart, msgEnd);
                        }
                    }
                    DEBUG_PRINT("[上报] 服务器拒绝: ");
                    DEBUG_PRINTLN(last_error_message_);
                    report_fail_count_++;
                    break;  // 服务器明确拒绝，不重试
                }

                // 成功：移除已上报的样本
                sample_tail_ = (sample_tail_ + upload_count) % MAX_CACHED_SAMPLES;
                sample_count_ -= upload_count;

                report_success_count_++;
                report_fail_count_ = 0; // 清除失败计数
                success = true;
            } else if (http_code == 403) {
                // 设备未注册
                String response = http.getString();
                DEBUG_PRINT("[上报] 设备未注册! 响应: ");
                DEBUG_PRINTLN(response);

                // 提取错误消息
                int msgStart = response.indexOf("\"message\":\"");
                if (msgStart >= 0) {
                    msgStart += 11;
                    int msgEnd = response.indexOf("\"", msgStart);
                    if (msgEnd > msgStart) {
                        last_error_message_ = response.substring(msgStart, msgEnd);
                    }
                } else {
                    last_error_message_ = "此设备未记录，请联系管理员";
                }
                DEBUG_PRINTLN(last_error_message_);
                report_fail_count_++;
                break;  // 设备未注册，不重试
            } else {
                DEBUG_PRINT("[上报] 失败! HTTP错误码: ");
                DEBUG_PRINT(http_code);
                DEBUG_PRINT(" - ");
                if (http_code < 0) {
                    printHTTPClientError(http_code);
                    last_error_message_ = getHTTPClientErrorMessage(http_code);
                } else {
                    DEBUG_PRINTLN("HTTP状态码");
                    last_error_message_ = "HTTP错误 " + String(http_code);
                }

                // 只有连接拒绝等瞬时错误才即时重试
                if (http_code != HTTPC_ERROR_CONNECTION_REFUSED &&
                    http_code != HTTPC_ERROR_CONNECTION_LOST &&
                    http_code != HTTPC_ERROR_NOT_CONNECTED &&
                    http_code != HTTPC_ERROR_READ_TIMEOUT) {
                    report_fail_count_++;
                    break;  // 非瞬时错误，不即时重试
                }
            }

            http.end();
            if (secure_client != nullptr) delete secure_client;
            if (plain_client != nullptr) delete plain_client;
        }

        // 恢复WiFi节能设置
        if (wifi_prepared && wifi_manager_ != nullptr) {
            wifi_manager_->endDataReport();
        }

        if (!success) {
            report_fail_count_++;
        }

        is_uploading_ = false;
        return success;
    }

    /**
     * @brief 获取HTTPClient错误信息字符串
     */
    String getHTTPClientErrorMessage(int error_code) {
        switch (error_code) {
            case HTTPC_ERROR_CONNECTION_REFUSED:
                return "连接被拒绝";
            case HTTPC_ERROR_SEND_HEADER_FAILED:
                return "发送头失败";
            case HTTPC_ERROR_SEND_PAYLOAD_FAILED:
                return "发送数据失败";
            case HTTPC_ERROR_NOT_CONNECTED:
                return "未连接";
            case HTTPC_ERROR_CONNECTION_LOST:
                return "连接丢失";
            case HTTPC_ERROR_NO_STREAM:
                return "无数据流";
            case HTTPC_ERROR_NO_HTTP_SERVER:
                return "无HTTP服务器";
            case HTTPC_ERROR_TOO_LESS_RAM:
                return "内存不足";
            case HTTPC_ERROR_ENCODING:
                return "编码错误";
            case HTTPC_ERROR_STREAM_WRITE:
                return "流写入错误";
            case HTTPC_ERROR_READ_TIMEOUT:
                return "读取超时";
            default:
                return "未知错误 (" + String(error_code) + ")";
        }
    }

    /**
     * @brief 打印HTTPClient错误信息
     */
    void printHTTPClientError(int error_code) {
        DEBUG_PRINTLN(getHTTPClientErrorMessage(error_code));
    }
};

#endif // DATA_REPORTER_H
