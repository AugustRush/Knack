//
//  Knack.h
//  Knack
//
//  Created by pingwei liu on 2018/11/28.
//  Copyright © 2018 pingwei liu. All rights reserved.
//

#ifndef Knack_h
#define Knack_h

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
 
    typedef struct KnackMap KnackMap;
    
    typedef struct KnackResult {
        uint8_t type;
        uint32_t valueLength;
        void *value;
    } KnackResult;
    
    KnackMap *KnackMapInit(void);
    void KnackMapPut(KnackMap *map, const void *key, uint32_t keyLength, const void *value, uint32_t valueLength, int8_t type);
    const KnackResult *KnackMapGet(KnackMap *map, const void *key, uint32_t keyLength);
    
#ifdef __cplusplus
}
#endif

#endif /* Knack_h */
