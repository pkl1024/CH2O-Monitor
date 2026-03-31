/**
 * @file WebServer.h
 * @brief Web 服务器模块 - 节能版本
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include "../config.h"
#include "../sensors/ZE08CH2OSensor.h"
#include "../wifi/WiFiManager.h"
#include "../data/DataReporter.h"
#include "../data/TimeSync.h"

#ifndef DISABLE_SERIAL
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#endif

/**
 * @brief Web 服务器类
 */
class SensorWebServer {
public:
    /**
     * @brief 构造函数
     * @param ch2o ZE08-CH2O 传感器引用
     * @param wifi WiFi 管理器引用
     */
    SensorWebServer(ZE08CH2OSensor& ch2o, WiFiManager& wifi)
        : server_(WEB_SERVER_PORT), ch2o_(ch2o), wifi_(wifi), reporter_(nullptr), timeSync_(nullptr) {}

    /**
     * @brief 设置数据上报器引用
     */
    void setDataReporter(DataReporter& reporter) { reporter_ = &reporter; }

    /**
     * @brief 设置时间同步器引用
     */
    void setTimeSync(TimeSync& timeSync) { timeSync_ = &timeSync; }

    /**
     * @brief 初始化 Web 服务器
     */
    void begin() {
        setupRoutes();
        server_.begin();
#ifndef DISABLE_SERIAL
        DEBUG_PRINTLN("HTTP服务器已启动");
        DEBUG_PRINTLN("注册路由: /, /config, /scan, /connect, /status, /manage, /delete_wifi, /data, /sensor, /report_config, /report_status, /report_now");
#endif
    }

    /**
     * @brief 处理客户端请求（应该在 loop 中调用）
     */
    void update() {
        server_.handleClient();
    }

private:
    /**
     * @brief 设置路由
     */
    void setupRoutes() {
        server_.on("/", std::bind(&SensorWebServer::handleRoot, this));
        server_.on("/config", std::bind(&SensorWebServer::handleConfig, this));
        server_.on("/scan", std::bind(&SensorWebServer::handleScan, this));
        server_.on("/connect", std::bind(&SensorWebServer::handleConnect, this));
        server_.on("/status", std::bind(&SensorWebServer::handleStatus, this));
        server_.on("/manage", std::bind(&SensorWebServer::handleManage, this));
        server_.on("/delete_wifi", std::bind(&SensorWebServer::handleDeleteWiFi, this));
        server_.on("/data", std::bind(&SensorWebServer::handleData, this));
        server_.on("/sensor", std::bind(&SensorWebServer::handleSensor, this));
        server_.on("/report_config", std::bind(&SensorWebServer::handleReportConfig, this));
        server_.on("/report_status", std::bind(&SensorWebServer::handleReportStatus, this));
        server_.on("/report_now", std::bind(&SensorWebServer::handleReportNow, this));
        server_.onNotFound(std::bind(&SensorWebServer::handleNotFound, this));
    }

    /**
     * @brief 获取时间戳
     */
    String getTimeStamp() {
        unsigned long current_time = millis();
        unsigned long seconds = current_time / 1000;
        unsigned long minutes = seconds / 60;
        unsigned long hours = minutes / 60;
        seconds %= 60;
        minutes %= 60;

        char buffer[12];
        sprintf(buffer, "%02lu:%02lu:%02lu", hours, minutes, seconds);
        return String(buffer);
    }

    /**
     * @brief 获取组合的 JSON 数据
     */
    String getCombinedJSON() {
        String json = "{";
        json += "\"ch2o\":" + ch2o_.getJSON() + ",";
        json += "\"timestamp\":\"" + getTimeStamp() + "\"";
        json += "}";
        return json;
    }

    // ============== HTTP 处理函数 ==============

    void handleRoot() {
        server_.send(200, "text/html; charset=utf-8", getDashboardHTML());
    }

    void handleData() {
        String response = "ESP32 ZE08-CH2O 甲醛传感器数据（节能版）\n";
        response += "======================\n";
        response += "时间: " + getTimeStamp() + "\n";
        response += ch2o_.getText();

        sendCORSHeaders();
        server_.send(200, "text/plain; charset=utf-8", response);
    }

