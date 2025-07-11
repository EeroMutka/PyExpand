// ds.h - minimal data structures (https://github.com/EeroMutka/ds)
// Written by Eero Mutka
//
// - Dynamic array
// - Hash map, set
// - Allocator API, arena allocator
// - String, string view
// 
// This code is released under the MIT license (https://opensource.org/licenses/MIT).

#pragma once

#include <stdint.h>
#include <string.h>  // memcpy, memmove, memset, memcmp, strlen
#include <stdarg.h>  // va_list
#include <type_traits>

#ifndef DS_NO_PRINTF
#include <stdio.h>
#endif

#ifndef DS_ASSERT
#include <assert.h>
#define DS_ASSERT(X) assert(X)
#endif

struct DS_Allocator
{
	// A new allocation is made when new_size > 0.
	// An existing allocation is freed when new_size == 0; in this case the old_size parameter is ignored.
	// To resize an existing allocation, pass the existing pointer into `old_data` and its size into `old_size`.
	void* (*AllocatorFunc)(DS_Allocator* self, void* old_data, size_t old_size, size_t size, size_t alignment);

	// -- API -----------------------------------------------------------------

	inline void* MemAlloc(size_t size, size_t alignment = 16);
	
	inline void* MemRealloc(void* old_data, size_t old_size, size_t size, size_t alignment = 16);
	
	inline void* MemClone(void* data, size_t size, size_t alignment = 16);
	
	inline void  MemFree(void* data);
};

// To provide your own malloc/free implementation, add:
//
//  #define DS_CUSTOM_MALLOC
//  DS_Allocator* DS_HeapAllocator() { return _your_custom_allocator_; }
//
#ifndef DS_CUSTOM_MALLOC
#include <stdlib.h>
static void* DS_HeapAllocatorProc(DS_Allocator* allocator, void* ptr, size_t old_size, size_t size, size_t align) {
	if (size == 0) {
		_aligned_free(ptr);
		return NULL;
	}
	else {
		return _aligned_realloc(ptr, size, align);
	}
}

static DS_Allocator* DS_HeapAllocator() {
	static const DS_Allocator result = { DS_HeapAllocatorProc };
	return (DS_Allocator*)&result;
}
#endif

struct DS_ArenaBlockHeader
{
	uint32_t SizeIncludingHeader;
	bool AllocatedFromBackingAllocator;
	DS_ArenaBlockHeader* Next; // may be NULL
};

struct DS_ArenaMark
{
	DS_ArenaBlockHeader* Block; // If the arena has no blocks allocated yet, then we mark the beginning of the arena by setting this member to NULL.
	char* Ptr;
};

struct DS_Arena : DS_Allocator
{
	DS_Allocator* BackingAllocator;
	DS_ArenaBlockHeader* FirstBlock; // may be NULL
	DS_ArenaMark Mark;
	uint32_t BlockSize;
	uint32_t BlockAlignment;

#ifdef DS_ARENA_MEMORY_TRACKING
	size_t TotalMemReserved;
#endif
	
	// ------------------------------------------------------------------------

	// if `backing_allocator` is NULL, the heap allocator is used.
	void Init(DS_Allocator* backing_allocator = NULL, void* initial_block = NULL, uint32_t block_size = 4096, uint32_t block_alignment = 16);
	void Deinit();
	
	char* PushUninitialized(size_t size, size_t alignment = 1);
	
	DS_ArenaMark GetMark();
	void SetMark(DS_ArenaMark mark);
	void Reset();
	
	template<typename T>
	inline T* New(const T& default_value)
	{
		T* x = (T*)PushUninitialized(sizeof(T), alignof(T));
		*x = default_value;
		return x;
	}

	template<typename T>
	inline T* Alloc(intptr_t n = 1)
	{
		return (T*)PushUninitialized(n * sizeof(T), alignof(T));
	}

	template<typename T>
	inline T* Clone(const T* src, intptr_t n = 1)
	{
		T* result = (T*)PushUninitialized(n * sizeof(T), alignof(T));
		memcpy(result, src, n * sizeof(T));
		return result;
	}

