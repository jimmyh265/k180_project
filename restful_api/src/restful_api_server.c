#include <microhttpd.h>
#include <json-c/json.h>
#include <uuid/uuid.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <math.h>
#include <arpa/inet.h>  // for inet_pton
#include <dirent.h>
#include <ctype.h>
#include <sys/file.h>	// flock()
#include <fcntl.h>		// open()
#include <unistd.h>		// crypt()

#define API_VER "1.0"

#define PORT 12178
#define MAX_UPLOAD_SIZE 2048
#define TOKEN_EXPIRY_SECONDS 300

#define K180_CONFIG_DIR "/etc/k180"
#define K180_STATE_DIR "/var/lib/k180"
#define K180_RECORD_DIR "/data"
#define K180_LIBEXEC_DIR "/usr/local/libexec/k180"
#define K180_WEB_PRIVATE_DIR "/var/www/private"

#define MEMBER_SYSCFG "system_cfg"
#define MEMBER_STREAM1 "stream_1"
#define MEMBER_STREAM2 "stream_2"
#define JSON_SYS_IPADDR "ipaddress"
#define JSON_SYS_MASK "netmask"
#define JSON_SYS_GW "gateway"
#define JSON_RECORD "recorded"
#define JSON_OBJ_DET "objectdet"
#define JSON_ROTATE "rotate180"
#define JSON_RES "resolution"
#define JSON_FPS "fps"
#define JSON_DATARATE "datarate"

static struct json_object *messages;
static struct json_object *session_map;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
// static const char *valid_top_keys[] = { "system_cfg", "stream_1", "stream_2" };
static const char *valid_top_keys[] = { MEMBER_SYSCFG, MEMBER_STREAM1, MEMBER_STREAM2 };
// static const char *valid_system_cfg_keys[] = { "ipaddress", "netmask", "gateway", "recorded", "objectdet", "rotate180" };
static const char *valid_system_cfg_keys[] = { JSON_SYS_IPADDR, JSON_SYS_MASK, JSON_SYS_GW, JSON_RECORD, JSON_OBJ_DET, JSON_ROTATE };
static const char *valid_stream_keys[] = { JSON_RES, JSON_FPS, JSON_DATARATE };
const char *RECORD_MP4_DIR = K180_RECORD_DIR;
const char *RF_REG_FILE = K180_CONFIG_DIR "/user_def_setting.json";
const char *FW_INFO_FILE = K180_STATE_DIR "/firmware_ver_info.json";
const char *API_VER_FILE = K180_STATE_DIR "/api_ver_info.json";
const char *USER_FILE = K180_WEB_PRIVATE_DIR "/users.json";
const char *REC_FILE_LOCK = K180_STATE_DIR "/tmp_rec_zip/zip_batch.lock";
const char *MODIFY_SYSIP_NETPLAN = K180_LIBEXEC_DIR "/modify_sysip_netplan";
const char *RESTART_PROGRAM_CAMERA = K180_LIBEXEC_DIR "/restart_program_camera";
const char *REBOOT_SYSTEM = K180_LIBEXEC_DIR "/reboot_system";

bool sys_cfg_change = false;

static int send_json(struct MHD_Connection *conn, struct json_object *json, int code) {
    const char *str = json_object_to_json_string(json);
    struct MHD_Response *res = MHD_create_response_from_buffer(strlen(str), (void*)str, MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(res, "Content-Type", "application/json");
    int ret = MHD_queue_response(conn, code, res);
    MHD_destroy_response(res);
    return ret;
}

static bool in_list(const char *key, const char *list[], size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (strcmp(key, list[i]) == 0)
            return true;
    }
    return false;
}

bool is_valid_filename(const char *filename) {
    if (!filename) return false;

    size_t len = strlen(filename);
    if (len == 0 || len > 255) return false;

    // 禁止路徑符號與路徑穿越
    if (strstr(filename, "..") || strchr(filename, '/') || strchr(filename, '\\')) {
        return false;
    }

    // 僅允許字母、數字、底線、減號、點
    for (size_t i = 0; i < len; ++i) {
        char c = filename[i];
        if (!isalnum(c) && c != '_' && c != '-' && c != '.') {
            return false;
        }
    }

    // 限制副檔名（例如只允許 .mp4）
    const char *ext = strrchr(filename, '.');
    if (!ext || strcmp(ext, ".mp4") != 0) {
        return false;
    }

    return true;
}

bool is_valid_query(struct json_object *query) {
    if (!query || !json_object_is_type(query, json_type_object))
        return false;

    json_object_object_foreach(query, top_key, top_val) {
        // 頂層 key 檢查
        if (!in_list(top_key, valid_top_keys, sizeof(valid_top_keys) / sizeof(valid_top_keys[0])))
            return false;

        // 第二層必須是物件
        if (!json_object_is_type(top_val, json_type_object))
            return false;

        // 檢查第二層 key
        json_object_object_foreach(top_val, sub_key, sub_val) {
            if (json_object_is_type(sub_val, json_type_object))
                return false;

            if (strcmp(top_key, "system_cfg") == 0) {
                if (!in_list(sub_key, valid_system_cfg_keys, sizeof(valid_system_cfg_keys) / sizeof(valid_system_cfg_keys[0])))
                    return false;
            } else if (strcmp(top_key, "stream_1") == 0 || strcmp(top_key, "stream_2") == 0) {
                if (!in_list(sub_key, valid_stream_keys, sizeof(valid_stream_keys) / sizeof(valid_stream_keys[0])))
                    return false;
            }
        }
    }

    return true;
}

