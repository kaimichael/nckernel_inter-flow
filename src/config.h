#ifndef _NCK_CONFIG_H_
#define _NCK_CONFIG_H_

#ifdef __cplusplus
#include <cstdint>
#include <cstdio>
#else
#include <stdint.h>
#include <stdio.h>
#endif

#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

int nck_parse_u32(uint32_t *value, const char *text);
int nck_parse_s32(int32_t *value, const char *text);
int nck_parse_u16(uint16_t *value, const char *text);
int nck_parse_u8(uint8_t *value, const char *text);
int nck_parse_timeval(struct timeval *value, const char *text);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _NCK_CONFIG_H_ */
