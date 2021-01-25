/*
 * Copyright (c) 2015, 2017, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.  You
 * may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/*
 * gc.h
 *
 */

#ifndef GC_H
#define GC_H

/**
 * DOC: Idio Types and Values
 *
 * IDIO
 * ----
 *
 * ``IDIO`` entities are the (references to) Idio *values* that are
 * passed everywhere.
 *
 * They are typedef'd to be pointers to &struct idio_s.
 *
 * struct idio_s
 * -------------
 *
 * This structure essentially contains a ``type`` indicator, some
 * flags then a union of the individual type structures.
 *
 * Idio Types
 * ----------
 *
 * The construction of types is formulaic, so for some new type
 * ``foo``:
 *
 * In ``gc.h``:
 *
 * .. code-block:: c
 *
 *     #define IDIO_TYPE_FOO	99
 *
 *     struct idio_foo_s {
 *       int i;
 *     };
 *
 *     typedef struct idio_foo_s idio_foo_t;
 *
 *     #define IDIO_FOO_I(S)	((S)->u.foo.i)
 *
 *     struct idio_s {
 *       ...
 *       union idio_s_u {
 *         ...
 *         idio_foo_t          foo;
 *         ...
 *       } u;
 *     };
 *
 * If your value can refer to or "contain" other Idio values then you
 * must have a ``grey`` pointer for the garbage collector to use:
 *
 * .. code-block:: c
 *
 *     #define IDIO_TYPE_BAR	100
 *
 *     struct idio_bar_s {
 *       struct idio_s *grey;
 *       struct idio_s *ref;
 *       int j;
 *     };
 *
 *     typedef struct idio_bar_s idio_bar_t;
 *
 *     #define IDIO_BAR_GREY(S)	((S)->u.bar.grey)
 *     #define IDIO_BAR_REF(S)	((S)->u.bar.ref)
 *     #define IDIO_BAR_J(S)	((S)->u.bar.j)
 *
 * If your value structure is "large" (probably more than three
 * pointers worth but see the commentary in ``struct idio_s`` for
 * specifics) then your entry in the &struct idio_s union should be a
 * pointer and your accessor macros must reflect that:
 *
 * .. code-block:: c
 *
 *     #define IDIO_TYPE_BAZ	101
 *
 *     struct idio_baz_s {
 *       struct idio_s *grey;
 *       struct idio_s *ref;
 *       ...
 *       int k;
 *     };
 *
 *     typedef struct idio_baz_s idio_baz_t;
 *
 *     #define IDIO_BAZ_GREY(S)	((S)->u.baz->grey)
 *     #define IDIO_BAZ_REF(S)	((S)->u.baz->ref)
 *     #define IDIO_BAZ_K(S)	((S)->u.baz->k)
 *
 *     struct idio_s {
 *       ...
 *       union idio_s_u {
 *         ...
 *         idio_baz_t          *baz;
 *         ...
 *       } u;
 *     };
 *
 * In ``foo.[ch]``:
 *
 * .. code-block:: c
 *
 *     IDIO idio_foo (args ...);
 *     int idio_isa_foo (IDIO o);
 *     void idio_free_foo (IDIO f);
 *
 *     void idio_foo_add_primitives ();
 *     void idio_final_foo ();
 *     void idio_init_foo ();
 *
 */

/*
 * Distinct Idio types
 *
 * This is regardless of implementation, eg. multiple constant types
 */
#define IDIO_TYPE_NONE			0
#define IDIO_TYPE_FIXNUM		1
#define IDIO_TYPE_CONSTANT_IDIO		2
#define IDIO_TYPE_CONSTANT_TOKEN	3
#define IDIO_TYPE_CONSTANT_I_CODE	4
#define IDIO_TYPE_CONSTANT_CHARACTER	5 /* deprecated -- see UNICODE */
#define IDIO_TYPE_CONSTANT_UNICODE	6
/*
#define IDIO_TYPE_CONSTANT_6		7
#define IDIO_TYPE_CONSTANT_7		8
#define IDIO_TYPE_CONSTANT_8		9
*/
#define IDIO_TYPE_PLACEHOLDER		10
#define IDIO_TYPE_STRING		11
#define IDIO_TYPE_SUBSTRING		12
#define IDIO_TYPE_SYMBOL		13
#define IDIO_TYPE_KEYWORD		14
#define IDIO_TYPE_PAIR			15
#define IDIO_TYPE_ARRAY			16
#define IDIO_TYPE_HASH			17
#define IDIO_TYPE_CLOSURE		18
#define IDIO_TYPE_PRIMITIVE		19
#define IDIO_TYPE_BIGNUM		20

#define IDIO_TYPE_MODULE		21
#define IDIO_TYPE_FRAME			22
#define IDIO_TYPE_HANDLE		23
#define IDIO_TYPE_STRUCT_TYPE		24
#define IDIO_TYPE_STRUCT_INSTANCE	25
#define IDIO_TYPE_THREAD		26
#define IDIO_TYPE_CONTINUATION		27
#define IDIO_TYPE_BITSET		28

#define IDIO_TYPE_C_INT         	30
#define IDIO_TYPE_C_UINT        	31
#define IDIO_TYPE_C_FLOAT       	32
#define IDIO_TYPE_C_DOUBLE      	33
#define IDIO_TYPE_C_POINTER     	34
#define IDIO_TYPE_C_VOID        	35

#define IDIO_TYPE_C_INT8_T      	36
#define IDIO_TYPE_C_UINT8_T     	37
#define IDIO_TYPE_C_INT16_T     	38
#define IDIO_TYPE_C_UINT16_T    	39
#define IDIO_TYPE_C_INT32_T     	40
#define IDIO_TYPE_C_UINT32_T    	41
#define IDIO_TYPE_C_INT64_T     	42
#define IDIO_TYPE_C_UINT64_T    	43
/*
#define IDIO_TYPE_C_CHAR        	28
#define IDIO_TYPE_C_UCHAR       	29
#define IDIO_TYPE_C_SHORT       	30
#define IDIO_TYPE_C_USHORT      	31
#define IDIO_TYPE_C_INT         	32
#define IDIO_TYPE_C_UINT        	33
#define IDIO_TYPE_C_LONG        	34
#define IDIO_TYPE_C_ULONG       	35
*/
#define IDIO_TYPE_CTD           	50
#define IDIO_TYPE_C_TYPEDEF     	51
#define IDIO_TYPE_C_STRUCT      	52
#define IDIO_TYPE_C_INSTANCE    	53
#define IDIO_TYPE_C_FFI         	54
#define IDIO_TYPE_OPAQUE        	55
#define IDIO_TYPE_MAX           	56

/**
 * typedef idio_type_e - Idio type discriminator
 *
 * It is an ``unsigned char`` as it consumes space in every Idio value
 * -- rather than an ``enum`` (which might be an ``unsigned int`` thus
 * wasting 3 or 7 bytes per value).
 *
 * Example:
 * #define IDIO_TYPE_STRING	4
 */
typedef unsigned char idio_type_e;

typedef int32_t idio_unicode_t;	/* must handle EOF as well! */

/* byte compiler instruction */
typedef uint8_t IDIO_I;
#define IDIO_I_MAX	UINT8_MAX

#define IDIO_GC_FLAG_NONE		0
#define IDIO_GC_FLAG_GCC_SHIFT		0	/* GC colours -- four bits */
#define IDIO_GC_FLAG_FREE_SHIFT		4	/* debug */
#define IDIO_GC_FLAG_STICKY_SHIFT	5	/* memory pinning */
#define IDIO_GC_FLAG_FINALIZER_SHIFT	6

#define IDIO_GC_FLAG_GCC_MASK		(0xf << IDIO_GC_FLAG_GCC_SHIFT)
#define IDIO_GC_FLAG_GCC_UMASK		(~ IDIO_GC_FLAG_GCC_MASK)
#define IDIO_GC_FLAG_GCC_BLACK		(1 << (IDIO_GC_FLAG_GCC_SHIFT+0))
#define IDIO_GC_FLAG_GCC_DGREY		(1 << (IDIO_GC_FLAG_GCC_SHIFT+1))
#define IDIO_GC_FLAG_GCC_LGREY		(1 << (IDIO_GC_FLAG_GCC_SHIFT+2))
#define IDIO_GC_FLAG_GCC_WHITE		(1 << (IDIO_GC_FLAG_GCC_SHIFT+3))

