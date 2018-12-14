//
//  _Venom.c
//  _Venom
//
//  Created by pingwei liu on 2018/11/28.
//  Copyright © 2018 pingwei liu. All rights reserved.
//

#include "Venom.h"
#include "xxhash/xxhash.h"
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define VENOM_ORDER 6 //必须为偶数
#define VENOM_NODE_MAX (VENOM_ORDER - 1)
#define PAGE_SIZE 4096
#define VENOM_INVALID_LOC 0xFFFFFFFF
#define VENOM_DEBUG 1
#define VENOM_SEGMENT_LIMIT 1024*1024*16 //16M

typedef uint8_t Byte;

typedef struct __attribute__((__packed__)) _VenomNode {
    uint32_t hash;
    uint32_t keyLength;
    uint32_t valueLength;
    uint32_t contentOffset;
} _VenomNode;

typedef struct __attribute__((__packed__)) _VenomPiece {
    unsigned int count : 7;
    unsigned int isLeaf : 1;
    uint32_t loc;
    _VenomNode nodes[VENOM_ORDER - 1];
    uint32_t children[VENOM_ORDER];
    
} _VenomPiece;

typedef struct __attribute__((__packed__)) _VenomHeader {
    uint32_t hash;
    uint32_t keysCount;
    uint64_t totalSize;
    uint32_t headLoc;
    uint32_t pieceStart;
    uint32_t pieceCount;
    uint32_t contentStart;
    uint32_t contentUsed;
} _VenomHeader;

typedef struct Venom {
    int fd;
    Byte *memory;
    _VenomHeader *header;
    _VenomPiece *pieces;
    _VenomPiece *headPiece;
    Byte *contents;
} Venom;


const size_t VENOMNODE_SIZE = sizeof(_VenomNode);
const size_t VENOMPIECE_SIZE = sizeof(_VenomPiece);

/*+++++++++++++++++++++++++++++++++++++++++++++util funcs++++++++++++++++++++++++++++++++++++*/

void _VenomAssert(const char *error) {
    printf("%s\n",error);
    assert(0);
}

void _VenomFtruncate(int fd, uint64_t size) {
    if (ftruncate(fd, size) != 0) {
        _VenomAssert("can not truncate file size.");
    }
}

void _VenomUnmap(void *ptr, uint64_t size) {
    if (munmap(ptr, size) != 0) {
        _VenomAssert("_Venom can not unmap file!");
    }
}

uint32_t _VenomFileValidHash() {
    static uint32_t hash = 0;
    hash = XXH32("KNAC_FILE_VALID_HASH", 20, 0);
    return hash;
}

int _VenomFileIsValidate(const void *ptr) {
    uint32_t validFileHash = XXH32("KNAC_FILE_VALID_HASH", 20, 0);
    if (memcmp(ptr, &validFileHash, sizeof(uint32_t)) == 0) {
        return validFileHash;
    }
    return 0;
}

/*+++++++++++++++++++++++++++++++++++++++++++++文件操作++++++++++++++++++++++++++++++++++++*/
void _VenomReset(Venom *map, int fd, void *ptr, uint64_t size, uint32_t validHash) {
    map->fd = fd;
    map->memory = ptr;
    map->header = (_VenomHeader *)map->memory;
    //
    memset(ptr, 0, size);
    map->header->hash = validHash;
    map->header->totalSize = size;
    map->header->keysCount = 0;
    map->header->headLoc = 0;
    map->header->pieceStart = sizeof(_VenomHeader);
    map->header->pieceCount = 1;
    map->header->contentStart = PAGE_SIZE;
    map->header->contentUsed = 0;
    map->pieces = (_VenomPiece *)(map->memory + map->header->pieceStart);
    map->headPiece = map->pieces + map->header->headLoc;
    map->headPiece->isLeaf = 1;
    map->headPiece->loc = map->header->headLoc;
    map->contents = map->memory + map->header->contentStart;
}

void _VenomConstruct(Venom *map, int fd, void *ptr, uint64_t size) {
    map->fd = fd;
    map->memory = ptr;
    map->header = (_VenomHeader *)map->memory;
    map->header->totalSize = size;
    map->pieces = (_VenomPiece *)(map->memory + map->header->pieceStart);
    map->headPiece = map->pieces + map->header->headLoc;
    map->headPiece->loc = map->header->headLoc;
    map->contents = map->memory + map->header->contentStart;
}

