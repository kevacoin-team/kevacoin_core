#pragma once

#include <cryptonote_basic/cryptonote_basic.h>
#include <cryptonote_basic/cryptonote_format_utils.h>

uint32_t convert_blob(const char *blob, size_t len, char *out);
bool validate_address(const char *addr, size_t len);
uint64_t slow_memmem(const void* start_buff, size_t buflen,const void* pat,size_t patlen);