    void handleSensor() {
        sendCORSHeaders();
        server_.send(200, "application/json; charset=utf-8", getCombinedJSON());
    }

    void handleReportConfig() {
        if (!reporter_) {
            server_.send(500, "application/json", "{\"success\":false,\"message\":\"未初始化\"}");
            return;
        }

        if (server_.method() == HTTP_POST) {
            if (server_.hasArg("url")) {
                String url = server_.arg("url");
                reporter_->setServerUrl(url);
                String json = "{\"success\":true,\"message\":\"配置已保存\",\"url\":\"" + url + "\"}";
                server_.send(200, "application/json", json);
#ifndef DISABLE_SERIAL
                DEBUG_PRINT("[上报] 服务器已配置: ");
                DEBUG_PRINTLN(url);
#endif
            } else {
                server_.send(400, "application/json", "{\"success\":false,\"message\":\"缺少参数\"}");
            }
        } else {
            String json = "{";
            json += "\"configured\":" + String(reporter_->isConfigured() ? "true" : "false");
            if (reporter_->isConfigured()) {
                json += ",\"url\":\"" + reporter_->getServerUrl() + "\"";
            }
            json += ",\"report_interval\":" + String(REPORT_INTERVAL);
            json += "}";
            sendCORSHeaders();
            server_.send(200, "application/json", json);
        }
    }

    void handleReportStatus() {
        if (!reporter_) {
            server_.send(500, "application/json", "{\"success\":false,\"message\":\"未初始化\"}");
            return;
        }
        sendCORSHeaders();
        server_.send(200, "application/json", reporter_->getStatusJSON());
    }

    void handleReportNow() {
        if (!reporter_) {
            server_.send(500, "application/json", "{\"success\":false,\"message\":\"数据上报模块未初始化\"}");
            return;
        }
        bool success = reporter_->reportNow();
        String json = "{\"success\":" + String(success ? "true" : "false");
        if (!success) {
            json += ",\"message\":\"" + reporter_->getLastErrorMessage() + "\"";
        }
        json += "}";
        server_.send(200, "application/json", json);
    }

    void handleConfig() {
        server_.send(200, "text/html; charset=utf-8", getConfigHTML());
    }

    void handleScan() {
        String json = wifi_.scanNetworks();
        sendCORSHeaders();
        server_.sendHeader("Cache-Control", "no-cache");
        server_.send(200, "application/json; charset=utf-8", json);
    }

    void handleConnect() {
        if (!server_.hasArg("ssid")) {
            server_.send(400, "application/json", "{\"success\":false,\"message\":\"缺少SSID参数\"}");
            return;
        }

        String ssid = server_.arg("ssid");
        String password = server_.arg("password");

        bool success = wifi_.connectWiFi(ssid, password);

        if (success) {
            String json = "{\"success\":true,\"ip\":\"" + wifi_.getLocalIP() + "\"}";
            server_.send(200, "application/json", json);
#ifndef DISABLE_SERIAL
            DEBUG_PRINTLN("WiFi连接成功!");
            DEBUG_PRINTLN("IP地址: " + wifi_.getLocalIP());
#endif
        } else {
            server_.send(200, "application/json", "{\"success\":false,\"message\":\"连接超时，请检查密码\"}");
#ifndef DISABLE_SERIAL
            DEBUG_PRINTLN("WiFi连接失败");
#endif
        }
    }

    void handleStatus() {
        server_.send(200, "application/json", wifi_.getStatusJSON());
    }

    void handleManage() {
        server_.send(200, "text/html; charset=utf-8", getManageHTML());
    }

    void handleDeleteWiFi() {
        if (wifi_.getSavedSSID().length() == 0) {
            server_.send(200, "application/json", "{\"success\":false,\"message\":\"没有保存的WiFi配置\"}");
            return;
        }

        String old_ssid = wifi_.getSavedSSID();
        wifi_.deleteWiFiConfig();

        String json = "{\"success\":true,\"message\":\"WiFi配置 '" + old_ssid + "' 已删除\"}";
        server_.send(200, "application/json", json);
#ifndef DISABLE_SERIAL
        DEBUG_PRINTLN("WiFi配置删除成功");
#endif
    }

