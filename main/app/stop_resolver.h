#pragma once

#include <cstddef>

// 保存 Bus Setup 前的在线站名/ID 解析结果。
enum class StopResolveResult {
    Ok,           // 成功：bus_id / bus_name 已按正确的一栏解析
    IdNotFound,   // 填的 ID 查无此站，且 name 也未能反查到 ID
    NameNotFound, // 填的 name 查无此站
    Ambiguous,    // name 匹配到多个站，需改用 ID
    NetworkError, // WiFi/HTTP failed; input buffers remain unchanged
};

// 解析公交站信息（保存前调用；需要 WiFi/网络）。
// 入参为当前输入（bus_id / bus_name 至少一个非空），出参为解析后的结果：
// - 若 bus_id 非空：优先用 ID 查询站名并覆盖 bus_name（ID 优先，
//   不显示用户手输 name）；ID 无效且 name 非空时回退用 name 反查 ID。
// - 若 bus_id 为空且 bus_name 非空：用站名反查 ID，唯一匹配则补全。
//
// 数据源：
//   新加坡  https://data.busrouter.sg/v1/stops.min.json
//   香港    https://data.etabus.gov.hk/v1/transport/kmb/stop[/<站码>]
StopResolveResult resolve_stop(bool use_singapore,
                               char *bus_id, size_t id_cap,
                               char *bus_name, size_t name_cap);