#warning 需要分段t映射，否则文件过大会失败
void *_VenomMemoryMap(int fd, uint64_t size) {
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
        _VenomAssert("_Venom can not map file");
    }
    return ptr;
}

void _VenomMMPFile(Venom *map,const char *path, uint64_t minimalSize) {
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
                _VenomFtruncate(fd, size);
            }
            void *ptr = _VenomMemoryMap(fd, size);
            if (_VenomFileIsValidate(ptr)) {
                _VenomConstruct(map, fd, ptr, size);
            } else {
                _VenomReset(map, fd, ptr, size, _VenomFileValidHash());
            }
        } else {
            _VenomAssert("file state exception.");
        }
    }
}

/*+++++++++++++++++++++++++++++++++++++++++++++实现b+ tree 连续内存存储++++++++++++++++++++++++++++++++++++*/

void _VenomExtendPieceSpaceIfNeeded(Venom *map, uint32_t pieceCount) {
    uint32_t spaceForPiece = pieceCount * VENOMPIECE_SIZE + map->header->pieceStart;
    if (spaceForPiece > map->header->contentStart) {
        uint64_t size = map->header->totalSize;
        uint64_t aftertExtend = size * 2;
        _VenomFtruncate(map->fd, aftertExtend);
        /*unmap之后会导致之前所有的指针指向失效，需要重新赋值*/
        _VenomUnmap(map->memory, size);
        void *newPtr = _VenomMemoryMap(map->fd, aftertExtend);
        _VenomConstruct(map, map->fd, newPtr, aftertExtend);
        // move contents to new location
        memmove(map->memory + map->header->contentStart + size, map->memory + map->header->contentStart, map->header->contentUsed);
        // set old contents to 0
        memset(map->memory + map->header->contentStart, 0, size);
        //change map ptr
        map->header->contentStart += size;
        map->contents = map->memory + map->header->contentStart;
    }
}

void _VenomExtendContentSpaceIfNeeded(Venom *map, uint64_t size) {
    if (size > map->header->totalSize) {
        uint64_t afterExtend = map->header->totalSize;
        do {
            afterExtend *= 2;
        } while (size > afterExtend);
        _VenomFtruncate(map->fd, afterExtend);
        _VenomUnmap(map->memory, map->header->totalSize);
        void *newPtr = _VenomMemoryMap(map->fd, afterExtend);
        _VenomConstruct(map, map->fd, newPtr, afterExtend);
    }
}

_VenomPiece *_VenomGetPieceAtLoc(Venom *map, uint32_t loc) {
    return map->pieces + loc;
}

_VenomPiece *_VenomCreateNewPiece(Venom *map, unsigned int isLeaf) {
    uint32_t count = map->header->pieceCount;
    uint32_t afterUsed = count + 1;
    _VenomExtendPieceSpaceIfNeeded(map, afterUsed);
    //
    map->header->pieceCount = afterUsed;
    //
    _VenomPiece *piece = map->pieces + count;
    piece->count = 0;
    piece->loc = count;
    piece->isLeaf = isLeaf;
    return piece;
}

void _VenomMoveNodeToNeighbor(Venom *map,_VenomPiece *sibling, uint32_t sibIdx,_VenomPiece *parent, _VenomPiece *current, uint32_t curIdx) {
    if (sibIdx < curIdx) { // 向左移动
        _VenomPiece *piece = sibling;
        uint32_t copyIdx = piece->count;
        for (int i = sibIdx; i < curIdx; i++) {
            memcpy(piece->nodes + copyIdx, parent->nodes + i, VENOMNODE_SIZE);
            _VenomPiece *leaf = _VenomGetPieceAtLoc(map, parent->children[i+1]);
            memcpy(parent->nodes + i, leaf->nodes, VENOMNODE_SIZE);
            int moveCount = leaf->count - 1;
            if (moveCount > 0) {
                memmove(leaf->nodes, leaf->nodes + 1, moveCount * VENOMNODE_SIZE);
            }
            piece = leaf;
            copyIdx = piece->count - 1;
        }
        current->count -= 1;
        sibling->count += 1;
    } else { // 向右移动
        memmove(sibling->nodes + 1, sibling->nodes, sibling->count * VENOMNODE_SIZE);
        _VenomPiece *piece = sibling;
        for (int i = sibIdx; i > curIdx; i--) {
            memcpy(piece->nodes, parent->nodes + i - 1, VENOMNODE_SIZE);
            _VenomPiece *leaf = _VenomGetPieceAtLoc(map, parent->children[i - 1]);
            memcpy(parent->nodes + i - 1, leaf->nodes + leaf->count - 1, VENOMNODE_SIZE);
            int moveCount = leaf->count - 1;
            if (moveCount > 0 && i > curIdx + 1) {
                memmove(leaf->nodes + 1, leaf->nodes, moveCount * VENOMNODE_SIZE);
            }
            piece = leaf;
        }
        current->count -= 1;
        sibling->count += 1;
    }
}

