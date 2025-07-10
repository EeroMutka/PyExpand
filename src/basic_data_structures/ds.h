// ds.h - basic data structures
// Made by Eero Mutka (https://eeromutka.github.io/)
//
// Features:
// - Dynamic arrays
// - Hash maps & sets
// - Memory arenas
// - Bucket arrays
// - Slot allocators
// 
// This code is released under the MIT license (https://opensource.org/licenses/MIT).

#pragma once

#define DS_VERSION 1

#include <stdint.h>
#include <string.h>  // memcpy, memmove, memset, memcmp, strlen
#include <stdarg.h>  // va_list
#include <type_traits>

#ifndef DS_NO_MALLOC
#include <stdlib.h>
#endif

#ifndef DS_NO_PRINTF
#include <stdio.h>
#endif

#ifndef DS_ASSERT
#include <assert.h>
#define DS_ASSERT(X) assert(X)
#endif

// `p` must be a power of 2.
// `x` is allowed to be negative as well.
#define DS_AlignUpPow2(x, p) (((x) + (p) - 1) & ~((p) - 1)) // e.g. (x=30, p=16) -> 32
#define DS_AlignDownPow2(x, p) ((x) & ~((p) - 1)) // e.g. (x=30, p=16) -> 16

#define DS_KIB(x) ((uint64_t)(x) << 10)
#define DS_MIB(x) ((uint64_t)(x) << 20)
#define DS_GIB(x) ((uint64_t)(x) << 30)
#define DS_TIB(x) ((uint64_t)(x) << 40)

#if defined(_MSC_VER)
#define DS_FORCE_INLINE __forceinline
#else // defined(_MSC_VER)
#define	DS_FORCE_INLINE inline __attribute__((always_inline))
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
	inline void  MemFree(void* data);

	// TODO: clone memory with size?
	// TODO: new value?
};

static DS_Allocator* DS_HeapAllocator();

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
	
	// -- Methods -------------------------------------------------------------

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
	
	// -- Methods -------------------------------------------------------------

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

// TODO: remove this
#if 0

// -- Bucket array ------------------------------------------------------------

template<typename T, int N>
struct DS_BucketArray
{
	DS_Allocator* allocator;
	struct { T* elems; }* buckets;
	int32_t buckets_count;
	int32_t buckets_capacity;
	int32_t last_bucket_end;
	int32_t count;
	
	// ------------------------------------------------------------------------

	inline DS_BucketArray<T, N>(DS_Arena* arena = NULL);

	inline void Init(DS_Allocator* allocator = NULL);

	inline void Deinit();

	inline T* Add(const T& value = {});
	
	// ------------------------------------------------------------------------
	
	struct Iter
	{
		T* value;
		const DS_BucketArray<T, N>* _array;
		int _bucket_idx;
		T* _bucket_end;
		inline bool operator!=(const Iter& other) { return value != other.value; }
		inline Iter& operator*() { return *this; }
		inline Iter& operator++() {
			if (value == _bucket_end) {
				value = (T*)_array->buckets[++_bucket_idx].elems;
				_bucket_end = value + N;
			}
			else value++;
			return *this;
		}
	};
	inline Iter begin() const {
		if (buckets_count == 0) return {};
		return { buckets[0].elems, this, 0, buckets[0].elems + N };
	};
	inline Iter end() const {
		if (buckets_count == 0) return {};
		return { buckets[buckets_count - 1].elems + N };
	};
};
#endif

// -- String ------------------------------------------------------------------

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
	
	// Find "split_by", set this string to the string after `split_by`, return the string before `split_by`
	DS_StringView Split(DS_StringView split_by);

	DS_StringView Slice(intptr_t from, intptr_t to = INTPTR_MAX);

	DS_String Clone(DS_Arena* arena) const;

	char* ToCStr(DS_Arena* arena) const;
	
	// ------------------------------------------------------------------------

	bool operator==(DS_StringView other) { return Size == other.Size && memcmp(Data, other.Data, Size) == 0; }

	operator DS_Slice<char> () const { return DS_Slice<char>(Data, (int32_t)Size); }

	DS_StringView() : Data(0), Size(0) {}

	DS_StringView(const char* data, intptr_t size) : Data((char*)data), Size(size) {}

	// Implicitly construct from a string literal without an internal "strlen" call.
	// To construct from a non-literal C-string, call DS_ToString(). It's a separate function and not a constructor to avoid overload-conflict.
	template <size_t SIZE>
	DS_StringView(const char (&c_str)[SIZE]) : Data((char*)c_str), Size(SIZE - 1) {}
};

