#include "bus_eta.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <atomic>

#include "app_data.h"
#include "app_event.h"
#include "app_state.h"
#include "bus_config.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sticky_rtc.h"
#include "user_config.h"
#include "wifi_config_page.h"

namespace {

constexpr char kTag[] = "bus_eta";
constexpr int kMaxBuses = 10;
constexpr int kWifiTimeoutMs = 30000;
constexpr int kRetryDelayMs = 30000;
constexpr int kHttpTimeoutMs = 15000;
constexpr size_t kHttpBufferSize = 32768;

constexpr EventBits_t kWifiGotIpBit = BIT0;
constexpr EventBits_t kWifiConnectedBit = BIT1;
EventGroupHandle_t s_wifi_events = nullptr;
SemaphoreHandle_t s_data_mutex = nullptr;

// -----------------------------------------------------------------------------
// 后台任务与 AppState 之间的共享数据（互斥锁保护）
// -----------------------------------------------------------------------------
struct SharedBusData {
    bool wifi_connected = false;
    bool data_valid = false;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    char status[32] = {};
    char routes[kMaxBuses][16] = {};
    char arrivals[kMaxBuses][8] = {};
    int eta_minutes[kMaxBuses] = {};
    size_t bus_count = 0;
};
SharedBusData s_data;
std::atomic<bool> s_system_time_valid{false};

// -----------------------------------------------------------------------------
// WiFi
// -----------------------------------------------------------------------------
void wifi_event_handler(void *arg, esp_event_base_t base,
                        int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(kTag, "WiFi associated with AP; waiting for DHCP IP...");
        xEventGroupSetBits(s_wifi_events, kWifiConnectedBit);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(kTag, "WiFi disconnected; updating UI");
        wifi_config_page_set_connected_ssid("", false);
        xEventGroupClearBits(s_wifi_events, kWifiGotIpBit | kWifiConnectedBit);
        // 立即更新共享状态并通知 UI，WiFi 图标马上变为 no wifi，
        // 不用等下一轮拉取（路由器断电/断连时立刻可见）。
        if (s_data_mutex != nullptr &&
            xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_data.wifi_connected = false;
            s_data.data_valid = false;
            std::snprintf(s_data.status, sizeof(s_data.status), "No WiFi");
            xSemaphoreGive(s_data_mutex);
        }
        app_event_post(AppEvent::BusDataReady);
        // 注意：不在这里立即 esp_wifi_connect()。当 SSID 不存在时立即重连
        // 会造成 2~3 秒一次的 connect→fail 死循环，WiFi 驱动永远处于
        // connecting 状态，导致 esp_wifi_scan_start 被拒绝（WiFi 配置页
        // 扫不到热点）。重连统一由 bus_eta_task 主循环按周期执行。
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = static_cast<ip_event_got_ip_t *>(data);
        ESP_LOGI(kTag, "WiFi got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_events, kWifiGotIpBit);
        wifi_config_t wifi_config = {};
        if (esp_wifi_get_config(WIFI_IF_STA, &wifi_config) == ESP_OK) {
            wifi_config_page_set_connected_ssid(
                reinterpret_cast<const char *>(wifi_config.sta.ssid), true);
        }
        if (s_data_mutex != nullptr &&
            xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_data.wifi_connected = true;
            s_data.data_valid = false;
            s_data.bus_count = 0;
            std::snprintf(s_data.status, sizeof(s_data.status), "Connected");
            xSemaphoreGive(s_data_mutex);
        }
        app_event_post(AppEvent::BusDataReady);
    }
}

esp_err_t wifi_init_sta()
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), kTag, "esp_netif_init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), kTag,
                        "esp_event_loop_create_default");

    s_wifi_events = xEventGroupCreate();
    if (s_wifi_events == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    // 创建 STA 网络接口：把 TCP/IP 栈挂到 WiFi 上并启动 DHCP 客户端。
    // 之前漏了这一步，导致 WiFi 关联成功但永远拿不到 IP（没有 netif 就没有 DHCP）。
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif == nullptr) {
        ESP_LOGE(kTag, "esp_netif_create_default_wifi_sta failed");
        return ESP_FAIL;
    }
    esp_netif_set_default_netif(sta_netif);

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), kTag, "esp_wifi_init");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                   &wifi_event_handler, nullptr),
        kTag, "register wifi event");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                   &wifi_event_handler, nullptr),
        kTag, "register ip event");

    wifi_config_t wifi_config = {};
    const UserConfig &cfg = get_user_config();
    std::strncpy(reinterpret_cast<char *>(wifi_config.sta.ssid),
                 cfg.wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char *>(wifi_config.sta.password),
                 cfg.wifi_password, sizeof(wifi_config.sta.password) - 1);

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), kTag, "set sta mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config),
                        kTag, "set wifi config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), kTag, "start wifi");
    ESP_LOGI(kTag, "WiFi init done");
    return ESP_OK;
}

