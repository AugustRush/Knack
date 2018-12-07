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
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define KNACK_ORDER 6 //必须为偶数
#define KNACK_NODE_MAX (KNACK_ORDER - 1)
#define PAGE_SIZE 4096
#define KNACK_INVALID_LOC 0xFFFFFFFF
#define KNACK_DEBUG 1


typedef uint8_t Byte;

typedef struct __attribute__((__packed__)) KnackNode {
    uint32_t hash;
    uint32_t keyLength;
    uint32_t valueLength;
    uint32_t contentOffset;
} KnackNode;

typedef struct __attribute__((__packed__)) KnackPiece {
    unsigned int count : 7;
    unsigned int isLeaf : 1;
    uint32_t loc;
    KnackNode nodes[KNACK_ORDER - 1];
    uint32_t children[KNACK_ORDER];
    
} KnackPiece;

typedef struct __attribute__((__packed__)) KnackHeader {
    uint32_t hash;
    uint32_t nodeCount;
    uint64_t totalSize;
    uint32_t headLoc;
    uint32_t pieceStart;
    uint32_t pieceCount;
    uint32_t contentStart;
    uint32_t contentUsed;
} KnackHeader;

typedef struct KnackMap {
    int fd;
    Byte *memory;
    KnackHeader *header;
    KnackPiece *pieces;
    KnackPiece *headPiece;
    Byte *contents;
} KnackMap;


const size_t KNACK_NODE_SIZE = sizeof(KnackNode);
const size_t KNACK_PIECE_SIZE = sizeof(KnackPiece);

/*+++++++++++++++++++++++++++++++++++++++++++++util funcs++++++++++++++++++++++++++++++++++++*/

void KnackAssert(const char *error) {
    printf("%s\n",error);
    assert(0);
}

void KnackFtruncate(int fd, uint64_t size) {
    if (ftruncate(fd, size) != 0) {
        KnackAssert("can not truncate file size.");
    }
}

void KnackUnmap(void *ptr, uint64_t size) {
    if (munmap(ptr, size) != 0) {
        KnackAssert("knack can not unmap file!");
    }
}

uint32_t KnackFileValidHash() {
    static uint32_t hash = 0;
    hash = XXH32("KNAC_FILE_VALID_HASH", 20, 0);
    return hash;
}

int KnackFileIsValidate(const void *ptr) {
    uint32_t validFileHash = XXH32("KNAC_FILE_VALID_HASH", 20, 0);
    if (memcmp(ptr, &validFileHash, sizeof(uint32_t)) == 0) {
        return validFileHash;
    }
    return 0;
}

/*+++++++++++++++++++++++++++++++++++++++++++++文件操作++++++++++++++++++++++++++++++++++++*/
void KnackReset(KnackMap *map, int fd, void *ptr, uint64_t size, uint32_t validHash) {
    map->fd = fd;
    map->memory = ptr;
    map->header = (KnackHeader *)map->memory;
    //
    memset(ptr, 0, size);
    map->header->hash = validHash;
    map->header->totalSize = size;
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
}

void KnackConstruct(KnackMap *map, int fd, void *ptr, uint64_t size) {
    map->fd = fd;
    map->memory = ptr;
    map->header = (KnackHeader *)map->memory;
    map->header->totalSize = size;
    map->pieces = (KnackPiece *)(map->memory + map->header->pieceStart);
    map->headPiece = map->pieces + map->header->headLoc;
    map->headPiece->loc = map->header->headLoc;
    map->contents = map->memory + map->header->contentStart;
}

void *KnackMemoryMap(int fd, uint64_t size) {
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        KnackAssert("knack can not map file");
    }
    return ptr;
}

void KnackMMPFile(KnackMap *map,const char *path, uint64_t minimalSize) {
    int fd = open(path, O_RDWR | O_CREAT, S_IRWXU);
    if (fd < 0) {
        printf("can not open file!\n");
        assert(0);
    } else {
        uint64_t size = 0;
        struct stat st = {};
        if (fstat(fd, &st) != -1) {
            size = st.st_size;
            if (size < minimalSize || size % PAGE_SIZE != 0) {
                size = (size / PAGE_SIZE + 1) * PAGE_SIZE;
                size = size > minimalSize ? size : minimalSize;
                KnackFtruncate(fd, size);
            }
            void *ptr = KnackMemoryMap(fd, size);
            if (KnackFileIsValidate(ptr)) {
                KnackConstruct(map, fd, ptr, size);
            } else {
                KnackReset(map, fd, ptr, size, KnackFileValidHash());
            }
        } else {
            KnackAssert("file state exception.");
        }
    }
}