#define IDIO_GC_FLAG_FREE_MASK		(1 << IDIO_GC_FLAG_FREE_SHIFT)
#define IDIO_GC_FLAG_FREE_UMASK		(~ IDIO_GC_FLAG_FREE_MASK)
#define IDIO_GC_FLAG_NOTFREE		(0 << IDIO_GC_FLAG_FREE_SHIFT)
#define IDIO_GC_FLAG_FREE		(1 << IDIO_GC_FLAG_FREE_SHIFT)

#define IDIO_GC_FLAG_STICKY_MASK	(1 << IDIO_GC_FLAG_STICKY_SHIFT)
#define IDIO_GC_FLAG_STICKY_UMASK	(~ IDIO_GC_FLAG_STICKY_MASK)
#define IDIO_GC_FLAG_NOTSTICKY		(0 << IDIO_GC_FLAG_STICKY_SHIFT)
#define IDIO_GC_FLAG_STICKY		(1 << IDIO_GC_FLAG_STICKY_SHIFT)

#define IDIO_GC_FLAG_FINALIZER_MASK	(1 << IDIO_GC_FLAG_FINALIZER_SHIFT)
#define IDIO_GC_FLAG_FINALIZER_UMASK	(~ IDIO_GC_FLAG_FINALIZER_MASK)
#define IDIO_GC_FLAG_NOFINALIZER	(0 << IDIO_GC_FLAG_FINALIZER_SHIFT)
#define IDIO_GC_FLAG_FINALIZER		(1 << IDIO_GC_FLAG_FINALIZER_SHIFT)

/**
 * struct idio_string_s - Idio ``string`` structure
 */
#define IDIO_STRING_FLAG_NONE		0
/*
 * Coincidentally, these are the number of bytes required for storage
 * per code-point.  Do not rely on this, these are just flags!
 */
#define IDIO_STRING_FLAG_1BYTE		(1<<0)
#define IDIO_STRING_FLAG_2BYTE		(1<<1)
#define IDIO_STRING_FLAG_4BYTE		(1<<2)

struct idio_string_s {
    /**
     * @blen: length in bytes
     *
     * The string is not expected to be NUL-terminated.
     */
    size_t blen;		/* bytes */
    size_t len;			/* code points */
    /**
     * @s: the "string": BYTE, UCS2, UCS4
     */
    char *s;
};

/**
 * typedef idio_string_t - Idio ``string`` type
 */
typedef struct idio_string_s idio_string_t;

/**
 * IDIO_STRING_BLEN - accessor to @blen in &struct idio_string_s
 * @S: the &struct idio_string_s
 */
#define IDIO_STRING_BLEN(S)	((S)->u.string.blen)
/**
 * IDIO_STRING_LEN - accessor to @len in &struct idio_string_s
 * @S: the &struct idio_string_s
 */
#define IDIO_STRING_LEN(S)	((S)->u.string.len)
/**
 * IDIO_STRING_S - accessor to @s in &struct idio_string_s
 * @S: the &struct idio_string_s
 */
#define IDIO_STRING_S(S)	((S)->u.string.s)
#define IDIO_STRING_FLAGS(S)	((S)->tflags)

typedef struct idio_substring_s {
    /*
     * By rights, we should have a *grey here but we know we point to
     * a simple string and can just mark it as seen directly
     */
    struct idio_s *parent;
    size_t len;			/* code points */
    char *s;			/* no allocation, just a pointer into
				   parent's string */
} idio_substring_t;

#define IDIO_SUBSTRING_LEN(S)	((S)->u.substring.len)
#define IDIO_SUBSTRING_S(S)	((S)->u.substring.s)
#define IDIO_SUBSTRING_PARENT(S) ((S)->u.substring.parent)

#define IDIO_SYMBOL_FLAG_NONE		0
#define IDIO_SYMBOL_FLAG_GENSYM		(1<<0)

typedef struct idio_symbol_s {
    char *s;			/* C string */
} idio_symbol_t;

#define IDIO_SYMBOL_S(S)	((S)->u.symbol.s)
#define IDIO_SYMBOL_FLAGS(S)	((S)->tflags)

typedef struct idio_keyword_s {
    char *s;			/* C string */
} idio_keyword_t;

#define IDIO_KEYWORD_S(S)	((S)->u.keyword.s)

typedef struct idio_pair_s {
    struct idio_s *grey;
    struct idio_s *h;
    struct idio_s *t;
} idio_pair_t;

#define IDIO_PAIR_GREY(P)	((P)->u.pair.grey)
#define IDIO_PAIR_H(P)		((P)->u.pair.h)
#define IDIO_PAIR_T(P)		((P)->u.pair.t)

#define IDIO_PAIR_HH(P)		IDIO_PAIR_H (IDIO_PAIR_H (P))
#define IDIO_PAIR_HT(P)		IDIO_PAIR_H (IDIO_PAIR_T (P))
#define IDIO_PAIR_TH(P)		IDIO_PAIR_T (IDIO_PAIR_H (P))
#define IDIO_PAIR_TT(P)		IDIO_PAIR_T (IDIO_PAIR_T (P))

#define IDIO_PAIR_HHH(P)	IDIO_PAIR_H (IDIO_PAIR_HH (P))
#define IDIO_PAIR_THH(P)	IDIO_PAIR_T (IDIO_PAIR_HH (P))
#define IDIO_PAIR_HHT(P)	IDIO_PAIR_H (IDIO_PAIR_HT (P))
#define IDIO_PAIR_THT(P)	IDIO_PAIR_T (IDIO_PAIR_HT (P))

#define IDIO_PAIR_HTH(P)	IDIO_PAIR_H (IDIO_PAIR_TH (P))
#define IDIO_PAIR_TTH(P)	IDIO_PAIR_T (IDIO_PAIR_TH (P))
#define IDIO_PAIR_HTT(P)	IDIO_PAIR_H (IDIO_PAIR_TT (P))
#define IDIO_PAIR_TTT(P)	IDIO_PAIR_T (IDIO_PAIR_TT (P))

#define IDIO_PAIR_TTH(P)	IDIO_PAIR_T (IDIO_PAIR_TH (P))
#define IDIO_PAIR_HTTH(P)	IDIO_PAIR_H (IDIO_PAIR_TTH (P))

#define IDIO_PAIR_TTT(P)	IDIO_PAIR_T (IDIO_PAIR_TT (P))
#define IDIO_PAIR_HTTT(P)	IDIO_PAIR_H (IDIO_PAIR_TTT (P))

/*
 * This character is used by util.c and read.c
 */
#define IDIO_PAIR_SEPARATOR	'&'

/*
 * Array Indices
 *
 * On normal machines size_t is probably the same as intptr_t (an
 * integral type that can contain a pointer).  On segmented
 * architectures, SIZE_MAX might be 65535, the index of the largest
 * addressable element of an array on this architecture if it has
 * 16bit segments.
 *
 * Following that, ptrdiff_t is the difference between the indices of
 * two elements *in the same array* (and is not well defined
 * otherwise).  Technically, ptrdiff_t requires one more bit than
 * size_t (as it can be negative) but otherwise is the same (broad)
 * size and unrelated to pointers.
 *
 * Hence, if you are using array indexing, "a[i]", then you should be
 * using size_t/ptrdiff_t to be portable.  We are using array
 * indexing, usually via "IDIO_ARRAY_AE (a, i)" although the odd a[i]
 * creeps in.
 *
 * So, let's revisit the original comment:
 *
 * C99 suggests that sizes should be size_t so we could create an
 * array with SIZE_MAX elements.  On non-segmented architectures, such
 * a memory allocation will almost certainly fail(1) but, sticking to
 * principles, someone might want to create a just-over-half-of-memory
 * (2**(n-1))+1 element array.
 *
 * (1) as every Idio array element is a pointer, ie 4 or 8 bytes, then
 * we can't physically allocate nor address 2**32 * 4 bytes or 2**64 *
 * 8 bytes just for the array as those are 4 and 8 times larger than
 * addressable memory.  So, in practice, we're limited to arrays of
 * length 2**30 or 2**61 -- with no room for any other data!
 *
 *   As a real-world example, on an OpenSolaris 4GB/32bit machine:
 *
 *     make-array ((expt 2 29) - 1)
 *
 *   was successful.  2**30-1 was not.
 *
 * However, we accomodate negative array indices, eg. the nominal,
 * array[-i], which we take to mean the i'th last index.  The means
 * using a signed type even if we won't ever actually use a[-i] -- as
 * we'll convert it into a[size-i].
 *
 * So, the type we use must be ptrdiff_t and therefore the largest
 * positive index is PTRDIFF_MAX.
 *
 */