bool wifi_connect_blocking(int timeout_ms)
{
    const EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events, kWifiGotIpBit, pdFALSE, pdTRUE,
        pdMS_TO_TICKS(timeout_ms));
    return (bits & kWifiGotIpBit) != 0;
}

// -----------------------------------------------------------------------------
// NTP 校时并写回 RTC（移植自 HelloWorld 的 syncTimeFromNtp）
// -----------------------------------------------------------------------------
bool sync_time_from_ntp()
{
    // v5.4.4 类型名为 esp_sntp_config_t（不是 esp_netif_sntp_config_t）
    // 宏默认 .start=true，init 会自动启动 SNTP 服务，无需再调 esp_netif_sntp_start()
    //
    // 顺序尝试多个服务器：部分热点/网络会屏蔽 UDP 123，导致单个服务器超时。
    constexpr int kNtpTimeoutMs = 15000;
    const char *kNtpServers[] = {
        BUS_NTP_SERVER,
        "ntp.aliyun.com",
        "time.google.com",
    };

    for (const char *server : kNtpServers) {
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(server);
        if (esp_netif_sntp_init(&config) != ESP_OK) {
            ESP_LOGE(kTag, "SNTP init failed (%s)", server);
            continue;
        }

        const esp_err_t wait_result =
            esp_netif_sntp_sync_wait(pdMS_TO_TICKS(kNtpTimeoutMs));
        esp_netif_sntp_deinit();
        if (wait_result != ESP_OK) {
            ESP_LOGW(kTag, "NTP sync timeout via %s; trying next server", server);
            continue;
        }

        // SNTP updates the ESP system clock even when the separate PCF8563
        // write below fails. Keep that distinction for the UI.
        s_system_time_valid.store(true);

        // NTP 返回的是 UTC 时间，这里换算成公交站本地时区（新加坡/香港均为
        // UTC+8）再写入 RTC，保证屏幕时钟与公交站时刻一致。
        time_t now = 0;
        time(&now);
        now += BUS_UTC_OFFSET_SECONDS;
        struct tm local = {};
        // The RTC stores the configured UTC+8 wall clock. Use UTC conversion
        // on the already-offset value so an ambient TZ setting cannot apply a
        // second, platform-dependent conversion.
        gmtime_r(&now, &local);

        RtcDateTime rtc = {};
        rtc.year = local.tm_year + 1900;
        rtc.month = local.tm_mon + 1;
        rtc.day = local.tm_mday;
        rtc.weekday = local.tm_wday;
        rtc.hour = local.tm_hour;
        rtc.minute = local.tm_min;
        rtc.second = local.tm_sec;

        const esp_err_t rtc_result = sticky_rtc_write(rtc);
        if (rtc_result != ESP_OK) {
            ESP_LOGW(kTag, "NTP time obtained but RTC write failed: %s",
                     esp_err_to_name(rtc_result));
            // NTP has already set the ESP32 system clock. Bus arrivals can
            // still be fetched; the UI hides minute countdowns while its RTC
            // validity flag is false.
            return true;
        }
        ESP_LOGI(kTag, "NTP sync OK via %s, RTC set to %04d-%02d-%02d "
                       "%02d:%02d:%02d",
                 server, rtc.year, rtc.month, rtc.day,
                 rtc.hour, rtc.minute, rtc.second);
        return true;
    }

    ESP_LOGE(kTag, "All NTP servers failed; keeping existing RTC time");
    return false;
}

