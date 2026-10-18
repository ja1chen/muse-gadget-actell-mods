#pragma once
#include <arpa/inet.h>
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(p) ((const unsigned char *)&(p)->addr)[0], ((const unsigned char *)&(p)->addr)[1], ((const unsigned char *)&(p)->addr)[2], ((const unsigned char *)&(p)->addr)[3]
