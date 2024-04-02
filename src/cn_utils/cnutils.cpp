#include <cmath>
#include <stdint.h>
#include <string>
#include <algorithm>
#include "cryptonote_basic/cryptonote_basic.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_basic/blobdatatype.h"
#include "crypto/crypto.h"
#include "crypto/hash.h"
#include "common/base58.h"
#include "serialization/binary_utils.h"
#include "cnutils.h"


uint32_t convert_blob(const char *blob, size_t len, char *out) {
    std::string input = std::string(blob, len);
    cryptonote::blobdata output = "";

    cryptonote::block b = AUTO_VAL_INIT(b);
    if (!cryptonote::parse_and_validate_block_from_blob(input, b)) {
        return 0;
    }
    output = cryptonote::get_block_hashing_blob(b);
    output.copy(out, output.length(), 0);
    return output.length();
}

bool validate_address(const char *addr, size_t len) {
    std::string input = std::string(addr, len);
    std::string output = "";
    uint64_t prefix;
    return tools::base58::decode_addr(addr, prefix, output);
}

//------------------------------------------------------------------------------------------------------------------------------
// equivalent of strstr, but with arbitrary bytes (ie, NULs)
// This does not differentiate between "not found" and "found at offset 0"
uint64_t slow_memmem(const void* start_buff, size_t buflen,const void* pat,size_t patlen)
{
    const void* buf = start_buff;
    const void* end=(const char*)buf+buflen;
    if (patlen > buflen || patlen == 0) return 0;
    while(buflen>0 && (buf=memchr(buf,((const char*)pat)[0],buflen-patlen+1)))
    {
        if(memcmp(buf,pat,patlen)==0)
        return (const char*)buf - (const char*)start_buff;
        buf=(const char*)buf+1;
        buflen = (const char*)end - (const char*)buf;
    }
    return 0;
}