// -----------------------------------------------------------------------------
// HTTP GET
// -----------------------------------------------------------------------------
bool http_get(const char *url, char *response, size_t response_size,
              int *out_len)
{
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = kHttpTimeoutMs;
    // 使用 ESP 内置 CA 证书包验证服务器（busrouter.sg / etabus.gov.hk 的
    // 证书都在 Let's Encrypt / DigiCert 等常用 CA 包里）。
    // v5.4 里旧的 CONFIG_ESP_TLS_INSECURE 已改名，且不设置验证选项会直接
    // 拒绝建立 TLS 连接，所以这里显式挂上证书包。
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return false;
    }

    const esp_err_t open_result = esp_http_client_open(client, 0);
    if (open_result != ESP_OK) {
        ESP_LOGE(kTag, "HTTP open failed: %s", esp_err_to_name(open_result));
        esp_http_client_cleanup(client);
        return false;
    }
    esp_http_client_fetch_headers(client);
    const int total = esp_http_client_read_response(client, response,
                                                    static_cast<int>(response_size) - 1);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total < 0) {
        ESP_LOGE(kTag, "HTTP read failed");
        return false;
    }
    response[static_cast<size_t>(total)] = '\0';
    if (out_len != nullptr) {
        *out_len = total;
    }
    ESP_LOGI(kTag, "HTTP %d bytes from %s", total, url);
    return total > 0;
}

// -----------------------------------------------------------------------------
// JSON 解析（移植自 parseSingaporeBusData / parseHongKongKmbBusData）
// -----------------------------------------------------------------------------
void parse_singapore(const char *json, SharedBusData &data)
{
    cJSON *root = cJSON_Parse(json);
    if (root == nullptr) {
        data.error = ESP_ERR_INVALID_RESPONSE;
        std::snprintf(data.status, sizeof(data.status), "JSON parse error");
        return;
    }

    const cJSON *services = cJSON_GetObjectItem(root, "services");
    if (!cJSON_IsArray(services)) {
        data.error = ESP_ERR_INVALID_RESPONSE;
        // 停靠站 ID 不对时接口常返回非预期结构：直接提示 ID 错误
        std::snprintf(data.status, sizeof(data.status), "ID Error");
        cJSON_Delete(root);
        return;
    }

    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, services)
    {
        if (data.bus_count >= kMaxBuses) {
            break;
        }
        const cJSON *no = cJSON_GetObjectItem(item, "no");
        const cJSON *next = cJSON_GetObjectItem(item, "next");
        const cJSON *time = (next != nullptr) ? cJSON_GetObjectItem(next, "time") : nullptr;
        const cJSON *duration_ms = (next != nullptr)
                                       ? cJSON_GetObjectItem(next, "duration_ms")
                                       : nullptr;
        if (!cJSON_IsString(no) || !cJSON_IsString(time)) {
            continue;
        }
        const char *value = time->valuestring;
        if (std::strlen(value) < 16) {
            continue;  // 没有下一班车时间
        }
        std::snprintf(data.routes[data.bus_count],
                      sizeof(data.routes[data.bus_count]), "%s", no->valuestring);
        std::snprintf(data.arrivals[data.bus_count],
                      sizeof(data.arrivals[data.bus_count]),
                      "%.2s:%.2s", value + 11, value + 14);
        data.eta_minutes[data.bus_count] =
            cJSON_IsNumber(duration_ms)
                ? static_cast<int>(duration_ms->valuedouble / 60000.0)
                : 0;
        ++data.bus_count;
    }
    cJSON_Delete(root);

    data.error = data.bus_count > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
    std::snprintf(data.status, sizeof(data.status),
                  data.bus_count > 0 ? "%d routes" : "No arrivals",
                  static_cast<int>(data.bus_count));
}

int minutes_until_iso_time(const char *iso_time)
{
    int year, month, day, hour, minute, second;
    if (std::sscanf(iso_time, "%d-%d-%dT%d:%d:%d",
                    &year, &month, &day, &hour, &minute, &second) != 6) {
        return -1;
    }
    struct tm eta = {};
    eta.tm_year = year - 1900;
    eta.tm_mon = month - 1;
    eta.tm_mday = day;
    eta.tm_hour = hour;
    eta.tm_min = minute;
    eta.tm_sec = second;
    const time_t eta_epoch = mktime(&eta);
    if (eta_epoch == static_cast<time_t>(-1)) {
        return -1;
    }
    // API 返回的 eta 是香港本地时刻（UTC+8）。mktime 在未设 TZ 时按 UTC
    // 解释，会把时刻"当成"UTC 的 epoch；真实 epoch 要减去时区偏移，再与
    // 当前 UTC epoch（time(nullptr)）相减得到剩余秒数。
    const long seconds = static_cast<long>(
        eta_epoch - BUS_UTC_OFFSET_SECONDS - time(nullptr));
    return seconds <= 0 ? 0 : static_cast<int>((seconds + 59) / 60);
}

