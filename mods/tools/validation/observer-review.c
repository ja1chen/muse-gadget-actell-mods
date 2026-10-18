/* Host-only harness: production observer function copied verbatim; locks stubbed. */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define EVENTS 64
#define EVENT_SIZE 400
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
static char (*ring)[EVENT_SIZE]; static uint32_t next_seq; static bool active,oversized_event;
void headless_console_observe(const char *line, size_t len) {
    if (!ring || len < 7 || memcmp(line, "@chat ", 6)) return;
    const char *json = line + 6;
    size_t n = len - 6;
    while (n && (json[n-1] == '\n' || json[n-1] == '\r')) --n;
    if (n >= EVENT_SIZE) { portENTER_CRITICAL(&lock); oversized_event=true; portEXIT_CRITICAL(&lock); return; }
    portENTER_CRITICAL(&lock);
    ++next_seq;
    memcpy(ring[next_seq % EVENTS], json, n);
    ring[next_seq % EVENTS][n] = '\0';
    if (strstr(ring[next_seq % EVENTS], "\"type\":\"done\"") || strstr(ring[next_seq % EVENTS], "\"type\":\"error\"")) active = false;
    portEXIT_CRITICAL(&lock);
}
int main(void){
 headless_console_observe("@chat {}",8);assert(next_seq==0);
 ring=calloc(EVENTS,EVENT_SIZE);assert(ring);
 const char *done="@chat {\"type\":\"done\"}\n";size_t n=strlen(done);char *raw=malloc(n);memcpy(raw,done,n);active=true;headless_console_observe(raw,n);assert(!active);assert(next_seq==1);free(raw);
 const char *txt="@chat {\"type\":\"text\",\"text\":\"safe\"}\n";n=strlen(txt);raw=malloc(n);memcpy(raw,txt,n);active=true;headless_console_observe(raw,n);assert(active);assert(next_seq==2);free(raw);
 char oversized[410];memset(oversized,'x',sizeof oversized);memcpy(oversized,"@chat ",6);headless_console_observe(oversized,sizeof oversized);assert(oversized_event);assert(next_seq==2);
 for(int i=0;i<130;i++){headless_console_observe(done,strlen(done));}
 assert(next_seq==132);assert(!active);
 free(ring);
 puts("PASS isolated observer: non-NUL exact buffer, terminal/nonterminal, oversize signal, ring wrap; ASan/UBSan host-only");}