/**
 * typedef idio_ai_t - Idio ``array`` index
 */
typedef ptrdiff_t idio_ai_t;

#define IDIO_ARRAY_FLAG_NONE		0

/**
 * struct idio_array_s - Idio ``array`` structure
 */
struct idio_array_s {
    /**
     * @grey: grey list pointer (garbage collector)
     */
    struct idio_s *grey;
    /**
     * @asize: allocated size
     */
    idio_ai_t asize;
    /**
     * @usize: used size/user-visible size
     *
     * In practice it is the index of the lowest unused index.  An
     * empty array will have @usize of 0 -- there are zero elements
     * used in the array.
     *
     * Push an element on (therefore at index 0 itself) will have
     * @usize be 1 -- there is one element in the array.
     */
    idio_ai_t usize;
    /**
     * @dv: default value
     */
    struct idio_s *dv;
    /**
     * @ae: array elements
     */
    struct idio_s* *ae;
};

/**
 * typedef idio_array_t - Idio ``array`` type
 */
typedef struct idio_array_s idio_array_t;

#define IDIO_ARRAY_GREY(A)	((A)->u.array->grey)
#define IDIO_ARRAY_ASIZE(A)	((A)->u.array->asize)
#define IDIO_ARRAY_USIZE(A)	((A)->u.array->usize)
#define IDIO_ARRAY_DV(A)	((A)->u.array->dv)
#define IDIO_ARRAY_AE(A,i)	((A)->u.array->ae[i])
#define IDIO_ARRAY_FLAGS(A)	((A)->tflags)

/*
 * idio_hi_t should be be an unsigned type, hence size_t.
 *
 *   As a real-world example, on an OpenSolaris 4GB/32bit machine:
 *
 *     make-hash #n #n ((expt 2 27) - 1)
 *
 *   was successful.  2**28-1 was not.
 *
 *   XXX This is two powers less than make-array (see array.h).  Why?
 *
 */
typedef size_t idio_hi_t;

typedef struct idio_hash_entry_s {
    struct idio_hash_entry_s *next;
    struct idio_s *key;
    struct idio_s *value;
} idio_hash_entry_t;

#define IDIO_HASH_HE_NEXT(HE)	((HE)->next)
#define IDIO_HASH_HE_KEY(HE)	((HE)->key)
#define IDIO_HASH_HE_VALUE(HE)	((HE)->value)

#define IDIO_HASH_FLAG_NONE		0
#define IDIO_HASH_FLAG_STRING_KEYS	(1<<0)

typedef struct idio_hash_s {
    struct idio_s *grey;
    idio_hi_t size;
    idio_hi_t mask;	      /* bitmask for easy modulo arithmetic */
    idio_hi_t count;	      /* (key) count */
    int (*comp_C) (void *k1, void *k2);	/* C equivalence function */
    idio_hi_t (*hash_C) (struct idio_s *h, void *k); /* C hashing function */
    struct idio_s *comp;	/* user-supplied comparator */
    struct idio_s *hash;	/* user-supplied hashing function */
    idio_hash_entry_t* *ha;	/* a C array */
} idio_hash_t;

#define IDIO_HASH_GREY(H)	((H)->u.hash->grey)
#define IDIO_HASH_SIZE(H)	((H)->u.hash->size)
#define IDIO_HASH_MASK(H)	((H)->u.hash->mask)
#define IDIO_HASH_COUNT(H)	((H)->u.hash->count)
#define IDIO_HASH_COMP_C(H)	((H)->u.hash->comp_C)
#define IDIO_HASH_HASH_C(H)	((H)->u.hash->hash_C)
#define IDIO_HASH_COMP(H)	((H)->u.hash->comp)
#define IDIO_HASH_HASH(H)	((H)->u.hash->hash)
#define IDIO_HASH_HA(H,i)	((H)->u.hash->ha[i])
#define IDIO_HASH_FLAGS(H)	((H)->tflags)

/*
 * If we wanted to store the arity and varargs boolean of a closure
 * (for a possible thunk? predicate) without increasing the size of
 * idio_closure_s (and thus spoiling idio_s' union) then we should be
 * able to sneak in an extra char in idio_s (after tflags) for the
 * arity -- with a special value of 255 for 255+ arguments (how many
 * of those might we see?) and use a bit in tflags for varargs.
 */
typedef struct idio_closure_s {
    struct idio_s *grey;
    size_t code_pc;
    size_t code_len;
    struct idio_s *frame;
    struct idio_s *env;
#ifdef IDIO_VM_PROF
    uint64_t called;
    struct timespec call_time;
    struct timeval ru_utime;
    struct timeval ru_stime;
#endif
} idio_closure_t;

#define IDIO_CLOSURE_GREY(C)       ((C)->u.closure->grey)
#define IDIO_CLOSURE_CODE_PC(C)    ((C)->u.closure->code_pc)
#define IDIO_CLOSURE_CODE_LEN(C)   ((C)->u.closure->code_len)
#define IDIO_CLOSURE_FRAME(C)      ((C)->u.closure->frame)
#define IDIO_CLOSURE_ENV(C)        ((C)->u.closure->env)
#ifdef IDIO_VM_PROF
#define IDIO_CLOSURE_CALLED(C)     ((C)->u.closure->called)
#define IDIO_CLOSURE_CALL_TIME(C)  ((C)->u.closure->call_time)
#define IDIO_CLOSURE_RU_UTIME(C)   ((C)->u.closure->ru_utime)
#define IDIO_CLOSURE_RU_STIME(C)   ((C)->u.closure->ru_stime)
#endif

/*
 * idio_prinitimve_desc_t is for the static allocation in C of the
 * description of a primitive value.
 *
 * Yes, it contains much of an idio_primitive_t but it is never *used*
 * as an idio_primitive_t, the data is always copied in
 * idio_primitive_data()
 */
typedef struct idio_primitive_desc_s {
    struct idio_s *(*f) ();	/* don't declare args */
    char *name;
    uint8_t arity;
    char varargs;
    char *sigstr;
    char *docstr;
} idio_primitive_desc_t;

/*
 * Having varargs (a boolean) using a slot in this data structure is a
 * waste as it could be a flag (in tflags).  However, initialisation
 * then becomes more opaque.  There's only a few (hundred) of them and
 * we're currently are OK size-wise for a native object in the idio_s
 * union.  So we'll leave as is.
 */
typedef struct idio_primitive_s {
    struct idio_s *grey;
    struct idio_s *(*f) ();	/* don't declare args */
    char *name;
    uint8_t arity;
    char varargs;
#ifdef IDIO_VM_PROF
    uint64_t called;
    struct timespec call_time;
    struct timeval ru_utime;
    struct timeval ru_stime;
#endif
} idio_primitive_t;

#define IDIO_PRIMITIVE_GREY(P)       ((P)->u.primitive->grey)
#define IDIO_PRIMITIVE_F(P)          ((P)->u.primitive->f)
#define IDIO_PRIMITIVE_NAME(P)       ((P)->u.primitive->name)
#define IDIO_PRIMITIVE_ARITY(P)      ((P)->u.primitive->arity)
#define IDIO_PRIMITIVE_VARARGS(P)    ((P)->u.primitive->varargs)
#ifdef IDIO_VM_PROF
#define IDIO_PRIMITIVE_CALLED(P)     ((P)->u.primitive->called)
#define IDIO_PRIMITIVE_CALL_TIME(P)  ((P)->u.primitive->call_time)
#define IDIO_PRIMITIVE_RU_UTIME(P)   ((P)->u.primitive->ru_utime)
#define IDIO_PRIMITIVE_RU_STIME(P)   ((P)->u.primitive->ru_stime)
#endif

typedef struct idio_module_s {
    struct idio_s *grey;
    struct idio_s *name;
    struct idio_s *exports;	/* symbols */
    struct idio_s *imports;	/* modules */
    struct idio_s *symbols;	/* hash table */
    struct idio_s *vci;		/* hash table: VM constant index mapping: module-specific -> global */
    struct idio_s *vvi;		/* hash table: VM value index mapping: module-specific -> global */
} idio_module_t;

#define IDIO_MODULE_GREY(F)	((F)->u.module->grey)
#define IDIO_MODULE_NAME(F)	((F)->u.module->name)
#define IDIO_MODULE_EXPORTS(F)	((F)->u.module->exports)
#define IDIO_MODULE_IMPORTS(F)	((F)->u.module->imports)
#define IDIO_MODULE_SYMBOLS(F)	((F)->u.module->symbols)
#define IDIO_MODULE_VCI(F)	((F)->u.module->vci)
#define IDIO_MODULE_VVI(F)	((F)->u.module->vvi)

