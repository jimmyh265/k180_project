#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include "crypto_config.h"

#define K180_FW_STAGING_TEMPLATE "/var/lib/k180/fw_update.XXXXXX"
#define K180_BIN_DIR "/usr/local/bin"
#define PAYLOAD_COUNT 2

struct output_spec {
    const char *payload_name;
    const char *install_path;
    const char *restart_cmd;
    int restart_required;
};

static int install_file_atomic(const char *src_path, const char *dst_path) {
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%ld", dst_path, (long)getpid());

    int in_fd = open(src_path, O_RDONLY);
    if (in_fd < 0) {
        perror("open source");
        return -1;
    }

    int out_fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (out_fd < 0) {
        perror("open destination temp");
        close(in_fd);
        return -1;
    }

    char buf[65536];
    while (1) {
        ssize_t nread = read(in_fd, buf, sizeof(buf));
        if (nread < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("read source");
            close(in_fd);
            close(out_fd);
            unlink(tmp_path);
            return -1;
        }
        if (nread == 0) {
            break;
        }

        char *p = buf;
        ssize_t remain = nread;
        while (remain > 0) {
            ssize_t nwritten = write(out_fd, p, remain);
            if (nwritten < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("write destination");
                close(in_fd);
                close(out_fd);
                unlink(tmp_path);
                return -1;
            }
            p += nwritten;
            remain -= nwritten;
        }
    }

    if (fsync(out_fd) != 0) {
        perror("fsync destination");
        close(in_fd);
        close(out_fd);
        unlink(tmp_path);
        return -1;
    }

    close(in_fd);
    if (close(out_fd) != 0) {
        perror("close destination");
        unlink(tmp_path);
        return -1;
    }

    if (chmod(tmp_path, 0755) != 0) {
        perror("chmod destination");
        unlink(tmp_path);
        return -1;
    }

    if (rename(tmp_path, dst_path) != 0) {
        perror("rename destination");
        unlink(tmp_path);
        return -1;
    }

    return 0;
}

static void cleanup_staging(const char *staging_dir, const struct output_spec specs[], int count) {
    char path[512];
    for (int i = 0; i < count; i++) {
        snprintf(path, sizeof(path), "%s/%s", staging_dir, specs[i].payload_name);
        unlink(path);
    }
    rmdir(staging_dir);
}