    void handleNotFound() {
        String uri = server_.uri();
        String message = "404: Not Found\n\n";
        message += "URI: " + uri + "\n";
        message += "Method: " + String((server_.method() == HTTP_GET) ? "GET" : "POST") + "\n";
        server_.send(404, "text/plain", message);
#ifndef DISABLE_SERIAL
        DEBUG_PRINTLN("404: " + uri);
#endif
    }

    void sendCORSHeaders() {
        server_.sendHeader("Access-Control-Allow-Origin", "*");
        server_.sendHeader("Access-Control-Allow-Methods", "GET, POST");
        server_.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    }

    // ============== HTML 页面生成 ==============

    String getDashboardHTML() {
        const CH2OData& ch2o_data = ch2o_.getData();

        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
        html += "<title>🐱 ESP32 甲醛监测仪（节能版）</title>";
        html += "<style>";
        html += "body{font-family:'Segoe UI',Arial,sans-serif;margin:0;padding:20px;background:#f0f2f5;}";
        html += ".container{max-width:1000px;margin:0 auto;}";
        html += ".header{background:linear-gradient(135deg,#4CAF50,#45a049);color:white;padding:25px;border-radius:15px;margin-bottom:25px;box-shadow:0 4px 15px rgba(0,0,0,0.1);}";
        html += "h1{margin:0;font-size:2.2em;}";
        html += ".subtitle{font-size:1.1em;opacity:0.9;margin-top:8px;}";
        html += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:20px;margin-bottom:25px;}";
        html += ".card{background:white;border-radius:12px;padding:25px;box-shadow:0 2px 10px rgba(0,0,0,0.08);transition:transform 0.3s,box-shadow 0.3s;}";
        html += ".card:hover{transform:translateY(-5px);box-shadow:0 5px 20px rgba(0,0,0,0.15);}";
        html += ".card h2{margin-top:0;color:#333;border-bottom:2px solid #4CAF50;padding-bottom:12px;margin-bottom:20px;}";
        html += ".status-badge{display:inline-block;padding:6px 12px;border-radius:20px;font-size:0.9em;font-weight:bold;}";
        html += ".status-connected{background:#d4edda;color:#155724;}";
        html += ".btn{display:inline-block;padding:12px 20px;margin:5px;background:#4CAF50;color:white;text-decoration:none;border-radius:6px;font-weight:bold;transition:background 0.3s;}";
        html += ".btn:hover{background:#45a049;transform:scale(1.05);}";
        html += ".btn-danger{background:#f44336;}";
        html += ".btn-danger:hover{background:#d32f2f;}";
        html += ".btn-warning{background:#ff9800;}";
        html += ".btn-warning:hover{background:#f57c00;}";
        html += ".btn-secondary{background:#2196F3;}";
        html += ".btn-secondary:hover{background:#0b7dda;}";
        html += ".data-item{margin:12px 0;display:flex;justify-content:space-between;align-items:center;padding:10px;background:#f8f9fa;border-radius:6px;}";
        html += ".data-label{font-weight:bold;color:#555;}";
        html += ".data-value{font-family:monospace;font-size:1.1em;color:#333;}";
        html += ".actions{text-align:center;padding:20px;background:#e3f2fd;border-radius:10px;margin-top:20px;}";
        html += ".notification{position:fixed;top:20px;right:20px;padding:15px 25px;border-radius:8px;color:white;font-weight:bold;z-index:1000;transform:translateX(120%);transition:transform 0.3s;}";
        html += ".notification.show{transform:translateX(0%);}";
        html += ".notification.success{background:#4CAF50;}";
        html += ".notification.error{background:#f44336;}";
        html += ".form-group{margin:15px 0;}";
        html += ".form-group label{display:block;margin-bottom:5px;font-weight:bold;color:#555;}";
        html += ".form-group input{width:100%;padding:10px;border:1px solid #ddd;border-radius:6px;box-sizing:border-box;font-size:16px;}";
        html += ".form-row{display:flex;gap:10px;}";
        html += ".form-row .form-group{flex:1;}";
        html += "@media(max-width:768px){.grid{grid-template-columns:1fr;}.header{text-align:center;}.form-row{flex-direction:column;}}";
        html += "</style></head><body>";
        html += "<div class='container'>";
        html += "<div class='header'>";
        html += "<h1>🐱 ESP32 甲醛监测仪</h1>";
        html += "<div class='subtitle'>节能版本 | 实时甲醛监测</div>";
        html += "</div>";

