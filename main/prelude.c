#include "prelude.h"

#define ERROR_BUFFER_SIZE 128

static char error_name[ERROR_BUFFER_SIZE];

const char* get_error_name(esp_err_t errorCode) {
	esp_err_to_name_r(errorCode, error_name, ERROR_BUFFER_SIZE);
	return error_name;
}
