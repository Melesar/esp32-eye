#include "network/wifi.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/projdefs.h>
#include <freertos/task.h>

#include <esp_err.h>
#include <esp_event.h>
#include <esp_event_base.h>
#include <esp_log.h>
#include <esp_wifi.h>

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define MAX_RETRY_COUNT 5

static char *TAG = "wifi-app";

static int s_retry_num = 0;
static EventGroupHandle_t s_wifi_event_group;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < MAX_RETRY_COUNT) {
      esp_wifi_connect();
      s_retry_num += 1;
    } else {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got ip: %d.%d.%d.%d", IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

status_t wifi_init() {
  ESP_LOGI(TAG, "Initializing wifi module");
  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  esp_netif_create_default_wifi_sta();

  wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

  esp_event_handler_instance_t wifi_evt_handler;
  esp_event_handler_instance_t ip_evt_handler;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
      &wifi_evt_handler));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
      &ip_evt_handler));
  wifi_config_t wifi_config = {
	  .sta = {
		  .ssid = CONFIG_ESP_WIFI_SSID,
		  .password = CONFIG_ESP_WIFI_PASSWORD,
		  .threshold.authmode = WIFI_AUTH_WPA2_PSK,
	  }};


  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE, portMAX_DELAY);

  status_t status;
  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "Sucessfully connected to the WiFi");
    status = ST_SUCCESS;
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGI(TAG, "Connection failed");
    status = ST_WIFI_INITIALIZATION_FAILED;
  } else {
    ESP_LOGI(TAG, "Unexpected event hapenned");
    status = ST_WIFI_INITIALIZATION_FAILED;
  }

  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
      IP_EVENT, IP_EVENT_STA_GOT_IP, ip_evt_handler));
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister(
      WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evt_handler));
  vEventGroupDelete(s_wifi_event_group);

  return status;
}
