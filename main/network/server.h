#include <stdbool.h>

#include "prelude.h"

typedef struct client_connection client_connection_t;

status_t server_start();
void server_shutdown();

int server_accept_connections();

char* server_get_client_address(int client_index);

int server_get_clients_count();
bool server_send_heartbeat(int client_index);
void server_disconnect_client(int client_index);