        html += "<div class='grid'>";
        html += "<div class='card'>";
        html += "<h2>📡 WiFi连接状态</h2>";
        html += "<div class='data-item'><span class='data-label'>连接状态:</span><span class='data-value'><span class='status-badge status-connected'>" + String(wifi_.isConnected() ? "✅ 已连接" : "❌ 未连接") + "</span></span></div>";
        html += "<div class='data-item'><span class='data-label'>SSID:</span><span class='data-value'>" + wifi_.getSavedSSID() + "</span></div>";
        html += "<div class='data-item'><span class='data-label'>IP地址:</span><span class='data-value'>" + wifi_.getLocalIP() + "</span></div>";
        html += "<div class='data-item'><span class='data-label'>信号强度:</span><span class='data-value'>" + String(wifi_.getRSSI()) + " dBm</span></div>";
        html += "<div style='margin-top:20px;text-align:center;'>";
        html += "<a href='/manage' class='btn'>⚙️ WiFi管理</a>";
        html += "</div>";
        html += "</div>";

        html += "<div class='card'>";
        html += "<h2>🏠 甲醛监测</h2>";
        html += "<div class='data-item'><span class='data-label'>甲醛浓度:</span><span class='data-value' id='ch2oMg'>" + String(ch2o_data.concentration_mg, 4) + " mg/m³</span></div>";
        html += "<div class='data-item'><span class='data-label'>甲醛浓度:</span><span class='data-value' id='ch2oPpm'>" + String(ch2o_data.concentration_ppm, 4) + " ppm</span></div>";
        html += "<div class='data-item'><span class='data-label'>甲醛浓度:</span><span class='data-value' id='ch2oPpb'>" + String(ch2o_data.concentration_ppb, 2) + " ppb</span></div>";
        html += "<div class='data-item'><span class='data-label'>数据状态:</span><span class='data-value' id='ch2oValid'>" + String(ch2o_data.valid ? "✅ 有效" : "❌ 无效") + "</span></div>";
        html += "<div class='data-item'><span class='data-label'>健康状态:</span><span class='data-value' id='ch2oStatus'>" + ch2o_data.status + "</span></div>";
        html += "</div>";

        html += "<div class='card'>";
        html += "<h2>💻 系统信息</h2>";
        html += "<div class='data-item'><span class='data-label'>运行时间:</span><span class='data-value' id='uptime'>" + getTimeStamp() + "</span></div>";
        if (timeSync_) {
            html += "<div class='data-item'><span class='data-label'>NTP时间:</span><span class='data-value' id='ntpTime'>" + timeSync_->getTimeString() + "</span></div>";
            html += "<div class='data-item'><span class='data-label'>时间同步:</span><span class='data-value' id='timeSync'>" + String(timeSync_->isSynced() ? "✅ 已同步" : "⏳ 同步中") + "</span></div>";
        }
        html += "<div class='data-item'><span class='data-label'>AP热点:</span><span class='data-value'>" + String(wifi_.isAPActive() ? "✅ 开启" : "❌ 关闭") + "</span></div>";
        html += "<div class='data-item'><span class='data-label'>节能模式:</span><span class='data-value'>✅ 已启用</span></div>";
        html += "</div>";

