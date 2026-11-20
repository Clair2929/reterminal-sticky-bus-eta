#include "stop_resolver.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "stop_resolver";
constexpr int kHttpTimeoutMs = 15000;
// 目录文件较大且部分边缘链路不稳定，单次 0 字节/断连时重试几次退避。
constexpr int kHttpMaxAttempts = 3;
constexpr int kHttpRetryBackoffMs = 1500;

// 新加坡站点目录（BusRouter SG 官方公开静态数据，作者 cheeaun/sgbusdata）。
//   {"<站码>":[经度,纬度,"站名","路名"], ...}
// data.busrouter.sg 走 CloudFront + ECDSA 证书，设备端 mbedtls 握手时好时坏；
// 同文件在作者 GitHub 仓库的 CDN 镜像（jsDelivr/raw）走 RSA 主流 CDN，
// 与 arrivelah2 一致、设备端稳定。按优先级依次回退，多源兜底。
constexpr char kSgStopsUrls[][80] = {
    "https://cdn.jsdelivr.net/gh/cheeaun/sgbusdata@main/data/v1/stops.min.json",
    "https://raw.githubusercontent.com/cheeaun/sgbusdata/main/data/v1/stops.min.json",
    "https://data.busrouter.sg/v1/stops.min.json",
};
constexpr size_t kSgStopsMaxBytes = 400 * 1024;

// 香港 KMB 站点 API（全量列表实测 1193542 字节；单站为 .../stop/<站码>）
constexpr char kKmbStopUrl[] =
    "https://data.etabus.gov.hk/v1/transport/kmb/stop";
constexpr size_t kKmbStopsMaxBytes = 1400 * 1024;
constexpr size_t kKmbSingleMaxBytes = 16 * 1024;

// KMB 站码为 16 位十六进制，+'\0' 需要 17 字节；统一放宽到 24。
constexpr size_t kIdBufferSize = 24;
constexpr size_t kNameBufferSize = 64;

struct Download {
    char *data = nullptr;
    size_t len = 0;
    size_t cap = 0;
};

// 下载 URL 全文到 PSRAM 缓冲（CONFIG_SPIRAM_USE_MALLOC 已启用，
// 16KB 以上的 malloc 自动落在 PSRAM）。成功返回 true，data 以 '\0' 结尾。
// 目录文件较大且部分边缘链路会在握手后直接断连，失败时按退避重试几次。
bool http_get_all(const char *url, size_t max_bytes, Download &out)
{
    out.data = static_cast<char *>(
        heap_caps_malloc(max_bytes + 1, MALLOC_CAP_SPIRAM));
    if (out.data == nullptr) {
        ESP_LOGE(kTag, "No PSRAM for %u-byte download",
                 static_cast<unsigned>(max_bytes));
        return false;
    }
    out.cap = max_bytes;
    out.len = 0;

    // 大文件下载期间临时关闭 WiFi modem sleep，避免 TCP 吞吐过低导致
    // 服务端在传输中途重置连接；结束后恢复原省电策略。
    wifi_ps_type_t previous_ps = WIFI_PS_MIN_MODEM;
    esp_wifi_get_ps(&previous_ps);
    esp_wifi_set_ps(WIFI_PS_NONE);

    for (int attempt = 1; attempt <= kHttpMaxAttempts; ++attempt) {
        esp_http_client_config_t config = {};
        config.url = url;
        config.method = HTTP_METHOD_GET;
        config.timeout_ms = kHttpTimeoutMs;
        config.crt_bundle_attach = esp_crt_bundle_attach;

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == nullptr) {
            break;
        }

        out.len = 0;
        bool ok = false;
        int read_err = 0;
        int64_t content_length = -1;
        const esp_err_t open_result = esp_http_client_open(client, 0);
        if (open_result == ESP_OK) {
            content_length = esp_http_client_fetch_headers(client);
            const int status = esp_http_client_get_status_code(client);
            ESP_LOGI(kTag, "HTTP open OK: status=%d content_length=%lld",
                     status, static_cast<long long>(content_length));
            while (out.len < max_bytes) {
                const int n = esp_http_client_read(
                    client, out.data + out.len,
                    static_cast<int>(max_bytes - out.len));
                if (n < 0) {
                    read_err = n;
                    break;
                }
                if (n == 0) {
                    break;  // EOF
                }
                out.len += static_cast<size_t>(n);
            }
            if (read_err != 0) {
                ESP_LOGW(kTag, "read error %d (%u bytes read)", read_err,
                         static_cast<unsigned>(out.len));
            }
            // 已知 content-length 时必须读满，否则视为传输被中断，不能把
            // 半截 JSON 交给上层解析。
            ok = status == 200 &&
                 (content_length > 0 ? out.len == static_cast<size_t>(content_length)
                                     : out.len > 0);
        } else {
            ESP_LOGW(kTag, "HTTP open failed: %s",
                     esp_err_to_name(open_result));
        }

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        if (ok) {
            out.data[out.len] = '\0';
            ESP_LOGI(kTag, "Downloaded %u bytes from %s", out.len, url);
            esp_wifi_set_ps(previous_ps);
            return true;
        }

        ESP_LOGW(kTag, "fetch attempt %d/%d failed (%u bytes)",
                 attempt, kHttpMaxAttempts, static_cast<unsigned>(out.len));
        if (attempt < kHttpMaxAttempts) {
            vTaskDelay(pdMS_TO_TICKS(kHttpRetryBackoffMs * attempt));
        }
    }

    esp_wifi_set_ps(previous_ps);
    heap_caps_free(out.data);
    out.data = nullptr;
    out.len = 0;
    return false;
}

