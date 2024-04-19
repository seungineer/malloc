#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"


/* 기본 변수 및 매크로 정의 */
#define ALIGNMENT 8
#define WSIZE 4                                 // 워드, 헤더/푸터 Size(bytes)
#define DSIZE 8                                 // 더블 워드 Size(bytes)
#define CHUNKSIZE (1 << 12)                      // Extend heap Size(4 bytes)
#define PACK(size, alloc) ((size) | (alloc))    // Size bit에 allocated bit를 pack(얹음)함
#define MAX(x, y) ((x) > (y) ? (x) : (y))
/* p(ptr)address에서 Read/Write word */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (unsigned int)(val))
/* p(ptr) address에서 Size와 Allocated fields 읽음 */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define GET_SUCC(p) (*(void **)(((char *)bp + WSIZE))) // 다음 가용 블록 주소
#define GET_PRED(bp) (*(void **)bp)                    // 이전 가용 블록 주소
#define HDRP(bp) ((char *)(bp)-WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
/* next, prev 블럭 pointer 계산 */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

team_t team = {
    "10th",
    "Seungwoo Cho",
    "choseung97@gmail.com",
    "",
    ""};

static char *free_listp;
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void splice_free_block(void *bp);
static void add_free_block(void *bp);

/* malloc package를 시작하는 것, 16bytes의 더미 가용 블록을 초기에 할당 함 */
int mm_init(void)
{
    if ((free_listp = mem_sbrk(8 * WSIZE)) == (void *)-1) // 8워드 크기의 힙 생성, free_listp에 'free'인 힙의 시작 주소값 할당
        return -1;
    PUT(free_listp, 0);                              // 초기 정렬 패딩
    PUT(free_listp + (1 * WSIZE), PACK(2*WSIZE, 1)); // 프롤로그 Header
    PUT(free_listp + (2 * WSIZE), PACK(2*WSIZE, 1)); // 프롤로그 Footer
    PUT(free_listp + (3 * WSIZE), PACK(4*WSIZE, 0)); // 첫 가용 블록 Header
    PUT(free_listp + (4 * WSIZE), NULL);             // 이전 가용 블록 주소
    PUT(free_listp + (5 * WSIZE), NULL);             // 다음 가용 블록 주소
    PUT(free_listp + (6 * WSIZE), PACK(4*WSIZE, 0)); // 첫 가용 블록 Footer
    PUT(free_listp + (7 * WSIZE), PACK(0, 1));       // 에필로그 Header
    
    free_listp += (4*WSIZE);

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;
    
    return 0;
}

/* explicit-list */
void *mm_malloc(size_t size)
{
    size_t asize;      // Alignment 정책에 맞는 블록 Size
    size_t extendsize; // 확장 Size
    char *bp;
    
    /* 예외 처리 */
    if (size == 0) return NULL;

    /* 할당 Size 결정 */
    asize = MAX(ALIGN(size + DSIZE), 2 * DSIZE); // 8byte 미만의 요청은 8byte로 할당. 이상은 hdr,ftr 고려한 ALIGN 크기 중 큰 것.

    /* 가용 블록 find */
    if ((bp = find_fit(asize)) != NULL)
    {
        place(bp, asize); // 할당
        return bp;        // 새로 할당된 블록의 포인터 리턴
    }

    /* 가용 블록 못 찾을 경우 -> 힙 확장 */
    extendsize = MAX(asize, CHUNKSIZE); // 최소 Alignment 정책 크기 만큼 확장
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    /* 할당 해제된 블럭 병합 */
    coalesce(bp);
}

static void *coalesce(void *bp)
{
    /* bp 블럭 관련 정보 */
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));                   

    if (prev_alloc && next_alloc)       // 이전, 다음 블럭 모두 이미 할당된 경우
    {
        add_free_block(bp);
        return bp;
    }

    else if (prev_alloc && !next_alloc) // 다음 블록만 빈 경우
    {
        splice_free_block(NEXT_BLKP(bp));        // 기존 free list에 있던 '다음' 블럭 제거
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));            
        PUT(FTRP(bp), PACK(size, 0));            
    }
    else if (!prev_alloc && next_alloc) // 이전 블록만 빈 경우
    {
        splice_free_block(PREV_BLKP(bp));   
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); 
        PUT(FTRP(bp), PACK(size, 0)); 
        bp = PREV_BLKP(bp);                      // 이전 블록의 시작점으로 포인터 변경
    }
    else                                // 이전, 다음 블록 모두 빈 경우
    {
        splice_free_block(PREV_BLKP(bp));   
        splice_free_block(NEXT_BLKP(bp));   
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); 
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0)); 
        bp = PREV_BLKP(bp);                      // 이전 블록의 시작점으로 포인터 변경
    }
    add_free_block(bp);
    return bp; // 병합된 블록의 포인터 반환
}

