#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * ArchaOS SHA-256
 *
 * Usage:
 *   sha256 hello
 *
 * Produces the SHA-256 digest of the supplied string.
 */

typedef unsigned int uint32;

static const uint32 K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static uint32 rotr(uint32 x, int n)
{
    return (x >> n) | (x << (32 - n));
}

static uint32 choose(uint32 x, uint32 y, uint32 z)
{
    return (x & y) ^ (~x & z);
}

static uint32 majority(uint32 x, uint32 y, uint32 z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

static uint32 big_sigma0(uint32 x)
{
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

static uint32 big_sigma1(uint32 x)
{
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

static uint32 small_sigma0(uint32 x)
{
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

static uint32 small_sigma1(uint32 x)
{
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

static void sha256(
    const unsigned char *input,
    size_t length,
    unsigned char output[32]
)
{
    /*
     * SHA-256 padding:
     *
     * message
     * 0x80
     * zeroes
     * 64-bit big-endian message length
     */
    size_t padded_length = length + 1;

    while ((padded_length % 64) != 56)
        padded_length++;

    padded_length += 8;

    unsigned char *data =
    (unsigned char *)malloc(padded_length);

    if (data == NULL) {
        printf("Error: out of memory\n");
        exit(1);
    }

    memset(data, 0, padded_length);

    memcpy(data, input, length);

    data[length] = 0x80;

    /*
     * Length is encoded as bits in the final
     * 64-bit big-endian integer.
     *
     * This implementation assumes size_t is
     * 32-bit, which matches the ArchaOS target.
     */
    uint32 bit_length = (uint32)length * 8;

    data[padded_length - 4] =
    (unsigned char)(bit_length >> 24);

    data[padded_length - 3] =
    (unsigned char)(bit_length >> 16);

    data[padded_length - 2] =
    (unsigned char)(bit_length >> 8);

    data[padded_length - 1] =
    (unsigned char)bit_length;

    uint32 h0 = 0x6a09e667;
    uint32 h1 = 0xbb67ae85;
    uint32 h2 = 0x3c6ef372;
    uint32 h3 = 0xa54ff53a;
    uint32 h4 = 0x510e527f;
    uint32 h5 = 0x9b05688c;
    uint32 h6 = 0x1f83d9ab;
    uint32 h7 = 0x5be0cd19;

    size_t offset;

    for (offset = 0; offset < padded_length; offset += 64) {

        uint32 w[64];

        int i;

        /*
         * First 16 words come directly from
         * the 512-bit message block.
         */
        for (i = 0; i < 16; i++) {
            int j = i * 4;

            w[i] =
            ((uint32)data[offset + j] << 24) |
            ((uint32)data[offset + j + 1] << 16) |
            ((uint32)data[offset + j + 2] << 8) |
            ((uint32)data[offset + j + 3]);
        }

        /*
         * Expand 16 words into 64 words.
         */
        for (i = 16; i < 64; i++) {
            uint32 s0 = small_sigma0(w[i - 15]);
            uint32 s1 = small_sigma1(w[i - 2]);

            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32 a = h0;
        uint32 b = h1;
        uint32 c = h2;
        uint32 d = h3;
        uint32 e = h4;
        uint32 f = h5;
        uint32 g = h6;
        uint32 h = h7;

        /*
         * SHA-256 compression.
         */
        for (i = 0; i < 64; i++) {

            uint32 S1 = big_sigma1(e);
            uint32 ch = choose(e, f, g);

            uint32 temp1 =
            h + S1 + ch + K[i] + w[i];

            uint32 S0 = big_sigma0(a);
            uint32 maj = majority(a, b, c);

            uint32 temp2 = S0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
        h5 += f;
        h6 += g;
        h7 += h;
    }

    free(data);

    /*
     * Convert the eight 32-bit state words
     * into the final 32-byte big-endian digest.
     */
    uint32 hash[8];

    hash[0] = h0;
    hash[1] = h1;
    hash[2] = h2;
    hash[3] = h3;
    hash[4] = h4;
    hash[5] = h5;
    hash[6] = h6;
    hash[7] = h7;

    for (int i = 0; i < 8; i++) {
        output[i * 4] =
        (unsigned char)(hash[i] >> 24);

        output[i * 4 + 1] =
        (unsigned char)(hash[i] >> 16);

        output[i * 4 + 2] =
        (unsigned char)(hash[i] >> 8);

        output[i * 4 + 3] =
        (unsigned char)hash[i];
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("ArchaOS SHA-256\n");
        printf("Usage: %s <text>\n", argv[0]);
        return 1;
    }

    unsigned char digest[32];

    sha256(
        (const unsigned char *)argv[1],
           strlen(argv[1]),
           digest
    );

    printf("SHA-256: ");

    for (int i = 0; i < 32; i++) {
        printf("%x", digest[i] >> 4);
        printf("%x", digest[i] & 0x0f);
    }

    putchar('\n');

    return 0;
}
