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
#include "xxhash/xxhash.h"

void printTime()      //直接调用这个函数就行了，返回值最好是int64_t，long long应该也可以
{
    struct timeval tv;
    gettimeofday(&tv,NULL);    //该函数在sys/time.h头文件中
    printf("%ld  %d\n",tv.tv_sec,tv.tv_usec);
}

int main(int argc, const char * argv[]) {
    // insert code here...
    printTime();
    
    uint32_t testNum = 5000;
    
    KnackMap *map = KnackMapInit();
    for (int i = 0; i < testNum; i++) {
        KnackMapPut(map, &i, 4, &i, 4, 10);
    }
    printTime();
    for (int i = 0; i < testNum; i++) {
        KnackMapGet(map, &i, 4);
    }
    printTime();
    return 0;
}