/*+++++++++++++++++++++++++++++++++++++++++++++实现b+ tree 连续内存存储++++++++++++++++++++++++++++++++++++*/

void KnackExtendPieceSpaceIfNeeded(KnackMap *map, uint32_t pieceCount) {
    uint32_t spaceForPiece = pieceCount * KNACK_PIECE_SIZE + map->header->pieceStart;
    if (spaceForPiece > map->header->contentStart) {
        uint64_t size = map->header->totalSize;
        uint64_t aftertExtend = size * 2;
        KnackFtruncate(map->fd, aftertExtend);
        /*unmap之后会导致之前所有的指针指向失效，需要重新赋值*/
        KnackUnmap(map->memory, size);
        void *newPtr = KnackMemoryMap(map->fd, aftertExtend);
        KnackConstruct(map, map->fd, newPtr, aftertExtend);
        // move contents to new location
        memmove(map->memory + map->header->contentStart + size, map->memory + map->header->contentStart, map->header->contentUsed);
        // set old contents to 0
        memset(map->memory + map->header->contentStart, 0, size);
        //change map ptr
        map->header->contentStart += size;
        map->contents = map->memory + map->header->contentStart;
    }
}

void KnackExtendContentSpaceIfNeeded(KnackMap *map, uint64_t size) {
    if (size > map->header->totalSize) {
        uint64_t afterExtend = map->header->totalSize;
        do {
            afterExtend *= 2;
        } while (size > afterExtend);
        KnackFtruncate(map->fd, afterExtend);
        KnackUnmap(map->memory, map->header->totalSize);
        void *newPtr = KnackMemoryMap(map->fd, afterExtend);
        KnackConstruct(map, map->fd, newPtr, afterExtend);
    }
}

KnackPiece *KnackGetPieceAtLoc(KnackMap *map, uint32_t loc) {
    return map->pieces + loc;
}

KnackPiece *KnackCreateNewPiece(KnackMap *map, unsigned int isLeaf) {
    uint32_t count = map->header->pieceCount;
    uint32_t afterUsed = count + 1;
    KnackExtendPieceSpaceIfNeeded(map, afterUsed);
    //
    map->header->pieceCount = afterUsed;
    //
    KnackPiece *piece = map->pieces + count;
    piece->count = 0;
    piece->loc = count;
    piece->isLeaf = isLeaf;
    return piece;
}

void KnackMoveNodeToNeighbor(KnackMap *map,KnackPiece *sibling, uint32_t sibIdx,KnackPiece *parent, KnackPiece *current, uint32_t curIdx) {
    if (sibIdx < curIdx) { // 向左移动
        KnackPiece *piece = sibling;
        uint32_t copyIdx = piece->count;
        for (int i = sibIdx; i < curIdx; i++) {
            memcpy(piece->nodes + copyIdx, parent->nodes + i, KNACK_NODE_SIZE);
            KnackPiece *leaf = KnackGetPieceAtLoc(map, parent->children[i+1]);
            memcpy(parent->nodes + i, leaf->nodes, KNACK_NODE_SIZE);
            int moveCount = leaf->count - 1;
            if (moveCount > 0) {
                memmove(leaf->nodes, leaf->nodes + 1, moveCount * KNACK_NODE_SIZE);
            }
            piece = leaf;
            copyIdx = piece->count - 1;
        }
        current->count -= 1;
        sibling->count += 1;
    } else { // 向右移动
        memmove(sibling->nodes + 1, sibling->nodes, sibling->count * KNACK_NODE_SIZE);
        KnackPiece *piece = sibling;
        for (int i = sibIdx; i > curIdx; i--) {
            memcpy(piece->nodes, parent->nodes + i - 1, KNACK_NODE_SIZE);
            KnackPiece *leaf = KnackGetPieceAtLoc(map, parent->children[i - 1]);
            memcpy(parent->nodes + i - 1, leaf->nodes + leaf->count - 1, KNACK_NODE_SIZE);
            int moveCount = leaf->count - 1;
            if (moveCount > 0 && i > curIdx + 1) {
                memmove(leaf->nodes + 1, leaf->nodes, moveCount * KNACK_NODE_SIZE);
            }
            piece = leaf;
        }
        current->count -= 1;
        sibling->count += 1;
    }
}

