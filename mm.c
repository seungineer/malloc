#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"

#define ALIGNMENT 8

/* 기본 변수 및 매크로 정의 */
#define WSIZE 4             // 워드, 헤더/푸터 Size(bytes)
#define DSIZE 8             // 더블 워드 Size(bytes)
#define CHUNKSIZE (1 << 12) // Extend heap Size(4k bytes)

#define MAX(x, y)((x) > (y) ? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc)) // Size bit에 allocated bit를 pack(얹음)함
/* Read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))
/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define HDRP(bp) ((char *)(bp)-WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

team_t team = {
    /* Team name */
    "10th",
    /* First member's full name */
    "Seungwoo Cho",
    /* First member's email address */
    "choseung97@gmail.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

static char *heap_listp;

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);

/* malloc package를 시작하는 것 */
int mm_init(void)
{
    char *heap_listp;
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1) // 4워드 크기의 힙 생성, heap_listp에 힙의 시작 주소값 할당
        return -1;
    PUT(heap_listp, 0);                            // 초기 정렬 패딩
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); // 프롤로그 Header
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); // 프롤로그 Footer
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     // 에필로그 Header

    
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;
    return 0;
}

/* implicit-list */
void *mm_malloc(size_t size)
{
    size_t asize;      // Alignment 정책에 맞는 블록 Size
    size_t extendsize; // 확장할 Size
    char *bp;

    /* 할당 Size 결정 */
    asize = MAX(ALIGN(size + DSIZE), 2*DSIZE); // 8byte 미만의 요청은 8byte로 할당. 이상은 hdr,ftr 고려한 ALIGN 크기 중 큰 것.

    /* 가용 블록 find */
    if ((bp = find_fit(asize)) != NULL)
    {
        place(bp, asize); // 할당
        return bp;        // 새로 할당된 블록의 포인터 리턴
    }

    /* 가용 블록을 not find -> 힙 확장 */
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
    coalesce(bp);
}

static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); // 이전 블록 할당 상태
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); // 다음 블록 할당 상태
    size_t size = GET_SIZE(HDRP(bp));                   // 현재 블록 사이즈

    if (prev_alloc && next_alloc) // 모두 할당된 경우
        return bp;

    else if (prev_alloc && !next_alloc) // 다음 블록만 빈 경우
    {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0)); // 현재 블록 헤더 재설정
        PUT(FTRP(bp), PACK(size, 0)); // 다음 블록 푸터 재설정 (위에서 헤더를 재설정했으므로, FTRP(bp)는 합쳐질 다음 블록의 푸터가 됨)
    }
    else if (!prev_alloc && next_alloc) // 이전 블록만 빈 경우
    {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); // 이전 블록 헤더 재설정
        PUT(FTRP(bp), PACK(size, 0));            // 현재 블록 푸터 재설정
        bp = PREV_BLKP(bp);                      // 이전 블록의 시작점으로 포인터 변경
    }
    else // 이전 블록과 다음 블록 모두 빈 경우
    {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); // 이전 블록 헤더 재설정
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0)); // 다음 블록 푸터 재설정
        bp = PREV_BLKP(bp);                      // 이전 블록의 시작점으로 포인터 변경
    }

    return bp; // 병합된 블록의 포인터 반환
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    if (ptr == NULL) // 포인터가 NULL인 경우 할당만 수행
        return mm_malloc(size);

    if (size <= 0) // size가 0인 경우 메모리 반환만 수행
    {
        mm_free(ptr);
        return 0;
    }

    /* 새 블록에 할당 */
    void *newptr = mm_malloc(size); // 새로 할당한 블록의 포인터
    if (newptr == NULL)
        return NULL; // 할당 실패

    /* 데이터 복사 */
    size_t copySize = GET_SIZE(HDRP(ptr)) - DSIZE; // payload만큼 복사
    if (size < copySize)                           // 기존 사이즈가 새 크기보다 더 크면
        copySize = size;                           // size로 크기 변경 (기존 메모리 블록보다 작은 크기에 할당하면, 일부 데이터만 복사)

    memcpy(newptr, ptr, copySize); // 새 블록으로 데이터 복사

    /* 기존 블록 반환 */
    mm_free(ptr);

    return newptr;
}

static void *extend_heap(size_t words)
{
    char *bp;

    size_t size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE; // 2워드의 가장 가까운 배수로 반올림 

    if ((long)(bp = mem_sbrk(size)) == -1) 
        return NULL;                      // 힙 확장 불가능 시 return NULL

    PUT(HDRP(bp), PACK(size, 0));         // 새 빈 블록 헤더 초기화
    PUT(FTRP(bp), PACK(size, 0));         // 새 빈 블록 푸터 초기화
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // 에필로그 블록 헤더 초기화

    return coalesce(bp); // 병합 후 coalesce 함수에서 리턴된 블록 포인터 반환
}

static void *find_fit(size_t asize)
{
    void *bp = mem_heap_lo() + 2 * WSIZE; //힙의 가장 낮은 주소 + 8바이트 부터 시작(first fit)
    while (GET_SIZE(HDRP(bp)) > 0)
    {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) // 가용 상태(=1)이고, 사이즈가 적합하면
            return bp;                                             // 해당 블록이 fit
        bp = NEXT_BLKP(bp);                                        // fit하지 않으므로 다음 bp로 이동
    }
    return NULL;
}

static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp)); // 현재 블록의 크기

    if ((csize - asize) >= (2 * DSIZE)) // 차이가 최소 블록 크기 16보다 같거나 크면 분할
    {
        PUT(HDRP(bp), PACK(asize, 1)); // 현재 블록에는 필요한 만큼만 할당
        PUT(FTRP(bp), PACK(asize, 1));

        PUT(HDRP(NEXT_BLKP(bp)), PACK((csize - asize), 0)); // 남은 크기를 다음 블록에 할당(가용 블록)
        PUT(FTRP(NEXT_BLKP(bp)), PACK((csize - asize), 0));
    }
    else
    {
        PUT(HDRP(bp), PACK(csize, 1)); // 해당 블록 전부 사용
        PUT(FTRP(bp), PACK(csize, 1));
    }
}