// 大小写不敏感比较（英文站名匹配不区分大小写）
bool ci_equal(const char *a, const char *b)
{
    if (a == nullptr || b == nullptr) {
        return false;
    }
    while (*a != '\0' && *b != '\0') {
        const char ca = static_cast<char>(
            std::tolower(static_cast<unsigned char>(*a)));
        const char cb = static_cast<char>(
            std::tolower(static_cast<unsigned char>(*b)));
        if (ca != cb) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

// 去除首尾空白（虚拟键盘可输入空格，会破坏 URL 与站名匹配）
void trim_whitespace(char *s)
{
    if (s == nullptr) {
        return;
    }
    char *start = s;
    while (*start == ' ' || *start == '\t') {
        ++start;
    }
    char *end = start + std::strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
        --end;
    }
    *end = '\0';
    if (start != s) {
        std::memmove(s, start, static_cast<size_t>(end - start + 1));
    }
}

// -----------------------------------------------------------------------------
// 新加坡：stops.min.json
// -----------------------------------------------------------------------------

// 站码 -> 站名（数组第 3 个元素）
const char *sg_name_for_id(const cJSON *root, const char *id)
{
    const cJSON *item = cJSON_GetObjectItem(root, id);
    if (!cJSON_IsArray(item)) {
        return nullptr;
    }
    const cJSON *name = cJSON_GetArrayItem(item, 2);
    if (cJSON_IsString(name)) {
        return name->valuestring;
    }
    return nullptr;
}

// 站名 -> 站码；返回匹配数，唯一匹配时回填 out_id。
int sg_find_id_by_name(const cJSON *root, const char *name,
                       char *out_id, size_t id_cap)
{
    int matches = 0;
    for (const cJSON *item = root->child; item != nullptr; item = item->next) {
        if (item->string == nullptr || !cJSON_IsArray(item)) {
            continue;
        }
        const cJSON *name_json = cJSON_GetArrayItem(item, 2);
        if (!cJSON_IsString(name_json)) {
            continue;
        }
        if (!ci_equal(name_json->valuestring, name)) {
            continue;
        }
        ++matches;
        if (matches == 1 && out_id != nullptr) {
            std::snprintf(out_id, id_cap, "%s", item->string);
        }
        if (matches >= 2) {
            break;
        }
    }
    return matches;
}

// -----------------------------------------------------------------------------
// 香港 KMB
// -----------------------------------------------------------------------------

// 单站响应：{"type":"Stop","data":{"stop":"...","name_en":"...","name_tc":"..."}}
// 取显示名（name_en，回退 name_tc）。
bool kmb_name_from_single(const char *json, char *out_name, size_t name_cap)
{
    cJSON *root = cJSON_Parse(json);
    if (root == nullptr) {
        return false;
    }
    const cJSON *data = cJSON_GetObjectItem(root, "data");
    const cJSON *name_en =
        (data != nullptr) ? cJSON_GetObjectItem(data, "name_en") : nullptr;
    const cJSON *name_tc =
        (data != nullptr) ? cJSON_GetObjectItem(data, "name_tc") : nullptr;
    bool ok = false;
    if (cJSON_IsString(name_en) && name_en->valuestring[0] != '\0') {
        std::snprintf(out_name, name_cap, "%s", name_en->valuestring);
        ok = true;
    } else if (cJSON_IsString(name_tc) && name_tc->valuestring[0] != '\0') {
        std::snprintf(out_name, name_cap, "%s", name_tc->valuestring);
        ok = true;
    }
    cJSON_Delete(root);
    return ok;
}

// 全量列表：{"type":"StopList","data":[{"stop":"...","name_en":"...","name_tc":"..."},...]}
// 按 name_en / name_tc 反查站码；返回匹配数，唯一匹配时回填站码与标准显示名。
int kmb_find_id_by_name(const char *json, const char *name,
                        char *out_id, size_t id_cap,
                        char *out_name, size_t name_cap)
{
    cJSON *root = cJSON_Parse(json);
    if (root == nullptr) {
        return 0;
    }
    const cJSON *data = cJSON_GetObjectItem(root, "data");
    int matches = 0;
    if (cJSON_IsArray(data)) {
        cJSON *item = nullptr;
        cJSON_ArrayForEach(item, data)
        {
            const cJSON *stop = cJSON_GetObjectItem(item, "stop");
            const cJSON *name_en = cJSON_GetObjectItem(item, "name_en");
            const cJSON *name_tc = cJSON_GetObjectItem(item, "name_tc");
            if (!cJSON_IsString(stop)) {
                continue;
            }
            const char *hit_name = nullptr;
            if (cJSON_IsString(name_en) &&
                ci_equal(name_en->valuestring, name)) {
                hit_name = name_en->valuestring;
            } else if (cJSON_IsString(name_tc) &&
                       ci_equal(name_tc->valuestring, name)) {
                hit_name = name_tc->valuestring;
            }
            if (hit_name == nullptr) {
                continue;
            }
            ++matches;
            if (matches == 1) {
                if (out_id != nullptr) {
                    std::snprintf(out_id, id_cap, "%s", stop->valuestring);
                }
                if (out_name != nullptr) {
                    std::snprintf(out_name, name_cap, "%s", hit_name);
                }
            }
            if (matches >= 2) {
                break;
            }
        }
    }
    cJSON_Delete(root);
    return matches;
}

}  // namespace

