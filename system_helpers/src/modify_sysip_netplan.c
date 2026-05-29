#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <json-c/json.h>
#include <sys/file.h>  // flock()
#include <fcntl.h>     // open()

#define K180_CONFIG_DIR "/etc/k180"
#define MEMBER_SYSCFG "system_cfg"
#define JSON_SYS_IPADDR "ipaddress"
#define JSON_SYS_MASK "netmask"
#define JSON_SYS_GW "gateway"

int main( int argc, char *argv[]){
	
	FILE *file;
	char name[] = K180_CONFIG_DIR "/01-netcfg.yaml";
	char RF_REG_FILE[] = K180_CONFIG_DIR "/user_def_setting.json";
	char rescue_ip[] = K180_CONFIG_DIR "/rescue_sys_ip.json";
	char JSON_GET_SYS_IPADDR[16];
	int JSON_GET_SYS_MASK;
	char JSON_GET_SYS_GW[16];
	char JSON_GET_REC_IP[16];
	int ret = 0;
	json_object *root_obj = NULL;
	json_object *main_obj = NULL;
	json_object *tmp_obj = NULL;

	int fd = open(RF_REG_FILE, O_RDWR);  // RW 模式，因為可能要寫入
	if (fd < 0) {
		printf("Cannot open %s. rf_reg_file\n", RF_REG_FILE);
		exit(0);
	}

	if (flock(fd, LOCK_EX) < 0) {
		close(fd);
		struct json_object *e = json_object_new_object();
		json_object_object_add(e, "error", json_object_new_string("Failed to lock config file"));
		printf("Failed to lock config file %s.\n", RF_REG_FILE);
		exit(0);
	}
		
	root_obj = json_object_from_file(RF_REG_FILE);
	if (!root_obj) {
		flock(fd, LOCK_UN);
		close(fd);
		printf("Cannot open %s. rf_reg_file\n", RF_REG_FILE);
		exit(0);
	}
	
	ret = json_object_object_get_ex(root_obj, MEMBER_SYSCFG, &main_obj);
	if (!ret) {
		printf("Cannot get %s object\n", MEMBER_SYSCFG);
	}
	
	ret = json_object_object_get_ex(main_obj, JSON_SYS_IPADDR, &tmp_obj);
	if (!ret) {
		printf("Cannot get %s object\n", JSON_SYS_IPADDR);
	}
	snprintf(JSON_GET_SYS_IPADDR, json_object_get_string_len(tmp_obj)+1, "%s", json_object_get_string(tmp_obj));
	printf("System ip address:   %s\n", JSON_GET_SYS_IPADDR);
	
	ret = json_object_object_get_ex(main_obj, JSON_SYS_MASK, &tmp_obj);
	if (!ret) {
		printf("Cannot get %s object\n", JSON_SYS_MASK);
	}
	JSON_GET_SYS_MASK = json_object_get_int(tmp_obj);
	printf("System netmask:   %d\n", JSON_GET_SYS_MASK);
	
	ret = json_object_object_get_ex(main_obj, JSON_SYS_GW, &tmp_obj);
	if (!ret) {
		printf("Cannot get %s object\n", JSON_SYS_GW);
	}
	snprintf(JSON_GET_SYS_GW, json_object_get_string_len(tmp_obj)+1, "%s", json_object_get_string(tmp_obj));
	printf("System gw:   %s\n", JSON_GET_SYS_GW);
	
	flock(fd, LOCK_UN);
	close(fd);
	// ----------------------
	root_obj = json_object_from_file(rescue_ip);
	if (!root_obj) {
		printf("Cannot open %s. rescue_sys_ip.json\n", rescue_ip);
		exit(0);
	}
	
	ret = json_object_object_get_ex(root_obj, "system_cfg", &main_obj);
	if (!ret) {
		printf("Cannot get %s object\n", "system_cfg");
	}
	
	ret = json_object_object_get_ex(main_obj, "rescueip", &tmp_obj);
	if (!ret) {
		printf("Cannot get %s object\n", "rescueip");
	}
	snprintf(JSON_GET_REC_IP, json_object_get_string_len(tmp_obj)+1, "%s", json_object_get_string(tmp_obj));
	printf("rescue ip address:   %s\n", JSON_GET_REC_IP);
/*
network:
  version: 2
  renderer: NetworkManager
  ethernets:
    eth0:
      addresses:
      - 192.168.0.91/24
      routes:
        - to: 0.0.0.0/0
          via: 192.168.0.254
    eth1:
      addresses: [192.168.145.91/24]

*/	
	file = fopen(name, "w");

    int fd2 = fileno(file);  // 取得底層 file descriptor

    // 加上排他鎖（阻塞直到成功）
    if (flock(fd2, LOCK_EX) != 0) {
        perror("flock failed");
        fclose(file);
        exit(0);
    }
	
	fprintf(file, "network:\n");
	fprintf(file, "  version: 2\n");
	fprintf(file, "  renderer: NetworkManager\n");
	fprintf(file, "  ethernets:\n");
	fprintf(file, "    eth0:\n");
	fprintf(file, "      addresses:\n");
	fprintf(file, "      - %s/%d\n", JSON_GET_SYS_IPADDR, JSON_GET_SYS_MASK );
	fprintf(file, "      routes:\n" );
	fprintf(file, "        - to: 0.0.0.0/0\n" );
	fprintf(file, "          via: %s\n", JSON_GET_SYS_GW );
	fprintf(file, "    eth1:\n");
	fprintf(file, "      addresses:\n");
	fprintf(file, "      - %s/24\n", JSON_GET_REC_IP );
	fflush(file);
    flock(fd2, LOCK_UN);
    fclose(file);
	return 0; // just to avoid the warning (since never returns)
}