        html += "<div class='card'>";
        html += "<h2>📡 数据上报配置</h2>";
        if (reporter_) {
            html += "<div class='data-item'><span class='data-label'>配置状态:</span><span class='data-value' id='reportConfigured'>" + String(reporter_->isConfigured() ? "✅ 已配置" : "❌ 未配置") + "</span></div>";
            html += "<div class='data-item'><span class='data-label'>上报间隔:</span><span class='data-value'>" + String(REPORT_INTERVAL / 1000 / 60) + " 分钟</span></div>";
            html += "<div class='data-item'><span class='data-label'>采样间隔:</span><span class='data-value'>" + String(CACHE_SAMPLE_INTERVAL / 1000) + " 秒</span></div>";
            html += "<div class='data-item'><span class='data-label'>缓存样本:</span><span class='data-value' id='cachedSamples'>" + String(reporter_->getSampleCount()) + "/" + String(reporter_->getMaxSamples()) + "</span></div>";
            html += "<div class='data-item'><span class='data-label'>成功次数:</span><span class='data-value' id='successCount'>0</span></div>";
            html += "<div class='data-item'><span class='data-label'>失败次数:</span><span class='data-value' id='failCount'>0</span></div>";
            html += "<div class='data-item'><span class='data-label'>错误信息:</span><span class='data-value' id='errorMsg' style='color:#dc3545;'></span></div>";
        }
        html += "<div class='form-group'><label>上报地址</label><input type='text' id='serverUrl' placeholder='例如: https://server.wayok.cn/DataCollection/api/collect' style='width:100%;padding:10px;border:1px solid #ddd;border-radius:6px;box-sizing:border-box;font-size:16px;'></div>";
        html += "<div style='text-align:center;margin-top:10px;'>";
        html += "<button class='btn btn-secondary' onclick='saveReportConfig()'>💾 保存配置</button>";
        html += "<button class='btn' onclick='testReport()'>📤 立即上报</button>";
        html += "</div>";
        html += "</div>";
        html += "</div>";

        html += "<div style='margin-top:20px;text-align:center;'>";
        html += "<button class='btn' onclick='refreshData()'>🔄 刷新数据</button>";
        html += "<a href='/data' class='btn' style='background:#2196F3;margin-left:10px;'>📄 文本数据</a>";
        html += "<a href='/sensor' class='btn' style='background:#9c27b0;margin-left:10px;'>📱 JSON数据</a>";
        html += "</div>";

        html += "<div class='actions'>";
        html += "<h2>⚡ 快捷操作</h2>";
        html += "<a href='/scan' class='btn' target='_blank'>🔍 扫描WiFi</a>";
        html += "<a href='/manage' class='btn btn-warning' style='margin:0 10px;'>⚙️ WiFi管理</a>";
        html += "</div>";

        html += "</div>";
        html += "<div class='notification' id='notification'></div>";

        html += "<script>";
        html += "function refreshData(){";
        html += "fetch('/sensor').then(r=>r.json()).then(data=>{";
        html += "document.getElementById('ch2oPpb').textContent=data.ch2o.concentration_ppb.toFixed(2)+' ppb';";
        html += "document.getElementById('ch2oPpm').textContent=data.ch2o.concentration_ppm.toFixed(4)+' ppm';";
        html += "document.getElementById('ch2oMg').textContent=data.ch2o.concentration_mgm3.toFixed(4)+' mg/m³';";
        html += "document.getElementById('ch2oValid').innerHTML=data.ch2o.valid?'✅ 有效':'❌ 无效';";
        html += "document.getElementById('ch2oStatus').textContent=data.ch2o.status;";
        html += "document.getElementById('uptime').textContent=data.timestamp;";
        html += "}).catch(e=>{});";
        html += "loadReportConfig();";
        html += "loadReportStatus();";
        html += "}";
        html += "function loadReportConfig(){";
        html += "fetch('/report_config').then(r=>r.json()).then(data=>{";
        html += "document.getElementById('reportConfigured').innerHTML=data.configured?'✅ 已配置':'❌ 未配置';";
        html += "if(data.configured){document.getElementById('serverUrl').value=data.url;}";
        html += "});}";
        html += "function loadReportStatus(){";
        html += "fetch('/report_status').then(r=>r.json()).then(data=>{";
        html += "document.getElementById('cachedSamples').textContent=(data.cached_samples||0)+'/'+(data.max_samples||100);";
        html += "document.getElementById('successCount').textContent=data.success_count||0;";
        html += "document.getElementById('failCount').textContent=data.fail_count||0;";
        html += "if(data.last_error&&data.fail_count>0){";
        html += "document.getElementById('errorMsg').textContent=data.last_error;";
        html += "}else{document.getElementById('errorMsg').textContent='';}";
        html += "}).catch(e=>{});}";
        html += "function saveReportConfig(){";
        html += "let url=document.getElementById('serverUrl').value;";
        html += "if(!url){showNotification('请输入上报地址','error');return;}";
        html += "fetch('/report_config?url='+encodeURIComponent(url),{method:'POST'}).then(r=>r.json()).then(data=>{";
        html += "if(data.success){showNotification('配置已保存','success');loadReportConfig();}else{showNotification(data.message,'error');}";
        html += "}).catch(e=>showNotification('请求失败','error'));}";
        html += "function testReport(){";
        html += "showNotification('正在上报...','success');";
        html += "fetch('/report_now',{method:'POST'}).then(r=>r.json()).then(data=>{";
        html += "if(data.success){showNotification('上报成功','success');}else{showNotification('上报失败: '+(data.message||'未知错误'),'error');}";
        html += "}).catch(e=>showNotification('请求失败: '+e,'error'));}";
        html += "function showNotification(message,type){";
        html += "let notification=document.getElementById('notification');";
        html += "notification.textContent=message;notification.className='notification '+type;notification.classList.add('show');";
        html += "setTimeout(()=>{notification.classList.remove('show');},3000);}";
        html += "setInterval(refreshData,5000);";
        html += "</script></body></html>";

