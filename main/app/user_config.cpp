#include "user_config.h"

#include <cstdio>
#include <cstring>

#include "bus_config.h"
#include "esp_log.h"
#include "nvs.h"

namespace {

constexpr char kTag[] = "user_config";
constexpr char kNvsNamespace[] = "config";

// 从 NVS 读字符串到目标缓冲区；成功返回 true。
bool read_nvs_str(nvs_handle_t handle, const char *key, char *dst,
                  size_t dst_size)
{
    size_t len = 0;
    if (nvs_get_str(handle, key, nullptr, &len) != ESP_OK || len == 0) {
        return false;
    }
    // 留一个字节放 '\0'
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    if (nvs_get_str(handle, key, dst, &len) != ESP_OK) {
        return false;
    }
    return dst[0] != '\0';
}

// 解析 busProvider 字符串：含 hong/kmb 视为香港，其余视为新加坡
bool parse_provider_is_singapore(const char *provider)
{
    char buf[24] = {};
    std::snprintf(buf, sizeof(buf), "%s", provider ? provider : "");
    for (char &c : buf) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return std::strstr(buf, "hong") == nullptr &&
           std::strstr(buf, "kmb") == nullptr;
}

}  // namespace

const UserConfig &get_user_config()
{
    // 函数内静态变量：首次调用时初始化一次，之后直接复用
    static const UserConfig config = []() {
        UserConfig cfg;

        // 1) 编译期默认值（bus_config.h，占位符；本地开发可自行填写）
        std::snprintf(cfg.wifi_ssid, sizeof(cfg.wifi_ssid), "%s",
                      BUS_WIFI_SSID);
        std::snprintf(cfg.wifi_password, sizeof(cfg.wifi_password), "%s",
                      BUS_WIFI_PASSWORD);
        cfg.use_singapore = (BUS_USE_SINGAPORE != 0);
        std::snprintf(cfg.bus_id, sizeof(cfg.bus_id), "%s", BUS_STOP_ID);
        std::snprintf(cfg.bus_name, sizeof(cfg.bus_name), "%s", BUS_STOP_NAME);
        cfg.refresh_seconds = BUS_REFRESH_SECONDS;

        // 2) NVS 覆盖（烧录页在烧录前写入）
        nvs_handle_t handle = 0;
        if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
            ESP_LOGI(kTag,
                     "No NVS 'config' namespace; using compile-time defaults");
            return cfg;
        }

        read_nvs_str(handle, "wifiSsid", cfg.wifi_ssid, sizeof(cfg.wifi_ssid));
        read_nvs_str(handle, "wifiPassword", cfg.wifi_password,
                     sizeof(cfg.wifi_password));
        char provider[24] = {};
        if (read_nvs_str(handle, "busProvider", provider, sizeof(provider))) {
            cfg.use_singapore = parse_provider_is_singapore(provider);
        }
        read_nvs_str(handle, "busId", cfg.bus_id, sizeof(cfg.bus_id));
        read_nvs_str(handle, "busName", cfg.bus_name, sizeof(cfg.bus_name));

        // 刷新间隔（秒）：i32 键 refreshSeconds，与 HelloWorld.ino 的
        // Preferences.getInt("refreshSeconds", 120) 对齐。越界值回退到
        // 编译期默认，避免 Web Flasher 误写导致后台任务循环异常。
        int32_t refresh_seconds = 0;
        if (nvs_get_i32(handle, "refreshSeconds", &refresh_seconds) == ESP_OK &&
            refresh_seconds >= 30 && refresh_seconds <= 3600) {
            cfg.refresh_seconds = static_cast<int>(refresh_seconds);
        }

        nvs_close(handle);

        ESP_LOGI(kTag,
                 "User config: wifi_configured=%s provider=%s bus_id=%s bus_name=%s",
                 cfg.wifi_ssid[0] != '\0' ? "yes" : "no",
                 cfg.use_singapore ? "singapore" : "hongkongkmb",
                 cfg.bus_id, cfg.bus_name);
        return cfg;
    }();

    return config;
}