#define IDIO_FRAME_FLAG_NONE	 0
#define IDIO_FRAME_FLAG_ETRACE   (1<<0)
#define IDIO_FRAME_FLAG_KTRACE   (1<<1)
#define IDIO_FRAME_FLAG_PROFILE  (1<<2)
#define IDIO_FRAME_FLAG_C_STRUCT (1<<3)

#if PTRDIFF_MAX == 2147483647L
typedef uint16_t idio_frame_args_t;
#else
typedef uint32_t idio_frame_args_t;
#endif

typedef struct idio_frame_s {
    struct idio_s *grey;
    struct idio_s *next;
    idio_frame_args_t nparams;	/* number in use */
    idio_frame_args_t nalloc;	/* number allocated */
    struct idio_s *names;	/* a ref into vm_constants */
    struct idio_s* *args;
} idio_frame_t;

#define IDIO_FRAME_GREY(F)	((F)->u.frame->grey)
#define IDIO_FRAME_NEXT(F)	((F)->u.frame->next)
#define IDIO_FRAME_NPARAMS(F)	((F)->u.frame->nparams)
#define IDIO_FRAME_NALLOC(F)	((F)->u.frame->nalloc)
#define IDIO_FRAME_NAMES(F)	((F)->u.frame->names)
#define IDIO_FRAME_ARGS(F,i)	((F)->u.frame->args[i])
#define IDIO_FRAME_FLAGS(F)	((F)->tflags)

#define IDIO_BIGNUM_FLAG_NONE          (0)
#define IDIO_BIGNUM_FLAG_INTEGER       (1<<0)
#define IDIO_BIGNUM_FLAG_REAL          (1<<1)
#define IDIO_BIGNUM_FLAG_REAL_NEGATIVE (1<<2)
#define IDIO_BIGNUM_FLAG_REAL_INEXACT  (1<<3)
#define IDIO_BIGNUM_FLAG_NAN           (1<<4)

/*
 * Bignum Significand Array
 *
 * The array of words where each word holds IDIO_BIGNUM_DPW
 * significant digits.
 */

typedef intptr_t IDIO_BS_T;

typedef struct idio_bsa_s {
    size_t refs;
    size_t avail;
    size_t size;
    IDIO_BS_T *ae;
} idio_bsa_t;

typedef idio_bsa_t* IDIO_BSA;

#define IDIO_BSA_AVAIL(BSA)	((BSA)->avail)
#define IDIO_BSA_SIZE(BSA)	((BSA)->size)
#define IDIO_BSA_AE(BSA,i)	((BSA)->ae[i])

typedef struct idio_bignum_s {
    IDIO_BS_T exp;
    IDIO_BSA sig;
} idio_bignum_t;

#define IDIO_BIGNUM_FLAGS(B) ((B)->tflags)
#define IDIO_BIGNUM_EXP(B)   ((B)->u.bignum.exp)
#define IDIO_BIGNUM_SIG(B)   ((B)->u.bignum.sig)
#define IDIO_BIGNUM_SIG_AE(B,i)   IDIO_BSA_AE((B)->u.bignum.sig,i)

typedef struct idio_handle_methods_s {
    void (*free) (struct idio_s *h);
    int (*readyp) (struct idio_s *h);
    int (*getb) (struct idio_s *h);
    int (*eofp) (struct idio_s *h);
    int (*close) (struct idio_s *h);
    int (*putb) (struct idio_s *h, uint8_t c);
    int (*putc) (struct idio_s *h, idio_unicode_t c);
    ptrdiff_t (*puts) (struct idio_s *h, char *s, size_t slen);
    int (*flush) (struct idio_s *h);
    off_t (*seek) (struct idio_s *h, off_t offset, int whence);
    void (*print) (struct idio_s *h, struct idio_s *o);
} idio_handle_methods_t;

#define IDIO_HANDLE_FLAG_NONE		0
#define IDIO_HANDLE_FLAG_READ		(1<<0)
#define IDIO_HANDLE_FLAG_WRITE		(1<<1)
#define IDIO_HANDLE_FLAG_CLOSED		(1<<2)
#define IDIO_HANDLE_FLAG_FILE		(1<<3)
#define IDIO_HANDLE_FLAG_STRING		(1<<4)

typedef struct idio_handle_s {
    struct idio_s *grey;
    void *stream;		/* file/string specific stream data */
    idio_handle_methods_t *methods; /* file/string specific methods */
    int lc;			/* lookahead char */
    off_t line;			/* 1+ */
    off_t pos;			/* position in file: 0+ */
    struct idio_s *filename;	/* filename the user used */
    struct idio_s *pathname;	/* pathname or some other identifying data */
} idio_handle_t;

#define IDIO_HANDLE_GREY(H)	((H)->u.handle->grey)
#define IDIO_HANDLE_STREAM(H)	((H)->u.handle->stream)
#define IDIO_HANDLE_METHODS(H)	((H)->u.handle->methods)
#define IDIO_HANDLE_LC(H)	((H)->u.handle->lc)
#define IDIO_HANDLE_LINE(H)	((H)->u.handle->line)
#define IDIO_HANDLE_POS(H)	((H)->u.handle->pos)
#define IDIO_HANDLE_FILENAME(H)	((H)->u.handle->filename)
#define IDIO_HANDLE_PATHNAME(H)	((H)->u.handle->pathname)
#define IDIO_HANDLE_FLAGS(H)	((H)->tflags)

#define IDIO_INPUTP_HANDLE(H)	(IDIO_HANDLE_FLAGS(H) & IDIO_HANDLE_FLAG_READ)
#define IDIO_OUTPUTP_HANDLE(H)	(IDIO_HANDLE_FLAGS(H) & IDIO_HANDLE_FLAG_WRITE)
#define IDIO_CLOSEDP_HANDLE(H)	(IDIO_HANDLE_FLAGS(H) & IDIO_HANDLE_FLAG_CLOSED)
#define IDIO_FILEP_HANDLE(H)	(IDIO_HANDLE_FLAGS(H) & IDIO_HANDLE_FLAG_FILE)
#define IDIO_STRINGP_HANDLE(H)	(IDIO_HANDLE_FLAGS(H) & IDIO_HANDLE_FLAG_STRING)

#define IDIO_HANDLE_M_FREE(H)	(IDIO_HANDLE_METHODS (H)->free)
#define IDIO_HANDLE_M_READYP(H)	(IDIO_HANDLE_METHODS (H)->readyp)
#define IDIO_HANDLE_M_GETB(H)	(IDIO_HANDLE_METHODS (H)->getb)
#define IDIO_HANDLE_M_EOFP(H)	(IDIO_HANDLE_METHODS (H)->eofp)
#define IDIO_HANDLE_M_CLOSE(H)	(IDIO_HANDLE_METHODS (H)->close)
#define IDIO_HANDLE_M_PUTB(H)	(IDIO_HANDLE_METHODS (H)->putb)
#define IDIO_HANDLE_M_PUTC(H)	(IDIO_HANDLE_METHODS (H)->putc)
#define IDIO_HANDLE_M_PUTS(H)	(IDIO_HANDLE_METHODS (H)->puts)
#define IDIO_HANDLE_M_FLUSH(H)	(IDIO_HANDLE_METHODS (H)->flush)
#define IDIO_HANDLE_M_SEEK(H)	(IDIO_HANDLE_METHODS (H)->seek)
#define IDIO_HANDLE_M_PRINT(H)	(IDIO_HANDLE_METHODS (H)->print)

typedef struct idio_struct_type_s {
    struct idio_s *grey;
    struct idio_s *name;	/* a symbol */
    struct idio_s *parent;	/* a struct-type */
    size_t size;		/* number of fields *including parents* */
    struct idio_s* *fields;	/* an array of strings */
} idio_struct_type_t;

#define IDIO_STRUCT_TYPE_GREY(ST)	((ST)->u.struct_type->grey)
#define IDIO_STRUCT_TYPE_NAME(ST)	((ST)->u.struct_type->name)
#define IDIO_STRUCT_TYPE_PARENT(ST)	((ST)->u.struct_type->parent)
#define IDIO_STRUCT_TYPE_SIZE(ST)	((ST)->u.struct_type->size)
#define IDIO_STRUCT_TYPE_FIELDS(ST,i)	((ST)->u.struct_type->fields[i])

