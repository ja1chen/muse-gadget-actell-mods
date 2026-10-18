/* Generated raw asset views; little-endian ESP32/host. No pixel array embedded. */
#pragma once
#include <stdint.h>
typedef struct {uint32_t offset,length;int16_t x,y;uint16_t w,h;} aa_record_t;
#define aa_records ((const aa_record_t*)(aa_file+2146180u))
#define AA_RECORDS_COUNT 6836u
#define aa_head_ids ((const uint16_t*)(aa_file+2255556u))
#define AA_HEAD_IDS_COUNT 68u
#define aa_ellipse_sizes ((const uint8_t(*)[2])(aa_file+2255692u))
#define AA_ELLIPSE_SIZES_COUNT 56u
#define aa_ellipse_ids ((const uint16_t(*)[16])(aa_file+2255804u))
#define AA_ELLIPSE_IDS_COUNT 56u
#define aa_eye_ids ((const uint16_t(*)[16])(aa_file+2257596u))
#define AA_EYE_IDS_COUNT 363u
#define aa_phone_ids ((const uint16_t(*)[16])(aa_file+2269212u))
#define AA_PHONE_IDS_COUNT 2u
#define aa_brow_ids ((const uint16_t*)(aa_file+2269276u))
#define AA_BROW_IDS_COUNT 16u
#define aa_mouth_ids ((const uint16_t*)(aa_file+2269308u))
#define AA_MOUTH_IDS_COUNT 16u
#define AA_RAW_BYTES 2269276u
#define AA_RAW_CRC32 0x49d6b8b0u
#define AA_PIXEL_BYTES 2146116u
