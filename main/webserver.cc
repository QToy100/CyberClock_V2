#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include <string.h>
#include <stdlib.h> // 用于 malloc 和 free
#include "html/adjust.h"
#include "html/index.h"
#include "html/update.h"
#include "html/factory.h"
#include "html/timer.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "settings.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include <sys/time.h>
#include <time.h>
#include "esp_random.h"
#include "esp_system.h"
#include "config.h"
#include "main.h"
#include "CyberClock.h"
// Captive Portal 探测路径处理
#include "esp_http_server.h"

#define TAG "WebServer"

// HTTP Server
httpd_handle_t web_server_ = nullptr;


// 声明信号量
extern SemaphoreHandle_t server_time_ready_semaphore;



static esp_err_t handle_get_calibration(httpd_req_t *req) {
    // 创建 JSON 文档
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create JSON object");
        return ESP_FAIL;
    }

    // 通过CyberClock实例获取servo_offsets_
    int* servo_offsets = CyberClock::GetInstance().GetServoOffsets();
    for (int i = 0; i < 4; i++) {
        cJSON *digit_array = cJSON_CreateArray();
        if (!digit_array) {
            ESP_LOGE(TAG, "Failed to create JSON array");
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create JSON array");
            return ESP_FAIL;
        }
        for (int j = 0; j < 7; j++) {
            cJSON_AddItemToArray(digit_array, cJSON_CreateNumber(servo_offsets[i * 7 + j]));
        }
        char digit_key[8];
        snprintf(digit_key, sizeof(digit_key), "digit%d", i + 1);
        cJSON_AddItemToObject(root, digit_key, digit_array);
    }

    // 序列化 JSON 数据
    char *json_response = cJSON_Print(root);
    if (!json_response) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to serialize JSON");
        return ESP_FAIL;
    }

    // 发送 JSON 响应
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response, HTTPD_RESP_USE_STRLEN);

    // 清理
    cJSON_Delete(root);
    free(json_response);

    return ESP_OK;
}

// 固件上传处理函数
static esp_err_t handle_firmware_upload(httpd_req_t *req) {
    char *ota_write_data = (char *)malloc(1024); // 动态分配缓冲区
    if (!ota_write_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for OTA buffer");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to allocate memory");
        return ESP_FAIL;
    }

    int received;
    esp_ota_handle_t ota_handle;
    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);

    if (!ota_partition) {
        ESP_LOGE(TAG, "Failed to find OTA partition");
        free(ota_write_data); // 释放缓冲区
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA partition not found");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Starting OTA update...");
    if (esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle) != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed");
        free(ota_write_data); // 释放缓冲区
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    // 解析 multipart/form-data
    bool is_firmware_data = false;
    while ((received = httpd_req_recv(req, ota_write_data, 1024)) > 0) {
        if (!is_firmware_data) {
            // 查找文件内容的起始位置
            char *file_start = strstr(ota_write_data, "\r\n\r\n");
            if (file_start) {
                file_start += 4; // 跳过 "\r\n\r\n"
                int file_data_len = received - (file_start - ota_write_data);
                ESP_LOGI(TAG, "Found firmware data start");

                // 写入文件内容到 OTA 分区
                if (esp_ota_write(ota_handle, file_start, file_data_len) != ESP_OK) {
                    ESP_LOGE(TAG, "OTA write failed");
                    esp_ota_end(ota_handle);
                    free(ota_write_data); // 释放缓冲区
                    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
                    return ESP_FAIL;
                }
                is_firmware_data = true;
                continue;
            }
        } else {
            // 写入后续的文件内容到 OTA 分区
            if (esp_ota_write(ota_handle, ota_write_data, received) != ESP_OK) {
                ESP_LOGE(TAG, "OTA write failed");
                esp_ota_end(ota_handle);
                free(ota_write_data); // 释放缓冲区
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
                return ESP_FAIL;
            }
        }
    }

    if (received < 0) {
        ESP_LOGE(TAG, "OTA receive error");
        esp_ota_end(ota_handle);
        free(ota_write_data); // 释放缓冲区
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA receive error");
        return ESP_FAIL;
    }

    // 完成 OTA 更新
    if (esp_ota_end(ota_handle) != ESP_OK) {
        ESP_LOGE(TAG, "OTA end failed");
        free(ota_write_data); // 释放缓冲区
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        return ESP_FAIL;
    }

    if (esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
        ESP_LOGE(TAG, "OTA set boot partition failed");
        free(ota_write_data); // 释放缓冲区
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA set boot partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA update complete. Restarting...");
    free(ota_write_data); // 释放缓冲区
    httpd_resp_sendstr(req, "Firmware update complete. Restarting...");

    // 延迟 2 秒
    vTaskDelay(pdMS_TO_TICKS(2000)); // 延迟 2000 毫秒（2 秒）
   
    esp_restart(); // 重启设备

    return ESP_OK;
}