typedef struct idio_struct_instance_s {
    struct idio_s *grey;
    struct idio_s *type;	/* a struct-type */
    struct idio_s* *fields;	/* an array */
} idio_struct_instance_t;

#define IDIO_STRUCT_INSTANCE_GREY(SI)		((SI)->u.struct_instance.grey)
#define IDIO_STRUCT_INSTANCE_TYPE(SI)		((SI)->u.struct_instance.type)
#define IDIO_STRUCT_INSTANCE_FIELDS(SI,i)	((SI)->u.struct_instance.fields[i])

#define IDIO_STRUCT_INSTANCE_SIZE(SI)		(IDIO_STRUCT_TYPE_SIZE(IDIO_STRUCT_INSTANCE_TYPE(SI)))

typedef struct idio_thread_s {
    struct idio_s *grey;
    idio_ai_t pc;
    struct idio_s *stack;
    struct idio_s *val;

    /*
     * frame is linked arrays of local variable values for the
     * currently executing closure
     */
    struct idio_s *frame;

    /*
     * env is the operating module for the currently executing closure
     */
    struct idio_s *env;

    /*
     * jmp_buf is used to clear the C-stack
     *
     * NB it is a pointer to a C stack variable
     */
    sigjmp_buf *jmp_buf;

#ifdef IDIO_VM_DYNAMIC_REGISTERS
    /*
     * trap_sp is the SP of the current trap with SP-2 containing the
     * SP of the next trap
     */
    struct idio_s *trap_sp;

    /*
     * dynamic_sp, environ_sp are the SP of the topmost
     * dynamic/environ variable
     */
    struct idio_s *dynamic_sp;
    struct idio_s *environ_sp;
#endif

    /*
     * func, reg1 and reg1 are transient registers, ie. they don't
     * require preserving/restoring
     */
    struct idio_s *func;
    struct idio_s *reg1;
    struct idio_s *reg2;

    /*
     * expr is the original source expression from which we might
     * derive some lexical information (file and line number)
     */
    struct idio_s *expr;

    struct idio_s *input_handle;
    struct idio_s *output_handle;
    struct idio_s *error_handle;

    /*
     * module is the current module -- distinct from env, above
     */
    struct idio_s *module;
} idio_thread_t;

#define IDIO_THREAD_GREY(T)           ((T)->u.thread->grey)
#define IDIO_THREAD_PC(T)             ((T)->u.thread->pc)
#define IDIO_THREAD_STACK(T)          ((T)->u.thread->stack)
#define IDIO_THREAD_VAL(T)            ((T)->u.thread->val)
#define IDIO_THREAD_FRAME(T)          ((T)->u.thread->frame)
#define IDIO_THREAD_ENV(T)            ((T)->u.thread->env)
#define IDIO_THREAD_JMP_BUF(T)        ((T)->u.thread->jmp_buf)
#ifdef IDIO_VM_DYNAMIC_REGISTERS
#define IDIO_THREAD_TRAP_SP(T)        ((T)->u.thread->trap_sp)
#define IDIO_THREAD_DYNAMIC_SP(T)     ((T)->u.thread->dynamic_sp)
#define IDIO_THREAD_ENVIRON_SP(T)     ((T)->u.thread->environ_sp)
#endif
#define IDIO_THREAD_FUNC(T)           ((T)->u.thread->func)
#define IDIO_THREAD_REG1(T)           ((T)->u.thread->reg1)
#define IDIO_THREAD_REG2(T)           ((T)->u.thread->reg2)
#define IDIO_THREAD_EXPR(T)           ((T)->u.thread->expr)
#define IDIO_THREAD_INPUT_HANDLE(T)   ((T)->u.thread->input_handle)
#define IDIO_THREAD_OUTPUT_HANDLE(T)  ((T)->u.thread->output_handle)
#define IDIO_THREAD_ERROR_HANDLE(T)   ((T)->u.thread->error_handle)
#define IDIO_THREAD_MODULE(T)	      ((T)->u.thread->module)
#define IDIO_THREAD_FLAGS(T)          ((T)->tflags)

/*
 * A continuation needs to save everything important about the state
 * of the current thread.  So all the SPs, the current frame and the
 * stack itself.
 *
 * Also, the current jmp_buf.  In a single span of code the jmp_buf is
 * unchanged, however when we "load" a file we get a new, nested
 * jmp_buf. If a continuation from the "outer" idio_vm_run() is
 * invoked then we must use its contextually correct jmp_buf.
 *
 * We'll be duplicating the efforts of idio_vm_preserve_state() but we
 * can't call that as it modifies the stack.  That said, we'll be
 * copying the stack so once we've done that we can push everything
 * else idio_vm_restore_state() needs onto that copy of the stack.
 */
typedef struct idio_continuation_s {
    struct idio_s *grey;
    sigjmp_buf *jmp_buf;
    struct idio_s *stack;
} idio_continuation_t;

#define IDIO_CONTINUATION_GREY(T)	((T)->u.continuation->grey)
#define IDIO_CONTINUATION_JMP_BUF(T)	((T)->u.continuation->jmp_buf)
#define IDIO_CONTINUATION_STACK(T)	((T)->u.continuation->stack)

typedef	uint32_t idio_bitset_word_t;
#define IDIO_BITSET_WORD_MAX 0xffffffffUL

typedef struct idio_bitset_s {
    size_t size;
    idio_bitset_word_t *words;
} idio_bitset_t;

#define IDIO_BITSET_SIZE(BS)	((BS)->u.bitset.size)
#define IDIO_BITSET_WORDS(BS,i)	((BS)->u.bitset.words[i])

/*
 * Who called siglongjmp?
 */
#define IDIO_VM_SIGLONGJMP_CONDITION	1
#define IDIO_VM_SIGLONGJMP_CONTINUATION	2
#define IDIO_VM_SIGLONGJMP_CALLCC	3
#define IDIO_VM_SIGLONGJMP_SIGNAL	4
#define IDIO_VM_SIGLONGJMP_EVENT	5
#define IDIO_VM_SIGLONGJMP_EXIT		6

typedef struct idio_C_pointer_s {
    void *p;
    char *(*printer) (struct idio_s *cp);
    char freep;
} idio_C_pointer_t;

typedef struct idio_C_type_s {
    union {
	intmax_t          C_int;
	uintmax_t         C_uint;
/*
	int8_t            C_int8;
	uint8_t           C_uint8;
	int16_t           C_int16;
	uint16_t          C_uint16;
	int32_t           C_int32;
	uint32_t          C_uint32;
	int64_t           C_int64;
	uint64_t          C_uint64;
	char              C_char;
	unsigned char     C_uchar;
	short             C_short;
	unsigned short    C_ushort;
	int               C_int;
	unsigned int      C_uint;
	long              C_long;
	unsigned long     C_ulong;
*/
	float             C_float;
	double            C_double;
	idio_C_pointer_t *C_pointer;
    } u;
} idio_C_type_t;

#define IDIO_C_TYPE_INT(C)           ((C)->u.C_type.u.C_int)
#define IDIO_C_TYPE_UINT(C)          ((C)->u.C_type.u.C_uint)
/*
#define IDIO_C_TYPE_INT8(C)          ((C)->u.C_type->u.C_int8)
#define IDIO_C_TYPE_UINT8(C)         ((C)->u.C_type->u.C_uint8)
#define IDIO_C_TYPE_INT16(C)         ((C)->u.C_type->u.C_int16)
#define IDIO_C_TYPE_UINT16(C)        ((C)->u.C_type->u.C_uint16)
#define IDIO_C_TYPE_INT32(C)         ((C)->u.C_type->u.C_int32)
#define IDIO_C_TYPE_UINT32(C)        ((C)->u.C_type->u.C_uint32)
#define IDIO_C_TYPE_INT64(C)         ((C)->u.C_type->u.C_int64)
#define IDIO_C_TYPE_UINT64(C)        ((C)->u.C_type->u.C_uint64)
#define IDIO_C_TYPE_CHAR(C)          ((C)->u.C_type->u.C_char)
#define IDIO_C_TYPE_UCHAR(C)         ((C)->u.C_type->u.C_uchar)
#define IDIO_C_TYPE_SHORT(C)         ((C)->u.C_type->u.C_short)
#define IDIO_C_TYPE_USHORT(C)        ((C)->u.C_type->u.C_ushort)
#define IDIO_C_TYPE_INT(C)           ((C)->u.C_type->u.C_int)
#define IDIO_C_TYPE_UINT(C)          ((C)->u.C_type->u.C_uint)
#define IDIO_C_TYPE_LONG(C)          ((C)->u.C_type->u.C_long)
#define IDIO_C_TYPE_ULONG(C)         ((C)->u.C_type->u.C_ulong)
*/
#define IDIO_C_TYPE_FLOAT(C)         ((C)->u.C_type.u.C_float)
#define IDIO_C_TYPE_DOUBLE(C)        ((C)->u.C_type.u.C_double)
/*
#define IDIO_C_TYPE_POINTER(C)       ((C)->u.C_type->u.C_pointer)
*/
#define IDIO_C_TYPE_POINTER_P(C)     ((C)->u.C_type.u.C_pointer->p)
#define IDIO_C_TYPE_POINTER_PRINTER(C)     ((C)->u.C_type.u.C_pointer->printer)
#define IDIO_C_TYPE_POINTER_FREEP(C) ((C)->u.C_type.u.C_pointer->freep)

