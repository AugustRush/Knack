//
//  main.c
//  Knack
//
//  Created by pingwei liu on 2018/11/28.
//  Copyright © 2018 pingwei liu. All rights reserved.
//

#include <stdio.h>
#import "Knack.h"

int main(int argc, const char * argv[]) {
    // insert code here...
    printf("Hello, World!\n");
    KnackMap *map = KnackMapInit();
    for (int i = 0; i < 50; i++) {
        KnackMapPut(map, &i, 4, &i, 4, 10);
    }
    return 0;
}
