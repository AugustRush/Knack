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
#define VN_EXTERN_C_BEGIN extern "C" {
#define VN_EXTERN_C_END }
#else
#define VN_EXTERN_C_BEGIN
#define VN_EXTERN_C_END
#endif

VN_EXTERN_C_BEGIN

typedef struct Venom Venom;

Venom *VenomInit(const char *path);
void VenomPut(Venom *map, const void *key, uint32_t keyLength, const void *value, uint32_t valueLength, int8_t type);
const void * VenomGet(Venom *map, const void *key, uint32_t keyLength, uint32_t *valueLength, uint8_t *type);
void VenomRemove(Venom *map, const void *key, uint32_t keyLength);

void VenomRelease(Venom *map);
void VenomDebugPrint(Venom *map);

VN_EXTERN_C_END

#endif /* _Venom_h */