// -----------------------------------------------------------------------------
// 公共入口
// -----------------------------------------------------------------------------
StopResolveResult resolve_stop(bool use_singapore,
                               char *bus_id, size_t id_cap,
                               char *bus_name, size_t name_cap)
{
    if (bus_id != nullptr) {
        trim_whitespace(bus_id);
    }
    if (bus_name != nullptr) {
        trim_whitespace(bus_name);
    }
    const bool has_id = bus_id != nullptr && bus_id[0] != '\0';
    const bool has_name = bus_name != nullptr && bus_name[0] != '\0';
    if (!has_id && !has_name) {
        return StopResolveResult::NameNotFound;
    }

    // ---------- 新加坡 ----------
    if (use_singapore) {
        Download dl = {};
        ESP_LOGI(kTag, "sg resolve start: heap=%u largest_internal=%u "
                       "largest_psram=%u",
                 static_cast<unsigned>(esp_get_free_heap_size()),
                 static_cast<unsigned>(
                     heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(
                     MALLOC_CAP_SPIRAM)));
        bool downloaded = false;
        for (const char *url : kSgStopsUrls) {
            ESP_LOGI(kTag, "sg trying source: %s", url);
            if (http_get_all(url, kSgStopsMaxBytes, dl)) {
                downloaded = true;
                break;
            }
        }
        if (!downloaded) {
            return StopResolveResult::NetworkError;
        }
        cJSON *root = cJSON_Parse(dl.data);
        ESP_LOGI(kTag, "sg json parse %s: len=%u heap_after=%u",
                 root != nullptr ? "OK" : "FAILED",
                 static_cast<unsigned>(dl.len),
                 static_cast<unsigned>(esp_get_free_heap_size()));
        if (root == nullptr) {
            heap_caps_free(dl.data);
            return StopResolveResult::NetworkError;
        }

        StopResolveResult result = StopResolveResult::NameNotFound;
        if (has_id) {
            const char *resolved = sg_name_for_id(root, bus_id);
            if (resolved != nullptr && resolved[0] != '\0') {
                // ID 优先：用 ID 查出的站名覆盖手输 name
                std::snprintf(bus_name, name_cap, "%s", resolved);
                result = StopResolveResult::Ok;
            } else if (has_name) {
                // ID 查无此站：回退用 name 反查
                char found_id[kIdBufferSize] = {};
                const int m = sg_find_id_by_name(root, bus_name, found_id,
                                                 sizeof(found_id));
                if (m == 1) {
                    std::snprintf(bus_id, id_cap, "%s", found_id);
                    result = StopResolveResult::Ok;
                } else if (m >= 2) {
                    result = StopResolveResult::Ambiguous;
                } else {
                    result = StopResolveResult::NameNotFound;
                }
            } else {
                result = StopResolveResult::IdNotFound;
            }
        } else {
            char found_id[kIdBufferSize] = {};
            const int m = sg_find_id_by_name(root, bus_name, found_id,
                                             sizeof(found_id));
            if (m == 1) {
                std::snprintf(bus_id, id_cap, "%s", found_id);
                result = StopResolveResult::Ok;
            } else if (m >= 2) {
                result = StopResolveResult::Ambiguous;
            } else {
                result = StopResolveResult::NameNotFound;
            }
        }
        cJSON_Delete(root);
        heap_caps_free(dl.data);
        return result;
    }

    // ---------- 香港 KMB ----------
    if (has_id) {
        // 轻量单站查询：ID -> 站名
        char url[160] = {};
        std::snprintf(url, sizeof(url), "%s/%s", kKmbStopUrl, bus_id);
        Download dl = {};
        if (http_get_all(url, kKmbSingleMaxBytes, dl)) {
            char resolved_name[kNameBufferSize] = {};
            if (kmb_name_from_single(dl.data, resolved_name,
                                     sizeof(resolved_name))) {
                std::snprintf(bus_name, name_cap, "%s", resolved_name);
                heap_caps_free(dl.data);
                return StopResolveResult::Ok;
            }
            heap_caps_free(dl.data);
            // 单站 data 为空：站码无效，继续走 name 回退
        } else if (!has_name) {
            return StopResolveResult::NetworkError;
        }
        // ID 无效且填了 name：回退用 name 反查
        if (has_name) {
            Download list = {};
            if (!http_get_all(kKmbStopUrl, kKmbStopsMaxBytes, list)) {
                return StopResolveResult::NetworkError;
            }
            char found_id[kIdBufferSize] = {};
            char found_name[kNameBufferSize] = {};
            const int m = kmb_find_id_by_name(
                list.data, bus_name, found_id, sizeof(found_id),
                found_name, sizeof(found_name));
            heap_caps_free(list.data);
            if (m == 1) {
                std::snprintf(bus_id, id_cap, "%s", found_id);
                std::snprintf(bus_name, name_cap, "%s", found_name);
                return StopResolveResult::Ok;
            }
            if (m >= 2) {
                return StopResolveResult::Ambiguous;
            }
            return StopResolveResult::NameNotFound;
        }
        return StopResolveResult::IdNotFound;
    }

    // 只填了 name：全量列表反查 ID
    Download list = {};
    if (!http_get_all(kKmbStopUrl, kKmbStopsMaxBytes, list)) {
        return StopResolveResult::NetworkError;
    }
    char found_id[kIdBufferSize] = {};
    char found_name[kNameBufferSize] = {};
    const int m = kmb_find_id_by_name(
        list.data, bus_name, found_id, sizeof(found_id),
        found_name, sizeof(found_name));
    heap_caps_free(list.data);
    if (m == 1) {
        std::snprintf(bus_id, id_cap, "%s", found_id);
        std::snprintf(bus_name, name_cap, "%s", found_name);
        return StopResolveResult::Ok;
    }
    if (m >= 2) {
        return StopResolveResult::Ambiguous;
    }
    return StopResolveResult::NameNotFound;
}