void _VenomSplitPiece(Venom *map,uint32_t parentLoc, uint32_t index, uint32_t currentLoc) {
    /*应该先创建New Piece，因为创建新的有可能导致map失效，之前获得的parent和current会变成野指针*/
    _VenomPiece *newPiece = _VenomCreateNewPiece(map, 0);
    _VenomPiece *parent = _VenomGetPieceAtLoc(map, parentLoc);
    _VenomPiece *current = _VenomGetPieceAtLoc(map, currentLoc);
    newPiece->isLeaf = current->isLeaf;
    
    int mid = current->count / 2;
    int copyCount = current->count - mid - 1;
    memcpy(newPiece->nodes, current->nodes + mid + 1, copyCount * VENOMNODE_SIZE);
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
        memmove(parent->nodes + index + 1, parent->nodes + index, moveCount * VENOMNODE_SIZE);
    }
    //
    memcpy(parent->nodes + index, current->nodes + mid, VENOMNODE_SIZE);
    parent->count += 1;
}

//return all raw data take bytes
uint32_t _VenomWriteContent(Venom *map,const void *key, uint32_t keyLength, const void *value, uint32_t valueLength, uint8_t type, uint32_t contentOffset) {
    int bytesCount = keyLength + valueLength + 1;
    _VenomExtendContentSpaceIfNeeded(map, map->header->contentStart + contentOffset + bytesCount);
    memcpy(map->contents + contentOffset, key, keyLength);
    map->contents[contentOffset + keyLength] = type;
    memcpy(map->contents + contentOffset + 1 + keyLength, value, valueLength);
    return bytesCount;
}

const void * _VenomReadContent(const Venom *map, const _VenomNode *node, const void *key, uint32_t keyLength, uint32_t *valueLength, uint8_t *type) {
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

void _VenomRepalceNode(Venom *map, _VenomNode *node) {
    
}

_VenomNode * _VenomInsertToNotFullPiece(Venom *map, _VenomPiece *parent, _VenomPiece *current, uint32_t hash, uint32_t keyLength, uint32_t valueLength, uint32_t contentOffset, uint32_t prevSibIdx, uint32_t prevCurIdx) {
    int i = 0;
    _VenomNode *exsitNode = NULL;
    while (i < current->count) {
        _VenomNode *node = current->nodes + i;
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
                memmove(current->nodes + i + 1, current->nodes + i, copyCount *VENOMNODE_SIZE);
            }
            _VenomNode *node = current->nodes + i;
            node->hash = hash;
            node->keyLength = keyLength;
            node->valueLength = valueLength;
            node->contentOffset = contentOffset;
            current->count += 1;
        } else {
            _VenomPiece *leaf = _VenomGetPieceAtLoc(map, current->children[i]);
            if (leaf->count == VENOM_NODE_MAX) {
                
                _VenomPiece *sibling = NULL;
                uint32_t siblingIndex = 0;
                for (int i = 0; i <= current->count; i++) {
                    _VenomPiece *piece = _VenomGetPieceAtLoc(map, current->children[i]);
                    if (piece->count < VENOM_NODE_MAX && piece->isLeaf) {
                        sibling = piece;
                        siblingIndex = i;
                        break;
                    }
                }
                
                if (sibling != NULL && (siblingIndex != prevCurIdx || i != prevSibIdx)) {
                    _VenomMoveNodeToNeighbor(map, sibling, siblingIndex, current, leaf, i);
                    _VenomInsertToNotFullPiece(map, parent, current, hash, keyLength, valueLength, contentOffset, siblingIndex, i);
                } else {
                    uint32_t parentLoc = current->loc;
                    _VenomSplitPiece(map,parentLoc, i, leaf->loc);
                    current = _VenomGetPieceAtLoc(map, parentLoc);
                    if (hash > current->nodes[i].hash) {
                        ++i;
                    }
                    leaf = _VenomGetPieceAtLoc(map, current->children[i]);
                    _VenomInsertToNotFullPiece(map, current, leaf, hash, keyLength, valueLength, contentOffset,VENOM_INVALID_LOC,VENOM_INVALID_LOC);
                }
            } else {
                _VenomInsertToNotFullPiece(map, current, leaf, hash, keyLength, valueLength, contentOffset,VENOM_INVALID_LOC,VENOM_INVALID_LOC);
            }
        }
    }
    return exsitNode;
}