// 遞迴：是否為 GET（子 key 為空字串）
bool is_get_query(struct json_object *obj) {
    if (!obj || !json_object_is_type(obj, json_type_object))
        return false;

    json_object_object_foreach(obj, key, val) {
        (void)key;
        if (json_object_is_type(val, json_type_string)) {
            if (strcmp(json_object_get_string(val), "") == 0)
                return true;
        } else if (json_object_is_type(val, json_type_object)) {
            if (is_get_query(val))
                return true;
        }
    }
    return false;
}

// 遞迴 GET
struct json_object *query_json(struct json_object *source, struct json_object *query) {
    if (!json_object_is_type(source, json_type_object) || !json_object_is_type(query, json_type_object))
        return NULL;

    struct json_object *result = json_object_new_object();

    json_object_object_foreach(query, key, query_val) {
        struct json_object *src_val;
        if (!json_object_object_get_ex(source, key, &src_val))
            continue;

        if (json_object_is_type(query_val, json_type_object) && json_object_is_type(src_val, json_type_object)) {
            struct json_object *child = query_json(src_val, query_val);
            if (child != NULL && json_object_object_length(child) > 0)
                json_object_object_add(result, key, child);
        } else if (json_object_is_type(query_val, json_type_string) &&
                   strcmp(json_object_get_string(query_val), "") == 0) {
            json_object_object_add(result, key, json_object_get(src_val));
        }
    }

    return result;
}

bool is_valid_ip(const char *ip) {
    struct sockaddr_in sa;
    return inet_pton(AF_INET, ip, &(sa.sin_addr)) == 1;
}

bool is_in_int_list(int value, const int *list, size_t len) {
    for (size_t i = 0; i < len; i++)
        if (value == list[i]) return true;
    return false;
}

bool is_in_double_list(double value, const double *list, size_t len) {
    for (size_t i = 0; i < len; i++)
        if (fabs(value - list[i]) < 0.0001) return true;
    return false;
}

static int get_max_stream_fps(void) {
    const int default_max_fps = 30;
    int max_fps = default_max_fps;

    int fd = open(FW_INFO_FILE, O_RDONLY);
    if (fd < 0) {
        return default_max_fps;
    }

    if (flock(fd, LOCK_SH) < 0) {
        close(fd);
        return default_max_fps;
    }

    FILE *fp = fdopen(fd, "r");
    if (!fp) {
        flock(fd, LOCK_UN);
        close(fd);
        return default_max_fps;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        flock(fd, LOCK_UN);
        fclose(fp);
        return default_max_fps;
    }

    long size = ftell(fp);
    if (size < 0 || size > 1024 * 1024) {
        flock(fd, LOCK_UN);
        fclose(fp);
        return default_max_fps;
    }
    rewind(fp);

    char *buffer = (char*)malloc((size_t)size + 1);
    if (!buffer) {
        flock(fd, LOCK_UN);
        fclose(fp);
        return default_max_fps;
    }

    size_t read_bytes = fread(buffer, 1, (size_t)size, fp);
    if (read_bytes != (size_t)size) {
        free(buffer);
        flock(fd, LOCK_UN);
        fclose(fp);
        return default_max_fps;
    }
    buffer[size] = '\0';

    struct json_object *root = json_tokener_parse(buffer);
    free(buffer);

    if (root) {
        struct json_object *sysinfo = NULL;
        struct json_object *max_stream_fps = NULL;
        if (json_object_object_get_ex(root, "sysinfo", &sysinfo) &&
            json_object_is_type(sysinfo, json_type_object) &&
            json_object_object_get_ex(sysinfo, "max_stream_fps", &max_stream_fps) &&
            json_object_is_type(max_stream_fps, json_type_int)) {
            int parsed = json_object_get_int(max_stream_fps);
            if (parsed == 30 || parsed == 60) {
                max_fps = parsed;
            }
        }
        json_object_put(root);
    }

    flock(fd, LOCK_UN);
    fclose(fp);
    return max_fps;
}

static bool is_valid_stream_fps(int fps) {
    const int valid_vals[] = {1, 5, 10, 15, 20, 30, 60};
    return is_in_int_list(fps, valid_vals, sizeof(valid_vals) / sizeof(valid_vals[0])) &&
           fps <= get_max_stream_fps();
}

