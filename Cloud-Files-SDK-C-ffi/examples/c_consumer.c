#include "CloudFilesSDKJsonSDK.h"
#include <string.h>
int main(void){uint64_t session=0;if(cloud_files_sdk_json_open(&session))return 1;char *reply=cloud_files_sdk_json_call(session,"{",1);if(!reply){cloud_files_sdk_json_close(session);return 2;}int failed=strstr(reply,"\"ok\":false")==0;cloud_files_sdk_json_free(reply);if(cloud_files_sdk_json_close(session))return 3;return failed;}