	inline const char* CloneStr(const char* src)
	{
		size_t len = strlen(src);
		char* result = PushUninitialized(len + 1, 1);
		memcpy(result, src, len + 1);
		return result;
	}
};

template<uint32_t STACK_BUFFER_SIZE>
struct DS_ScopedArena : DS_Arena
{
	alignas(16) char StackBuffer[STACK_BUFFER_SIZE];

	inline DS_ScopedArena()  { Init(NULL, StackBuffer, STACK_BUFFER_SIZE); }
	inline ~DS_ScopedArena() { Deinit(); }
};

// -- Array, Slice ------------------------------------------------------------

template<typename T> struct DS_Slice;
template<typename T> struct DS_Array;

template<typename T>
struct DS_Array
{
	T* Data;
	int32_t Size;
	int32_t Capacity;
	DS_Allocator* Allocator;

	// ------------------------------------------------------------------------

	// If the arena is NULL, the stored allocator is set to NULL and you must call Init() before use
	inline DS_Array<T>(DS_Arena* arena = NULL, int32_t initial_capacity = 0);

	// If allocator is NULL, the heap allocator is used.
	inline void Init(DS_Allocator* allocator = NULL, int32_t initial_capacity = 0);

	inline void Deinit();
	
	// Clear does not free memory. We could have "Clear" and "ClearNoRealloc"
	inline void Clear();
	
	inline void Reserve(int32_t reserve_count);
	
	inline void Resize(int32_t new_count, const T& default_value);

	inline void Add(const T& value);

	inline void AddSlice(DS_Slice<T> values);
	
	inline void Insert(int32_t at, const T& value, int n = 1);

	inline T& Remove(int32_t index, int n = 1);
	
	inline T& PopBack(int n = 1);
	
	inline void ReverseOrder();
	
	inline size_t SizeInBytes();

	inline T& Back();
	
	inline T& operator [](size_t i) {
		DS_ASSERT(i < (size_t)Size);
		return Data[i];
	}

	inline T operator [](size_t i) const {
		DS_ASSERT(i < (size_t)Size);
		return Data[i];
	}
};

template<typename T>
struct DS_Slice {
	T* Data;
	intptr_t Size;
	
	// ------------------------------------------------------------------------

	DS_Slice() : Data(0), Size(0) {}
	
	DS_Slice(T* data, intptr_t size) : Data(data), Size(size) {}
	
	DS_Slice(const DS_Array<T>& array) : Data(array.Data), Size(array.Size) {}
	
	template <size_t SIZE>
	DS_Slice(const T (&values)[SIZE]) : Data((T*)&values), Size(SIZE) {}

	// ------------------------------------------------------------------------

	inline const T& operator [](size_t i) const {
		DS_ASSERT(i < (size_t)Size);
		return Data[i];
	}
	inline T& operator [](size_t i) {
		DS_ASSERT(i < (size_t)Size);
		return Data[i];
	}
};

// -- String ------------------------------------------------------------------

// For passing a DS_StringView into a printf-string with %.*s
#define DS_StrVArg(STR)  STR.Size, STR.Data

struct DS_String;

// Non-null-terminated view to a string
struct DS_StringView
{
	char* Data;
	intptr_t Size;

	// ------------------------------------------------------------------------

	// Returns the character at `offset`, then moves it forward.
	// Returns 0 if goes past the end.
	uint32_t NextCodepoint(intptr_t* offset);

	// Moves `offset` backward, then returns the character at it.
	// Returns 0 if goes past the start.
	uint32_t PrevCodepoint(intptr_t* offset);

	intptr_t CodepointCount();
	
	// returns `size` if not found
	intptr_t Find(DS_StringView other, intptr_t start_from = 0);

	// returns `size` if not found
	intptr_t RFind(DS_StringView other, intptr_t start_from = INTPTR_MAX);
	
	// returns `Size` if not found
	intptr_t FindChar(char other, intptr_t start_from = 0);

	// returns `Size` if not found
	intptr_t RFindChar(char other, intptr_t start_from = INTPTR_MAX);
	
	// Find "split_by", set this string to the string after `split_by`, return the string before `split_by`
	DS_StringView Split(DS_StringView split_by);