// 驗證更新內容是否合法
bool validate_json(struct json_object *target, struct json_object *updates) {
    json_object_object_foreach(updates, key, update_val) {
        struct json_object *target_val;
        if (json_object_get_type(update_val) == json_type_object &&
            json_object_object_get_ex(target, key, &target_val) &&
            json_object_get_type(target_val) == json_type_object) {
            if (!validate_json(target_val, update_val))
                return false;
            continue;
        }

        // ✅ 若不是 object（直接在第二層設定欄位），此處也需驗證
        if (strcmp(key, JSON_RES) == 0) {	// resolution
            const int valid_vals[] = {720, 1080};
            if (!json_object_is_type(update_val, json_type_int) ||
                !is_in_int_list(json_object_get_int(update_val), valid_vals, 2))
                return false;
        } else if (strcmp(key, JSON_FPS) == 0) {	// fps
            if (!json_object_is_type(update_val, json_type_int) ||
                !is_valid_stream_fps(json_object_get_int(update_val)))
                return false;
        } else if (strcmp(key, JSON_DATARATE) == 0) {	// datarate
            const double valid_vals[] = {0.2, 0.5, 1.0, 2.0, 4.0, 6.0, 8.0};
            if (!json_object_is_type(update_val, json_type_double) &&
                !json_object_is_type(update_val, json_type_int))
                return false;
            double val = json_object_get_double(update_val);
            if (!is_in_double_list(val, valid_vals, 7))
                return false;
        } else if (strcmp(key, JSON_RECORD) == 0) {		// recorded
            const int valid_vals[] = {0, 1, 2};
            if (!json_object_is_type(update_val, json_type_int) ||
                !is_in_int_list(json_object_get_int(update_val), valid_vals, 3))
                return false;
        } else if (strcmp(key, JSON_OBJ_DET) == 0) {	// objectdet
            const int valid_vals[] = {0, 1};
            if (!json_object_is_type(update_val, json_type_int) ||
                !is_in_int_list(json_object_get_int(update_val), valid_vals, 2))
                return false;
        } else if (strcmp(key, JSON_ROTATE) == 0) {		// rotate180
            const int valid_vals[] = {0, 1};
            if (!json_object_is_type(update_val, json_type_int) ||
                !is_in_int_list(json_object_get_int(update_val), valid_vals, 2))
                return false;
        } else if (strcmp(key, JSON_SYS_IPADDR) == 0 || strcmp(key, JSON_SYS_GW) == 0) {		// ipaddress & gateway
            if (!json_object_is_type(update_val, json_type_string) ||
                !is_valid_ip(json_object_get_string(update_val)))
                return false;		// 檢查發現錯誤，跳出
			else 
				sys_cfg_change = true;		// 若無錯，表示 有值更新
        } else if (strcmp(key, JSON_SYS_MASK) == 0) {	// netmask
            if (!json_object_is_type(update_val, json_type_int)) return false;
            int v = json_object_get_int(update_val);
            if (v < 8 || v > 32) return false;		// 檢查發現錯誤，跳出
			else sys_cfg_change = true;		// 若無錯，表示 有值更新
        }
    }

    return true;
}

// 若驗證成功才套用更新
void apply_update_json(struct json_object *target, struct json_object *updates) {
    json_object_object_foreach(updates, key, update_val) {
        struct json_object *target_val;
        if (json_object_get_type(update_val) == json_type_object &&
            json_object_object_get_ex(target, key, &target_val) &&
            json_object_get_type(target_val) == json_type_object) {
            apply_update_json(target_val, update_val);
        } else {
            json_object_object_add(target, key, json_object_get(update_val));
        }
    }
}

// 封裝：先驗證，再更新
bool update_json(struct json_object *target, struct json_object *updates) {
    if (!validate_json(target, updates)) {
        fprintf(stderr, "PUT Rejected: Invalid value(s) detected.\n");
        return false;
    }

    apply_update_json(target, updates);
    return true;
}

struct ConnectionInfo {
    char *data;
    size_t size;
};

void generate_uuid(char *uuid_out) {
    uuid_t uuid;
    uuid_generate(uuid);
    uuid_unparse_lower(uuid, uuid_out);
}

int is_token_valid(const char *token) {

    pthread_mutex_lock(&lock);
    struct json_object *session;
    if (!json_object_object_get_ex(session_map, token, &session)) {
        pthread_mutex_unlock(&lock);
        return 0;
    }

    time_t now = time(NULL);
    struct json_object *last_used_obj;
    if (!json_object_object_get_ex(session, "last_used", &last_used_obj)) {
        pthread_mutex_unlock(&lock);
        return 0;
    }

    time_t last_used = (time_t)json_object_get_int64(last_used_obj);
    int valid = difftime(now, last_used) <= TOKEN_EXPIRY_SECONDS;

    if (valid) {
        json_object_object_add(session, "last_used", json_object_new_int64(now));
    }

    pthread_mutex_unlock(&lock);
    return valid;
}

int check_auth(struct MHD_Connection *conn) {
    const char *auth = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Authorization");
    if (!auth || strncmp(auth, "Bearer ", 7) != 0) return 0;
    return is_token_valid(auth + 7);
}

