/* ws_base64.c
 *
 * Base64 encoding and decoding functions for WebSocket and HTTP API
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "wolfmqtt/mqtt_types.h"

/* ========================================================================= */
/* Base64 Encoding Functions (Original - for WebSocket)                      */
/* ========================================================================= */

#define ENCODE(alphabet,x) ((alphabet)[0x3F & (x)])

static void
encode_raw(const char *alphabet,
           char *dst, size_t length, const unsigned char *src)
{
  const unsigned char *in = src + length;
  char *out = dst + ((length + 2) / 3) * 4;

  unsigned left_over = length % 3;

  if (left_over)
    {
      in -= left_over;
      *--out = '=';
      switch(left_over)
        {
        case 1:
          *--out = '=';
          *--out = ENCODE(alphabet, (in[0] << 4));
          break;
          
        case 2:
          *--out = ENCODE(alphabet, (in[1] << 2));
          *--out = ENCODE(alphabet, ((in[0] << 4) | (in[1] >> 4)));
          break;

        default:
          abort();
        }
      *--out = ENCODE(alphabet, (in[0] >> 2));
    }
  
  while (in > src)
    {
      in -= 3;
      *--out = ENCODE(alphabet, (in[2]));
      *--out = ENCODE(alphabet, ((in[1] << 2) | (in[2] >> 6)));
      *--out = ENCODE(alphabet, ((in[0] << 4) | (in[1] >> 4)));
      *--out = ENCODE(alphabet, (in[0] >> 2));
    }
  assert(in == src);
  assert(out == dst);
}

static const char base64_encode_table[64] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "abcdefghijklmnopqrstuvwxyz"
  "0123456789+/";

void
base64_encode_raw(char *dst, size_t length, const unsigned char *src)
{
  encode_raw(base64_encode_table, dst, length, src);
}

/* ========================================================================= */
/* Base64 Decoding Functions (New - for HTTP API binary payload)            */
/* ========================================================================= */

/* Base64 decode table - maps ASCII to 6-bit values, -1 for invalid */
static const signed char base64_decode_table[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
    -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
    -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

/* Decode base64 string to binary data
 * Returns: number of bytes decoded, or -1 on error
 */
int ws_base64_decode(const char* src, int src_len, byte* dst, int dst_max)
{
    int i, j;
    int decoded_len;
    
    if (!src || !dst || src_len <= 0 || dst_max <= 0) {
        return -1;
    }
    
    /* Calculate expected output size */
    int padding = 0;
    if (src[src_len - 1] == '=') padding++;
    if (src_len > 1 && src[src_len - 2] == '=') padding++;
    decoded_len = (src_len * 3) / 4 - padding;
    
    if (decoded_len > dst_max) {
        return -1; /* Output buffer too small */
    }
    
    /* Process 4 characters at a time */
    for (i = 0, j = 0; i < src_len; ) {
        unsigned char a, b, c, d;
        
        /* Get 4 base64 characters, skip whitespace */
        do { a = (i < src_len) ? (unsigned char)src[i++] : '='; } while (isspace(a));
        do { b = (i < src_len) ? (unsigned char)src[i++] : '='; } while (isspace(b));
        do { c = (i < src_len) ? (unsigned char)src[i++] : '='; } while (isspace(c));
        do { d = (i < src_len) ? (unsigned char)src[i++] : '='; } while (isspace(d));
        
        /* Convert to 6-bit values */
        int va = (a < 128) ? base64_decode_table[a] : -1;
        int vb = (b < 128) ? base64_decode_table[b] : -1;
        int vc = (c < 128) ? base64_decode_table[c] : -1;
        int vd = (d < 128) ? base64_decode_table[d] : -1;
        
        /* Check for invalid characters */
        if (va < 0 || vb < 0) {
            return -1; /* Invalid base64 character */
        }
        
        /* First byte */
        if (j < dst_max) {
            dst[j++] = (unsigned char)((va << 2) | (vb >> 4));
        }
        
        /* Second byte */
        if (vc >= 0 && j < dst_max) {
            dst[j++] = (unsigned char)(((vb & 0x0F) << 4) | (vc >> 2));
        }
        
        /* Third byte */
        if (vd >= 0 && j < dst_max) {
            dst[j++] = (unsigned char)(((vc & 0x03) << 6) | vd);
        }
    }
    
    return j;
}