	DS_StringView Slice(intptr_t from, intptr_t to = INTPTR_MAX);

	DS_String Clone(DS_Arena* arena) const;

	char* ToCStr(DS_Arena* arena) const;
	
	// ------------------------------------------------------------------------

	bool operator==(DS_StringView other) { return Size == other.Size && memcmp(Data, other.Data, Size) == 0; }
	bool operator!=(DS_StringView other) { return Size != other.Size || memcmp(Data, other.Data, Size) == 0; }
	bool operator==(const char* other) {
		intptr_t other_len = other ? strlen(other) : INTPTR_MIN;
		return Size == other_len && memcmp(Data, other, Size) == 0;
	}
	bool operator!=(const char* other) {
		intptr_t other_len = other ? strlen(other) : INTPTR_MIN;
		return Size != other_len || memcmp(Data, other, Size) == 0;
	}

	operator DS_Slice<char> () const { return DS_Slice<char>(Data, (int32_t)Size); }

	DS_StringView() : Data(0), Size(0) {}

	DS_StringView(const char* data, intptr_t size) : Data((char*)data), Size(size) {}

	// Implicitly construct from a string literal without an internal "strlen" call.
	// To construct from a non-literal C-string, call DS_Str(). It's a separate function and not a constructor to avoid overload-conflict.
	template <size_t SIZE>
	DS_StringView(const char (&c_str)[SIZE]) : Data((char*)c_str), Size(SIZE - 1) {}
};

// Null-terminated view to a string
struct DS_String : public DS_StringView
{
	DS_String() : DS_StringView() {}

	DS_String(const char* data, intptr_t size) : DS_StringView(data, size) {}
	
	// Implicitly construct from a string literal without an internal "strlen" call.
	// To construct from a non-literal C-string, call DS_Str(). It's a separate function and not a constructor to avoid overload-conflict.
	template <size_t SIZE>
	DS_String(const char (&c_str)[SIZE]) : DS_StringView((char*)c_str, SIZE - 1) {}

	// As C-string
	inline char* CStr() const { return Data; }
};

static inline DS_String DS_Str(const char* c_str) {
	return c_str ? DS_String(c_str, (intptr_t)strlen(c_str)) : DS_String();
}

static inline DS_String DS_Str(DS_Arena* arena, const char* c_str) {
	return c_str ? DS_String(c_str, (intptr_t)strlen(c_str)).Clone(arena) : DS_String();
}

// Null-terminated owned dynamic string
struct DS_DynamicString : public DS_String
{
	intptr_t Capacity;
	DS_Allocator* Allocator;

	// ------------------------------------------------------------------------

	// If the arena is NULL, the stored allocator is set to NULL and you must call Init() before use
	inline DS_DynamicString(DS_Arena* arena = NULL, intptr_t initial_capacity = 0);

	// If allocator is NULL, the heap allocator is used.
	inline void Init(DS_Allocator* allocator = NULL, intptr_t initial_capacity = 0);

	inline void Deinit();

	inline void Reserve(intptr_t reserve_size);
	
	inline void Add(DS_StringView str);

#ifndef DS_NO_PRINTF
	inline void Addf(const char* fmt, ...);
	inline void AddfVargs(const char* fmt, va_list args);
#endif
};

// -- Map, Set ----------------------------------------------------------------

template<typename KEY, typename VALUE>
struct DS_MapSlot {
	KEY Key; // The default value of KEY means an empty slot
	VALUE Value;
};

