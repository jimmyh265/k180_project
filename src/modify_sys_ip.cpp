#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/socket.h>
 #include <arpa/inet.h>
#include <json-c/json.h>
#include "user_def_json.h"

const char RF_REG_FILE[] = "/home/fourd/projects/rtsp_server/user_def_setting.json";

int main( int argc, char *argv[]){
	
	FILE *file;
	char name[] = "01-netcfg-jj.yaml";
	char JSON_GET_SYS_IPADDR[16];
	int JSON_GET_SYS_MASK;
	char JSON_GET_SYS_GW[16];
	int ret = 0;
	json_object *root_obj = NULL;
	json_object *main_obj = NULL;
	json_object *tmp_obj = NULL;
	root_obj = json_object_from_file(RF_REG_FILE);
	if (!root_obj) {
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
	
/*
network:
  version: 2
  renderer: NetworkManager
  ethernets:
    eth0:
      addresses:
      - 192.168.1.92/24
      gateway4: 192.168.1.87
*/	
	file = fopen(name, "w");
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
	fprintf(file, "      addresses: [192.168.145.91/24]\n");
	 fclose(file);
	 setuid(0);
	system("reboot");
	 
   return 0; // just to avoid the warning (since never returns)
}
