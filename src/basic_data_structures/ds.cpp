#include "ds.h"

#define DS_IsUtf8FirstByte(c) (((c) & 0xC0) != 0x80) /* is c the start of a utf8 sequence? */

static const uint32_t DS_UTF8_OFFSETS[6] = {
	0x00000000UL, 0x00003080UL, 0x000E2080UL,
	0x03C82080UL, 0xFA082080UL, 0x82082080UL
};

static uint32_t DS_NextCodepoint(char* str, intptr_t size, intptr_t* offset)
{
	if (*offset >= size) return 0;

	// See https://www.cprogramming.com/tutorial/unicode.html (u8_nextchar)
	uint32_t ch = 0;
	size_t sz = 0;
	do {
		ch <<= 6;
		ch += (unsigned char)str[(*offset)++];
		sz++;
	} while (*offset < size && !DS_IsUtf8FirstByte(str[*offset]));
	ch -= DS_UTF8_OFFSETS[sz - 1];

	return (uint32_t)ch;
}

static uint32_t DS_PrevCodepoint(char* str, intptr_t size, intptr_t* offset)
{
	if (*offset <= 0) return 0;

	// See https://www.cprogramming.com/tutorial/unicode.html
	(void)(DS_IsUtf8FirstByte(str[--(*offset)]) ||
		DS_IsUtf8FirstByte(str[--(*offset)]) ||
		DS_IsUtf8FirstByte(str[--(*offset)]) || --(*offset));

	intptr_t b = *offset;
	uint32_t result = DS_NextCodepoint(str, size, &b);
	return result;
}

static intptr_t DS_CodepointCount(char* str, intptr_t size)
{
	intptr_t count = 0;
	intptr_t offset = 0;
	for (;;) {
		uint32_t c = DS_NextCodepoint(str, size, &offset);
		if (c == 0) break;
		count++;
	}
	return count;
}

uint32_t DS_StringView::NextCodepoint(intptr_t* offset) {
	return DS_NextCodepoint(Data, Size, offset);
}

uint32_t DS_StringView::PrevCodepoint(intptr_t* offset) {
	return DS_PrevCodepoint(Data, Size, offset);
}

intptr_t DS_StringView::CodepointCount() {
	return DS_CodepointCount(Data, Size);
}

intptr_t DS_StringView::Find(DS_StringView other, intptr_t start_from)
{
	DS_ASSERT(start_from >= 0 && start_from <= Size);
	intptr_t result = Size;
	if (other.Size <= Size)
	{
		char* ptr = Data + start_from;
		char* end = Data + Size - other.Size;
		for (; ptr <= end; ptr++)
		{
			if (memcmp(ptr, other.Data, other.Size) == 0) {
				result = ptr - Data;
				break;
			}
		}
	}
	return result;
}

intptr_t DS_StringView::RFind(DS_StringView other, intptr_t start_from)
{
	intptr_t result = Size;
	if (other.Size <= Size)
	{
		char* ptr = Data + (start_from >= Size ? Size : start_from) - other.Size;
		for (; ptr >= Data; ptr--)
		{
			if (memcmp(ptr, other.Data, other.Size) == 0) {
				result = ptr - Data;
				break;
			}
		}
	}
	return result;
}

DS_StringView DS_StringView::Split(DS_StringView split_by)
{
	intptr_t offset = Find(split_by);
	DS_StringView result = {Data, offset};
	intptr_t advance = offset + split_by.Size > Size ? Size : offset + split_by.Size;
	Data += advance;
	Size -= advance;
	return result;
}

DS_StringView DS_StringView::Slice(intptr_t from, intptr_t to)
{
	if (to == INTPTR_MAX) to = Size;
	DS_ASSERT(from >= 0);
	DS_ASSERT(to <= Size);
	DS_ASSERT(to >= from);
	return DS_StringView(Data + from, to - from);
}

DS_String DS_StringView::Clone(DS_Arena* arena) const {
	DS_String result;
	result.Data = arena->PushUninitialized(Size + 1);
	result.Size = Size;
	memcpy(result.Data, Data, Size);
	result.Data[Size] = 0;
	return result;
}