// Null-terminated view to a string
struct DS_String : public DS_StringView
{
	DS_String() : DS_StringView() {}

	DS_String(const char* data, intptr_t size) : DS_StringView(data, size) {}
	
	// Implicitly construct from a string literal without an internal "strlen" call.
	// To construct from a non-literal C-string, call DS_ToString(). It's a separate function and not a constructor to avoid overload-conflict.
	template <size_t SIZE>
	DS_String(const char (&c_str)[SIZE]) : DS_StringView((char*)c_str, SIZE - 1) {}

	// As C-string
	inline char* CStr() const { return Data; }
};

static inline DS_String DS_ToString(const char* c_str) {
	return c_str ? DS_String(c_str, (intptr_t)strlen(c_str)) : DS_String();
}

static inline DS_String DS_ToString(DS_Arena* arena, const char* c_str) {
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

template<typename T>
struct DS_HashOperator
{
	uint32_t operator()(const T& value) = delete;
};

template<>
struct DS_HashOperator<int>
{
	uint32_t operator()(const int& value)
	{
		return value;
	}
};

template<typename KEY, typename VALUE>
struct DS_MapSlot {
	uint32_t Hash; // 0 means an empty slot; valid hash values always have the (1 << 31) bit set.
	KEY Key;
	VALUE Value;
};

template<typename KEY, typename VALUE>
struct DS_Map
{
	DS_MapSlot<KEY, VALUE>* Data;
	int32_t HasCount;
	int32_t Capacity;
	DS_Allocator* Allocator;

	// -- Methods -------------------------------------------------------------

	// If allocator is NULL, the heap allocator is used.
	inline void Init(DS_Allocator* allocator = NULL);

	inline void Deinit();

	// Populates a slot at a given key without settting its value, returns true if newly added.
	// `value` is set to point to either the newly added value or the existing value.
	inline bool Add(const KEY& key, VALUE** value);

	// Returns true if the key existed
	inline bool Remove(const KEY& key);

	inline bool Has(const KEY& key);
	
	// Set or add a value at a given key
	inline void Set(const KEY& key, const VALUE& value);
	
	inline bool Find(const KEY& key, VALUE* value);
	
	inline VALUE* FindPtr(const KEY& key);
};

template<typename KEY>
struct DS_SetSlot {
	uint32_t Hash; // 0 means an empty slot; valid hash values always have the (1 << 31) bit set.
	KEY Key;
};

template<typename KEY>
struct DS_Set
{
	DS_SetSlot<KEY>* Data;
	int32_t HasCount;
	int32_t Capacity;
	DS_Allocator* Allocator;

	// -- Methods -------------------------------------------------------------

	// If allocator is NULL, the heap allocator is used.
	inline void Init(DS_Allocator* allocator = NULL);
	
	inline void Deinit();

	// Returns true if newly added
	inline bool Add(const KEY& key);
	
	// Returns true if the key existed
	inline bool Remove(const KEY& key);

	inline bool Has(const KEY& key);
};

// -- Implementation ----------------------------------------------------------

inline void* DS_Allocator::MemAlloc(size_t size, size_t alignment) {
	return AllocatorFunc(this, NULL, 0, size, alignment);
}

inline void* DS_Allocator::MemRealloc(void* old_data, size_t old_size, size_t size, size_t alignment) {
	return AllocatorFunc(this, old_data, old_size, size, alignment);
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

DS_FORCE_INLINE uint32_t DS_fmix32(uint32_t h);
DS_FORCE_INLINE uint64_t DS_fmix64(uint64_t h);

template<typename T>
struct DS_KeyType;

template<typename T> static inline uint32_t DS_Hash(const T& x) { return DS_KeyType<T>::hash(x); }
template<typename T> static inline bool DS_IsEqual(const T& a, const T& b) { return DS_KeyType<T>::is_equal(a, b); }

template <class T, class... TArgs> decltype(void(T{std::declval<TArgs>()...}), std::true_type{}) DS__TestIsBracesConstructible(int);
template <class, class...> std::false_type DS__TestIsBracesConstructible(...);
template <class T, class... TArgs> using DS__IsBracesConstructible = decltype(DS__TestIsBracesConstructible<T, TArgs...>(0));
struct DS__ANY_T { template <class T> constexpr operator T(); };

uint32_t DS_MurmurHash3(const void* key, intptr_t len, uint32_t seed);

template<typename T>
struct DS_KeyType
{
	static uint32_t hash(const T& x) {
		using type = std::decay_t<T>;

		// When combining hashes, we multiply the first hash value by 2 and then add the second hash value.
		// This makes the hashing order-dependent. What we multiply by doesn't really matter, because get_hash() always returns an
		// already-mixed hash.

		if constexpr(std::is_fundamental<T>::value) {
			if constexpr(sizeof(T) == 4) {
				// We XOR with a random number first to make sure we don't accidentally pass 0 into the hash mixer.
				return DS_fmix32(*(uint32_t*)&x ^ 2607369547);
			} else {
				return DS_fmix64(*(uint64_t*)&x ^ 2607369547);
			}
		} else if constexpr(std::is_same<T, DS_String>::value || std::is_same<T, DS_StringView>::value) {
			return DS_MurmurHash3(x.Data, x.Size, 2607369547);
		} else if constexpr(DS__IsBracesConstructible<type, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T>{}) {
			auto[x1, x2, x3, x4, x5, x6, x7, x8] = x;
			return 2*(2*(2*(2*(2*(2*(2*DS_Hash(x1) + DS_Hash(x2)) + DS_Hash(x3)) + DS_Hash(x4)) + DS_Hash(x5)) + DS_Hash(x6)) + DS_Hash(x7)) + DS_Hash(x8);
		} else if constexpr(DS__IsBracesConstructible<type, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T>{}) {
			auto[x1, x2, x3, x4, x5, x6, x7] = x;
			return 2*(2*(2*(2*(2*(2*DS_Hash(x1) + DS_Hash(x2)) + DS_Hash(x3)) + DS_Hash(x4)) + DS_Hash(x5)) + DS_Hash(x6)) + DS_Hash(x7);
		} else if constexpr(DS__IsBracesConstructible<type, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T>{}) {
			auto[x1, x2, x3, x4, x5, x6] = x;
			return 2*(2*(2*(2*(2*DS_Hash(x1) + DS_Hash(x2)) + DS_Hash(x3)) + DS_Hash(x4)) + DS_Hash(x5)) + DS_Hash(x6);
		} else if constexpr(DS__IsBracesConstructible<type, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T>{}) {
			auto[x1, x2, x3, x4, x5] = x;
			return 2*(2*(2*(2*DS_Hash(x1) + DS_Hash(x2)) + DS_Hash(x3)) + DS_Hash(x4)) + DS_Hash(x5);
		} else if constexpr(DS__IsBracesConstructible<type, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T>{}) {
			auto[x1, x2, x3, x4] = x;
			return 2*(2*(2*DS_Hash(x1) + DS_Hash(x2)) + DS_Hash(x3)) + DS_Hash(x4);
		} else if constexpr(DS__IsBracesConstructible<type, DS__ANY_T, DS__ANY_T, DS__ANY_T>{}) {
			auto[x1, x2, x3] = x;
			return 2*(2*DS_Hash(x1) + DS_Hash(x2)) + DS_Hash(x3);
		} else if constexpr(DS__IsBracesConstructible<type, DS__ANY_T, DS__ANY_T>{}) {
			auto[x1, x2] = x;
			return 2*DS_Hash(x1) + DS_Hash(x2);
		} else if constexpr(DS__IsBracesConstructible<type, DS__ANY_T>{}) {
			auto[x1] = x;
			return DS_Hash(x1);
		} else {
			DS_ASSERT(false);
		}
	}

	static bool is_equal(const T& a, const T& b) {
		using type = std::decay_t<T>;
		if constexpr(std::is_fundamental<T>::value) {
			return a == b;
		} else if constexpr(std::is_same<T, DS_String>::value || std::is_same<T, DS_StringView>::value) {
			return a.Size == b.Size && memcmp(a.Data, b.Data, a.Size) == 0;
		} else if constexpr(DS__IsBracesConstructible<type, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T>{}) {
			auto[a1, a2, a3, a4, a5, a6, a7, a8] = a; auto[b1, b2, b3, b4, b5, b6, b7, b8] = b;
			return DS_IsEqual(a1, b1) && DS_IsEqual(a2, b2) && DS_IsEqual(a3, b3) && DS_IsEqual(a4, b4) && DS_IsEqual(a5, b5) && DS_IsEqual(a6, b6) && DS_IsEqual(a7, b7) && DS_IsEqual(a8, b8);
		} else if constexpr(DS__IsBracesConstructible<type, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T>{}) {
			auto[a1, a2, a3, a4, a5, a6, a7] = a; auto[b1, b2, b3, b4, b5, b6, b7] = b;
			return DS_IsEqual(a1, b1) && DS_IsEqual(a2, b2) && DS_IsEqual(a3, b3) && DS_IsEqual(a4, b4) && DS_IsEqual(a5, b5) && DS_IsEqual(a6, b6) && DS_IsEqual(a7, b7);
		} else if constexpr(DS__IsBracesConstructible<type, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T>{}) {
			auto[a1, a2, a3, a4, a5, a6] = a; auto[b1, b2, b3, b4, b5, b6] = b;
			return DS_IsEqual(a1, b1) && DS_IsEqual(a2, b2) && DS_IsEqual(a3, b3) && DS_IsEqual(a4, b4) && DS_IsEqual(a5, b5) && DS_IsEqual(a6, b6);
		} else if constexpr(DS__IsBracesConstructible<type, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T>{}) {
			auto[a1, a2, a3, a4, a5] = a; auto[b1, b2, b3, b4, b5] = b;
			return DS_IsEqual(a1, b1) && DS_IsEqual(a2, b2) && DS_IsEqual(a3, b3) && DS_IsEqual(a4, b4) && DS_IsEqual(a5, b5);
		} else if constexpr(DS__IsBracesConstructible<type, DS__ANY_T, DS__ANY_T, DS__ANY_T, DS__ANY_T>{}) {
			auto[a1, a2, a3, a4] = a; auto[b1, b2, b3, b4] = b;
			return DS_IsEqual(a1, b1) && DS_IsEqual(a2, b2) && DS_IsEqual(a3, b3) && DS_IsEqual(a4, b4);
		} else if constexpr(DS__IsBracesConstructible<type, DS__ANY_T, DS__ANY_T, DS__ANY_T>{}) {
			auto[a1, a2, a3] = a; auto[b1, b2, b3] = b;
			return DS_IsEqual(a1, b1) && DS_IsEqual(a2, b2) && DS_IsEqual(a3, b3);
		} else if constexpr(DS__IsBracesConstructible<type, DS__ANY_T, DS__ANY_T>{}) {
			auto[a1, a2] = a;
			auto[b1, b2] = b;
			return DS_IsEqual(a1, b1) && DS_IsEqual(a2, b2);
		} else if constexpr(DS__IsBracesConstructible<type, DS__ANY_T>{}) {
			auto[a1] = a;
			auto[b1] = b;
			return DS_IsEqual(a1, b1);
		} else {
			DS_ASSERT(false);
		}
	}
};

template<typename KEY, typename VALUE>
inline void DS_Map<KEY, VALUE>::Init(DS_Allocator* allocator)
{
	Data = NULL;
	HasCount = 0;
	Capacity = 0;
	Allocator = allocator ? allocator : DS_HeapAllocator();
}

template<typename KEY, typename VALUE>
inline void DS_Map<KEY, VALUE>::Deinit()
{
#ifndef DS_NO_DEBUG_CHECKS
	memset(Data, 0xCC, Capacity * sizeof(DS_MapSlot<KEY, VALUE>));
#endif

	Allocator->MemFree(Data);

#ifndef DS_NO_DEBUG_CHECKS
	memset(this, 0xCC, sizeof(*this));
#endif
}

template<typename KEY, typename VALUE>
static bool DS__MapAdd(DS_Map<KEY, VALUE>* map, const KEY& key, VALUE** out_value, uint32_t hash) {
	DS_ASSERT(map->Allocator != NULL); // Have you called Init?

	if (100 * (map->HasCount + 1) > 70 * map->Capacity)
	{
		// Grow the map

		DS_MapSlot<KEY, VALUE>* old_data = map->Data;
		int old_capacity = map->Capacity;

		map->Capacity = old_capacity == 0 ? 8 : old_capacity * 2;
		map->HasCount = 0;

		size_t allocation_size = map->Capacity * sizeof(DS_MapSlot<KEY, VALUE>);
		map->Data = (DS_MapSlot<KEY, VALUE>*)map->Allocator->MemAlloc(allocation_size, alignof(DS_MapSlot<KEY, VALUE>));
		memset(map->Data, 0, allocation_size); // set hash values to 0

		for (int i = 0; i < old_capacity; i++)
		{
			DS_MapSlot<KEY, VALUE>* elem = &old_data[i];
			if (elem->Hash != 0)
			{
				VALUE* new_value;
				DS__MapAdd(map, elem->Key, &new_value, elem->Hash);
				*new_value = elem->Value;
			}
		}

#ifndef DS_NO_DEBUG_CHECKS
		memset(old_data, 0xCC, old_capacity * sizeof(DS_MapSlot<KEY, VALUE>));
#endif
		map->Allocator->MemFree(old_data);
	}

	uint32_t mask = (uint32_t)map->Capacity - 1;
	uint32_t index = hash & mask;

	bool added_new;
	for (;;)
	{
		DS_MapSlot<KEY, VALUE>* elem = &map->Data[index];

		if (elem->Hash == 0)
		{
			// Found an empty slot
			elem->Key = key;
			elem->Hash = hash;
			*out_value = &elem->Value;
			map->HasCount += 1;
			added_new = true;
			break;
		}

		if (hash == elem->Hash && DS_IsEqual(key, elem->Key))
		{
			// This key already exists
			*out_value = &elem->Value;
			added_new = false;
			break;
		}

		index = (index + 1) & mask;
	}

	return added_new;
}

template<typename KEY, typename VALUE>
inline void DS_Map<KEY, VALUE>::Set(const KEY& key, const VALUE& value)
{
	uint32_t hash = DS_Hash(key) | (1 << 31);
	VALUE* slot_value;
	DS__MapAdd(this, key, &slot_value, hash);
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
	if (Capacity == 0)
		return NULL;

	uint32_t hash = DS_Hash(key) | (1 << 31);
	uint32_t mask = (uint32_t)Capacity - 1;
	uint32_t index = hash & mask;

	for (;;)
	{
		DS_MapSlot<KEY, VALUE>* elem = &Data[index];
		if (elem->Hash == 0)
			return NULL;

		if (hash == elem->Hash && DS_IsEqual(key, elem->Key))
			return &elem->Value;

		index = (index + 1) & mask;
	}
}

template<typename KEY, typename VALUE>
inline bool DS_Map<KEY, VALUE>::Add(const KEY& key, VALUE** value)
{
	uint32_t hash = DS_Hash(key) | (1 << 31);
	bool added_new = DS__MapAdd(this, key, value, hash);
	return added_new;
}

template<typename KEY, typename VALUE>
inline bool DS_Map<KEY, VALUE>::Remove(const KEY& key)
{
	if (Capacity == 0)
		return false;

	uint32_t hash = DS_Hash(key) | (1 << 31);
	uint32_t mask = (uint32_t)Capacity - 1;
	uint32_t index = hash & mask;

	bool removed;

	for (;;) {
		DS_MapSlot<KEY, VALUE>* elem = &Data[index];

		if (elem->Hash == 0) { // Empty slot, the key does not exist in the map
			removed = false;
			break;
		}

		if (hash == elem->Hash && DS_IsEqual(key, elem->Key)) { // Found the element!
#ifndef DS_NO_DEBUG_CHECKS
			memset(elem, 0xCC, sizeof(*elem));
#endif
			elem->Hash = 0;
			HasCount--;

			// Backwards-shift deletion
			for (;;) {
				index = (index + 1) & mask;

				DS_MapSlot<KEY, VALUE>* moving = &Data[index];
				if (moving->Hash == 0) break;

				DS_MapSlot<KEY, VALUE> temp = *moving;
#ifndef DS_NO_DEBUG_CHECKS
				memset(moving, 0xCC, sizeof(*moving));
#endif
				moving->Hash = 0;
				HasCount--;

				VALUE* readded;
				DS__MapAdd(this, temp.Key, &readded, temp.Hash);
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
inline void DS_Set<KEY>::Init(DS_Allocator* allocator)
{
	Data = NULL;
	HasCount = 0;
	Capacity = 0;
	Allocator = allocator ? allocator : DS_HeapAllocator();
}

template<typename KEY>
inline void DS_Set<KEY>::Deinit()
{
#ifndef DS_NO_DEBUG_CHECKS
	memset(Data, 0xCC, Capacity * sizeof(DS_SetSlot<KEY>));
#endif

	Allocator->MemFree(Data);

#ifndef DS_NO_DEBUG_CHECKS
	memset(this, 0xCC, sizeof(*this));
#endif
}

template<typename KEY>
static bool DS__SetAdd(DS_Set<KEY>* set, const KEY& key, uint32_t hash) {
	DS_ASSERT(set->Allocator != NULL); // Have you called Init?

	if (100 * (set->HasCount + 1) > 70 * set->Capacity)
	{
		// Grow the set

		DS_SetSlot<KEY>* old_data = set->Data;
		int old_capacity = set->Capacity;

		set->Capacity = old_capacity == 0 ? 8 : old_capacity * 2;
		set->HasCount = 0;

		size_t allocation_size = set->Capacity * sizeof(DS_SetSlot<KEY>);
		set->Data = (DS_SetSlot<KEY>*)set->Allocator->MemAlloc(allocation_size, alignof(DS_SetSlot<KEY>));
		memset(set->Data, 0, allocation_size); // set hash values to 0

		for (int i = 0; i < old_capacity; i++)
		{
			DS_SetSlot<KEY>* elem = &old_data[i];
			if (elem->Hash != 0)
				DS__SetAdd(set, elem->Key, elem->Hash);
		}

#ifndef DS_NO_DEBUG_CHECKS
		memset(old_data, 0xCC, old_capacity * sizeof(DS_SetSlot<KEY>));
#endif
		set->Allocator->MemFree(old_data);
	}

	uint32_t mask = (uint32_t)set->Capacity - 1;
	uint32_t index = hash & mask;

	bool added_new;
	for (;;)
	{
		DS_SetSlot<KEY>* elem = &set->Data[index];

		if (elem->Hash == 0)
		{
			// Found an empty slot
			elem->Key = key;
			elem->Hash = hash;
			set->HasCount += 1;
			added_new = true;
			break;
		}

		if (hash == elem->Hash && DS_IsEqual(key, elem->Key))
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
inline bool DS_Set<KEY>::Has(const KEY& key)
{
	uint32_t hash = DS_Hash(key) | (1 << 31);
	uint32_t mask = (uint32_t)Capacity - 1;
	uint32_t index = hash & mask;

	for (;;)
	{
		DS_SetSlot<KEY>* elem = &Data[index];
		if (elem->Hash == 0)
			return false;

		if (hash == elem->Hash && DS_IsEqual(key, elem->Key))
			return true;

		index = (index + 1) & mask;
	}
}

template<typename KEY>
inline bool DS_Set<KEY>::Add(const KEY& key)
{
	uint32_t hash = DS_Hash(key) | (1 << 31);
	bool added_new = DS__SetAdd(this, key, hash);
	return added_new;
}

template<typename KEY>
inline bool DS_Set<KEY>::Remove(const KEY& key)
{
	if (Capacity == 0)
		return false;

	uint32_t hash = DS_Hash(key) | (1 << 31);
	uint32_t mask = (uint32_t)Capacity - 1;
	uint32_t index = hash & mask;

	bool removed;

	for (;;) {
		DS_SetSlot<KEY>* elem = &Data[index];

		if (elem->Hash == 0) { // Empty slot, the key does not exist in the set
			removed = false;
			break;
		}

		if (hash == elem->Hash && DS_IsEqual(key, elem->Key)) { // Found the element!
#ifndef DS_NO_DEBUG_CHECKS
			memset(elem, 0xCC, sizeof(*elem));
#endif
			elem->Hash = 0;
			HasCount--;

			// Backwards-shift deletion
			for (;;) {
				index = (index + 1) & mask;

				DS_SetSlot<KEY>* moving = &Data[index];
				if (moving->Hash == 0) break;

				DS_SetSlot<KEY>temp = *moving;
#ifndef DS_NO_DEBUG_CHECKS
				memset(moving, 0xCC, sizeof(*moving));
#endif
				moving->Hash = 0;
				HasCount--;

				DS__SetAdd(this, temp->key, temp->hash);
			}

			removed = true;
			break;
		}

		index = (index + 1) & mask;
	}

	return removed;
}

#if 0
template<typename T, int N>
inline DS_BucketArray<T, N>::DS_BucketArray(DS_Arena* arena)
{
	allocator = arena;
	buckets = NULL;
	last_bucket_end = N;
	buckets_count = 0;
	buckets_capacity = 0;
	count = 0;
}

template<typename T, int N>
inline void DS_BucketArray<T, N>::Init(DS_Allocator* allocator)
{
	allocator = allocator ? allocator : DS_HeapAllocator();
	buckets = NULL;
	last_bucket_end = N;
	buckets_count = 0;
	buckets_capacity = 0;
	count = 0;
}

template<typename T, int N>
inline void DS_BucketArray<T, N>::Deinit()
{
	for (int i = 0; i < buckets_count; i++)
		allocator->MemFree(buckets[i].elems);
	allocator->MemFree(buckets);
}

void DS_Internal_BucketArrayMoveToNextBucket(void* array, size_t elem_size, size_t N);

template<typename T, int N>
inline T* DS_BucketArray<T, N>::Add(const T& value)
{
	if (last_bucket_end == N)
		DS_Internal_BucketArrayMoveToNextBucket(this, sizeof(T), N);

	int slot_index = last_bucket_end++;
	T* result = &buckets[buckets_count - 1].elems[slot_index];
	count++;

	return result;
}
#endif

#ifndef DS_NO_MALLOC
static void* DS_HeapAllocatorProc(DS_Allocator* allocator, void* ptr, size_t old_size, size_t size, size_t align) {
	if (size == 0) {
		_aligned_free(ptr);
		return NULL;
	} else {
		return _aligned_realloc(ptr, size, align);
	}
}
static DS_Allocator* DS_HeapAllocator() {
	static const DS_Allocator result = { DS_HeapAllocatorProc };
	return (DS_Allocator*)&result;
}
#else
static DS_Allocator* DS_HeapAllocator() { return NULL; }
#endif // DS_NO_MALLOC

//-- DS_MurmurHash3 --------------------------------------------------------------
// Platform-specific functions and macros

// Microsoft Visual Studio

#if defined(_MSC_VER)
#include <stdlib.h>

#define DS_ROTL32(x,y) _rotl(x,y)
#define DS_ROTL64(x,y) _rotl64(x,y)

#define DS_BIG_CONSTANT(x) (x)

// Other compilers

#else // defined(_MSC_VER)

static inline uint32_t DS_rotl32( uint32_t x, int8_t r ) {
	return (x << r) | (x >> (32 - r));
}

static inline uint64_t DS_rotl64( uint64_t x, int8_t r ) {
	return (x << r) | (x >> (64 - r));
}

#define	DS_ROTL32(x,y)	DS_rotl32(x,y)
#define DS_ROTL64(x,y)	DS_rotl64(x,y)

#define DS_BIG_CONSTANT(x) (x##LLU)

#endif // !defined(_MSC_VER)

//-----------------------------------------------------------------------------
// Block read - if your platform needs to do endian-swapping or can only
// handle aligned reads, do the conversion here

#define DS_getblock32(p, i) p[i]
#define DS_getblock64(p, i) p[i]

//-----------------------------------------------------------------------------
// Finalization mix - force all bits of a hash block to avalanche

DS_FORCE_INLINE uint32_t DS_fmix32(uint32_t h)
{
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;
	return h;
}

//----------

DS_FORCE_INLINE uint64_t DS_fmix64(uint64_t k)
{
	k ^= k >> 33;
	k *= DS_BIG_CONSTANT(0xff51afd7ed558ccd);
	k ^= k >> 33;
	k *= DS_BIG_CONSTANT(0xc4ceb9fe1a85ec53);
	k ^= k >> 33;
	return k;
}

//-----------------------------------------------------------------------------