// Helper structs to be used as map/set keys.
// The benefit of DS_Uint32x2 over uint64_t is that the smaller alignment can make the data more tightly packed in a map in some cases.
// i.e. the size of a key-value pair in `DS_Map<uint64_t, float>` is 16 bytes, while in `DS_Map<DS_Uint32x2, float>` it's 12 bytes.
struct DS_Uint32x2 {
	uint32_t Values[2];
	inline DS_Uint32x2() : Values{ 0, 0 } {}
	inline DS_Uint32x2(uint32_t first, uint32_t second) : Values{ first, second } {}
	inline DS_Uint32x2(uint64_t u64) : Values{ (uint32_t)u64, (uint32_t)(u64 >> 32) } {}
	inline operator uint32_t() const { return Values[0]; }
	inline bool operator ==(const DS_Uint32x2& other) const { return Values[0] == other.Values[0] && Values[1] == other.Values[1]; }
};
struct DS_Uint32x3 {
	uint32_t Values[3];
	inline operator uint32_t() const { return Values[0]; }
	inline bool operator ==(const DS_Uint32x3& other) const { return Values[0] == other.Values[0] && Values[1] == other.Values[1] && Values[2] == other.Values[2]; }
};
struct DS_Uint64x2 {
	uint64_t Values[2];
	inline operator uint32_t() const { return (uint32_t)Values[0]; }
	inline bool operator ==(const DS_Uint64x2& other) const { return Values[0] == other.Values[0] && Values[1] == other.Values[1]; }
};

template<typename KEY, typename VALUE>
struct DS_Map
{
	DS_MapSlot<KEY, VALUE>* Data;
	int32_t NumElems;
	int32_t NumSlots;
	DS_Allocator* Allocator;

	// ------------------------------------------------------------------------

	// If allocator is NULL, the heap allocator is used.
	inline void Init(DS_Allocator* allocator = NULL, int initial_num_slots = 0);

	inline void Deinit();

	// * num_slots must be a power of two or zero!
	inline void Resize(int num_slots);

	// * Populates a slot at a given key without setting its value.
	// * Returns true if newly added, false otherwise.
	// * `value` is set to point to either the newly added value or the existing value.
	// * The key must be either a non-zero integer, or a value of a custom type that has the (uint32_t) cast operator and == operator defined.
	//   The default key value (zero in case of integers) is reserved internally by the map to represent empty slots.
	inline bool Add(const KEY& key, VALUE** value);

	// Returns true if the key existed
	inline bool Remove(const KEY& key);

	inline bool Has(const KEY& key);
	
	// * Set or add a value at a given key.
	// * The key must be either a non-zero integer, or a value of a custom type that has the (uint32_t) cast operator and == operator defined.
	//   The default key value (zero in case of integers) is reserved internally by the map to represent empty slots.
	inline void Set(const KEY& key, const VALUE& value);
	
	inline bool Find(const KEY& key, VALUE* value);
	
	inline VALUE* FindPtr(const KEY& key);
};

template<typename KEY>
struct DS_Set
{
	KEY* Data;
	int32_t NumElems;
	int32_t NumSlots;
	DS_Allocator* Allocator;

	// ------------------------------------------------------------------------

	// If allocator is NULL, the heap allocator is used.
	inline void Init(DS_Allocator* allocator = NULL, int initial_num_slots = 0);
	
	inline void Deinit();

	// * num_slots must be a power of two or zero!
	inline void Resize(int num_slots);

	// * Adds the key to the set.
	// * Returns true if newly added, false otherwise.
	// * The key must be either a non-zero integer, or a value of a custom type that has the (uint32_t) cast operator and == operator defined.
	//   The default key value (zero in case of integers) is reserved internally by the set to represent empty slots.
	inline bool Add(const KEY& key);
	
	// * Remove the key from the set.
	// * Returns true if the key existed, false otherwise.
	inline bool Remove(const KEY& key);

	// Does the set include the key?
	inline bool Has(const KEY& key);
};

// -- Implementation ----------------------------------------------------------

inline void* DS_Allocator::MemAlloc(size_t size, size_t alignment) {
	return AllocatorFunc(this, NULL, 0, size, alignment);
}

inline void* DS_Allocator::MemRealloc(void* old_data, size_t old_size, size_t size, size_t alignment) {
	return AllocatorFunc(this, old_data, old_size, size, alignment);
}

inline void* DS_Allocator::MemClone(void* data, size_t size, size_t alignment)
{
	void* result = AllocatorFunc(this, NULL, 0, size, alignment);
	memcpy(result, data, size);
	return result;
}

inline void DS_Allocator::MemFree(void* data) {
	AllocatorFunc(this, data, 0, 0, 1);
}