void parse_hong_kong_kmb(const char *json, SharedBusData &data)
{
    cJSON *root = cJSON_Parse(json);
    if (root == nullptr) {
        data.error = ESP_ERR_INVALID_RESPONSE;
        std::snprintf(data.status, sizeof(data.status), "JSON parse error");
        return;
    }

    const cJSON *records = cJSON_GetObjectItem(root, "data");
    if (!cJSON_IsArray(records)) {
        data.error = ESP_ERR_INVALID_RESPONSE;
        // 停靠站 ID 不对时接口常返回非预期结构：直接提示 ID 错误
        std::snprintf(data.status, sizeof(data.status), "ID Error");
        cJSON_Delete(root);
        return;
    }

    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, records)
    {
        if (data.bus_count >= kMaxBuses) {
            break;
        }
        const cJSON *eta_seq = cJSON_GetObjectItem(item, "eta_seq");
        if (cJSON_IsNumber(eta_seq) &&
            static_cast<int>(eta_seq->valuedouble) != 1) {
            continue;  // 同线路只显示下一班
        }
        const cJSON *route = cJSON_GetObjectItem(item, "route");
        const cJSON *eta = cJSON_GetObjectItem(item, "eta");
        if (!cJSON_IsString(route) || !cJSON_IsString(eta)) {
            continue;
        }
        const int minutes = minutes_until_iso_time(eta->valuestring);
        if (minutes < 0) {
            continue;  // 暂无实时到站
        }
        std::snprintf(data.routes[data.bus_count],
                      sizeof(data.routes[data.bus_count]), "%s", route->valuestring);
        std::snprintf(data.arrivals[data.bus_count],
                      sizeof(data.arrivals[data.bus_count]),
                      "%.2s:%.2s", eta->valuestring + 11, eta->valuestring + 14);
        data.eta_minutes[data.bus_count] = minutes;
        ++data.bus_count;
    }
    cJSON_Delete(root);

    data.error = data.bus_count > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
    std::snprintf(data.status, sizeof(data.status),
                  data.bus_count > 0 ? "%d routes" : "No arrivals",
                  static_cast<int>(data.bus_count));
}

// -----------------------------------------------------------------------------
// 拉取并解析公交数据
// -----------------------------------------------------------------------------
void fetch_bus_data(SharedBusData &data)
{
    data.error = ESP_ERR_INVALID_STATE;
    data.data_valid = false;
    data.bus_count = 0;
    std::snprintf(data.status, sizeof(data.status), "Fetching...");

    char url[192] = {};
    const UserConfig &cfg = get_user_config();
    if (cfg.use_singapore) {
        std::snprintf(url, sizeof(url),
                      "https://arrivelah2.busrouter.sg/?id=%s", cfg.bus_id);
    } else {
        std::snprintf(url, sizeof(url),
                      "https://data.etabus.gov.hk/v1/transport/kmb/stop-eta/%s",
                      cfg.bus_id);
    }

    static char response[kHttpBufferSize];
    int length = 0;
    if (!http_get(url, response, sizeof(response), &length)) {
        std::snprintf(data.status, sizeof(data.status), "HTTP error");
        data.error = ESP_FAIL;
        return;
    }

    if (cfg.use_singapore) {
        parse_singapore(response, data);
    } else {
        parse_hong_kong_kmb(response, data);
    }
    data.data_valid = data.error == ESP_OK;
}

void publish_shared_data(const SharedBusData &data)
{
    if (xSemaphoreTake(s_data_mutex, portMAX_DELAY) == pdTRUE) {
        s_data = data;
        xSemaphoreGive(s_data_mutex);
    }
    if (!app_event_post(AppEvent::BusDataReady)) {
        ESP_LOGW(kTag, "Bus data event queue is full");
    }
}

