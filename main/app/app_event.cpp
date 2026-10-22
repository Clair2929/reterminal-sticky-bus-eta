#include "app_event.h"

#include "freertos/queue.h"

namespace {

constexpr UBaseType_t kQueueLength = 8;
QueueHandle_t s_event_queue = nullptr;

}  // namespace

esp_err_t app_event_init()
{
    if (s_event_queue != nullptr) {
        return ESP_OK;
    }

    s_event_queue = xQueueCreate(kQueueLength, sizeof(AppEvent));
    return s_event_queue != nullptr ? ESP_OK : ESP_ERR_NO_MEM;
}

bool app_event_post(AppEvent event)
{
    return s_event_queue != nullptr &&
           xQueueSend(s_event_queue, &event, 0) == pdTRUE;
}

bool app_event_wait(AppEvent &event, TickType_t timeout)
{
    return s_event_queue != nullptr &&
           xQueueReceive(s_event_queue, &event, timeout) == pdTRUE;
}