// 返回固件升级页面
static esp_err_t handle_update_page(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, update_html, strlen(update_html));
    return ESP_OK;
}

// 返回主页
static esp_err_t handle_index(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html, strlen(index_html));
    return ESP_OK;
}

// 设置页
static esp_err_t handle_factory_page(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, factory_html, strlen(factory_html));
    return ESP_OK;
}

static esp_err_t handle_adjust_page(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, adjust_html, strlen(adjust_html));

    CyberClock::GetInstance().SetNumber(8, 8, 8, 8); // 设置数字为 8888，显示校准效果
    return ESP_OK;
}

static esp_err_t handle_timer_page(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, timer_html, strlen(timer_html));

    CyberClock::GetInstance().SetTimer(0); 
    return ESP_OK;
}

// 接收校准数据的处理程序
static esp_err_t handle_adjust(httpd_req_t *req) {
    char content[512]; // 假设数据不会超过 512 字节
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        return ESP_FAIL;    
    }
    content[ret] = '\0'; // 确保字符串以 '\0' 结尾

    ESP_LOGI("Adjust", "Received calibration data: %s", content);

    // 解析 JSON 数据
    cJSON *root = cJSON_Parse(content);
    if (!root) {
        ESP_LOGE("Adjust", "Failed to parse JSON");
        return ESP_FAIL;
    }

    // 更新 servo_offsets_
    int* servo_offsets = CyberClock::GetInstance().GetServoOffsets();
    cJSON *digits[4] = {
        cJSON_GetObjectItem(root, "digit1"),
        cJSON_GetObjectItem(root, "digit2"),
        cJSON_GetObjectItem(root, "digit3"),
        cJSON_GetObjectItem(root, "digit4")
    };

    int index = 0;
    for (int i = 0; i < 4; i++) {
        if (cJSON_IsArray(digits[i])) {
            cJSON *segment = nullptr;
            cJSON_ArrayForEach(segment, digits[i]) {
                if (index < 28 && cJSON_IsNumber(segment)) {
                    servo_offsets[index++] = segment->valueint;
                }
            }
        }
    }

    cJSON_Delete(root);

    // 保存调整数据到设置
    char adjust_data[512] = {0}; // 假设 28 个整数不会超过 512 字节
    int offset = 0;
    for (int i = 0; i < 28; i++) {
        offset += snprintf(adjust_data + offset, sizeof(adjust_data) - offset, "%d|", servo_offsets[i]);
    }

    // 移除最后一个多余的分隔符
    if (offset > 0) {
        adjust_data[offset - 1] = '\0';
    }

    Settings settings("cyberclock",true);
    settings.SetString("adjust_data", adjust_data);
    ESP_LOGI(TAG, "Saved adjust_data: %s", adjust_data);

    // 返回响应
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}