static void* DS_ArenaAllocatorFunction(DS_Allocator* self, void* old_data, size_t old_size, size_t size, size_t alignment)
{
	char* data = static_cast<DS_Arena*>(self)->PushUninitialized(size, alignment);
	if (old_data)
		memcpy(data, old_data, old_size);
	return data;
}

template<typename T>
inline DS_Array<T>::DS_Array(DS_Arena* arena, int32_t initial_capacity)
{
	Capacity = 0;
	Data = NULL;
	Size = 0;
	Allocator = arena;
	
	if (initial_capacity > 0)
		Reserve(initial_capacity);
}

template<typename T>
inline void DS_Array<T>::Init(DS_Allocator* allocator, int32_t initial_capacity)
{
	Capacity = 0;
	Data = NULL;
	Size = 0;
	Allocator = allocator ? allocator : DS_HeapAllocator();
	
	if (initial_capacity > 0)
		Reserve(initial_capacity);
}

template<typename T>
inline void DS_Array<T>::Deinit()
{
#ifndef DS_NO_DEBUG_CHECKS
	memset(Data, 0xCC, SizeInBytes());
#endif

	Allocator->MemFree(Data);

#ifndef DS_NO_DEBUG_CHECKS
	memset(this, 0xCC, sizeof(*this));
#endif
}

template<typename T>
inline void DS_Array<T>::Clear()
{
	Size = 0;
#ifndef DS_NO_DEBUG_CHECKS
	memset(Data, 0xCC, SizeInBytes());
#endif
}

template<typename T>
inline size_t DS_Array<T>::SizeInBytes()
{
	return (size_t)Size * sizeof(T);
}

template<typename T>
inline T& DS_Array<T>::Back()
{
	DS_ASSERT(Size > 0);
	return Data[Size - 1];
}

template<typename T>
inline void DS_Array<T>::Reserve(int32_t reserve_count)
{
	if (reserve_count > Capacity)
	{
		int32_t old_capacity = Capacity;
		while (reserve_count > Capacity) {
			Capacity = Capacity == 0 ? 8 : Capacity * 2;
		}

		Data = (T*)Allocator->MemRealloc(Data, old_capacity * sizeof(T), Capacity * sizeof(T), alignof(T));
	}
}

template<typename T>
inline void DS_Array<T>::Resize(int32_t new_count, const T& default_value)
{
	if (new_count > Size)
	{
		Reserve(new_count);
		for (int i = Size; i < new_count; i++)
			Data[i] = default_value;
	}
}

template<typename T>
inline void DS_Array<T>::Add(const T& value)
{
	Reserve(Size + 1);
	Data[Size] = value;
	Size = Size + 1;
}

template<typename T>
inline void DS_Array<T>::AddSlice(DS_Slice<T> values)
{
	Reserve(Size + (int32_t)values.Size);
	for (int i = 0; i < values.Size; i++)
		Data[Size + i] = values[i];
	Size = Size + (int32_t)values.Size;
}

template<typename T>
inline void DS_Array<T>::Insert(int32_t at, const T& value, int n)
{
	DS_ASSERT(at <= Size);
	Reserve(Size + n);

	char* insert_location = (char*)Data + at * sizeof(T);
	memmove(insert_location + n * sizeof(T), insert_location, (Size - at) * sizeof(T));

	for (int i = 0; i < n; i++)
		((T*)insert_location)[i] = value;
	
	Size += n;
}

template<typename T>
inline T& DS_Array<T>::Remove(int32_t index, int n)
{
	DS_ASSERT(index + n <= Size);

	T* dst = Data + index;
	T* src = dst + n;
	memmove(dst, src, (Size - index - n) * sizeof(T));

	Size -= n;
}

template<typename T>
inline T& DS_Array<T>::PopBack(int n)
{
	DS_ASSERT(Size >= n);
	Size -= n;
	return Data[Size];
}

template<typename T>
inline void DS_Array<T>::ReverseOrder()
{
	int i = 0;
	int j = Size - 1;

	T temp;
	while (i < j) {
		temp = Data[i];
		Data[i] = Data[j];
		Data[j] = temp;
		i += 1;
		j -= 1;
	}
}

