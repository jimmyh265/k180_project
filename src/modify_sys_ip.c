#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <json-c/json.h>
#include "user_def_json.h"

const char RF_REG_FILE[] = "/home/fourd/projects/rtsp_server/user_def_setting.json";

int main( int argc, char *argv[]){
	
	char JSON_GET_SYS_IPADDR[16];
	char JSON_GET_SYS_MASK[16];
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
	snprintf(JSON_GET_SYS_MASK, json_object_get_string_len(tmp_obj)+1, "%s", json_object_get_string(tmp_obj));
	printf("System netmask:   %s\n", JSON_GET_SYS_MASK);
	
	ret = json_object_object_get_ex(main_obj, JSON_SYS_GW, &tmp_obj);
	if (!ret) {
		printf("Cannot get %s object\n", JSON_SYS_GW);
	}
	snprintf(JSON_GET_SYS_GW, json_object_get_string_len(tmp_obj)+1, "%s", json_object_get_string(tmp_obj));
	printf("System gw:   %s\n", JSON_GET_SYS_GW);
	
	
	
	
	
	
	
   // pid_t pid = (pid_t)atoi(argv[1]);
   // setuid(0); // for uid to be 0, root
   // kill(pid, 9);
   // system("/home/fourd/projects/rtsp_server/build/gy 1 1");
   return 0; // just to avoid the warning (since never returns)
}
