#include "app.h"
#include "network/server.h"
#include "network/wifi.h"
#include "network/tasks.h"
#include "camera/camera.h"

#include <esp_log.h>
#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#define CHECK_STATUS(status) if (ST_SUCCESS != status) { return status; }

static char* TAG = "app";

static EventGroupHandle_t event_group;

static status_t nvs_init() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
	return ST_SUCCESS;
}

status_t app_init() {
	status_t status = nvs_init();
	CHECK_STATUS(status);

	status = wifi_init();
	CHECK_STATUS(status);

	status = camera_init();
	CHECK_STATUS(status);

	return status;
}

void app_run() {
	server_start();

	event_group = xEventGroupCreate();

	xTaskCreate(task_handle_network_messages, "Handle messages", 4096, event_group, PRIORITY_HIGH, NULL);
	xTaskCreate(task_send_heartbeats, "Heartbeats", 4096, event_group, PRIORITY_NORMAL, NULL);
}