static esp_err_t handle_set(httpd_req_t *req) {
    char query[128] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        ESP_LOGI(TAG, "Query: %s", query);
        char digit[16] = {0};
        char mode[16] = {0};
        char tz[8] = {0};
        char mtz[8] = {0};
        char time_str[16] = {0};
        char servosilent[8] = {0};
        char t1h[8] = {0};
        char t2h[8] = {0};
        char t1m[8] = {0};
        char t2m[8] = {0};
        char en[8] = {0};
        // 解析各参数
        if (httpd_query_key_value(query, "digit", digit, sizeof(digit)) == ESP_OK) {
            int d = atoi(digit);
            int a = d / 1000;
            int b = (d / 100) % 10;
            int c = (d / 10) % 10;
            int e = d % 10;
            CyberClock::GetInstance().SetNumber(a, b, c, e);
        }
        if (httpd_query_key_value(query, "mode", mode, sizeof(mode)) == ESP_OK) {
            int m = atoi(mode);
            if (m == 7) {
                CyberClock::GetInstance().ShutdownClock();
            } else if (m == 5) {
                CyberClock::GetInstance().SetCountDown(5 * 60);
            } else if (m == 6) {
                CyberClock::GetInstance().SetCountDown(60);
            } else if (m == 4) {
                CyberClock::GetInstance().IdleClock(); // 进入休眠模式
            } else if (m == 3) {
                CyberClock::GetInstance().ShowTime(); // 显示时间
            } 
        }
        if (httpd_query_key_value(query, "tz", tz, sizeof(tz)) == ESP_OK) {
            // 处理时区参数
            SetTimezoneOffset(atoi(tz)); // 设置时区偏移
            // 设置时区
            ESP_LOGI(TAG, "Set timezone: %s", tz);
        }
        if (httpd_query_key_value(query, "mtz", mtz, sizeof(mtz)) == ESP_OK) {
            // 处理分钟偏移参数
            SetTimezoneOffsetMinute(atoi(mtz)); // 设置分钟偏移
            // 保存到设置
            ESP_LOGI(TAG, "Set minute offset: %s", mtz);
        }
        if (httpd_query_key_value(query, "time", time_str, sizeof(time_str)) == ESP_OK) {
            // 处理时间同步参数
            struct timeval tv;
            tv.tv_sec = (time_t)(atoi(time_str)+ atoi(mtz) * 60); //加上偏移分钟
            tv.tv_usec = 0; // 微秒部分设置为0
            settimeofday(&tv, NULL); // 设置系统时间
            //设置时区
            char tz_str[16];
            if (atoi(tz) == 0) {// 0区特殊处理
                strcpy(tz_str, "UTC0");
            } else if (atoi(tz) > 0) {
                // 东区
                snprintf(tz_str, sizeof(tz_str), "UTC-%d", atoi(tz)); // 注意东区是负号
            } else {
                // 西区
                snprintf(tz_str, sizeof(tz_str), "UTC+%d", -atoi(tz)); // 注意西区是正号
            }          
            setenv("TZ", tz_str, 1);
            tzset();            
            // 通过localtime函数读取并打印时间，验证是否准确
            time_t new_time = time(NULL);
            struct tm *timeinfo = localtime(&new_time);
            xSemaphoreGive(server_time_ready_semaphore);
            ESP_LOGI(TAG, "Current time: %02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
        }
        if (httpd_query_key_value(query, "servosilent", servosilent, sizeof(servosilent)) == ESP_OK) {
            bool silent = atoi(servosilent) != 0;
            CyberClock::GetInstance().SetServoSilentMode(silent);
            ESP_LOGI(TAG, "Set servosilent: %d", silent);
        }
        //设置12小时制，var url = h1224.checked ? "/set?h=12" : "/set?h=24";
        if (httpd_query_key_value(query, "h", digit, sizeof(digit)) == ESP_OK) {
            int h = atoi(digit);
            if (h == 12) {
                CyberClock::GetInstance().Set12HourMode(true);
            } else if (h == 24) {
                CyberClock::GetInstance().Set12HourMode(false);
            }
            ESP_LOGI(TAG, "Set hour mode: %s", digit);
        }
        if (httpd_query_key_value(query, "t1h", t1h, sizeof(t1h)) == ESP_OK &&
            httpd_query_key_value(query, "t2h", t2h, sizeof(t2h)) == ESP_OK &&
            httpd_query_key_value(query, "t1m", t1m, sizeof(t1m)) == ESP_OK &&
            httpd_query_key_value(query, "t2m", t2m, sizeof(t2m)) == ESP_OK &&
            httpd_query_key_value(query, "en", en, sizeof(en)) == ESP_OK) {
            CyberClock::GetInstance().SetSleepTime(
                atoi(en) != 0, // 是否启用休眠
                atoi(t1h),      // 开始小时
                atoi(t1m),      // 开始分钟
                atoi(t2h),      // 结束小时
                atoi(t2m)       // 结束分钟
            );
            ESP_LOGI(TAG, "Set sleep time: %02d:%02d-%02d:%02d, en=%s", atoi(t1h), atoi(t1m), atoi(t2h), atoi(t2m), en);
        }
        //设置清理uuid,接收参数uuid，判断是否为“NULL”，如果是则清理uuid
        char uuid_str[37];
        if (httpd_query_key_value(query, "uuid", uuid_str, sizeof(uuid_str)) == ESP_OK) {
            //判断 uuid_str 是否为 "NULL"  
            std::string uuid_(uuid_str);
            if (uuid_ == "NULL") {
                // 清理 UUID
                Settings setting_board("board", true);
                setting_board.SetString("uuid", ""); // 清空 UUID
                ESP_LOGI(TAG, "Cleared UUID");
            };
        }
        //设置mac地址
        char mac_str[18];
        uint8_t mac[6];
        if (httpd_query_key_value(query, "mac", mac_str, sizeof(mac_str)) == ESP_OK) {
            // 处理 MAC 地址参数
            if (strcmp(mac_str, "new") == 0) {
                // 生成新的 MAC 地址
                esp_fill_random(mac, sizeof(mac));
                mac[0] &= 0xFE;
                mac[0] |= 0x02;
                // 转为字符串
                char mac_str_out[18];
                snprintf(mac_str_out, sizeof(mac_str_out), "%02X:%02X:%02X:%02X:%02X:%02X",
                        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                Settings setting_board("board", true);
                setting_board.SetString("mac", mac_str_out);
                ESP_LOGI(TAG, "Generated new MAC address: %s", mac_str_out);
            }        
            // 使用默认mac
            if (strcmp(mac_str, "default") == 0) {
                Settings setting_board("board", true);
                //删除mac设置项
                setting_board.EraseKey("mac");
                ESP_LOGI(TAG, "Deleted MAC address, set to default");
            }
        }
        // StartTimer，如果接受到参数Timer=start，则启动定时器
        char timer_str[16];
        if (httpd_query_key_value(query, "timer", timer_str, sizeof(timer_str)) == ESP_OK) {
            if (strcmp(timer_str, "start") == 0) {
                CyberClock::GetInstance().SetTimer(1);
            } else if (strcmp(timer_str, "stop") == 0) {
                CyberClock::GetInstance().SetTimer(2);
            } else if (strcmp(timer_str, "reset") == 0) {
                CyberClock::GetInstance().SetTimer(0);
            }
        }

    }
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}


static esp_err_t handle_default(httpd_req_t *req) {
    char host[64] = {0};
    esp_err_t ret = httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));
    // 判断Host是否为本地IP，否则302重定向到主页
    if (ret == ESP_OK && strcmp(host, "192.168.4.1") != 0 && strcmp(host, "localhost") != 0) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    // 否则返回主页
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html, strlen(index_html));
    return ESP_OK;
}

