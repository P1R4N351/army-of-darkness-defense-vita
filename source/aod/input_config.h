#ifndef AOD_INPUT_CONFIG_H
#define AOD_INPUT_CONFIG_H
#include <stddef.h>
typedef enum { AOD_INPUT_REAR=0, AOD_INPUT_SHOULDERS=1, AOD_INPUT_BOTH=2 } aod_input_mode;
#define AOD_INPUT_CONFIG_MAX_BYTES 64
#define AOD_INPUT_CONFIG_DEFAULT AOD_INPUT_SHOULDERS
int aod_input_config_parse(const char *data,size_t size,aod_input_mode *out);
int aod_input_config_load(const char *path,aod_input_mode *out);
int aod_input_config_save(const char *path,aod_input_mode mode);
#endif