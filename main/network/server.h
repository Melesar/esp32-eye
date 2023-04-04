#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>

#include "prelude.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

typedef struct client_connection client_connection_t;

status_t server_start();
void server_shutdown();

int server_accept_connections(SemaphoreHandle_t semaphore);

char* server_get_client_address(int client_index, SemaphoreHandle_t semaphore);

int server_get_clients_count(SemaphoreHandle_t semaphore);
bool server_send_heartbeat(int client_index, SemaphoreHandle_t semaphore);
void server_disconnect_client(int client_index, SemaphoreHandle_t semaphore);

#endif