int send_file_list(struct MHD_Connection *connection) {
    struct dirent *entry;
    DIR *dir = opendir(RECORD_MP4_DIR);
    if (!dir) {
        struct json_object *err = json_object_new_object();
        json_object_object_add(err, "error", json_object_new_string("Failed to open directory"));
        int ret = send_json(connection, err, MHD_HTTP_INTERNAL_SERVER_ERROR);
		json_object_put(err);  // ✅ 正確釋放
		return ret;
    }

    struct json_object *jarray = json_object_new_array();

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        json_object_array_add(jarray, json_object_new_string(entry->d_name));
    }

    closedir(dir);
    int ret = send_json(connection, jarray, MHD_HTTP_OK);
	json_object_put(jarray);  // ✅ 正確釋放
	return ret;
}


int verify_user(const char *input_user, const char *input_pass, const char *json_path) {
		int fd = open(json_path, O_RDONLY );  // 
		if (fd < 0) {
			return 0;
		}

		if (flock(fd, LOCK_SH) < 0) {
			close(fd);
			return 0;
		}
		FILE *fp = fdopen(fd, "r");
		if (!fp) {
			flock(fd, LOCK_UN);
			close(fd);
			return 0;
		}
		fseek(fp, 0, SEEK_END);
		size_t size = ftell(fp);
		rewind(fp);
		char *buffer = (char*)malloc(size + 1);
		size_t read_bytes = fread(buffer, 1, size, fp);
		if (read_bytes != size) {
			flock(fd, LOCK_UN);
			fclose(fp);
			free(buffer);
			return 0;
		}
		buffer[size] = '\0';
	
		struct json_object *root =json_tokener_parse(buffer);
		if (!root) {
			flock(fd, LOCK_UN);
			fclose(fp);
			return 0;
		}
		free(buffer);
		flock(fd, LOCK_UN);
		fclose(fp);

    if (!json_object_is_type(root, json_type_array)) {
		json_object_put(root);
		return 0;
    }

    size_t i, array_len = json_object_array_length(root);
    for (i = 0; i < array_len; i++) {
        struct json_object *user_obj = json_object_array_get_idx(root, i);
        const char *username = json_object_get_string(json_object_object_get(user_obj, "username"));
        const char *hash = json_object_get_string(json_object_object_get(user_obj, "password_hash"));

        if (strcmp(username, input_user) == 0) {
            const char *result = crypt(input_pass, hash);
            if (strcmp(result, hash) == 0) {
                return 1;  // 登入成功
            } else {
                return 0;  // 密碼錯誤
            }
        }
    }

    json_object_put(root);
    return 0;  // 無此使用者
}

int download_rec_file(struct MHD_Connection *connection, const char *filename) {
	
	if (!is_valid_filename(filename)) {
		struct json_object *err = json_object_new_object();
        json_object_object_add(err, "error", json_object_new_string("Invalid filename"));
        int ret = send_json(connection, err, MHD_HTTP_INTERNAL_SERVER_ERROR);
		json_object_put(err);  // ✅ 正確釋放
		return ret;
	}
	
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", RECORD_MP4_DIR, filename);

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        struct json_object *err = json_object_new_object();
        json_object_object_add(err, "error", json_object_new_string("File not found"));
        int ret = send_json(connection, err, MHD_HTTP_NOT_FOUND);
		json_object_put(err);
		return ret;
    }

    fseek(fp, 0, SEEK_END);
    size_t filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *buffer = malloc(filesize);
	size_t nread = fread(buffer, 1, filesize, fp);
	if (nread != filesize) {
		fclose(fp);
		free(buffer);
		struct json_object *err = json_object_new_object();
		json_object_object_add(err, "error", json_object_new_string("Failed to read full file"));
		int ret = send_json(connection, err, MHD_HTTP_INTERNAL_SERVER_ERROR);
		json_object_put(err);  // ✅ 正確釋放
		return ret;
	}
    fclose(fp);

    struct MHD_Response *res = MHD_create_response_from_buffer(filesize, buffer, MHD_RESPMEM_MUST_FREE);
    MHD_add_response_header(res, "Content-Type", "application/octet-stream");
	
	char content_disp[256];
	snprintf(content_disp, sizeof(content_disp), "attachment; filename=\"%s\"", filename);
	MHD_add_response_header(res, "Content-Disposition", content_disp);

    int ret = MHD_queue_response(connection, MHD_HTTP_OK, res);
    MHD_destroy_response(res);
    return ret;
}