        return html;
    }

    String getConfigHTML() {
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
        html += "<title>ESP32 WiFi配网（节能版）</title>";
        html += "<style>";
        html += "body{font-family:Arial,sans-serif;max-width:500px;margin:50px auto;padding:20px;background:#f5f5f5;}";
        html += ".container{background:white;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}";
        html += "h1{color:#333;text-align:center;margin-bottom:30px;}";
        html += ".form-group{margin-bottom:20px;}";
        html += "label{display:block;margin-bottom:5px;font-weight:bold;color:#555;}";
        html += "select,input{width:100%;padding:10px;border:1px solid #ddd;border-radius:5px;box-sizing:border-box;font-size:16px;}";
        html += "button{width:100%;padding:12px;background:#4CAF50;color:white;border:none;border-radius:5px;cursor:pointer;font-size:16px;margin-top:10px;}";
        html += "button:hover{background:#45a049;}";
        html += ".btn-scan{background:#2196F3;}";
        html += ".btn-scan:hover{background:#0b7dda;}";
        html += ".status{padding:15px;margin-top:20px;border-radius:5px;text-align:center;display:none;}";
        html += ".status.show{display:block;}";
        html += ".status.success{background:#d4edda;color:#155724;border:1px solid #c3e6cb;}";
        html += ".status.error{background:#f8d7da;color:#721c24;border:1px solid #f5c6cb;}";
        html += ".status.info{background:#d1ecf1;color:#0c5460;border:1px solid #bee5eb;}";
        html += ".loading{text-align:center;color:#666;display:none;}";
        html += ".loading.show{display:block;}";
        html += "</style></head><body>";
        html += "<div class='container'>";
        html += "<h1>🐱 WiFi配网</h1>";
        html += "<div class='form-group'>";
        html += "<label for='ssid'>选择WiFi网络:</label>";
        html += "<select id='ssid'><option value=''>正在扫描...</option></select>";
        html += "<button class='btn-scan' onclick='scanWiFi()'>重新扫描</button>";
        html += "</div>";
        html += "<div class='form-group'>";
        html += "<label for='password'>WiFi密码:</label>";
        html += "<input type='password' id='password' placeholder='请输入WiFi密码'>";
        html += "</div>";
        html += "<button onclick='connectWiFi()'>连接WiFi</button>";
        html += "<div class='loading' id='loading'>正在连接，请稍候...</div>";
        html += "<div class='status' id='status'></div>";
        html += "</div>";
        html += "<script>";
        html += "function scanWiFi(){";
        html += "document.getElementById('ssid').innerHTML='<option>扫描中...</option>';";
        html += "fetch('/scan').then(r=>r.json()).then(data=>{";
        html += "let select=document.getElementById('ssid');";
        html += "select.innerHTML='<option value=\"\">请选择WiFi网络</option>';";
        html += "data.networks.forEach(net=>{";
        html += "select.innerHTML+='<option value=\"'+net.ssid+'\">'+net.ssid+' ('+net.rssi+'dBm)</option>';";
        html += "});";
        html += "}).catch(e=>{document.getElementById('ssid').innerHTML='<option>扫描失败</option>';});";
        html += "}";
        html += "function connectWiFi(){";
        html += "let ssid=document.getElementById('ssid').value;";
        html += "let password=document.getElementById('password').value;";
        html += "if(!ssid){showStatus('请选择WiFi网络','error');return;}";
        html += "showStatus('','');";
        html += "document.getElementById('loading').classList.add('show');";
        html += "fetch('/connect?ssid='+encodeURIComponent(ssid)+'&password='+encodeURIComponent(password))";
        html += ".then(r=>r.json()).then(data=>{";
        html += "document.getElementById('loading').classList.remove('show');";
        html += "if(data.success){";
        html += "showStatus('连接成功！<br>IP地址: '+data.ip+'<br>3秒后自动跳转...','success');";
        html += "setTimeout(()=>{window.location.href='/';},3000);";
        html += "}else{";
        html += "showStatus('连接失败: '+data.message,'error');";
        html += "}";
        html += "}).catch(e=>{";
        html += "document.getElementById('loading').classList.remove('show');";
        html += "showStatus('请求失败，请重试','error');";
        html += "});}";
        html += "function showStatus(msg,type){";
        html += "let status=document.getElementById('status');";
        html += "status.innerHTML=msg;";
        html += "status.className='status '+(type||'');";
        html += "if(msg)status.classList.add('show');";
        html += "}";
        html += "window.onload=function(){scanWiFi();";
        html += "setInterval(checkStatus,3000);};";
        html += "function checkStatus(){";
        html += "fetch('/status').then(r=>r.json()).then(data=>{";
        html += "if(data.connected){";
        html += "showStatus('已连接到: '+data.ssid+'<br>IP地址: '+data.ip,'success');";
        html += "}";
        html += "});";
        html += "}";
        html += "</script></body></html>";
        return html;
    }

    String getManageHTML() {
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
        html += "<title>WiFi管理</title>";
        html += "<style>";
        html += "body{font-family:Arial,sans-serif;max-width:600px;margin:20px auto;padding:20px;background:#f5f5f5;}";
        html += ".container{background:white;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}";
        html += "h1{color:#333;text-align:center;margin-bottom:30px;}";
        html += ".config-info{background:#E3F2FD;padding:20px;border-radius:10px;margin:20px 0;}";
        html += ".config-info p{margin:10px 0;}";
        html += ".warning{background:#fff3cd;border-left:4px solid #ffc107;padding:15px;margin:15px 0;border-radius:4px;}";
        html += ".btn{padding:12px 25px;background:#f44336;color:white;border:none;border-radius:5px;cursor:pointer;font-size:16px;margin:10px 5px;}";
        html += ".btn:hover{background:#d32f2f;}";
        html += ".btn-warning{background:#ff9800;}";
        html += ".btn-warning:hover{background:#f57c00;}";
        html += ".btn-back{background:#2196F3;}";
        html += ".btn-back:hover{background:#0b7dda;}";
        html += ".status{padding:15px;margin:20px 0;border-radius:5px;text-align:center;display:none;}";
        html += ".status.show{display:block;}";
        html += ".status.success{background:#d4edda;color:#155724;border:1px solid #c3e6cb;}";
        html += ".status.error{background:#f8d7da;color:#721c24;border:1px solid #f5c6cb;}";
        html += "</style></head><body>";
        html += "<div class='container'>";
        html += "<h1>⚙️ WiFi管理</h1>";

        if (wifi_.getSavedSSID().length() > 0) {
            html += "<div class='config-info'>";
            html += "<h2>已保存的WiFi配置</h2>";
            html += "<p><strong>SSID:</strong> " + wifi_.getSavedSSID() + "</p>";
            html += "<p><strong>连接状态:</strong> " + String(wifi_.isConnected() ? "✅ 已连接" : "❌ 未连接") + "</p>";
            if (wifi_.isConnected()) {
                html += "<p><strong>IP地址:</strong> " + wifi_.getLocalIP() + "</p>";
            }
            html += "</div>";

            if (!wifi_.isConnected() && wifi_.getSavedSSID().length() > 0) {
                html += "<div class='warning'>";
                html += "⚠️ <strong>注意：</strong>检测到已保存但无法连接的WiFi网络，这可能会影响WiFi扫描功能。建议删除配置后重新扫描。";
                html += "</div>";
                html += "<button class='btn btn-warning' onclick='clearAndScan()'>📡 清除配置并扫描</button>";
                html += "<button class='btn' style='background:#4CAF50;' onclick='reconnectWiFi()'>🔄 重新连接</button>";
            }

            html += "<button class='btn' onclick='deleteWiFi()'>🗑️ 删除WiFi配置</button>";
        } else {
            html += "<div class='config-info'>";
            html += "<h2>WiFi配置信息</h2>";
            html += "<p>当前没有保存的WiFi配置</p>";
            html += "</div>";
            html += "<button class='btn' style='background:#4CAF50;' onclick='window.location.href=\"/config\"'>📶 开始配网</button>";
        }

        html += "<button class='btn btn-back' onclick='window.location.href=\"/\"'>🏠 返回主页</button>";
        html += "<div class='status' id='status'></div>";
        html += "</div>";
        html += "<script>";
        html += "function deleteWiFi(){";
        html += "if(confirm('确定要删除WiFi配置吗？这将断开当前连接并重启配网模式。')){";
        html += "showStatus('正在删除...','');";
        html += "fetch('/delete_wifi').then(r=>r.json()).then(data=>{";
        html += "if(data.success){";
        html += "showStatus('WiFi配置已删除！3秒后跳转到配网页面...','success');";
        html += "setTimeout(()=>{window.location.href='/config';},3000);";
        html += "}else{";
        html += "showStatus('删除失败: '+data.message,'error');";
        html += "}}).catch(e=>{";
        html += "showStatus('请求失败','error');";
        html += "});}}";
        html += "function clearAndScan(){";
        html += "if(confirm('确定要清除WiFi配置并进行扫描吗？')){";
        html += "showStatus('正在清除配置...','');";
        html += "fetch('/delete_wifi').then(r=>r.json()).then(data=>{";
        html += "if(data.success){";
        html += "showStatus('配置已清除，正在跳转到扫描页面...','success');";
        html += "setTimeout(()=>{window.location.href='/config';},2000);";
        html += "}else{";
        html += "showStatus('操作失败: '+data.message,'error');";
        html += "}}).catch(e=>{";
        html += "showStatus('请求失败','error');";
        html += "});}}";
        html += "function reconnectWiFi(){";
        html += "showStatus('正在重新连接...','');";
        html += "fetch('/connect?ssid='+encodeURIComponent('" + wifi_.getSavedSSID() + "')+'&password='+encodeURIComponent('" + wifi_.getSavedPassword() + "'))";
        html += ".then(r=>r.json()).then(data=>{";
        html += "if(data.success){";
        html += "showStatus('连接成功！IP: '+data.ip,'success');";
        html += "setTimeout(()=>{location.reload();},2000);";
        html += "}else{";
        html += "showStatus('连接失败: '+data.message,'error');";
        html += "}}).catch(e=>{";
        html += "showStatus('请求失败','error');";
        html += "});}";
        html += "function showStatus(msg,type){";
        html += "let status=document.getElementById('status');";
        html += "status.innerHTML=msg;";
        html += "status.className='status '+(type||'');";
        html += "if(msg)status.classList.add('show');";
        html += "}";
        html += "</script></body></html>";
        return html;
    }

    ::WebServer server_;
    ZE08CH2OSensor& ch2o_;
    WiFiManager& wifi_;
    DataReporter* reporter_;
    TimeSync* timeSync_;
};

#endif // WEB_SERVER_H