typedef struct idio_C_typedef_s {
    struct idio_s *grey;
    struct idio_s *sym;		/* a symbol */
} idio_C_typedef_t;

#define IDIO_C_TYPEDEF_GREY(C) ((C)->u.C_typedef->grey)
#define IDIO_C_TYPEDEF_SYM(C)  ((C)->u.C_typedef->sym)

typedef struct idio_C_struct_s {
    struct idio_s *grey;
    struct idio_s *fields;
    struct idio_s *methods;
    struct idio_s *frame;
    size_t size;
} idio_C_struct_t;

#define IDIO_C_STRUCT_GREY(C)    ((C)->u.C_struct->grey)
#define IDIO_C_STRUCT_FIELDS(C)   ((C)->u.C_struct->fields)
#define IDIO_C_STRUCT_METHODS(C) ((C)->u.C_struct->methods)
#define IDIO_C_STRUCT_FRAME(C)   ((C)->u.C_struct->frame)
#define IDIO_C_STRUCT_SIZE(C)    ((C)->u.C_struct->size)

typedef struct idio_C_instance_s {
    struct idio_s *grey;
    void *p;
    struct idio_s *C_struct;
    struct idio_s *frame;
} idio_C_instance_t;

#define IDIO_C_INSTANCE_GREY(C)     ((C)->u.C_instance->grey)
#define IDIO_C_INSTANCE_P(C)        ((C)->u.C_instance->p)
#define IDIO_C_INSTANCE_C_STRUCT(C) ((C)->u.C_instance->C_struct)
#define IDIO_C_INSTANCE_FRAME(C)    ((C)->u.C_instance->frame)

typedef struct idio_C_FFI_s {
    struct idio_s *grey;
    struct idio_s *symbol;
    struct idio_s *result;
    struct idio_s *args;
    struct idio_s *name;
    ffi_cif *ffi_cifp;
    ffi_type *ffi_rtype;
    ffi_type **ffi_arg_types;
    size_t nargs;
} idio_C_FFI_t;

#define IDIO_C_FFI_GREY(C)      ((C)->u.C_FFI->grey)
#define IDIO_C_FFI_SYMBOL(C)    ((C)->u.C_FFI->symbol)
#define IDIO_C_FFI_RESULT(C)    ((C)->u.C_FFI->result)
#define IDIO_C_FFI_ARGS(C)      ((C)->u.C_FFI->args)
#define IDIO_C_FFI_NAME(C)      ((C)->u.C_FFI->name)
#define IDIO_C_FFI_CIFP(C)      ((C)->u.C_FFI->ffi_cifp)
#define IDIO_C_FFI_RTYPE(C)     ((C)->u.C_FFI->ffi_rtype)
#define IDIO_C_FFI_ARG_TYPES(C) ((C)->u.C_FFI->ffi_arg_types)
#define IDIO_C_FFI_NARGS(C)     ((C)->u.C_FFI->nargs)

typedef struct idio_opaque_s {
    struct idio_s *grey;
    void *p;
    struct idio_s *args;
} idio_opaque_t;

#define IDIO_OPAQUE_GREY(C) ((C)->u.opaque->grey)
#define IDIO_OPAQUE_P(C)    ((C)->u.opaque->p)
#define IDIO_OPAQUE_ARGS(C) ((C)->u.opaque->args)

typedef unsigned char IDIO_FLAGS_T;

#define IDIO_FLAG_NONE		0
#define IDIO_FLAG_CONST		(1<<0)
#define IDIO_FLAG_TAINTED	(1<<1)

/**
 * struct idio_s - Idio type structure
 * @next: use by the garbage collector
 * @type: the type of this value (&typedef idio_type_e)
 * @flags: generic flags
 * @tflags: type-specific flags
 * @u: union of the type structures
 */
struct idio_s {
    struct idio_s *next;

    /*
     * The union will be word-aligned (or larger) so we have 4 or 8
     * bytes of room for "stuff"
     */
    idio_type_e type;
    IDIO_FLAGS_T gc_flags;
    IDIO_FLAGS_T flags;		/* generic type flags */
    IDIO_FLAGS_T tflags;	/* type-specific flags (since we have
				   room here) */
    /*
     * Rationale for union.  We need to decide whether the union
     * should embed the object or have a pointer to it.
     *
     * Far and away the most commonly used object is a pair(*) which
     * consists of three pointers (grey, head and tail).  If this
     * union is a pointer to such an object then we use a pointer here
     * in the union and then two pointers from malloc(3) as well as
     * the three pointers in the pair.
     *
     * (*) Unless you start using bignums (two pointers) in which case
     * they dominate.
     *
     * Of course, the moment we use the (three pointer) object
     * directly in the union then the original pointer from the union
     * is shadowed by the three pointers of the pair directly here.
     * We still save the two malloc(3) pointers and the cost of
     * malloc(3)/free(3).
     *
     * Any other object that is three pointers or less can then also
     * be used directly in the union with no extra cost.
     */
    union idio_s_u {
	idio_string_t          string;
	idio_substring_t       substring;
	idio_symbol_t          symbol;
	idio_keyword_t         keyword;
	idio_pair_t            pair;
	idio_array_t           *array;
	idio_hash_t            *hash;
	idio_closure_t         *closure;
	idio_primitive_t       *primitive;
	idio_bignum_t          bignum;
	idio_module_t          *module;
	idio_frame_t           *frame;
	idio_handle_t          *handle;
	idio_struct_type_t     *struct_type;
	idio_struct_instance_t struct_instance;
	idio_thread_t	       *thread;

	idio_C_type_t          C_type;

	idio_C_typedef_t       *C_typedef;
	idio_C_struct_t        *C_struct;
	idio_C_instance_t      *C_instance;
	idio_C_FFI_t           *C_FFI;
	idio_opaque_t          *opaque;
	idio_continuation_t    *continuation;
	idio_bitset_t	       bitset;
    } u;
};

typedef struct idio_s idio_t;

#define IDIO_FLAGS(I)	((I)->flags)

/**
 * typedef IDIO - Idio *value*
 *
 * ``IDIO`` entities are the (references to) Idio values that are used
 * everywhere.  They are typedef'd to be pointers to &struct idio_s.
 */
typedef idio_t* IDIO;

typedef struct idio_root_s {
    struct idio_root_s *next;
    struct idio_s *object;
} idio_root_t;

#define IDIO_GC_FLAG_NONE	 0
#define IDIO_GC_FLAG_REQUEST   (1<<0)
#define IDIO_GC_FLAG_FINISH    (1<<1)

typedef struct idio_gc_s {
    struct idio_gc_s *next;
    idio_root_t *roots;
    IDIO dynamic_roots;
    IDIO free;
    IDIO used;
    IDIO grey;
    IDIO weak;
    unsigned int pause;
    unsigned char verbose;
    unsigned char inst;
    IDIO_FLAGS_T flags;		/* generic GC flags */
    struct stats {
	unsigned long long nfree; /* # on free list */
	unsigned long long tgets[IDIO_TYPE_MAX];
	unsigned long long igets; /* gets since last collection */
	unsigned long long mgets; /* max igets */
	unsigned long long reuse; /* objects from free list */
	unsigned long long allocs; /* # allocations */
	unsigned long long tbytes; /* # bytes ever allocated */
	unsigned long long nbytes; /* # bytes currently allocated */
	unsigned long long nused[IDIO_TYPE_MAX]; /* per-type usage */
	unsigned long long collections;	/* # times gc has been run */
	unsigned long long bounces;
	struct timeval dur;
	struct timeval ru_utime;
	struct timeval ru_stime;
    }  stats;
} idio_gc_t;