int delete_rec_file(struct MHD_Connection *connection, const char *filename) {
    if (!is_valid_filename(filename)) {
        struct json_object *err = json_object_new_object();
        json_object_object_add(err, "error", json_object_new_string("Invalid filename"));
        int ret = send_json(connection, err, MHD_HTTP_INTERNAL_SERVER_ERROR);
		json_object_put(err);
		return ret;
    }

    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s", RECORD_MP4_DIR, filename);

    if (remove(filepath) != 0) {
        struct json_object *err = json_object_new_object();
        json_object_object_add(err, "error", json_object_new_string("Failed to delete file"));
        int ret = send_json(connection, err, MHD_HTTP_INTERNAL_SERVER_ERROR);
		json_object_put(err);  // ✅ 正確釋放
		return ret;
    }

    struct json_object *ok = json_object_new_object();
    json_object_object_add(ok, "status", json_object_new_string("File deleted"));
    int ret = send_json(connection, ok, MHD_HTTP_OK);
	json_object_put(ok);  // ✅ 正確釋放
	return ret;
}

static int handler(void *cls, struct MHD_Connection *conn,
                   const char *url, const char *method, const char *version,
                   const char *upload_data, size_t *upload_data_size, void **con_cls) {
    if (*con_cls == NULL) {
        struct ConnectionInfo *info = calloc(1, sizeof(struct ConnectionInfo));
        info->data = calloc(1, MAX_UPLOAD_SIZE);
        *con_cls = info;
        return MHD_YES;
    }

    struct ConnectionInfo *info = *con_cls;

    if (*upload_data_size > 0) {
        if (info->size + *upload_data_size >= MAX_UPLOAD_SIZE) return MHD_NO;
        memcpy(info->data + info->size, upload_data, *upload_data_size);
        info->size += *upload_data_size;
        *upload_data_size = 0;
        return MHD_YES;
    }

    if (strcmp(url, "/login") == 0 && strcmp(method, "POST") == 0) {
        struct json_object *body = json_tokener_parse(info->data);
        if (!body) {
            struct json_object *e = json_object_new_object();
            json_object_object_add(e, "error", json_object_new_string("Invalid JSON"));
            int ret = send_json(conn, e, MHD_HTTP_BAD_REQUEST);
			json_object_put(e);  // ✅ 正確釋放
			return ret;
        }

        struct json_object *username_obj, *password_obj;
        if (json_object_object_get_ex(body, "username", &username_obj) &&
            json_object_object_get_ex(body, "password", &password_obj)) {
            const char *username = json_object_get_string(username_obj);
            const char *password = json_object_get_string(password_obj);
            if (verify_user(username, password, USER_FILE)) {
                char token[37];
                generate_uuid(token);
                time_t now = time(NULL);

                pthread_mutex_lock(&lock);
                struct json_object *session = json_object_new_object();
                json_object_object_add(session, "last_used", json_object_new_int64(now));
                json_object_object_add(session_map, token, session);
                pthread_mutex_unlock(&lock);

                struct json_object *res = json_object_new_object();
                json_object_object_add(res, "token", json_object_new_string(token));
                json_object_put(body);
                int ret = send_json(conn, res, MHD_HTTP_OK);
				json_object_put(res);
				return ret;
            }
        }

        json_object_put(body);
        struct json_object *e = json_object_new_object();
        json_object_object_add(e, "error", json_object_new_string("Invalid credentials"));
        int ret = send_json(conn, e, MHD_HTTP_UNAUTHORIZED);
		json_object_put(e);  // ✅ 正確釋放
		return ret;
    }

	if (!check_auth(conn)) {
		struct json_object *e = json_object_new_object();
		json_object_object_add(e, "error", json_object_new_string("Unauthorized"));
		int ret = send_json(conn, e, MHD_HTTP_UNAUTHORIZED);
		json_object_put(e);  // ✅ 正確釋放
		return ret;
	}

	if (strcmp(url, "/api/messages") == 0) {
		
		int fd = open(RF_REG_FILE, O_RDWR);  // RW 模式，因為可能要寫入
		if (fd < 0) {
			struct json_object *e = json_object_new_object();
			json_object_object_add(e, "error", json_object_new_string("Failed to open config file 0"));
			int ret = send_json(conn, e, MHD_HTTP_BAD_REQUEST);
			json_object_put(e);
			return ret;
		}

		if (flock(fd, LOCK_EX) < 0) {
			close(fd);
			struct json_object *e = json_object_new_object();
			json_object_object_add(e, "error", json_object_new_string("Failed to lock config file 1"));
			int ret = send_json(conn, e, MHD_HTTP_BAD_REQUEST);
			json_object_put(e);
			return ret;
		}
		FILE *fp = fdopen(fd, "r");
		if (!fp) {
			struct json_object *e = json_object_new_object();
			json_object_object_add(e, "error", json_object_new_string("Failed to lock config file 2"));
			int ret = send_json(conn, e, MHD_HTTP_BAD_REQUEST);
			json_object_put(e);
			flock(fd, LOCK_UN);
			close(fd);
			return ret;
		}
		fseek(fp, 0, SEEK_END);
		size_t size = ftell(fp);
		rewind(fp);
		char *buffer = (char*)malloc(size + 1);
		size_t read_bytes = fread(buffer, 1, size, fp);
		if (read_bytes != size) {
			struct json_object *e = json_object_new_object();
			json_object_object_add(e, "error", json_object_new_string("Failed to lock config file 3"));
			int ret = send_json(conn, e, MHD_HTTP_BAD_REQUEST);
			json_object_put(e);
			flock(fd, LOCK_UN);
			fclose(fp);
			free(buffer);
			return ret;
		}
		buffer[size] = '\0';
	
		// struct json_object *config = json_object_from_file(RF_REG_FILE);
		struct json_object *config =json_tokener_parse(buffer);
		if (!config) {
			flock(fd, LOCK_UN);
			fclose(fp);
			struct json_object *e = json_object_new_object();
			json_object_object_add(e, "error", json_object_new_string("cfg file broken'"));
			int ret = send_json(conn, e, MHD_HTTP_BAD_REQUEST);
			json_object_put(e);  // ✅ 正確釋放
			return ret;
		}
		free(buffer);
		
		struct json_object *query = json_tokener_parse(info->data);
		if (!query) {
			flock(fd, LOCK_UN);
			fclose(fp);
			json_object_put(config);
			struct json_object *e = json_object_new_object();
			json_object_object_add(e, "error", json_object_new_string("Failed to parse user input JSON"));
			int ret = send_json(conn, e, MHD_HTTP_BAD_REQUEST);
			json_object_put(e);  // ✅ 正確釋放
			return ret;
		}
				
		if (strcmp(method, "GET") == 0) {
			if (!json_object_is_type(query, json_type_object)) {
				flock(fd, LOCK_UN);
				fclose(fp);
				json_object_put(config);
				json_object_put(query);
				struct json_object *e = json_object_new_object();
				json_object_object_add(e, "error", json_object_new_string("GET only supports a single query object"));
				int ret = send_json(conn, e, MHD_HTTP_BAD_REQUEST);
				json_object_put(e);  // ✅ 正確釋放
				return ret;
			}

			if (!is_valid_query(query) || !is_get_query(query)) {
				flock(fd, LOCK_UN);
				fclose(fp);
				json_object_put(config);
				json_object_put(query);
				struct json_object *e = json_object_new_object();
				json_object_object_add(e, "error", json_object_new_string("Invalid GET query format"));
				int ret = send_json(conn, e, MHD_HTTP_BAD_REQUEST);
				json_object_put(e);  // ✅ 正確釋放
				return ret;
			}
		
			struct json_object *result = query_json(config, query);
			if (result) {
				flock(fd, LOCK_UN);
				fclose(fp);
				int ret = send_json(conn, result, MHD_HTTP_OK);
				json_object_put(result);  // ✅ 正確釋放
				return ret;
			} else {
				flock(fd, LOCK_UN);
				fclose(fp);
				struct json_object *e = json_object_new_object();
				json_object_object_add(e, "error", json_object_new_string("No matching keys found"));
				int ret = send_json(conn, e, MHD_HTTP_BAD_REQUEST);
				json_object_put(e);  // ✅ 正確釋放
				return ret;
			}
		} else if( strcmp(method, "PUT") == 0 )
		{
			// 檢查單筆或多筆 query
			if (json_object_is_type(query, json_type_object)) {
				// 單筆 PUT
				if (!is_valid_query(query)) {
					flock(fd, LOCK_UN);
					fclose(fp);
					json_object_put(config);
					json_object_put(query);
					struct json_object *e = json_object_new_object();
					json_object_object_add(e, "error", json_object_new_string("Invalid single PUT query"));
					int ret = send_json(conn, e, MHD_HTTP_BAD_REQUEST);
					json_object_put(e);  // ✅ 正確釋放
					return ret;
				}

				if (!update_json(config, query)) {
					flock(fd, LOCK_UN);
					fclose(fp);
					json_object_put(config);
					json_object_put(query);
					struct json_object *e = json_object_new_object();
					json_object_object_add(e, "error", json_object_new_string("Update failed for single PUT"));
					int ret = send_json(conn, e, MHD_HTTP_BAD_REQUEST);
					json_object_put(e);  // ✅ 正確釋放
					return ret;
				}
			}
			else if (json_object_is_type(query, json_type_array)) {
				int len = json_object_array_length(query);
				if (len <= 0 || len > 12) {
					flock(fd, LOCK_UN);
					fclose(fp);
					json_object_put(config);
					json_object_put(query);
					struct json_object *e = json_object_new_object();
					json_object_object_add(e, "error", json_object_new_string("PUT array must be 1~12 items"));
					int ret = send_json(conn, e, MHD_HTTP_BAD_REQUEST);
					json_object_put(e);  // ✅ 正確釋放
					return ret;
				}

				// 預先檢查每一筆是否合法
				for (int i = 0; i < len; i++) {
					struct json_object *item = json_object_array_get_idx(query, i);
					if (!json_object_is_type(item, json_type_object) || !is_valid_query(item)) {
						flock(fd, LOCK_UN);
						fclose(fp);
						json_object_put(config);
						json_object_put(query);
						struct json_object *e = json_object_new_object();
						json_object_object_add(e, "error", json_object_new_string("One or more queries in PUT array are invalid"));
						int ret = send_json(conn, e, MHD_HTTP_BAD_REQUEST);
						json_object_put(e);  // ✅ 正確釋放
						return ret;
					}
				}

				// 全部合法，逐筆執行更新
				for (int i = 0; i < len; i++) {
					struct json_object *item = json_object_array_get_idx(query, i);
					if (!update_json(config, item)) {
						flock(fd, LOCK_UN);
						fclose(fp);
						json_object_put(config);
						json_object_put(query);
						struct json_object *e = json_object_new_object();
						json_object_object_add(e, "error", json_object_new_string("Update failed in PUT array"));
						int ret = send_json(conn, e, MHD_HTTP_BAD_REQUEST);
						json_object_put(e);  // ✅ 正確釋放
						return ret;
					}
				}
			}
			else {
				flock(fd, LOCK_UN);
				fclose(fp);		
				json_object_put(config);
				json_object_put(query);
				struct json_object *e = json_object_new_object();
				json_object_object_add(e, "error", json_object_new_string("Unsupported PUT JSON type"));
				int ret = send_json(conn, e, MHD_HTTP_BAD_REQUEST);
				json_object_put(e);  // ✅ 正確釋放
				return ret;
			}

			const char *new_json = json_object_to_json_string_ext(config, JSON_C_TO_STRING_PRETTY);
			if (!new_json) {
				flock(fd, LOCK_UN);
				fclose(fp);
				struct json_object *e = json_object_new_object();
				json_object_object_add(e, "error", json_object_new_string("Failed NO2"));
				int ret = send_json(conn, e, MHD_HTTP_BAD_REQUEST);
				json_object_put(e);
				return ret;
			}
			rewind(fp);
			if (ftruncate(fd, 0) != 0 ||
				fwrite(new_json, 1, strlen(new_json), fp) != strlen(new_json) ||
				fflush(fp) != 0 ||
				fsync(fd) != 0) {
				flock(fd, LOCK_UN);
				fclose(fp);
				struct json_object *e = json_object_new_object();
				json_object_object_add(e, "error", json_object_new_string("Failed NO3"));
				int ret = send_json(conn, e, MHD_HTTP_BAD_REQUEST);
				json_object_put(e);
				return ret;
			}
			
			flock(fd, LOCK_UN);
			fclose(fp);
			
			if (sys_cfg_change){
				char cmd[256];
				snprintf(cmd, sizeof(cmd), "sudo %s", MODIFY_SYSIP_NETPLAN);
				int ret = system(cmd);
				if (ret == -1) {
					struct json_object *e = json_object_new_object();
					json_object_object_add(e, "error", json_object_new_string("Failed NO4"));
					int ret = send_json(conn, e, MHD_HTTP_BAD_REQUEST);
					json_object_put(e);
					return ret;
				}
				sys_cfg_change = false;
			}
			
			struct json_object *res = json_object_new_object();
			json_object_object_add(res, "PUT", json_object_new_string("updated successfully"));
			int ret = send_json(conn, res, MHD_HTTP_OK);
			json_object_put(res);  // ✅ 正確釋放
			return ret;
		} 
	}

	if (strcmp(method, "GET") == 0 && strncmp(url, "/api/listfiles", 14) == 0) {
			int fd = open(REC_FILE_LOCK, O_RDWR | O_CREAT, 0664);
			if (flock(fd, LOCK_EX) < 0) {
				close(fd);
				return 1;
			}
			int ret = send_file_list(conn);
			flock(fd, LOCK_UN);
			close(fd);
            return ret;
	}
	
    if (strcmp(method, "GET") == 0 && strncmp(url, "/api/download", 13) == 0) {
        const char *filename = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "file");
        if (filename) {
			int fd = open(REC_FILE_LOCK, O_RDWR | O_CREAT, 0664);
			if (flock(fd, LOCK_EX) < 0) {
				close(fd);
				return 1;
			}
			int ret = download_rec_file(conn, filename);
			flock(fd, LOCK_UN);
			close(fd);
            return ret;
        }
    }
	
	if (strcmp(method, "DELETE") == 0 && strncmp(url, "/api/del_rec_file", 17) == 0) {
		const char *filename = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "file");
		if (filename) {			
			int fd = open(REC_FILE_LOCK, O_RDWR | O_CREAT, 0664);
			if (flock(fd, LOCK_EX) < 0) {
				close(fd);
				return 1;
			}
			int ret = delete_rec_file(conn, filename);
			flock(fd, LOCK_UN);
			close(fd);
            return ret;
		}
	}

    if (strcmp(method, "PUT") == 0 && strncmp(url, "/api/RestartProgram_camera", 26) == 0) {
		char cmd[256];
		snprintf(cmd, sizeof(cmd), "sudo %s", RESTART_PROGRAM_CAMERA);
        int rs = system(cmd);
		if ( rs != -1 ){
			struct json_object *ok = json_object_new_object();
			json_object_object_add(ok, "status", json_object_new_string("Restart Program"));
			int ret = send_json(conn, ok, MHD_HTTP_OK);
			json_object_put(ok);
			return ret;
		}
    }
	
    if (strcmp(method, "PUT") == 0 && strncmp(url, "/api/RebootSystem", 17) == 0) {
		char cmd[256];
		snprintf(cmd, sizeof(cmd), "sudo %s", REBOOT_SYSTEM);
        int ret = system(cmd);
		return ret;
    }
	
    struct json_object *res = json_object_new_object();
    json_object_object_add(res, "error", json_object_new_string("Not found"));
    int ret = send_json(conn, res, MHD_HTTP_NOT_FOUND);
	json_object_put(res);
	return ret;
}

