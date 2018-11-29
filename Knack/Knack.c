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

KnackPiece *KnackCreatePieceAtLoc(KnackMap *map, uint32_t loc) {
    map->header->pieceCount += 1;
    if (map->header->pieceCount * KNACK_NODE_SIZE + map->header->pieceStart > map->header->contentStart) {
        printf("need to extend.");
    }
    KnackPiece *piece = map->pieces + loc;
    piece->count = 0;
    piece->loc = loc;
    return piece;
}

void debugPrint(KnackMap *map) {
    if (map != NULL) {
        for (int i = 0; i < map->header->pieceCount; i++) {
            KnackPiece *piece = KnackGetPieceAtLoc(map, i);
            printf("level:-%d  ",i);
            for (int j = 0; j < piece->count; j++) {
                KnackNode node = piece->nodes[j];
                printf("%d ", node.hash);
            }
            printf("\n");
        }
    }
    
    printf("#######################\n");
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

void KnackSplitLeaf(KnackMap *map,KnackPiece *parent, uint32_t index, KnackPiece *current) {
    KnackPiece *newPiece = KnackCreatePieceAtLoc(map, map->header->pieceCount);
    newPiece->isLeaf = current->isLeaf;
    newPiece->count = KNACK_DEGREE - 1;
    //
    memcpy(newPiece->nodes, current->nodes + KNACK_DEGREE, newPiece->count);
    //
    if (!current->isLeaf) {
        memcpy(newPiece->nodes, current->nodes + KNACK_DEGREE, KNACK_DEGREE);
    }
    
    current->count = KNACK_DEGREE - 1;
    
    for (int i = parent->count; i > index; --i) {
        parent->children[i + 1] = parent->children[i];
    }
    
    parent->children[index + 1] = newPiece->loc;
    int moveCount = parent->count - index - 1;
    if (moveCount > 0) {
        memmove(parent->nodes + index + 1, parent->nodes + index, moveCount * KNACK_NODE_SIZE);
    }
    //
    memcpy(parent->nodes + index, current->nodes + KNACK_DEGREE - 1, KNACK_NODE_SIZE);
    parent->count += 1;
    
//    current->count -= 1;
}

void KnackInsertNotFull(KnackMap *map, KnackPiece *head, uint32_t hash, uint32_t keyLength, uint32_t valueLength, uint32_t contentOffset) {
    if (head->isLeaf) {
        int i = 0;
        while (i < head->count && hash > head->nodes[i].hash) {
            i++;
        }
        int copyCount = head->count - i;
        if (copyCount > 0) {
            memmove(head->nodes + i + 1, head->nodes + i, copyCount *KNACK_NODE_SIZE);
        }
        KnackNode *node = head->nodes + i;
        node->hash = hash;
        node->keyLength = keyLength;
        node->valueLength = valueLength;
        node->contentOffset = contentOffset;
        head->count += 1;
    } else {
        int i = 0;
        while (i < head->count && hash > head->nodes[i].hash) {
            i++;
        }
        KnackPiece *leaf = KnackGetPieceAtLoc(map, head->children[i]);
        if (leaf->count == KNACK_NODE_MAX) {
            KnackSplitLeaf(map,head, i, leaf);
            if (hash > head->nodes[i].hash) {
                ++i;
                leaf = KnackGetPieceAtLoc(map, head->children[i]);
            }
        }
        KnackInsertNotFull(map, leaf, hash, keyLength, valueLength, contentOffset);
    }
}


void KnackMapPut(KnackMap *map, const void *key, uint32_t keyLength, const void *value, uint32_t valueLength, uint8_t type) {
    if (map != NULL) {
        uint32_t hash = XXH32(key, keyLength, 0);
        static uint32_t hs = 0;
        hash = hs++;
        
        debugPrint(map);
        //
        KnackPiece *root = map->headPiece;
        if (root->count == KNACK_NODE_MAX) {
            KnackPiece *newRoot = KnackCreatePieceAtLoc(map, map->header->pieceCount);
            newRoot->isLeaf = 0;
            newRoot->count = 0;
            newRoot->children[0] = root->loc;
            //
            map->header->headLoc = newRoot->loc;
            map->headPiece = newRoot;
            //
            KnackSplitLeaf(map, newRoot, 0, root);
            KnackInsertNotFull(map, newRoot, hash, keyLength, valueLength, map->header->contentUsed);
        } else {
            KnackInsertNotFull(map, root, hash, keyLength, valueLength, map->header->contentUsed);
        }
        //insert contents
        
    }
}
