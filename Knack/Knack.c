//
//  Knack.c
//  Knack
//
//  Created by pingwei liu on 2018/11/28.
//  Copyright © 2018 pingwei liu. All rights reserved.
//

#include "Knack.h"
#include "xxhash/xxhash.h"
#include <string.h>

#define KNACK_DEGREE 2
#define KNACK_ORDER (KNACK_DEGREE * 2)
#define KNACK_NODE_MAX (KNACK_ORDER - 1)
#define PAGE_SIZE 4096

typedef int8_t Byte;

typedef struct __attribute__((__packed__)) KnackNode {
    uint32_t hash;
    uint32_t keyLength;
    uint32_t valueLength;
    uint32_t contentOffset;
//    uint32_t leaf_lhs_loc;
//    uint32_t leaf_rhs_loc;
} KnackNode;

typedef struct __attribute__((__packed__)) KnackPiece {
    unsigned int count : 7;
    unsigned int isLeaf : 1;
    uint32_t loc;
//    uint32_t super_piece_loc;
    KnackNode nodes[KNACK_ORDER - 1];
    uint32_t children[KNACK_ORDER];
    
} KnackPiece;

typedef struct __attribute__((__packed__)) KnackHeader {
    uint32_t nodeCount;
    uint32_t totalSize;
    uint32_t headLoc;
    uint32_t pieceStart;
    uint32_t pieceCount;
    uint32_t contentStart;
    uint32_t contentUsed;
} KnackHeader;

typedef struct KnackMap {
    Byte *memory;
    KnackHeader *header;
    KnackPiece *pieces;
    KnackPiece *headPiece;
    Byte *contents;
} KnackMap;


const size_t KNACK_NODE_SIZE = sizeof(KnackNode);



KnackPiece *KnackGetPieceAtLoc(KnackMap *map, uint32_t loc) {
    return map->pieces + loc;
}

KnackPiece *KnackCreateNewPiece(KnackMap *map) {
    uint32_t afterUsed = map->header->pieceCount + 1;
    if (map->header->pieceCount * KNACK_NODE_SIZE + map->header->pieceStart > map->header->contentStart) {
        printf("need to extend.");
    }
    //
    map->header->pieceCount = afterUsed;
    //
    KnackPiece *piece = map->pieces + afterUsed - 1;
    piece->count = 0;
    piece->loc = afterUsed - 1;
    
    return piece;
}

void _KnackDebugPrint(KnackMap *map, KnackPiece *head) {
    if (map != NULL && head->count > 0) {
        printf("level-%d:",head->loc);
        for (int i = 0; i < head->count; i++) {
            printf(" %d",head->nodes[i].hash);
        }
        printf("\n");
        
        for (int i = 0; i <= head->count; i++) {
            uint32_t index = head->children[i];
            if (index != head->loc) {
                KnackPiece *piece = KnackGetPieceAtLoc(map, head->children[i]);
                _KnackDebugPrint(map, piece);
            }
        }
    }
}

void KnackDebugPrint(KnackMap *map, KnackPiece *head) {
    _KnackDebugPrint(map,head);
    printf("====================\n");
}

KnackMap *KnackMapInit(void) {
    KnackMap *map = malloc(sizeof(KnackMap));
    map->memory = calloc(2, 4096);
    map->header = (KnackHeader *)map->memory;
    map->header->nodeCount = 0;
    map->header->headLoc = 0;
    map->header->pieceStart = sizeof(KnackHeader);
    map->header->pieceCount = 1;
    map->header->contentStart = PAGE_SIZE;
    map->header->contentUsed = 0;
    map->pieces = (KnackPiece *)(map->memory + map->header->pieceStart);
    map->headPiece = map->pieces + map->header->headLoc;
    map->headPiece->isLeaf = 1;
    map->headPiece->loc = map->header->headLoc;
    map->contents = map->memory + map->header->contentStart;
    return map;
}

KnackPiece *KnackFindSibling(KnackMap *map,KnackPiece *parent) {
    KnackPiece *piece = NULL;
    if (parent != NULL) {
        for (int i = 0; i <= parent->count; i++) {
            KnackPiece *current = KnackGetPieceAtLoc(map, parent->children[i]);
            if (current->count < KNACK_NODE_MAX) {
                piece = current;
                break;
            }
        }
    }
    return piece;
}