// 获取配置参数
static esp_err_t handle_get_config(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create JSON");
        return ESP_FAIL;
    }
    Settings settings("cyberclock", true);
    int tz = settings.GetInt("tz", 8);
    int mtz = settings.GetInt("mtz", 0);
    int servosilent = settings.GetInt("servo_mute_en", 0);
    int sleep_en = settings.GetInt("sleep_clock_en", 0);
    int t1h = settings.GetInt("sleep_s_hour", 22);
    int t2h = settings.GetInt("sleep_e_hour", 7);
    int t1m = settings.GetInt("sleep_s_minute", 0);
    int t2m = settings.GetInt("sleep_e_minute", 0);
    cJSON_AddNumberToObject(root, "tz", tz);
    cJSON_AddNumberToObject(root, "mtz", mtz);
    cJSON_AddNumberToObject(root, "servosilent", servosilent);
    cJSON_AddNumberToObject(root, "sleep_en", sleep_en);
    cJSON_AddNumberToObject(root, "t1h", t1h);
    cJSON_AddNumberToObject(root, "t2h", t2h);
    cJSON_AddNumberToObject(root, "t1m", t1m);
    cJSON_AddNumberToObject(root, "t2m", t2m);
    //读取uuid
    Settings setting_board("board", true);
    std::string uuid_ = setting_board.GetString("uuid", "");
    cJSON_AddStringToObject(root, "uuid", uuid_.c_str());
    // 获取mac地址
    char mac_str[18];
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac); // 确保获取正确的 MAC 地址
    //esp_read_mac(mac, ESP_MAC_WIFI_STA); // 获取 Wi-Fi MAC 地址
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(root, "mac", mac_str);
    // firmware版本
    cJSON_AddStringToObject(root, "version", FIRMWARE_VERSION);
    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    cJSON_Delete(root);
    free(json_str);
    return ESP_OK;
}


