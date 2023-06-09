#include "app.h"
#include "network/server.h"
#include "network/wifi.h"
#include "network/tasks.h"
#include "camera/camera.h"

#include <esp_log.h>
#include <esp_camera.h>
#include <nvs_flash.h>

#define CHECK_STATUS(status) if (ST_SUCCESS != status) { return status; }

static char* TAG = "app";

static task_sync_t task_sync;

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

	task_sync.event_group = xEventGroupCreate();
	task_sync.mutex = xSemaphoreCreateMutex();
	task_sync.image_produce_queue = xQueueCreate(1, sizeof(camera_fb_t*));
	task_sync.image_recycle_queue = xQueueCreate(1, sizeof(camera_fb_t*));

	xTaskCreatePinnedToCore(task_send_camera_image, "Send image", 4096, &task_sync, PRIORITY_HIGH, NULL, 0);
	xTaskCreatePinnedToCore(task_accept_new_clients, "Accept new clients", 4096, &task_sync, PRIORITY_NORMAL, NULL, 0);
	xTaskCreatePinnedToCore(task_handle_requests, "Handle requests", 4096, &task_sync, PRIORITY_NORMAL, NULL, 0);
	xTaskCreatePinnedToCore(task_send_broadcasts, "Broadcasts", 4096, &task_sync, PRIORITY_LOW, NULL, 0);

	xTaskCreatePinnedToCore(task_capture_camera_image, "Capture image", 4096, &task_sync, PRIORITY_HIGH, NULL, 1);
	xTaskCreatePinnedToCore(task_recycle_camera_image, "Recycle image", 4096, &task_sync, PRIORITY_HIGH, NULL, 1);
}