void *mm_realloc(void *ptr, size_t size) // re-allocation 하고자 하는 블록의 ptr와 allocation Size를 인자로 받음
{
    /* 예외 처리 */
    if (ptr == NULL)                     // 포인터가 NULL인 경우 malloc만 수행 후 return
        return mm_malloc(size);

    if (size <= 0)                       // Size가 0인 경우 free만 수행 후 return
    {
        mm_free(ptr);
        return 0;
    }

    /* 새 블록에 할당 */
    void *newptr = mm_malloc(size); // 새로 할당한 블록의 포인터
    /* 새 블록에 할당 중 예외 처리 */
    if (newptr == NULL) return NULL;

    /* 데이터 복사 */
    size_t copySize = GET_SIZE(HDRP(ptr)) - DSIZE; 
    if (size < copySize)                           
        copySize = size;                           

    memcpy(newptr, ptr, copySize); // 새 블록에 copy size

    /* 기존 블록 free */
    mm_free(ptr);

    return newptr;
}

/* heap 확장 */
static void *extend_heap(size_t words)
{
    char *bp;

    size_t size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;

    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL; // 힙 확장 불가능 시 return NULL

    PUT(HDRP(bp), PACK(size, 0));         
    PUT(FTRP(bp), PACK(size, 0));         
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); 

    return coalesce(bp); // bp 기준 병합 후 블록 포인터 반환
}

static void *find_fit(size_t asize)
{
    void *bp = free_listp;
    while (bp != NULL)
    {
        if (asize <= GET_SIZE(HDRP(bp))) // 사이즈가 적합하면
        {
            return bp; // 해당 블록이 fit
        }
        bp = GET_SUCC(bp);
    }
    // for (void *bp = free_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp))
    // {
    //     if (asize <= GET_SIZE(HDRP(bp))) // 사이즈가 적합하면
    //     {
    //         return bp; // 해당 블록이 fit
    //     }
    // }
    return NULL;
}

static void place(void *bp, size_t asize)
{
    /* bp 블럭 제거 */
    splice_free_block(bp);
    

    /* Place 수행 */
    size_t current_size = GET_SIZE(HDRP(bp));
    if ((current_size - asize) >= (2 * DSIZE)) // 차이가 최소 블록 크기 16보다 같거나 크면 분할
    {
        PUT(HDRP(bp), PACK(asize, 1)); // 현재 블록에는 필요한 만큼만 할당
        PUT(FTRP(bp), PACK(asize, 1));
        PUT(HDRP(NEXT_BLKP(bp)), PACK((current_size - asize), 0)); // 남은 크기를 다음 블록에 할당(가용 블록)
        PUT(FTRP(NEXT_BLKP(bp)), PACK((current_size - asize), 0));
        add_free_block(NEXT_BLKP(bp));
    }
    else
    {
        PUT(HDRP(bp), PACK(current_size, 1)); // 해당 블록 전부 사용
        PUT(FTRP(bp), PACK(current_size, 1));
    }
}

/* free_listp에서 bp 해당 블럭 제거 함수 */
static void splice_free_block(void *bp)
{
    /* 예외 처리 */
    if (bp == free_listp){ // 삭제하고자 하는 블럭이 가장 앞 블럭일 때
        free_listp = GET_SUCC(free_listp);
        return;
    }
    /* 이전 블록의 SUCC에 다음 가용 블록으로 연결 */
    GET_SUCC(GET_PRED(bp)) = GET_SUCC(bp);
    if (GET_SUCC(bp) != NULL)
    {
        GET_PRED(GET_SUCC(bp)) = GET_PRED(bp);
    }
}

/* bp 블럭을 가용 리스트에 추가 */
static void add_free_block(void *bp)
{
    GET_SUCC(bp) = free_listp;     // bp의 SUCC은 루트가 가리키던 블록
    if (free_listp != NULL)        // free list에 블록이 존재했을 경우만
        GET_PRED(free_listp) = bp; // 루트였던 블록의 PRED를 추가된 블록으로 연결
    free_listp = bp;               // 루트를 현재 블록으로 변경
}