static int read_exact(FILE *fp, void *buf, size_t size, const char *label) {
    if (fread(buf, 1, size, fp) != size) {
        fprintf(stderr, "failed to read %s\n", label);
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        return 1;
    }
    const struct output_spec specs[PAYLOAD_COUNT] = {
        {"grand_yeah", K180_BIN_DIR "/grand_yeah", "systemctl restart grand_yeah.service", 1},
        {"restful_api_server", K180_BIN_DIR "/restful_api_server", "systemctl try-restart restful_api.service", 0}
    };
    char staging_dir[] = K180_FW_STAGING_TEMPLATE;
    if (!mkdtemp(staging_dir)) {
        perror("mkdtemp staging");
        return 1;
    }
		
    FILE *fin = fopen(argv[1], "rb");
    if (!fin) {
        perror("fopen input");
        rmdir(staging_dir);
        return 1;
    }

    // 讀取 MAGIC_WORD
    char magic[33] = {0};
    if (read_exact(fin, magic, 32, "magic") != 0) {
        fclose(fin);
        rmdir(staging_dir);
        return 2;
    }
    if (strncmp(magic, MAGIC_WORD, 32) != 0) {
        fprintf(stderr, "MAGIC_WORD mismatch\n");
        fclose(fin);
        rmdir(staging_dir);
        return 2;
    }

    // 讀 IV 與 metadata
    unsigned char iv[AES_BLOCK_SIZE];
    if (read_exact(fin, iv, AES_BLOCK_SIZE, "iv") != 0) {
        fclose(fin);
        rmdir(staging_dir);
        return 2;
    }

    uint32_t len[PAYLOAD_COUNT];
    uint32_t total_len = 0;
    for (int i = 0; i < PAYLOAD_COUNT; i++) {
        char label[16];
        snprintf(label, sizeof(label), "len%d", i);
        if (read_exact(fin, &len[i], sizeof(uint32_t), label) != 0) {
            fclose(fin);
            rmdir(staging_dir);
            return 2;
        }
        total_len += len[i];
    }

    // 讀取 ciphertext
    fseek(fin, 0, SEEK_END);
    long file_size = ftell(fin);
    long cipher_len = file_size - 32 - AES_BLOCK_SIZE - PAYLOAD_COUNT * sizeof(uint32_t) - 32;
    fseek(fin, 32 + AES_BLOCK_SIZE + PAYLOAD_COUNT * sizeof(uint32_t), SEEK_SET);

    unsigned char *ciphertext = malloc(cipher_len);
    if (read_exact(fin, ciphertext, cipher_len, "ciphertext") != 0) {
        fclose(fin);
        free(ciphertext);
        rmdir(staging_dir);
        return 2;
    }

    unsigned char hmac_file[32];
    if (read_exact(fin, hmac_file, 32, "hmac") != 0) {
        fclose(fin);
        free(ciphertext);
        rmdir(staging_dir);
        return 2;
    }
    fclose(fin);

    // 驗證 HMAC
    unsigned char *hmac_input = malloc(AES_BLOCK_SIZE + cipher_len);
    memcpy(hmac_input, iv, AES_BLOCK_SIZE);
    memcpy(hmac_input + AES_BLOCK_SIZE, ciphertext, cipher_len);

    unsigned char hmac_calc[32];
    unsigned int hmac_len;
    HMAC(EVP_sha256(), HMAC_KEY, strlen(HMAC_KEY), hmac_input, AES_BLOCK_SIZE + cipher_len, hmac_calc, &hmac_len);
    free(hmac_input);

    if (memcmp(hmac_file, hmac_calc, 32) != 0) {
        fprintf(stderr, "HMAC 驗證失敗\n");
        free(ciphertext);
        rmdir(staging_dir);
        return 3;
    }

    // 解密
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    unsigned char *plaintext = malloc(cipher_len + AES_BLOCK_SIZE);
    int len1, len2;
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, (unsigned char*)KEY, iv);
    EVP_DecryptUpdate(ctx, plaintext, &len1, ciphertext, cipher_len);
    if (!EVP_DecryptFinal_ex(ctx, plaintext + len1, &len2)) {
        fprintf(stderr, "解密失敗（padding）\n");
        free(ciphertext);
        free(plaintext);
        rmdir(staging_dir);
        return 4;
    }
    EVP_CIPHER_CTX_free(ctx);
    free(ciphertext);

    int total_plain_len = len1 + len2;
    if ((uint32_t)total_plain_len != total_len) {
        fprintf(stderr, "長度驗證錯誤（header vs 解密不符）\n");
        free(plaintext);
        rmdir(staging_dir);
        return 5;
    }

    // 拆分並輸出
    FILE *fout[PAYLOAD_COUNT];
    for (int i = 0; i < PAYLOAD_COUNT; i++) {
        char out_path[512];
        snprintf(out_path, sizeof(out_path), "%s/%s", staging_dir, specs[i].payload_name);
        fout[i] = fopen(out_path, "wb");
        if (!fout[i]) {
            perror("fopen output");
			free(plaintext);
            cleanup_staging(staging_dir, specs, PAYLOAD_COUNT);
            return 6;
        }
    }

    int offset = 0;
    for (int i = 0; i < PAYLOAD_COUNT; i++) {
        fwrite(plaintext + offset, 1, len[i], fout[i]);
        fclose(fout[i]);
        offset += len[i];
    }

    free(plaintext);

    for (int i = 0; i < PAYLOAD_COUNT; i++) {
        char src_path[512];
        snprintf(src_path, sizeof(src_path), "%s/%s", staging_dir, specs[i].payload_name);

        if (install_file_atomic(src_path, specs[i].install_path) != 0) {
            cleanup_staging(staging_dir, specs, PAYLOAD_COUNT);
            return 7;
        }
    }

    for (int i = 0; i < PAYLOAD_COUNT; i++) {
        int ret = system(specs[i].restart_cmd);
        if (ret != 0 && specs[i].restart_required) {
            fprintf(stderr, "restart failed: %s\n", specs[i].restart_cmd);
            cleanup_staging(staging_dir, specs, PAYLOAD_COUNT);
            return 8;
        }
    }

    cleanup_staging(staging_dir, specs, PAYLOAD_COUNT);
    return 0;
}
