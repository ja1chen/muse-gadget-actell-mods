#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct { uint32_t size; } esp_partition_t;
typedef unsigned esp_partition_mmap_handle_t;
#define ESP_OK 0
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_MMAP_DATA 0
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label);
int esp_partition_mmap(const esp_partition_t *, size_t, size_t, int, const void **, esp_partition_mmap_handle_t *);
void esp_partition_munmap(esp_partition_mmap_handle_t);
