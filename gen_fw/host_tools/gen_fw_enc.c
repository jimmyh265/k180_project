/*
[MAGIC_WORD (32 bytes)]
[IV (16 bytes)]
[LEN1 (4 bytes)]
[LEN2 (4 bytes)]
[Ciphertext (加密後的資料 = data1 + data2)]
[HMAC (32 bytes, sha256(iv + ciphertext))]

./gen_fw_enc grand_yeah restful_api_server out.hhc
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include "crypto_config.h"

#define PAYLOAD_COUNT 2

int main(int argc, char *argv[]) {
    if (argc != PAYLOAD_COUNT + 2) {
        fprintf(stderr, "Usage: %s <grand_yeah> <restful_api_server> <output.hhc>\n", argv[0]);
        return 1;
    }

    unsigned char *input[PAYLOAD_COUNT];
    uint32_t len[PAYLOAD_COUNT];  // 每段長度
    uint32_t total_len = 0;

    // 讀取 payload 檔案
    for (int i = 0; i < PAYLOAD_COUNT; i++) {
        FILE *f = fopen(argv[i + 1], "rb");
        if (!f) {
            perror("fopen input");
            return 1;
        }
        fseek(f, 0, SEEK_END);
        len[i] = ftell(f);
        fseek(f, 0, SEEK_SET);
        input[i] = malloc(len[i]);
        if (fread(input[i], 1, len[i], f) != len[i]) {
            perror("fread input");
            fclose(f);
            free(input[i]);
            return 1;
        }
        fclose(f);
        total_len += len[i];
    }

    // 串接全部資料
    unsigned char *plain_all = malloc(total_len);
    int offset = 0;
    for (int i = 0; i < PAYLOAD_COUNT; i++) {
        memcpy(plain_all + offset, input[i], len[i]);
        free(input[i]);
        offset += len[i];
    }

    // 產生 IV
    unsigned char iv[AES_BLOCK_SIZE];
    RAND_bytes(iv, AES_BLOCK_SIZE);

    // 加密
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    unsigned char *ciphertext = malloc(total_len + AES_BLOCK_SIZE);
    int len1, len2;
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, (unsigned char*)KEY, iv);
    EVP_EncryptUpdate(ctx, ciphertext, &len1, plain_all, total_len);
    EVP_EncryptFinal_ex(ctx, ciphertext + len1, &len2);
    EVP_CIPHER_CTX_free(ctx);
    int cipher_len = len1 + len2;
    free(plain_all);

    // 計算 HMAC (iv + ciphertext)
    unsigned char *hmac_input = malloc(AES_BLOCK_SIZE + cipher_len);
    memcpy(hmac_input, iv, AES_BLOCK_SIZE);
    memcpy(hmac_input + AES_BLOCK_SIZE, ciphertext, cipher_len);
    unsigned char hmac[32];
    unsigned int hmac_len;
    HMAC(EVP_sha256(), HMAC_KEY, strlen(HMAC_KEY), hmac_input, AES_BLOCK_SIZE + cipher_len, hmac, &hmac_len);
    free(hmac_input);

    // 寫出加密檔
    FILE *fout = fopen(argv[PAYLOAD_COUNT + 1], "wb");
    if (!fout) {
        perror("fopen output");
        return 1;
    }

    fwrite(MAGIC_WORD, 1, 32, fout);
    fwrite(iv, 1, AES_BLOCK_SIZE, fout);
    for (int i = 0; i < PAYLOAD_COUNT; i++) {
        fwrite(&len[i], sizeof(uint32_t), 1, fout);
    }
    fwrite(ciphertext, 1, cipher_len, fout);
    fwrite(hmac, 1, 32, fout);
    fclose(fout);
    free(ciphertext);

    printf("加密成功，寫入：%s\n", argv[PAYLOAD_COUNT + 1]);
    return 0;
}
