#include "task_monitor.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#define TAG "TASK_STATS"

char task_list_buffer[1024];
char task_runtime_buffer[1024];

void print_task_stats(void) {
    ESP_LOGI(TAG, "=== Task List ===");
    vTaskList(task_list_buffer);
    ESP_LOGI(TAG, "\nName          State  Prio Stack Num\n%s", task_list_buffer);

    ESP_LOGI(TAG, "=== Runtime Stats ===");
    vTaskGetRunTimeStats(task_runtime_buffer);
    ESP_LOGI(TAG, "\nTask          Time    %%\n%s", task_runtime_buffer);
}