#define IDIO_GC_FLAGS(F)	((F)->flags)

/*
  the alignment of a structure field of type TYPE can be determined by
  looking at the offset of such a field in a structure where the first
  element is a single char
*/

#define IDIO_C_STRUCT_ALIGNMENT(TYPE)	offsetof (struct { char __gc_c_struct_1; TYPE __gc_c_struct_2; }, __gc_c_struct_2)

/*
 * Fixnums, characters and small constants

 * On a 32 bit processor a pointer to an Idio type structure will be
 * four-byte word-aligned meaning the bottom two bits will always be
 * 00.  We can use this to identify up to three other types which
 * require less than one word of data.

 * The three types are:

 * - fixnums: small integers (for small being ~ +/- 2**29 on 32 bit
     platforms: 32 less the two bits above and a sign bit)

     Fixnums, being architecture-related have a porting issue as
     clearly a 2**60 fixnum from a 64-bit machine encoded in a
     compiled file can't be read as a fixnum on a 32-bit machine.

     While we ponder the nuances, compiled files are
     architecture-oriented.

 * - (small) constants.

     Actually we have several sets of constants:

      - the kind the user can see and use: #t, #f, #eof etc..

      as well as various internal well-known values used internally:

       - reader tokens, idio_T_*
       - intermediate code identifiers, IDIO_I_*
       - VM instructions idio_A_*

     These are fixed sets -- fixed in the sense that we know it isn't
     going to be troubling a 32-bit boundary as we, in C-land, are in
     control of defining them and we're not going to remember what
     they are if we define more than a couple of dozen.  Even the
     intermediate code and VM instructions are broadly limited to 256
     as we are a byte compiler.

     Let's reserve three bits to cover distinguishing between constant
     types although we only have four at the moment.

     Another subset of constants (although much much larger) is
     characters.  Characters as a distinct type from fixnums to avoid
     the awkwardness of trying to assign a semantic meaning to:

     1 + #\®

     (Hmm, dunno.)

     We have the slight headache when we talk about characters in
     dancing the line between a Unix/C character (nominally a byte but
     in practice an int to include the sentinel value, EOF) and more
     flavoursome variants of character where a character is not just
     more than one byte but might be more than one entity within the
     coding system -- look up grapheme clusters and think that e-acute
     might be composed by an e glyph and an acute accent glyph rather
     than being a single e-acute glyph.

     Unicode, is a popular choice and is also a fixed set of 21 bits,
     not troubling a 32-bit boundary -- there's a little over a
     million code points possible (17 planes times 65k code points per
     plane -- a UTF-16 restriction) although in practice less than
     150k code points have been assigned (in Unicode 13.0).  That's
     still approximately 150k more characters than many of us in the
     English-speaking western world are used to.

     https://unicode.org/faq/utf_bom.html

     Although *how* we store Unicode is an interesting point.  With
     the various different constants chewing up a few bits of
     identification we should have ~27 bits on a 32bit platform to
     play with.  We can obviously, therefore, store Unicode internally
     as UTF-16.  However, we are much more likely to interact with the
     world in UTF-8.

     UTF-8 itself isn't perfect as the original Unicode spec, ISO
     10646, called for up to 6 octets although UTF-8 only encodes up
     to four.  Of those four, the worst case, from
     https://tools.ietf.org/html/rfc3629, is: 11110xxx 10xxxxxx
     10xxxxxx 10xxxxxx; demonstrating the 21 bits of data.  The
     leading 11110 to indicate this is the fourth of the four
     multi-octet encodings, ie, two bits of alternatives, isn't
     strictly necessary as it can be derived from the (binary) value.

     As an aside, if Unicode is only using 21 of our 27 bits then I
     suppose we could have 2^6 alternate Unicodes...

     Of course we don't want to store four bytes when we're only using
     21 bits.  And we don't want to store four bytes when we're trying
     to be obtuse and squeeze everything inside a "pointer."  If we
     did store just the 21 bits then our problem comes in decoding
     those 21 bits back into UTF-8 (or other).  Without careful byte
     by byte checking of parts of bytes could we use the remaining 3
     bits of a 24-bit format to encode the number of bytes we expect
     UTF-8 to use at which point we can probably get away with some
     bit-shuffling.

     To take the example above a four byte code point would have the
     UTF-8 byte count bits as 100 (ie. four) followed by the 21 bits,
     ie 100xxx...  When we come to decode it we would see the byte
     code count as four then pull off the right number of bits per
     byte, so 100aaabbbbbbccccccdddddd can be bit-shuffled directly
     into 11110aaa 10bbbbbb 10cccccc 10dddddd.

     If I read Section D92, Table 3.6 in
     http://www.unicode.org/versions/Unicode13.0.0/ch03.pdf correctly
     the four (byte length) options prepended to the full 21 code
     point bits are:

     001 00000 00000000 0xxxxxxx => 0xxxxxxx
     010 00000 00000yyy yyxxxxxx => 110yyyyy 10xxxxxx
     011 00000 zzzzyyyy yyxxxxxx => 1110zzzz 10yyyyyy 10xxxxxx
     100 uuuuu zzzzyyyy yyxxxxxx => 11110uuu 10uuzzzz 10yyyyyy 10xxxxxx

     I think there's some mileage there.

 * - another type not yet determined (hence, PLACEHOLDER) but
     something that will use the whole address space usefully.
     Probably "flonums" based on IEEE 794.  Do shells need flonums?
     Who cares, they sound interesting to implement.

 * All three will then have a minimum of 30 bits of useful space to
 * work with on a 32bit machine.

 * Of course, we could subdivide any of these into further types each
 * using proportionally less space.  The constants (there's maybe a
 * few hundred) are the obvious candidate.
 */

/*
 * two bits to distinguish the four broad types
 */
#define IDIO_TYPE_BITS			2
#define IDIO_TYPE_BITS_SHIFT		IDIO_TYPE_BITS
#define IDIO_TYPE_MASK			0x3

#define IDIO_TYPE_POINTER_MARK		0x00
#define IDIO_TYPE_FIXNUM_MARK		0x01
#define IDIO_TYPE_CONSTANT_MARK		0x02
#define IDIO_TYPE_PLACEHOLDER_MARK	0x03

/*
 * three bits to distinguish between the constant types, shifted by
 * the two bits above
 *
 * The _MARKs include the _CONSTANT_MARK so the _MASK is the first
 * five bits
 */
#define IDIO_TYPE_CONSTANT_BITS			3
#define IDIO_TYPE_CONSTANT_BITS_SHIFT		(IDIO_TYPE_BITS + IDIO_TYPE_CONSTANT_BITS)
#define IDIO_TYPE_CONSTANT_MASK			0x1f
#define IDIO_TYPE_CONSTANT_IDIO_MARK		((0x00 << IDIO_TYPE_BITS_SHIFT) | IDIO_TYPE_CONSTANT_MARK)
#define IDIO_TYPE_CONSTANT_TOKEN_MARK		((0x01 << IDIO_TYPE_BITS_SHIFT) | IDIO_TYPE_CONSTANT_MARK)
#define IDIO_TYPE_CONSTANT_I_CODE_MARK		((0x02 << IDIO_TYPE_BITS_SHIFT) | IDIO_TYPE_CONSTANT_MARK)
#define IDIO_TYPE_CONSTANT_CHARACTER_MARK	((0x03 << IDIO_TYPE_BITS_SHIFT) | IDIO_TYPE_CONSTANT_MARK)
#define IDIO_TYPE_CONSTANT_UNICODE_MARK		((0x04 << IDIO_TYPE_BITS_SHIFT) | IDIO_TYPE_CONSTANT_MARK)
  /*
   * 0x05 - 0x07 to be defined (or even thought of)
   */

#define IDIO_TYPE_POINTERP(x)		((((intptr_t) x) & IDIO_TYPE_MASK) == IDIO_TYPE_POINTER_MARK)
#define IDIO_TYPE_FIXNUMP(x)		((((intptr_t) x) & IDIO_TYPE_MASK) == IDIO_TYPE_FIXNUM_MARK)
#define IDIO_TYPE_CONSTANTP(x)		((((intptr_t) x) & IDIO_TYPE_MASK) == IDIO_TYPE_CONSTANT_MARK)
#define IDIO_TYPE_PLACEHOLDERP(x)	((((intptr_t) x) & IDIO_TYPE_MASK) == IDIO_TYPE_PLACEHOLDER_MARK)

