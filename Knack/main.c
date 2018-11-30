//
//  main.c
//  Knack
//
//  Created by pingwei liu on 2018/11/28.
//  Copyright © 2018 pingwei liu. All rights reserved.
//

#include <stdio.h>
#import "Knack.h"
#include <sys/time.h>
#include <inttypes.h>
#include <string.h>
#include "xxhash/xxhash.h"

void printTime()      //直接调用这个函数就行了，返回值最好是int64_t，long long应该也可以
{
    struct timeval tv;
    gettimeofday(&tv,NULL);    //该函数在sys/time.h头文件中
    printf("%ld  %d\n",tv.tv_sec,tv.tv_usec);
}

int main(int argc, const char * argv[]) {
    // insert code here...
    KnackMap *map = KnackMapInit();
    
    printTime();
    uint32_t testNum = 5000;
    for (int i = 0; i < testNum; i++) {
        KnackMapPut(map, &i, 4, &i, 4, 10);
    }
    printTime();
    for (int i = 0; i < testNum; i++) {
        uint32_t length = 0;
        uint8_t type = 0;
        const void *value = KnackMapGet(map, &i, 4, &length, &type);
        uint32_t v = 0;
        memcpy(&v, value, 4);
//        printf("%u  %d  %u\n",v,type,length);
    }
    printTime();
    return 0;
}