void KnackSplitPiece(KnackMap *map,uint32_t parentLoc, uint32_t index, uint32_t currentLoc) {
    /*应该先创建New Piece，因为创建新的有可能导致map失效，之前获得的parent和current会变成野指针*/
    KnackPiece *newPiece = KnackCreateNewPiece(map, 0);
    KnackPiece *parent = KnackGetPieceAtLoc(map, parentLoc);
    KnackPiece *current = KnackGetPieceAtLoc(map, currentLoc);
    newPiece->isLeaf = current->isLeaf;
    
    int mid = current->count / 2;
    int copyCount = current->count - mid - 1;
    memcpy(newPiece->nodes, current->nodes + mid + 1, copyCount * KNACK_NODE_SIZE);
    newPiece->count = copyCount;
    //
    if (!current->isLeaf) {
        memcpy(newPiece->children, current->children + mid + 1, (copyCount+1) * sizeof(uint32_t));
    }
    
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

//return all raw data take bytes
uint32_t KnackWriteContent(KnackMap *map,const void *key, uint32_t keyLength, const void *value, uint32_t valueLength, uint8_t type, uint32_t contentOffset) {
    int bytesCount = keyLength + valueLength + 1;
    KnackExtendContentSpaceIfNeeded(map, map->header->contentStart + contentOffset + bytesCount);
    memcpy(map->contents + contentOffset, key, keyLength);
    map->contents[contentOffset + keyLength] = type;
    memcpy(map->contents + contentOffset + 1 + keyLength, value, valueLength);
    return bytesCount;
}

const void * KnackReadContent(const KnackMap *map, const KnackNode *node, const void *key, uint32_t keyLength, uint32_t *valueLength, uint8_t *type) {
    Byte *contents = map->contents + node->contentOffset;
    //compared length and bytes
    if (keyLength == node->keyLength && memcmp(contents, key, keyLength) == 0) {
        //
        *type = contents[node->keyLength];
        *valueLength = node->valueLength;
        //
        return contents + node->keyLength + 1;
    }
    return NULL;
}

void KnackRepalceNode(KnackMap *map, KnackNode *node) {
    
}

KnackNode * KnackInsertToNotFullPiece(KnackMap *map, KnackPiece *parent, KnackPiece *current, uint32_t hash, uint32_t keyLength, uint32_t valueLength, uint32_t contentOffset, uint32_t prevSibIdx, uint32_t prevCurIdx) {
    int i = 0;
    KnackNode *exsitNode = NULL;
    while (i < current->count) {
        KnackNode *node = current->nodes + i;
        if (hash == node->hash) {
            exsitNode = node;
            break;
        }
        if (hash < node->hash) {
            break;
        }
        i++;
    }
    if (exsitNode == NULL) {
        if (current->isLeaf) {
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
            KnackPiece *leaf = KnackGetPieceAtLoc(map, current->children[i]);
            if (leaf->count == KNACK_NODE_MAX) {
                
                KnackPiece *sibling = NULL;
                uint32_t siblingIndex = 0;
                for (int i = 0; i <= current->count; i++) {
                    KnackPiece *piece = KnackGetPieceAtLoc(map, current->children[i]);
                    if (piece->count < KNACK_NODE_MAX && piece->isLeaf) {
                        sibling = piece;
                        siblingIndex = i;
                        break;
                    }
                }
                
                if (sibling != NULL && (siblingIndex != prevCurIdx || i != prevSibIdx)) {
                    KnackMoveNodeToNeighbor(map, sibling, siblingIndex, current, leaf, i);
                    KnackInsertToNotFullPiece(map, parent, current, hash, keyLength, valueLength, contentOffset, siblingIndex, i);
                } else {
                    uint32_t parentLoc = current->loc;
                    KnackSplitPiece(map,parentLoc, i, leaf->loc);
                    current = KnackGetPieceAtLoc(map, parentLoc);
                    if (hash > current->nodes[i].hash) {
                        ++i;
                    }
                    leaf = KnackGetPieceAtLoc(map, current->children[i]);
                    KnackInsertToNotFullPiece(map, current, leaf, hash, keyLength, valueLength, contentOffset,KNACK_INVALID_LOC,KNACK_INVALID_LOC);
                }
            } else {
                KnackInsertToNotFullPiece(map, current, leaf, hash, keyLength, valueLength, contentOffset,KNACK_INVALID_LOC,KNACK_INVALID_LOC);
            }
        }
    }
    return exsitNode;
}

KnackNode *KnackSearchNode(KnackMap *map, KnackPiece *root, uint32_t hash) {
    KnackNode *node = NULL;
    int index= 0;
    for (int i = 0; i < root->count; i++) {
        KnackNode *current = root->nodes + i;
        if (current->hash == hash) {
            node = current;
            index = -1;
            break;
        }
        
        if (current->hash < hash) {
            index++;
        } else {
            break;
        }
    }
    
    if (index >= 0 && !root->isLeaf) {
        KnackPiece *piece = KnackGetPieceAtLoc(map, root->children[index]);
        node = KnackSearchNode(map, piece, hash);
    }
    
    return node;
}

KnackMap *KnackMapInit(const char *path) {
    uint32_t size = 2 * PAGE_SIZE;
    KnackMap *map = malloc(sizeof(KnackMap));
    KnackMMPFile(map, path, size);
    return map;
}

void KnackUpdateOrInsert(KnackMap *map, KnackPiece *parent, KnackPiece *current, uint32_t hash, const void *key, uint32_t keyLength, const void *value, uint32_t valueLength, int8_t type) {
    KnackNode *existedNode = KnackInsertToNotFullPiece(map, parent, current, hash, keyLength, valueLength, map->header->contentUsed,KNACK_INVALID_LOC,KNACK_INVALID_LOC);
    if (existedNode != NULL) {
        Byte *contents = map->contents + existedNode->contentOffset;
        if (keyLength == existedNode->keyLength && memcmp(contents, key, keyLength) == 0) {//同一个key
            //处理数据的存储
#warning to do
            KnackAssert("to do ....");
        } else {
            //用链表的形式处理hash冲突
#warning to do
            KnackAssert("to do ....");
        }
    } else {
        int writeBytes = KnackWriteContent(map, key, keyLength, value, valueLength, type, map->header->contentUsed);
        if (writeBytes > 0) {
            map->header->contentUsed += writeBytes;
        }
    }
}

void KnackMapPut(KnackMap *map, const void *key, uint32_t keyLength, const void *value, uint32_t valueLength, int8_t type) {
    if (map != NULL) {
        uint32_t hash = XXH32(key, keyLength, 0);
        
        KnackPiece *root = map->headPiece;
        
        if (root->count == KNACK_NODE_MAX) {
            KnackPiece *newRoot = KnackCreateNewPiece(map, 0);
            newRoot->children[0] = root->loc;
            //
            map->header->headLoc = newRoot->loc;
            map->headPiece = newRoot;
            //
            KnackSplitPiece(map, newRoot->loc, 0, root->loc);
            //
            root = newRoot;
        }
        KnackUpdateOrInsert(map, NULL, root, hash, key, keyLength, value, valueLength, type);
    }
}

const void * KnackMapGet(KnackMap *map, const void *key, uint32_t keyLength, uint32_t *valueLength, uint8_t *type) {
    if (map != NULL) {
        uint32_t hash = XXH32(key, keyLength, 0);
        KnackNode *node = KnackSearchNode(map, map->headPiece, hash);
        if (node != NULL) {
            return KnackReadContent(map, node,key,keyLength,valueLength, type);
        }
    }
    return NULL;
}

void KnackRemove(KnackMap *map, uint32_t hash, const void *key, uint32_t keyLength) {
    
}

void KnackMapRemove(KnackMap *map, const void *key, uint32_t keyLength) {
    if (map != NULL) {
        uint32_t hash = XXH32(key, keyLength, 0);
        KnackRemove(map, hash, key, keyLength);
    }
}

void KnackMapRelease(KnackMap *map) {
    free(map);
}

void _KnackDebugPrint(KnackMap *map, KnackPiece *head) {
    if (map != NULL && head->count > 0) {
        printf("level-%d:",head->loc);
        for (int i = 0; i < head->count; i++) {
            printf(" %u",head->nodes[i].hash);
        }
        printf("\n");
        if (!head->isLeaf) {
            for (int i = 0; i <= head->count; i++) {
                uint32_t index = head->children[i];
                if (index != head->loc) {
                    KnackPiece *piece = KnackGetPieceAtLoc(map, head->children[i]);
                    _KnackDebugPrint(map, piece);
                }
            }
        }
    }
}

void KnackDebugPrint(KnackMap *map) {
    _KnackDebugPrint(map,map->headPiece);
    printf("====================\n");
}
