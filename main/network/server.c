#include "server.h"
#include "esp_log.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include <lwip/inet.h>

#define PORT 3452
#define MAX_CONNECTIONS 10

#define TAG "server"

struct client_connection{
	int socket;
	char address_string[20];
};

static int server_socket;
static int next_connection_index = 0;
static client_connection_t connections[MAX_CONNECTIONS] = {0};

status_t server_start() {
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (!server_socket) {
        ESP_LOGE(TAG, "Server socket creation failed");
        return ST_SERVER_INITIALIZATION_FAILED;
    }

    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, NULL, 0);

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr*)&address, sizeof(address)) < 0) {
        ESP_LOGE(TAG, "Server socket bind failed");
        return ST_SERVER_INITIALIZATION_FAILED;
    }

    if (listen(server_socket, MAX_CONNECTIONS) < 0) {
        ESP_LOGE(TAG, "Server socket listening failed");
        return ST_SERVER_INITIALIZATION_FAILED;
    }

    ESP_LOGI(TAG, "Server started listening on port %d", PORT);

    return ST_SUCCESS;
}

int server_accept_connections(SemaphoreHandle_t semaphore) {
	xSemaphoreTake(semaphore, portMAX_DELAY);
	if (next_connection_index >= MAX_CONNECTIONS) {
		xSemaphoreGive(semaphore);
		return -1;
	}

	xSemaphoreGive(semaphore);
	struct sockaddr_in incoming_address;
	int address_length = sizeof(incoming_address);
	int client_socket = accept(server_socket, (struct sockaddr*) &incoming_address, (socklen_t*) &address_length);
	
	if (client_socket < 0) {
		return -1;
	}

	client_connection_t new_connection = {.socket = client_socket };
	strcpy(new_connection.address_string, inet_ntoa(incoming_address.sin_addr));

	xSemaphoreTake(semaphore, portMAX_DELAY);
	connections[next_connection_index++] = new_connection;
	int new_connection_index = next_connection_index - 1;
	xSemaphoreGive(semaphore);

	return new_connection_index;
}

char* server_get_client_address(int client_index, SemaphoreHandle_t semaphore) {
	xSemaphoreTake(semaphore, portMAX_DELAY);
	if (client_index < 0 || client_index >= next_connection_index) {
		xSemaphoreGive(semaphore);
		return "";
	}

	char* address_string = connections[client_index].address_string;
	xSemaphoreGive(semaphore);

	return address_string;
}

int server_get_clients_count(SemaphoreHandle_t semaphore) {
	xSemaphoreTake(semaphore, portMAX_DELAY);
	int count = next_connection_index;
	xSemaphoreGive(semaphore);

	return count;
}

bool server_send_heartbeat(int client_index, SemaphoreHandle_t semaphore) {
	xSemaphoreTake(semaphore, portMAX_DELAY);
	if (client_index < 0 || client_index >= next_connection_index) {
		xSemaphoreGive(semaphore);
		return false;
	}

	client_connection_t client = connections[client_index];

	char heartbeat = 0xDC;
	bool did_send = send(client.socket, &heartbeat, sizeof(heartbeat), 0) > 0;
	xSemaphoreGive(semaphore);

	return did_send;
}

void server_disconnect_client(int client_index, SemaphoreHandle_t semaphore) {
	xSemaphoreTake(semaphore, portMAX_DELAY);
	connections[client_index] = connections[--next_connection_index];
	xSemaphoreGive(semaphore);
}

void server_shutdown() {
}
