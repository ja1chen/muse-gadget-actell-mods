#pragma once
#include <assert.h>
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) do { assert(*(p)==0); *(p)=1; } while(0)
#define portEXIT_CRITICAL(p) do { assert(*(p)==1); *(p)=0; } while(0)
