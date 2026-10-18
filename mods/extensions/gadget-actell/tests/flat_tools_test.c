/* SPDX-License-Identifier: Apache-2.0 */
/* Synthetic schema and mapping tests; no network, pairing or phone access. */
#include "actell_flat.h"
#include "actell_bridge.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static cJSON *object(const char *s){cJSON *v=cJSON_Parse(s);assert(v);return v;}
static void valid(const char *name,const char *json,bool expected){cJSON *v=object(json);assert(actell_flat_validate(name,v)==expected);cJSON_Delete(v);}
int main(void){
 const char *bad="{\"text\":\"\\u0000\"}";assert(!actell_bridge_json_safe(bad,strlen(bad)));
 const char *literal="{\"text\":\"\\\\u0000\"}";assert(actell_bridge_json_safe(literal,strlen(literal)));
 char nonterminated[]={'{','}'};assert(actell_bridge_json_safe(nonterminated,sizeof(nonterminated)));
 const char malformed[]={'{', '\xc0','\x80','}'};assert(!actell_bridge_json_safe(malformed,sizeof(malformed)));
 valid("actell.screenshot","{}",true);valid("actell.list_apps","{}",true);
 valid("actell.screenshot","{\"run_id\":\"old\"}",false);
 valid("actell.tap","{\"x\":0,\"y\":1000,\"purpose\":\"Open item\"}",true);
 valid("actell.tap","{\"x\":true,\"y\":1000,\"purpose\":\"Open\"}",false);
 valid("actell.tap","{\"x\":0.5,\"y\":1000,\"purpose\":\"Open\"}",false);
 valid("actell.tap","{\"x\":-1,\"y\":1000,\"purpose\":\"Open\"}",false);
 valid("actell.tap","{\"x\":0,\"y\":1001,\"purpose\":\"Open\"}",false);
 valid("actell.tap","{\"x\":0,\"x\":1,\"y\":1000,\"purpose\":\"Open\"}",false);
 valid("actell.tap","{\"x\":0,\"y\":1000,\"purpose\":\" \"}",false);
 valid("actell.tap","{\"x\":0,\"y\":1000,\"purpose\":\"Open\",\"image_id\":\"old\"}",false);
 valid("actell.drag","{\"from_x\":0,\"from_y\":1000,\"to_x\":1000,\"to_y\":0,\"duration_ms\":100,\"purpose\":\"Scroll\"}",true);
 valid("actell.drag","{\"from_x\":0,\"from_y\":1000,\"to_x\":1000,\"to_y\":0,\"duration_ms\":2001,\"purpose\":\"Scroll\"}",false);
 valid("actell.launch_app","{\"bundle_id\":\"test.example-app\",\"purpose\":\"Open app\"}",true);
 valid("actell.launch_app","{\"bundle_id\":\"test.example\",\"purpose\":\"Open app\",\"arguments\":[]}",false);
 valid("actell.launch_app","{\"bundle_id\":\"test/app\",\"purpose\":\"Open app\"}",false);
 valid("actell.type_text","{\"text\":\"测试\",\"purpose\":\"Fill field\"}",true);
 valid("actell.type_text","{\"text\":\"\",\"purpose\":\"Fill field\"}",false);
 valid("actell.wait","{\"duration_ms\":1}",true);valid("actell.wait","{\"duration_ms\":3001}",false);
 valid("actell.call_status","{\"operation_id\":\"synthetic-operation\"}",true);
 valid("actell.call_status","{\"operation_id\":\"invalid/id\"}",false);
 assert(!actell_flat_command("actell.rpc")&&!actell_flat_command("actell.image")&&!actell_flat_command("actell.view"));
 cJSON *caps=object("{\"independent_input\":true,\"input_contracts\":[\"normalized-display-v1\"]}");assert(actell_flat_capability(caps));cJSON_Delete(caps);
 caps=object("{\"independent_input\":true,\"input_contracts\":[\"original-v1\"]}");assert(!actell_flat_capability(caps));cJSON_Delete(caps);
 caps=object("{\"independent_input\":1,\"input_contracts\":[\"normalized-display-v1\"]}");assert(!actell_flat_capability(caps));cJSON_Delete(caps);
 cJSON *args=object("{\"x\":250,\"y\":750,\"purpose\":\"Open item\"}");
 cJSON *p=actell_flat_call("actell.tap",args,"run-synthetic","call-synthetic",1234567890000.0);assert(p);
 assert(!cJSON_GetObjectItemCaseSensitive(p,"observation"));
 assert(!strcmp(cJSON_GetObjectItemCaseSensitive(p,"tool")->valuestring,"tap"));
 cJSON *copy=cJSON_GetObjectItemCaseSensitive(p,"arguments");assert(copy!=args&&cJSON_GetObjectItemCaseSensitive(copy,"x")->valueint==250);
 assert(!actell_flat_call("actell.tap",args,"bad/run","call-synthetic",123));
 assert(!actell_flat_call("actell.tap",args,"run-synthetic","call-synthetic",NAN));cJSON_Delete(p);cJSON_Delete(args);
 cJSON *v=cJSON_CreateObject();char *large=malloc(8194);assert(large);memset(large,'x',8193);large[8193]=0;
 cJSON_AddStringToObject(v,"text",large);cJSON_AddStringToObject(v,"purpose","Fill field");assert(!actell_flat_validate("actell.type_text",v));cJSON_Delete(v);free(large);
 v=cJSON_CreateObject();cJSON_AddStringToObject(v,"text","\xc0\x80");cJSON_AddStringToObject(v,"purpose","Fill field");assert(!actell_flat_validate("actell.type_text",v));cJSON_Delete(v);
 v=cJSON_CreateObject();cJSON_AddStringToObject(v,"purpose","Open");cJSON_AddNumberToObject(v,"x",INFINITY);cJSON_AddNumberToObject(v,"y",0);assert(!actell_flat_validate("actell.tap",v));cJSON_Delete(v);
 cJSON *commands=cJSON_CreateObject();actell_flat_register(commands,true);assert(cJSON_GetArraySize(commands)==9);
 for(cJSON *tool=commands->child;tool;tool=tool->next){cJSON *required=cJSON_GetObjectItemCaseSensitive(tool,"required");for(cJSON *f=required->child;f;f=f->next)assert(strcmp(f->string,"run_id")&&strcmp(f->string,"image_id")&&strcmp(f->string,"generation")&&strcmp(f->string,"geometry_revision"));}
 cJSON_Delete(commands);commands=cJSON_CreateObject();actell_flat_register(commands,false);assert(cJSON_GetArraySize(commands)==8&&!cJSON_GetObjectItemCaseSensitive(commands,"actell.screenshot"));cJSON_Delete(commands);
 puts("flat tool candidate: boundaries, capability gate, no screenshot coupling and schema passed");return 0;
}
