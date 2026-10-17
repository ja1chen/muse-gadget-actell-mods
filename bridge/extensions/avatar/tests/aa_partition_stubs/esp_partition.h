#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_MMAP_DATA 0
typedef struct {int type;uint32_t address;uint32_t size;} esp_partition_t;
typedef unsigned esp_partition_mmap_handle_t;
esp_err_t esp_partition_mmap(const esp_partition_t*,size_t,size_t,int,const void**,esp_partition_mmap_handle_t*);
void esp_partition_munmap(esp_partition_mmap_handle_t);
