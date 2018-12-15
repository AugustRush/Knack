//
//  VenomSerial.h
//  Venom
//
//  Created by pingwei liu on 2018/12/15.
//  Copyright © 2018 pingwei liu. All rights reserved.
//

#ifndef VenomSerial_h
#define VenomSerial_h

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "VenomMacors.h"

#define VN_M (4)
#define VN_LIMIT_M_2 (VN_M % 2 ? (VN_M + 1)/2 : VN_M/2)

VN_EXTERN_C_BEGIN

typedef struct VMNode *VMTree,*Position;
typedef uint32_t KeyType;
/* 初始化 */
extern VMTree Initialize(void);
/* 插入 */
extern VMTree Insert(VMTree T,KeyType Key);
/* 删除 */
extern VMTree Remove(VMTree T,KeyType Key);
/* 销毁 */
extern VMTree Destroy(VMTree T);
/* 遍历节点 */
extern void Travel(VMTree T);

VN_EXTERN_C_END

#endif /* VenomSerial_h */