void KnackSplitLeaf(KnackMap *map,KnackPiece *parent, uint32_t index, KnackPiece *current) {
//    KnackPiece *newPiece = KnackCreateNewPiece(map);
//    newPiece->isLeaf = current->isLeaf;
//    newPiece->count = KNACK_DEGREE - 1;
//    //
//    memcpy(newPiece->nodes, current->nodes + KNACK_DEGREE, newPiece->count);
//    //
//    if (!current->isLeaf) {
//        memcpy(newPiece->nodes, current->nodes + KNACK_DEGREE, KNACK_DEGREE);
//    }
//
//    current->count = KNACK_DEGREE - 1;
//
//    for (int i = parent->count; i > index; --i) {
//        parent->children[i + 1] = parent->children[i];
//    }
//
//    parent->children[index + 1] = newPiece->loc;
//    int moveCount = parent->count - index - 1;
//    if (moveCount > 0) {
//        memmove(parent->nodes + index + 1, parent->nodes + index, moveCount * KNACK_NODE_SIZE);
//    }
//    //
//    memcpy(parent->nodes + index, current->nodes + KNACK_DEGREE - 1, KNACK_NODE_SIZE);
//    parent->count += 1;
    
    KnackPiece *newPiece = KnackCreateNewPiece(map);
    newPiece->isLeaf = current->isLeaf;
    
    int mid = current->count / 2;
    int copyCount = current->count - mid - 1;
    memcpy(newPiece->nodes, current->nodes + mid + 1, copyCount * KNACK_NODE_SIZE);
    newPiece->count = copyCount;
    //
//    if (!current->isLeaf) {
//        memcpy(newPiece->nodes, current->nodes + KNACK_DEGREE, KNACK_DEGREE);
//    }
    
    current->count -= (copyCount + 1);
    
    for (int i = parent->count; i > index; --i) {
        parent->children[i + 1] = parent->children[i];
    }
    
    parent->children[index + 1] = newPiece->loc;
    int moveCount = parent->count - index;
    if (moveCount > 0) {
        memmove(parent->nodes + index + 1, parent->nodes + index, moveCount * KNACK_NODE_SIZE);
    }
    //
    memcpy(parent->nodes + index, current->nodes + mid, KNACK_NODE_SIZE);
    parent->count += 1;
}

void KnackInsertNotFull(KnackMap *map, KnackPiece *parent, KnackPiece *current, uint32_t hash, uint32_t keyLength, uint32_t valueLength, uint32_t contentOffset) {
    if (current->isLeaf) {
        int i = 0;
        while (i < current->count && hash > current->nodes[i].hash) {
            i++;
        }
        int copyCount = current->count - i;
        if (copyCount > 0) {
            memmove(current->nodes + i + 1, current->nodes + i, copyCount *KNACK_NODE_SIZE);
        }
        KnackNode *node = current->nodes + i;
        node->hash = hash;
        node->keyLength = keyLength;
        node->valueLength = valueLength;
        node->contentOffset = contentOffset;
        current->count += 1;
    } else {
        int i = 0;
        while (i < current->count && hash > current->nodes[i].hash) {
            i++;
        }
        KnackPiece *leaf = KnackGetPieceAtLoc(map, current->children[i]);
        if (leaf->count == KNACK_NODE_MAX) {
            
//            KnackPiece *sibling = KnackFindSibling(map, current);
            
            KnackSplitLeaf(map,current, i, leaf);
            if (hash > current->nodes[i].hash) {
                ++i;
                leaf = KnackGetPieceAtLoc(map, current->children[i]);
            }
        }
        KnackInsertNotFull(map, current, leaf, hash, keyLength, valueLength, contentOffset);
    }
}


void KnackMapPut(KnackMap *map, const void *key, uint32_t keyLength, const void *value, uint32_t valueLength, uint8_t type) {
    if (map != NULL) {
        uint32_t hash = XXH32(key, keyLength, 0);
        static uint32_t hs = 30;
        hash = hs--;
        
        KnackDebugPrint(map,map->headPiece);
        //
        KnackPiece *root = map->headPiece;
        if (root->count == KNACK_NODE_MAX) {
            KnackPiece *newRoot = KnackCreateNewPiece(map);
            newRoot->isLeaf = 0;
            newRoot->count = 0;
            newRoot->children[0] = root->loc;
            //
            map->header->headLoc = newRoot->loc;
            map->headPiece = newRoot;
            //
            KnackSplitLeaf(map, newRoot, 0, root);
            KnackInsertNotFull(map, NULL, newRoot, hash, keyLength, valueLength, map->header->contentUsed);
        } else {
            KnackInsertNotFull(map, NULL, root, hash, keyLength, valueLength, map->header->contentUsed);
        }
        //insert contents
        
    }
}