inline DS_DynamicString::DS_DynamicString(DS_Arena* arena, intptr_t initial_capacity)
{
	Capacity = 0;
	Data = NULL;
	Size = 0;
	Allocator = arena;

	if (initial_capacity > 0)
		Reserve(initial_capacity);
}

inline void DS_DynamicString::Init(DS_Allocator* allocator, intptr_t initial_capacity)
{
	Capacity = 0;
	Data = NULL;
	Size = 0;
	Allocator = allocator ? allocator : DS_HeapAllocator();

	if (initial_capacity > 0)
		Reserve(initial_capacity);
}

inline void DS_DynamicString::Deinit()
{
#ifndef DS_NO_DEBUG_CHECKS
	memset(Data, 0xCC, Size);
#endif

	Allocator->MemFree(Data);

#ifndef DS_NO_DEBUG_CHECKS
	memset(this, 0xCC, sizeof(*this));
#endif
}

inline void DS_DynamicString::Reserve(intptr_t reserve_size)
{
	if (reserve_size > Capacity)
	{
		intptr_t old_capacity = Capacity;
		while (reserve_size > Capacity) {
			Capacity = Capacity == 0 ? 8 : Capacity * 2;
		}

		Data = (char*)Allocator->MemRealloc(Data, old_capacity, Capacity, 1);
	}
}

inline void DS_DynamicString::Add(DS_StringView str)
{
	Reserve(Size + str.Size + 1);
	memcpy(Data + Size, str.Data, str.Size);
	Size += str.Size;
	Data[Size] = 0;
}

#ifndef DS_NO_PRINTF
inline void DS_DynamicString::Addf(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	AddfVargs(fmt, args);
	va_end(args);
}

inline void DS_DynamicString::AddfVargs(const char* fmt, va_list args)
{
	va_list args2;
    va_copy(args2, args);
	
	char buf[256];
	int required = vsnprintf(buf, 256, fmt, args);
	
	Reserve(Size + required + 1);

	if (required + 1 <= 256)
		memcpy(Data + Size, buf, required + 1);
	else
		vsnprintf(Data + Size, required + 1, fmt, args2);

	Size += required;
	va_end(args2);
}
#endif // #ifndef DS_NO_PRINTF

template<typename KEY, typename VALUE>
inline void DS_Map<KEY, VALUE>::Init(DS_Allocator* allocator, int initial_num_slots)
{
	Data = NULL;
	NumElems = 0;
	NumSlots = 0;
	Allocator = allocator ? allocator : DS_HeapAllocator();
	if (initial_num_slots > 0)
		Resize(initial_num_slots);
}

template<typename KEY, typename VALUE>
inline void DS_Map<KEY, VALUE>::Deinit()
{
#ifndef DS_NO_DEBUG_CHECKS
	memset(Data, 0xCC, NumSlots * sizeof(DS_MapSlot<KEY, VALUE>));
#endif

	Allocator->MemFree(Data);

#ifndef DS_NO_DEBUG_CHECKS
	memset(this, 0xCC, sizeof(*this));
#endif
}

template<typename KEY, typename VALUE>
inline void DS_Map<KEY, VALUE>::Set(const KEY& key, const VALUE& value)
{
	VALUE* slot_value;
	Add(key, &slot_value);
	*slot_value = value;
}

template<typename KEY, typename VALUE>
inline bool DS_Map<KEY, VALUE>::Find(const KEY& key, VALUE* value)
{
	VALUE* ptr = FindPtr(key);
	if (ptr) *value = *ptr;
	return ptr != NULL;
}

template<typename KEY, typename VALUE>
inline VALUE* DS_Map<KEY, VALUE>::FindPtr(const KEY& key)
{
	if (NumSlots == 0)
		return NULL;

	uint32_t mask = (uint32_t)NumSlots - 1;
	uint32_t index = (uint32_t)key & mask;

	for (;;)
	{
		DS_MapSlot<KEY, VALUE>* elem = &Data[index];
		if (elem->Key == KEY{})
			return NULL;

		if (key == elem->Key)
			return &elem->Value;

		index = (index + 1) & mask;
	}
}