// -----------------------------------------------------------------------------
// 后台任务：WiFi -> NTP -> 周期刷新公交数据
// -----------------------------------------------------------------------------
void bus_eta_task(void *arg)
{
    SharedBusData initial = {};
    std::snprintf(initial.status, sizeof(initial.status), "Connecting...");
    publish_shared_data(initial);

    const esp_err_t init_result = wifi_init_sta();
    if (init_result != ESP_OK) {
        ESP_LOGE(kTag, "WiFi init failed: %s", esp_err_to_name(init_result));
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(kRetryDelayMs));
        }
    }

    bool connected = wifi_connect_blocking(kWifiTimeoutMs);
    bool ntp_synced = false;
    if (connected) {
        ESP_LOGI(kTag, "WiFi connected");
        ntp_synced = sync_time_from_ntp();
    }

    while (true) {
        SharedBusData data = {};
        if (connected) {
            if (!ntp_synced) {
                // NTP 还没同步成功：每隔 kRetryDelayMs 重试一次，直到成功
                // 再开始拉公交数据，避免 RTC 停留在错误的旧时间上。
                vTaskDelay(pdMS_TO_TICKS(kRetryDelayMs));
                ntp_synced = sync_time_from_ntp();
                continue;
            }
            // WiFi 关联是否还活着：路由器断电等会触发 DISCONNECTED 清位，
            // 这里用事件位做兜底检测，避免一直拿旧状态拉数据。
            const bool wifi_up =
                (xEventGroupGetBits(s_wifi_events) & kWifiConnectedBit) != 0;
            if (wifi_up) {
                data.wifi_connected = true;
                fetch_bus_data(data);
            } else {
                // 关联已断（事件回调漏掉或重连前）：立即反映并重连
                ESP_LOGW(kTag, "WiFi association lost; reconnecting...");
                connected = false;
                data.wifi_connected = false;
                std::snprintf(data.status, sizeof(data.status), "No WiFi");
                publish_shared_data(data);
                esp_wifi_disconnect();
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_wifi_connect();
                connected = wifi_connect_blocking(kWifiTimeoutMs);
                if (connected) {
                    ESP_LOGI(kTag, "WiFi reconnected");
                    ntp_synced = sync_time_from_ntp();
                }
                continue;
            }
        } else {
            data.wifi_connected = false;
            std::snprintf(data.status, sizeof(data.status), "No WiFi");
        }
        publish_shared_data(data);

        const int refresh_ms = get_user_config().refresh_seconds * 1000;
        vTaskDelay(pdMS_TO_TICKS(connected ? refresh_ms : kRetryDelayMs));

        if (!connected) {
            ESP_LOGI(kTag, "Retrying WiFi...");
            // 先断开再连接：DHCP 卡住或上次关联残留时，驱动不会自动重连
            esp_wifi_disconnect();
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_wifi_connect();
            connected = wifi_connect_blocking(kWifiTimeoutMs);
            if (connected) {
                ESP_LOGI(kTag, "WiFi reconnected");
                ntp_synced = sync_time_from_ntp();
            }
        }
    }
}

}  // namespace

// -----------------------------------------------------------------------------
// 公共接口
// -----------------------------------------------------------------------------
esp_err_t bus_eta_start()
{
    if (s_data_mutex == nullptr) {
        s_data_mutex = xSemaphoreCreateMutex();
        if (s_data_mutex == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }
    const BaseType_t created = xTaskCreate(bus_eta_task, "bus_eta", 8192,
                                           nullptr, 5, nullptr);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

bool bus_eta_system_time_valid()
{
    return s_system_time_valid.load();
}

void update_bus_eta(AppState &state)
{
    std::snprintf(state.bus_eta.stop_name, sizeof(state.bus_eta.stop_name),
                  "%s", get_user_config().bus_name);

    if (s_data_mutex == nullptr) {
        state.bus_eta.error = ESP_ERR_INVALID_STATE;
        state.bus_eta.valid = false;
        return;
    }

    if (xSemaphoreTake(s_data_mutex, portMAX_DELAY) == pdTRUE) {
        state.bus_eta.wifi_connected = s_data.wifi_connected;
        state.bus_eta.error = s_data.error;
        state.bus_eta.valid = s_data.data_valid;
        state.bus_eta.bus_count = s_data.bus_count;
        std::snprintf(state.bus_eta.status, sizeof(state.bus_eta.status),
                      "%s", s_data.status);
        for (size_t i = 0; i < s_data.bus_count; ++i) {
            std::snprintf(state.bus_eta.buses[i].route,
                          sizeof(state.bus_eta.buses[i].route), "%s",
                          s_data.routes[i]);
            std::snprintf(state.bus_eta.buses[i].arrival,
                          sizeof(state.bus_eta.buses[i].arrival), "%s",
                          s_data.arrivals[i]);
            state.bus_eta.buses[i].eta_minutes = s_data.eta_minutes[i];
        }
        xSemaphoreGive(s_data_mutex);
    }
}