static void request_done(void *cls, struct MHD_Connection *conn, void **con_cls, enum MHD_RequestTerminationCode toe) {
    if (*con_cls) {
        struct ConnectionInfo *info = *con_cls;
        free(info->data);
        free(info);
    }
}

void *session_cleaner_thread(void *arg) {
    while (1) {
        sleep(5);
        pthread_mutex_lock(&lock);

        struct json_object_iterator it = json_object_iter_begin(session_map);
        struct json_object_iterator end = json_object_iter_end(session_map);
        time_t now = time(NULL);
        const char *to_delete[128];
        int count = 0;
#if 0
限制最大連線數
#endif
        while (!json_object_iter_equal(&it, &end) && count < 128) {
            const char *key = json_object_iter_peek_name(&it);
            struct json_object *val = json_object_iter_peek_value(&it);
            struct json_object *last_obj;
            if (json_object_object_get_ex(val, "last_used", &last_obj)) {
                time_t last = (time_t)json_object_get_int64(last_obj);
                if (difftime(now, last) > TOKEN_EXPIRY_SECONDS) {
                    to_delete[count++] = key;
                }
            }
            json_object_iter_next(&it);
        }

        for (int i = 0; i < count; i++) {
            json_object_object_del(session_map, to_delete[i]);
        }

        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

static int renew_ver_info( ){
    // 讀取 JSON 檔案
	int fd = open(API_VER_FILE, O_RDWR);
	if (fd < 0) {
		return 1;
	}
	
	if (flock(fd, LOCK_EX) < 0) {
		close(fd);
		return 1;
	}
	FILE *fp = fdopen(fd, "r+");
    if (!fp) {
        perror("fdopen");
		flock(fd, LOCK_UN);
        close(fd);
        return 1;
    }
	fseek(fp, 0, SEEK_END);
	size_t size = ftell(fp);
	rewind(fp);
	char *buffer = (char*)malloc(size + 1);
	size_t read_bytes = fread(buffer, 1, size, fp);
	if (read_bytes != size) {
		fprintf(stderr, "Warning: only read %zu of %zu bytes from file\n", read_bytes, size);
		flock(fd, LOCK_UN);
		fclose(fp);
		free(buffer);
		return 1;
	}
	buffer[size] = '\0';
	
	struct json_object *root = json_tokener_parse(buffer);
	free(buffer);
    if (!root) {
        fprintf(stderr, "Failed to open or parse JSON file: %s\n", API_VER_FILE);
		flock(fd, LOCK_UN);
		fclose(fp); 
        return 1;
    }

    // 取得 apiinfo 物件
    struct json_object *apiinfo = NULL;
    if (!json_object_object_get_ex(root, "apiinfo", &apiinfo)) {
        fprintf(stderr, "Missing 'apiinfo' object in JSON\n");
        json_object_put(root);
		flock(fd, LOCK_UN);
		fclose(fp);
        return 1;
    }

    // 建立新的 ver 值（使用字串）
    struct json_object *new_ver = json_object_new_string(API_VER);
    json_object_object_add(apiinfo, "ver", new_ver);
    const char *new_json = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
    rewind(fp);
	if (ftruncate(fd, 0) < 0) {
		perror("ftruncate failed");
		json_object_put(root);
		flock(fd, LOCK_UN);
		close(fd);
		return 1;
	}
    fwrite(new_json, 1, strlen(new_json), fp);
    fflush(fp);

	json_object_put(root);  // 釋放記憶體
	flock(fd, LOCK_UN);
	fclose(fp); // 自動 unlock + close
    return 0;
}

int main() {
	renew_ver_info();
    messages = json_object_new_object();
    session_map = json_object_new_object();

    pthread_t tid;
    pthread_create(&tid, NULL, session_cleaner_thread, NULL);

    struct MHD_Daemon *daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD, PORT,
        NULL, NULL,
        (MHD_AccessHandlerCallback)&handler, NULL,
        MHD_OPTION_NOTIFY_COMPLETED, request_done, NULL,
        MHD_OPTION_END
    );

    if (!daemon) {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }

    // printf("Server running on port %d...\n", PORT);
	pause();  // 永久等待訊號（除非收到 signal）

    MHD_stop_daemon(daemon);
    json_object_put(messages);
    json_object_put(session_map);
    return 0;
}
