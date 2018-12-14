//
//  _Venom.h
//  _Venom
//
//  Created by pingwei liu on 2018/11/28.
//  Copyright © 2018 pingwei liu. All rights reserved.
//

#ifndef _Venom_h
#define _Venom_h

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif
 
    typedef struct Venom Venom;
    
    Venom *VenomInit(const char *path);
    void VenomPut(Venom *map, const void *key, uint32_t keyLength, const void *value, uint32_t valueLength, int8_t type);
    const void * VenomGet(Venom *map, const void *key, uint32_t keyLength, uint32_t *valueLength, uint8_t *type);
    void VenomRemove(Venom *map, const void *key, uint32_t keyLength);
    
    void VenomRelease(Venom *map);
    void _VenomDebugPrint(Venom *map);
#ifdef __cplusplus
}
#endif

#endif /* _Venom_h */