static esp_err_t handle_captive(httpd_req_t *req) {
    // Captive Portal 探测路径统一302重定向到主页
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// iOS Captive Portal 探测路径
static esp_err_t handle_ios_success(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
static esp_err_t handle_ios_txt(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Success", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}


void StopWebServer() {
    if (web_server_) {
        ESP_LOGW(TAG, "Stopping web server...");
        httpd_stop(web_server_);
        web_server_ = nullptr;
    } else {
        ESP_LOGW(TAG, "Web server is not running");
    }
}

void StartWebServer(httpd_handle_t server) {
    ESP_LOGI(TAG, "WebServer.cc StartWebServer...");

    // 防止重复启动和重复注册
    if (web_server_ != nullptr) {
        ESP_LOGE(TAG, "Web server already started, skip re-registering handlers.");
        return;
    }

    if(server != nullptr) {
        web_server_ = server;
    }
    else{
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.max_uri_handlers = 50; // 增加 URI 处理程序的数量
        config.server_port = 80; // 强制HTTP端口为80，确保手机探测可用
        config.stack_size = 8192; // 增大webserver栈空间，兼容复杂页面
        if(httpd_start(&web_server_, &config) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start web server");
            return;
        }
    }


    // 注册 URI 处理程序 - /index.html
    httpd_uri_t uri_index = {
        .uri = "/index.html",
        .method = HTTP_GET,
        .handler = handle_index,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(web_server_, &uri_index);

    // 注册 URI 处理程序 - /update.html
    httpd_uri_t uri_update_page = {
        .uri = "/update.html",
        .method = HTTP_GET,
        .handler = handle_update_page,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(web_server_, &uri_update_page);

    // 注册 URI 处理程序 - /update (处理固件上传)
    httpd_uri_t uri_firmware_upload = {
        .uri = "/update",
        .method = HTTP_POST,
        .handler = handle_firmware_upload,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(web_server_, &uri_firmware_upload);

    // 注册 URI 处理程序 - /adjust.html
    httpd_uri_t uri_adjust_page = {
        .uri = "/adjust.html",
        .method = HTTP_GET,
        .handler = handle_adjust_page,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(web_server_, &uri_adjust_page);

    // 注册 URI 处理程序 - /timer.html
    httpd_uri_t uri_timer_page = {
        .uri = "/timer.html",
        .method = HTTP_GET,
        .handler = handle_timer_page,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(web_server_, &uri_timer_page);

    // 注册 factory URI
    httpd_uri_t uri_factory_page = {
        .uri = "/factory.html",
        .method = HTTP_GET,
        .handler = handle_factory_page,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(web_server_, &uri_factory_page);

    // 注册 /adjust URI
    httpd_uri_t uri_adjust = {
        .uri = "/adjust",
        .method = HTTP_POST,
        .handler = handle_adjust,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(web_server_, &uri_adjust);

    // 注册 /get_calibration URI
    httpd_uri_t uri_get_calibration = {
        .uri = "/get_calibration",
        .method = HTTP_GET,
        .handler = handle_get_calibration,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(web_server_, &uri_get_calibration);

    // 注册 URI 处理程序 - /set
    httpd_uri_t uri_set = {
        .uri = "/set",
        .method = HTTP_GET,
        .handler = handle_set,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(web_server_, &uri_set);

    // 注册 /get_config URI
    httpd_uri_t uri_get_config = {
        .uri = "/get_config",
        .method = HTTP_GET,
        .handler = handle_get_config,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(web_server_, &uri_get_config);

    // 注册默认 URI 处理程序
    httpd_uri_t uri_default = {
        .uri = "*",
        .method = HTTP_GET,
        .handler = handle_default,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(web_server_, &uri_default);

    // 注册 URI 处理程序 - /
    httpd_uri_t uri_root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = handle_index,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(web_server_, &uri_root);
}