template<typename KEY, typename VALUE>
inline void DS_Map<KEY, VALUE>::Resize(int num_slots)
{
	DS_ASSERT((num_slots & (num_slots - 1)) == 0); // num_slots must be a power of two or zero!
	DS_ASSERT(num_slots >= NumElems);

	DS_MapSlot<KEY, VALUE>* old_data = Data;
	int old_capacity = NumSlots;

	NumSlots = num_slots;
	NumElems = 0;

	size_t allocation_size = NumSlots * sizeof(DS_MapSlot<KEY, VALUE>);
	Data = (DS_MapSlot<KEY, VALUE>*)Allocator->MemAlloc(allocation_size, alignof(DS_MapSlot<KEY, VALUE>));
	for (int i = 0; i < NumSlots; i++)
		Data[i] = {};

	if (old_capacity > 0)
	{
		for (int i = 0; i < old_capacity; i++)
		{
			DS_MapSlot<KEY, VALUE>* elem = &old_data[i];
			if (!(elem->Key == KEY{}))
			{
				VALUE* new_value;
				Add(elem->Key, &new_value);
				*new_value = elem->Value;
			}
		}

#ifndef DS_NO_DEBUG_CHECKS
		memset(old_data, 0xCC, old_capacity * sizeof(DS_MapSlot<KEY, VALUE>));
#endif
		Allocator->MemFree(old_data);
	}
}

template<typename KEY, typename VALUE>
inline bool DS_Map<KEY, VALUE>::Add(const KEY& key, VALUE** value)
{
	DS_ASSERT(Allocator != NULL); // Have you called Init?
	DS_ASSERT(!(key == KEY{})); // The default key value is reserved internally to represent empty slots.

	// Grow the map if the map is filled over 70%
	if (100 * (NumElems + 1) > 70 * NumSlots)
		Resize(NumSlots == 0 ? 8 : NumSlots * 2);

	uint32_t mask = (uint32_t)NumSlots - 1;
	uint32_t index = (uint32_t)key & mask;

	bool added_new;
	for (;;)
	{
		DS_MapSlot<KEY, VALUE>* elem = &Data[index];

		if (elem->Key == KEY{})
		{
			// Found an empty slot
			elem->Key = key;
			*value = &elem->Value;
			NumElems += 1;
			added_new = true;
			break;
		}

		if (key == elem->Key)
		{
			// This key already exists
			*value = &elem->Value;
			added_new = false;
			break;
		}

		index = (index + 1) & mask;
	}

	return added_new;
}

template<typename KEY, typename VALUE>
inline bool DS_Map<KEY, VALUE>::Remove(const KEY& key)
{
	if (NumSlots == 0)
		return false;

	uint32_t mask = (uint32_t)NumSlots - 1;
	uint32_t index = (uint32_t)key & mask;

	bool removed;

	for (;;) {
		DS_MapSlot<KEY, VALUE>* elem = &Data[index];

		if (elem->Key == KEY{}) { // Empty slot, the key does not exist in the map
			removed = false;
			break;
		}

		if (key == elem->Key) { // Found the element!
#ifndef DS_NO_DEBUG_CHECKS
			memset(elem, 0xCC, sizeof(*elem));
#endif
			elem->Key = KEY{};
			NumElems -= 1;

			// Backwards-shift deletion
			for (;;) {
				index = (index + 1) & mask;

				DS_MapSlot<KEY, VALUE>* moving = &Data[index];
				if (moving->KEY == KEY{}) break;

				DS_MapSlot<KEY, VALUE> temp = *moving;
#ifndef DS_NO_DEBUG_CHECKS
				memset(moving, 0xCC, sizeof(*moving));
#endif
				moving->Key = KEY{};
				NumElems -= 1;

				VALUE* readded;
				Add(temp.Key, &readded);
				*readded = temp.Value;
			}

			removed = true;
			break;
		}

		index = (index + 1) & mask;
	}

	return removed;
}

template<typename KEY, typename VALUE>
inline bool DS_Map<KEY, VALUE>::Has(const KEY& key)
{
	VALUE* ptr = FindPtr(key);
	return ptr != NULL;
}

