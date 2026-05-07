#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* 简化的 SHA-1 实现用于调试 */
#define SHA1_DIGEST_SIZE 20

typedef struct {
    uint32_t h0, h1, h2, h3, h4;
    uint8_t buffer[64];
    uint32_t buffer_len;
    uint64_t total_len;
} sha1_ctx;

static uint32_t left_rotate(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

void sha1_init(sha1_ctx *ctx) {
    ctx->h0 = 0x67452301;
    ctx->h1 = 0xEFCDAB89;
    ctx->h2 = 0x98BADCFE;
    ctx->h3 = 0x10325476;
    ctx->h4 = 0xC3D2E1F0;
    ctx->buffer_len = 0;
    ctx->total_len = 0;
}

static void sha1_process_block(sha1_ctx *ctx) {
    uint32_t w[80];
    uint32_t a, b, c, d, e;
    int i;
    
    /* Prepare message schedule */
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)ctx->buffer[i*4] << 24) |
               ((uint32_t)ctx->buffer[i*4+1] << 16) |
               ((uint32_t)ctx->buffer[i*4+2] << 8) |
               ((uint32_t)ctx->buffer[i*4+3]);
    }
    
    for (i = 16; i < 80; i++) {
        w[i] = left_rotate(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    }
    
    /* Initialize hash values */
    a = ctx->h0;
    b = ctx->h1;
    c = ctx->h2;
    d = ctx->h3;
    e = ctx->h4;
    
    /* Main loop */
    for (i = 0; i < 80; i++) {
        uint32_t f, k;
        
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        
        uint32_t temp = left_rotate(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = left_rotate(b, 30);
        b = a;
        a = temp;
    }
    
    /* Add compressed chunk to current hash value */
    ctx->h0 += a;
    ctx->h1 += b;
    ctx->h2 += c;
    ctx->h3 += d;
    ctx->h4 += e;
}

void sha1_update(sha1_ctx *ctx, const uint8_t *data, size_t len) {
    size_t i;
    
    ctx->total_len += len;
    
    for (i = 0; i < len; i++) {
        ctx->buffer[ctx->buffer_len++] = data[i];
        
        if (ctx->buffer_len == 64) {
            sha1_process_block(ctx);
            ctx->buffer_len = 0;
        }
    }
}

void sha1_final(sha1_ctx *ctx, uint8_t *digest) {
    uint64_t bit_len = ctx->total_len * 8;
    int i;
    
    /* Append padding bit */
    ctx->buffer[ctx->buffer_len++] = 0x80;
    
    /* Pad to 56 bytes mod 64 */
    if (ctx->buffer_len > 56) {
        memset(&ctx->buffer[ctx->buffer_len], 0, 64 - ctx->buffer_len);
        sha1_process_block(ctx);
        ctx->buffer_len = 0;
    }
    
    memset(&ctx->buffer[ctx->buffer_len], 0, 56 - ctx->buffer_len);
    
    /* Append length in bits (big-endian) */
    ctx->buffer[56] = (uint8_t)(bit_len >> 56);
    ctx->buffer[57] = (uint8_t)(bit_len >> 48);
    ctx->buffer[58] = (uint8_t)(bit_len >> 40);
    ctx->buffer[59] = (uint8_t)(bit_len >> 32);
    ctx->buffer[60] = (uint8_t)(bit_len >> 24);
    ctx->buffer[61] = (uint8_t)(bit_len >> 16);
    ctx->buffer[62] = (uint8_t)(bit_len >> 8);
    ctx->buffer[63] = (uint8_t)(bit_len);
    
    sha1_process_block(ctx);
    
    /* Output digest (big-endian) */
    digest[0]  = (uint8_t)(ctx->h0 >> 24);
    digest[1]  = (uint8_t)(ctx->h0 >> 16);
    digest[2]  = (uint8_t)(ctx->h0 >> 8);
    digest[3]  = (uint8_t)(ctx->h0);
    digest[4]  = (uint8_t)(ctx->h1 >> 24);
    digest[5]  = (uint8_t)(ctx->h1 >> 16);
    digest[6]  = (uint8_t)(ctx->h1 >> 8);
    digest[7]  = (uint8_t)(ctx->h1);
    digest[8]  = (uint8_t)(ctx->h2 >> 24);
    digest[9]  = (uint8_t)(ctx->h2 >> 16);
    digest[10] = (uint8_t)(ctx->h2 >> 8);
    digest[11] = (uint8_t)(ctx->h2);
    digest[12] = (uint8_t)(ctx->h3 >> 24);
    digest[13] = (uint8_t)(ctx->h3 >> 16);
    digest[14] = (uint8_t)(ctx->h3 >> 8);
    digest[15] = (uint8_t)(ctx->h3);
    digest[16] = (uint8_t)(ctx->h4 >> 24);
    digest[17] = (uint8_t)(ctx->h4 >> 16);
    digest[18] = (uint8_t)(ctx->h4 >> 8);
    digest[19] = (uint8_t)(ctx->h4);
}

int main() {
    /* Test with the WebSocket magic string */
    const char *test_input = "66clJ3f/Iu+AxQMzXFXO2w==258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t digest[SHA1_DIGEST_SIZE];
    sha1_ctx ctx;
    int i;
    
    printf("Input: %s\n", test_input);
    printf("Input length: %zu bytes\n\n", strlen(test_input));
    
    sha1_init(&ctx);
    sha1_update(&ctx, (const uint8_t *)test_input, strlen(test_input));
    sha1_final(&ctx, digest);
    
    printf("Our SHA-1 result:\n");
    for (i = 0; i < SHA1_DIGEST_SIZE; i++) {
        printf("%02x", digest[i]);
    }
    printf("\n\n");
    
    printf("Expected (from Python):\n");
    printf("cd927d610be5dc9a1b9f7b9e052e2b6567fc1471\n\n");
    
    printf("Match: %s\n", 
           memcmp(digest, (const uint8_t[]){0xcd,0x92,0x7d,0x61,0x0b,0xe5,0xdc,0x9a,0x1b,0x9f,0x7b,0x9e,0x05,0x2e,0x2b,0x65,0x67,0xfc,0x14,0x71}, SHA1_DIGEST_SIZE) == 0 ? "YES" : "NO");
    
    return 0;
}
