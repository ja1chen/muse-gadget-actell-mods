#pragma once
typedef struct cJSON { int unused; } cJSON;
cJSON *cJSON_CreateObject(void);
void cJSON_AddNumberToObject(cJSON *,const char *,double);
void cJSON_AddStringToObject(cJSON *,const char *,const char *);
char *cJSON_PrintUnformatted(const cJSON *);
void cJSON_Delete(cJSON *);