char* DS_StringView::ToCStr(DS_Arena* arena) const {
	return Clone(arena).CStr();
}

void DS_Arena::Init(DS_Allocator* backing_allocator, void* initial_block, uint32_t block_size, uint32_t block_alignment)
{
	BackingAllocator = backing_allocator ? backing_allocator : DS_HeapAllocator();
	FirstBlock = (DS_ArenaBlockHeader*)initial_block;
	Mark.Block = FirstBlock;
	Mark.Ptr = NULL;
	BlockSize = block_size;
	BlockAlignment = block_alignment;
	AllocatorFunc = DS_ArenaAllocatorFunction;
#ifdef DS_ARENA_MEMORY_TRACKING
	total_mem_reserved = 0;
#endif
	
	if (initial_block)
	{
		DS_ASSERT(((uintptr_t)initial_block & ((uintptr_t)block_alignment - 1)) == 0); // make sure that the alignment is correct
		DS_ArenaBlockHeader* header = (DS_ArenaBlockHeader*)initial_block;
		header->AllocatedFromBackingAllocator = false;
		header->SizeIncludingHeader = block_size;
		header->Next = NULL;
		Mark.Ptr = (char*)FirstBlock + sizeof(DS_ArenaBlockHeader);
#ifdef DS_ARENA_MEMORY_TRACKING
		total_mem_reserved += block_size;
#endif
	}
}

void DS_Arena::Deinit()
{
	for (DS_ArenaBlockHeader* block = FirstBlock; block;)
	{
		DS_ArenaBlockHeader* next = block->Next;
		if (block->AllocatedFromBackingAllocator)
			BackingAllocator->MemFree(block);
		block = next;
	}

#ifndef DS_NO_DEBUG_CHECKS
	memset(this, 0xCC, sizeof(DS_Arena));
#endif
}

char* DS_Arena::PushUninitialized(size_t size, size_t alignment)
{
	bool alignment_is_power_of_2 = ((alignment) & ((alignment)-1)) == 0;
	DS_ASSERT(alignment != 0 && alignment_is_power_of_2);
	DS_ASSERT(alignment <= BlockAlignment);

	DS_ArenaBlockHeader* curr_block = Mark.Block; // may be NULL
	void* curr_ptr = Mark.Ptr;

	char* result = (char*)DS_AlignUpPow2((intptr_t)curr_ptr, alignment);
	intptr_t remaining_space = curr_block ? curr_block->SizeIncludingHeader - ((intptr_t)result - (intptr_t)curr_block) : 0;

	if ((intptr_t)size > remaining_space)
	{
		// We need a new block
		intptr_t result_offset = DS_AlignUpPow2(sizeof(DS_ArenaBlockHeader), alignment);
		intptr_t new_block_size = result_offset + size;
		if ((intptr_t)BlockSize > new_block_size) new_block_size = BlockSize;

		DS_ArenaBlockHeader* new_block = NULL;
		DS_ArenaBlockHeader* next_block = NULL;

		// If there is a block at the end of the list that we have used previously, but aren't using anymore, then try to start using that one.
		if (curr_block && curr_block->Next)
		{
			next_block = curr_block->Next;

			intptr_t next_block_remaining_space = next_block->SizeIncludingHeader - result_offset;
			if ((intptr_t)size <= next_block_remaining_space) {
				new_block = next_block; // Next block has enough space, let's use it!
			}
		}

		// Otherwise, insert a new block.
		if (new_block == NULL)
		{
			new_block = (DS_ArenaBlockHeader*)BackingAllocator->MemAlloc(new_block_size, BlockAlignment);
			DS_ASSERT(((uintptr_t)new_block & ((uintptr_t)BlockAlignment - 1)) == 0); // make sure that the alignment is correct

			new_block->AllocatedFromBackingAllocator = true;
			new_block->SizeIncludingHeader = (uint32_t)new_block_size;
			new_block->Next = next_block;
#ifdef DS_ARENA_MEMORY_TRACKING
			total_mem_reserved += new_block_size;
#endif
			if (curr_block) curr_block->Next = new_block;
			else FirstBlock = new_block;
		}

		Mark.Block = new_block;
		result = (char*)new_block + result_offset;
	}

	Mark.Ptr = result + size;
	return result;
}