#define IDIO_TYPE_CONSTANT_IDIOP(x)		((((intptr_t) x) & IDIO_TYPE_CONSTANT_MASK) == IDIO_TYPE_CONSTANT_IDIO_MARK)
#define IDIO_TYPE_CONSTANT_TOKENP(x)		((((intptr_t) x) & IDIO_TYPE_CONSTANT_MASK) == IDIO_TYPE_CONSTANT_TOKEN_MARK)
#define IDIO_TYPE_CONSTANT_I_CODEP(x)		((((intptr_t) x) & IDIO_TYPE_CONSTANT_MASK) == IDIO_TYPE_CONSTANT_I_CODE_MARK)
#define IDIO_TYPE_CONSTANT_CHARACTERP(x)	((((intptr_t) x) & IDIO_TYPE_CONSTANT_MASK) == IDIO_TYPE_CONSTANT_CHARACTER_MARK)
#define IDIO_TYPE_CONSTANT_UNICODEP(x)		((((intptr_t) x) & IDIO_TYPE_CONSTANT_MASK) == IDIO_TYPE_CONSTANT_UNICODE_MARK)

#define IDIO_FIXNUM_VAL(x)	(((intptr_t) x) >> IDIO_TYPE_BITS_SHIFT)
#define IDIO_FIXNUM(x)		((IDIO) ((x) << IDIO_TYPE_BITS_SHIFT | IDIO_TYPE_FIXNUM_MARK))
#define IDIO_FIXNUM_MIN		(INTPTR_MIN >> IDIO_TYPE_BITS_SHIFT)
#define IDIO_FIXNUM_MAX		(INTPTR_MAX >> IDIO_TYPE_BITS_SHIFT)

#define IDIO_CONSTANT_IDIO_VAL(x)	(((intptr_t) x) >> IDIO_TYPE_CONSTANT_BITS_SHIFT)
#define IDIO_CONSTANT_IDIO(x)		((const IDIO) (((intptr_t) x) << IDIO_TYPE_CONSTANT_BITS_SHIFT | IDIO_TYPE_CONSTANT_IDIO_MARK))
#define IDIO_CONSTANT_TOKEN_VAL(x)	(((intptr_t) x) >> IDIO_TYPE_CONSTANT_BITS_SHIFT)
#define IDIO_CONSTANT_TOKEN(x)		((const IDIO) (((intptr_t) x) << IDIO_TYPE_CONSTANT_BITS_SHIFT | IDIO_TYPE_CONSTANT_TOKEN_MARK))
#define IDIO_CONSTANT_I_CODE_VAL(x)	(((intptr_t) x) >> IDIO_TYPE_CONSTANT_BITS_SHIFT)
#define IDIO_CONSTANT_I_CODE(x)	((const IDIO) (((intptr_t) x) << IDIO_TYPE_CONSTANT_BITS_SHIFT | IDIO_TYPE_CONSTANT_I_CODE_MARK))

#define IDIO_CHARACTER_VAL(x)	(((intptr_t) x) >> IDIO_TYPE_CONSTANT_BITS_SHIFT)
/* #define IDIO_CHARACTER_IVAL(x)	(tolower (IDIO_CHARACTER_VAL (x))) */
#define IDIO_CHARACTER(x)	((const IDIO) (((intptr_t) x) << IDIO_TYPE_CONSTANT_BITS_SHIFT | IDIO_TYPE_CONSTANT_CHARACTER_MARK))
#define IDIO_UNICODE_VAL(x)	(((intptr_t) x) >> IDIO_TYPE_CONSTANT_BITS_SHIFT)
#define IDIO_UNICODE(x)		((const IDIO) (((intptr_t) x) << IDIO_TYPE_CONSTANT_BITS_SHIFT | IDIO_TYPE_CONSTANT_UNICODE_MARK))

/*
 * Idio instruction arrays.
 *
 * We generate our byte compiled code into these.  There's often
 * several floating around at once as, in order to determine how far
 * to jump over some not-immediately-relevant code to move onto the
 * next (source code) statement, we need to have already byte compiled
 * the not-immediately-relevant code.  Which we can then copy.
 *
 * XXX These are reference counted for reasons to do with an
 * undiscovered bug^W^Wundocumented feature.
 *
 * Used in both codegen.c and vm.c.
 */
typedef struct idio_ia_s {
    size_t asize;		/* alloc()'d */
    size_t usize;		/* used */
    IDIO_I *ae;
} idio_ia_t;

typedef idio_ia_t* IDIO_IA_T;

#define IDIO_IA_ASIZE(A)	((A)->asize)
#define IDIO_IA_USIZE(A)	((A)->usize)
#define IDIO_IA_AE(A,i)		((A)->ae[i])

void idio_gc_register_finalizer (IDIO o, void (*func) (IDIO o));
void idio_gc_deregister_finalizer (IDIO o);
void idio_run_finalizer (IDIO o);
void *idio_alloc (size_t s);
void idio_free (void *p);
void *idio_realloc (void *p, size_t s);
IDIO idio_gc_get (idio_type_e type);
void idio_gc_alloc (void **p, size_t size);
/**
 * IDIO_GC_ALLOC() - normalised call to allocate ``IDIO`` value data
 * @p: pointer to be set
 * @s: size in bytes
 *
 * Normally used to allocate the internal parts of an ``IDIO`` value,
 * eg. the array of bytes for the string inside an ``IDIO`` string
 * value.
 */
#define IDIO_GC_ALLOC(p,s)	(idio_gc_alloc ((void **)&(p), s))
#define IDIO_GC_FREE(p)		(idio_gc_free ((void *)(p)))
#ifdef IDIO_MALLOC
#define IDIO_ASPRINTF(strp,fmt,...) idio_malloc_asprintf (strp,fmt, ##__VA_ARGS__)
#else
#define IDIO_ASPRINTF(strp,fmt,...) asprintf (strp,fmt, ##__VA_ARGS__)
#endif

IDIO idio_clone_base (IDIO o);
int idio_isa (IDIO o, idio_type_e type);
void idio_gc_stats_free (size_t n);

void idio_mark (IDIO o, unsigned colour);
void idio_process_grey (unsigned colour);
unsigned idio_bw (IDIO o);
idio_root_t *idio_root_new ();
void idio_root_dump (idio_root_t *root);
void idio_root_mark (idio_root_t *root, unsigned colour);
idio_gc_t *idio_gc_new ();
#if IDIO_DEBUG > 2
void IDIO_FPRINTF (FILE *stream, const char *format, ...);
#else
#define IDIO_FPRINTF(...)	((void) 0)
#endif
void idio_gc_dump ();
void idio_gc_stats_inc (idio_type_e type);
void idio_gc_protect (IDIO o);
void idio_gc_protect_auto (IDIO o);
void idio_gc_expose (IDIO o);
void idio_gc_expose_all ();
void idio_gc_add_weak_object (IDIO o);
void idio_gc_remove_weak_object (IDIO o);
void idio_gc_mark (idio_gc_t *idio_gc);
void idio_gc_sweep (idio_gc_t *idio_gc);
void idio_gc_possibly_collect ();
#define IDIO_GC_COLLECT_GEN	0
#define IDIO_GC_COLLECT_ALL	1
void idio_gc_collect (idio_gc_t *idio_gc, int gen, char *caller);
void idio_gc_collect_gen (char *caller);
void idio_gc_collect_all (char *caller);
void idio_gc_new_gen ();
int idio_gc_get_pause (char *caller);
void idio_gc_pause (char *caller);
void idio_gc_resume (char *caller);
void idio_gc_reset (char *caller, int pause);
void idio_gc_free ();

char *idio_strcat (char *s1, size_t *s1sp, const char *s2, const size_t s2s);
char *idio_strcat_free (char *s1, size_t *s1sp, char *s2, const size_t s2s);

#define IDIO_STRCAT(s1,s1sp,s2)		{			\
	char *str = s2;						\
	size_t size = strlen (str);				\
	(s1) = idio_strcat ((s1), (s1sp), str, size);		\
    }
#define IDIO_STRCAT_FREE(s1,s1sp,s2,s2sp)	((s1) = idio_strcat_free ((s1), (s1sp), (s2), (s2sp)))

int idio_gc_verboseness (int n);
void idio_gc_set_verboseness (int n);
void idio_hcount (unsigned long long *bytes, int *scale);
void idio_gc_stats ();

void idio_init_gc ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