_VenomNode *_VenomSearchNode(Venom *map, _VenomPiece *root, uint32_t hash) {
    _VenomNode *node = NULL;
    int index= 0;
    for (int i = 0; i < root->count; i++) {
        _VenomNode *current = root->nodes + i;
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
        _VenomPiece *piece = _VenomGetPieceAtLoc(map, root->children[index]);
        node = _VenomSearchNode(map, piece, hash);
    }
    
    return node;
}

Venom *VenomInit(const char *path) {
    uint32_t size = 2 * PAGE_SIZE;
    Venom *map = malloc(sizeof(Venom));
    _VenomMMPFile(map, path, size);
    return map;
}

void _VenomUpdateOrInsert(Venom *map, _VenomPiece *parent, _VenomPiece *current, uint32_t hash, const void *key, uint32_t keyLength, const void *value, uint32_t valueLength, int8_t type) {
    _VenomNode *existedNode = _VenomInsertToNotFullPiece(map, parent, current, hash, keyLength, valueLength, map->header->contentUsed,VENOM_INVALID_LOC,VENOM_INVALID_LOC);
    if (existedNode != NULL) {
        Byte *contents = map->contents + existedNode->contentOffset;
        if (keyLength == existedNode->keyLength && memcmp(contents, key, keyLength) == 0) {//同一个key
            //处理数据的存储
#warning to do
            _VenomAssert("to do ....");
        } else {
            //用链表的形式处理hash冲突
#warning to do
            _VenomAssert("to do ....");
        }
    } else {
        int writeBytes = _VenomWriteContent(map, key, keyLength, value, valueLength, type, map->header->contentUsed);
        if (writeBytes > 0) {
            map->header->contentUsed += writeBytes;
        }
    }
}

void VenomPut(Venom *map, const void *key, uint32_t keyLength, const void *value, uint32_t valueLength, int8_t type) {
    if (map != NULL) {
        uint32_t hash = XXH32(key, keyLength, 0);
        
        _VenomPiece *root = map->headPiece;
        
        if (root->count == VENOM_NODE_MAX) {
            _VenomPiece *newRoot = _VenomCreateNewPiece(map, 0);
            newRoot->children[0] = root->loc;
            //
            map->header->headLoc = newRoot->loc;
            map->headPiece = newRoot;
            //
            _VenomSplitPiece(map, newRoot->loc, 0, root->loc);
            //
            root = newRoot;
        }
        _VenomUpdateOrInsert(map, NULL, root, hash, key, keyLength, value, valueLength, type);
    }
}

const void * VenomGet(Venom *map, const void *key, uint32_t keyLength, uint32_t *valueLength, uint8_t *type) {
    if (map != NULL) {
        uint32_t hash = XXH32(key, keyLength, 0);
        _VenomNode *node = _VenomSearchNode(map, map->headPiece, hash);
        if (node != NULL) {
            return _VenomReadContent(map, node,key,keyLength,valueLength, type);
        }
    }
    return NULL;
}

void _VenomRemove(Venom *map, uint32_t hash, const void *key, uint32_t keyLength) {
    
}

void VenomRemove(Venom *map, const void *key, uint32_t keyLength) {
    if (map != NULL) {
        uint32_t hash = XXH32(key, keyLength, 0);
        _VenomRemove(map, hash, key, keyLength);
    }
}

void VenomRelease(Venom *map) {
    _VenomUnmap(map->memory, map->header->totalSize);
    close(map->fd);
    free(map);
}

void __VENOM_DEBUGPrint(Venom *map, _VenomPiece *head) {
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
                    _VenomPiece *piece = _VenomGetPieceAtLoc(map, head->children[i]);
                    __VENOM_DEBUGPrint(map, piece);
                }
            }
        }
    }
}

void _VENOM_DEBUGPrint(Venom *map) {
    __VENOM_DEBUGPrint(map,map->headPiece);
    printf("====================\n");
}