template<typename KEY>
inline void DS_Set<KEY>::Init(DS_Allocator* allocator, int initial_num_slots)
{
	Data = NULL;
	NumElems = 0;
	NumSlots = 0;
	Allocator = allocator ? allocator : DS_HeapAllocator();
	if (initial_num_slots > 0)
		Resize(initial_num_slots);
}

template<typename KEY>
inline void DS_Set<KEY>::Deinit()
{
#ifndef DS_NO_DEBUG_CHECKS
	memset(Data, 0xCC, NumSlots * sizeof(KEY));
#endif

	Allocator->MemFree(Data);

#ifndef DS_NO_DEBUG_CHECKS
	memset(this, 0xCC, sizeof(*this));
#endif
}

template<typename KEY>
inline bool DS_Set<KEY>::Has(const KEY& key)
{
	if (NumSlots == 0)
		return false;

	uint32_t mask = (uint32_t)NumSlots - 1;
	uint32_t index = (uint32_t)key & mask;

	for (;;)
	{
		KEY* elem = &Data[index];
		if (*elem == KEY{})
			return false;

		if (key == *elem)
			return true;

		index = (index + 1) & mask;
	}
}

template<typename KEY>
inline void DS_Set<KEY>::Resize(int num_slots)
{
	DS_ASSERT((num_slots & (num_slots - 1)) == 0); // num_slots must be a power of two or zero!
	DS_ASSERT(num_slots >= NumElems);

	KEY* old_data = Data;
	int old_capacity = NumSlots;

	NumSlots = num_slots;
	NumElems = 0;

	size_t allocation_size = NumSlots * sizeof(KEY);
	Data = (KEY*)Allocator->MemAlloc(allocation_size, alignof(KEY));
	for (int i = 0; i < NumSlots; i++)
		Data[i] = {};

	if (num_slots > 0)
	{
		for (int i = 0; i < old_capacity; i++)
		{
			KEY elem = old_data[i];
			if (!(elem == KEY{}))
				Add(elem);
		}

	#ifndef DS_NO_DEBUG_CHECKS
		memset(old_data, 0xCC, old_capacity * sizeof(KEY));
	#endif
		Allocator->MemFree(old_data);
	}
}

template<typename KEY>
inline bool DS_Set<KEY>::Add(const KEY& key)
{
	DS_ASSERT(Allocator != NULL); // Have you called Init?
	DS_ASSERT(!(key == KEY{})); // The default key value is reserved internally to represent empty slots.

	// Grow the set if the set is filled over 70%
	if (100 * (NumElems + 1) > 70 * NumSlots)
		Resize(NumSlots == 0 ? 8 : NumSlots * 2);

	uint32_t mask = (uint32_t)NumSlots - 1;
	uint32_t index = (uint32_t)key & mask;

	bool added_new;
	for (;;)
	{
		KEY* elem = &Data[index];

		if (*elem == KEY{})
		{
			// Found an empty slot
			*elem = key;
			NumElems += 1;
			added_new = true;
			break;
		}

		if (key == *elem)
		{
			// This key already exists
			added_new = false;
			break;
		}

		index = (index + 1) & mask;
	}

	return added_new;
}

template<typename KEY>
inline bool DS_Set<KEY>::Remove(const KEY& key)
{
	if (NumSlots == 0)
		return false;

	uint32_t mask = (uint32_t)NumSlots - 1;
	uint32_t index = (uint32_t)key & mask;

	bool removed;

	for (;;) {
		KEY* elem = &Data[index];

		if (*elem == KEY{}) { // Empty slot, the key does not exist in the set
			removed = false;
			break;
		}

		if (key == *elem) { // Found the element!
			*elem = KEY{};
			NumElems -= 1;

			// Backwards-shift deletion
			for (;;) {
				index = (index + 1) & mask;

				KEY* moving = &Data[index];
				if (*moving == KEY{}) break;

				KEY temp = *moving;
#ifndef DS_NO_DEBUG_CHECKS
				memset(moving, 0xCC, sizeof(*moving));
#endif
				*moving = KEY{};
				NumElems -= 1;

				Add(temp);
			}

			removed = true;
			break;
		}

		index = (index + 1) & mask;
	}

	return removed;
}