void DS_Arena::Reset()
{
	if (FirstBlock) {
		// Free all blocks after the first block
		for (DS_ArenaBlockHeader* block = FirstBlock->Next; block;)
		{
			DS_ArenaBlockHeader* next = block->Next;
#ifdef DS_ARENA_MEMORY_TRACKING
			total_mem_reserved -= block->size_including_header;
#endif
			BackingAllocator->MemFree(block);
			block = next;
		}
		FirstBlock->Next = NULL;

		// Free the first block too if it's larger than block_size
		if (FirstBlock->SizeIncludingHeader > BlockSize)
		{
#ifdef DS_ARENA_MEMORY_TRACKING
			total_mem_reserved -= first_block->size_including_header;
#endif
			if (FirstBlock->AllocatedFromBackingAllocator)
				BackingAllocator->MemFree(FirstBlock);
			FirstBlock = NULL;
		}
	}

	Mark.Block = FirstBlock;
	Mark.Ptr = (char*)FirstBlock + sizeof(DS_ArenaBlockHeader);
}

DS_ArenaMark DS_Arena::GetMark() {
	return Mark;
}

void DS_Arena::SetMark(DS_ArenaMark mark)
{
	if (mark.Block == NULL) {
		Mark.Block = FirstBlock;
		Mark.Ptr = (char*)FirstBlock + sizeof(DS_ArenaBlockHeader);
	}
	else {
		Mark = mark;
	}
}

uint32_t DS_MurmurHash3(const void* key, intptr_t len, uint32_t seed)
{
	const uint8_t * data = (const uint8_t*)key;
	const intptr_t nblocks = len / 4;

	uint32_t h1 = seed;

	const uint32_t c1 = 0xcc9e2d51;
	const uint32_t c2 = 0x1b873593;

	//----------
	// body

	const uint32_t * blocks = (const uint32_t *)(data + nblocks*4);

	for(intptr_t i = -nblocks; i; i++)
	{
		uint32_t k1 = DS_getblock32(blocks,i);

		k1 *= c1;
		k1 = DS_ROTL32(k1,15);
		k1 *= c2;

		h1 ^= k1;
		h1 = DS_ROTL32(h1,13); 
		h1 = h1*5+0xe6546b64;
	}

	//----------
	// tail

	const uint8_t * tail = (const uint8_t*)(data + nblocks*4);

	uint32_t k1 = 0;

	switch(len & 3)
	{
	case 3: k1 ^= tail[2] << 16;
	case 2: k1 ^= tail[1] << 8;
	case 1: k1 ^= tail[0];
		k1 *= c1; k1 = DS_ROTL32(k1,15); k1 *= c2; h1 ^= k1;
	};

	//----------
	// finalization

	h1 ^= (uint32_t)len;

	h1 = DS_fmix32(h1);

	return h1;
}

//void DS_Internal_BucketArrayMoveToNextBucket(void* array, size_t elem_size, size_t N)
//{
//	DS_BucketArray<char, 1>* arr = (DS_BucketArray<char, 1>*)array;
//
//	if (arr->buckets_count == arr->buckets_capacity)
//	{
//		uint32_t new_cap = arr->buckets_capacity == 0 ? 8 : arr->buckets_capacity * 2;
//		*(void**)&arr->buckets = arr->allocator->MemRealloc(arr->buckets, arr->buckets_capacity * sizeof(void*), new_cap * sizeof(void*));
//		arr->buckets_capacity = new_cap;
//	}
//	
//	arr->buckets[arr->buckets_count].elems = (char*)arr->allocator->MemAlloc(elem_size * N);
//	arr->buckets_count += 1;
//	arr->last_bucket_end = 0;
//}