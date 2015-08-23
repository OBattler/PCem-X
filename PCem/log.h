#pragma once

#ifdef __cplusplus
#include <cstdio>
#include <string>
extern "C" {
#endif
void log_print(char *component, uint8_t level, char *msg, ...);
#ifdef __cplusplus
}
#endif

#define LOG_LEVEL_ERROR		0x01
#define LOG_LEVEL_WARNING	0x02
#define LOG_LEVEL_DEBUG		0x04
#define LOG_LEVEL_VERBOSE	0x08
#define LOG_LEVEL_INFO		0x10

#ifdef __cplusplus
#define to_string std::to_string
#endif