/*
 * Copyright (c) 2015-2022 Ian Fitchet <idf(at)idio-lang.org>
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
typedef enum {
    IDIO_TYPE_NONE,		/* 0 */
    IDIO_TYPE_FIXNUM,
    IDIO_TYPE_CONSTANT_IDIO,
    IDIO_TYPE_CONSTANT_TOKEN,
    IDIO_TYPE_CONSTANT_I_CODE,
    IDIO_TYPE_CONSTANT_UNICODE,
/*
  IDIO_TYPE_CONSTANT_6,
  IDIO_TYPE_CONSTANT_7,
  IDIO_TYPE_CONSTANT_8,
*/
    IDIO_TYPE_PLACEHOLDER,

    IDIO_TYPE_STRING,
    IDIO_TYPE_SUBSTRING,
    IDIO_TYPE_SYMBOL,
    IDIO_TYPE_KEYWORD,		/* 10 */
    IDIO_TYPE_PAIR,
    IDIO_TYPE_ARRAY,
    IDIO_TYPE_HASH,
    IDIO_TYPE_CLOSURE,
    IDIO_TYPE_PRIMITIVE,
    IDIO_TYPE_BIGNUM,

    IDIO_TYPE_MODULE,
    IDIO_TYPE_FRAME,
    IDIO_TYPE_HANDLE,
    IDIO_TYPE_STRUCT_TYPE,	/* 20 */
    IDIO_TYPE_STRUCT_INSTANCE,
    IDIO_TYPE_THREAD,
    IDIO_TYPE_CONTINUATION,
    IDIO_TYPE_BITSET,

    IDIO_TYPE_C_CHAR,
    IDIO_TYPE_C_SCHAR,
    IDIO_TYPE_C_UCHAR,
    IDIO_TYPE_C_SHORT,
    IDIO_TYPE_C_USHORT,
    IDIO_TYPE_C_INT,		/* 30 */
    IDIO_TYPE_C_UINT,
    IDIO_TYPE_C_LONG,
    IDIO_TYPE_C_ULONG,
    IDIO_TYPE_C_LONGLONG,
    IDIO_TYPE_C_ULONGLONG,
    IDIO_TYPE_C_FLOAT,
    IDIO_TYPE_C_DOUBLE,
    IDIO_TYPE_C_LONGDOUBLE,
    IDIO_TYPE_C_POINTER,
    IDIO_TYPE_C_VOID,		/* 40 */

    IDIO_TYPE_MAX
} idio_type_enum;

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

/*
 * A vtable method is a C function and a block of data for the
 * function, if required.
 *
 * The C function is passed this structure as its first argument, the
 * original Idio value as its second and any further arguments as
 * necessary, eg. an Idio struct field or C structure member name.
 */
struct idio_vtable_method_s {
    struct idio_s *(*func) (struct idio_vtable_method_s *method, struct idio_s *value, ...);
    size_t size;
    void *data;
};

typedef struct idio_vtable_method_s idio_vtable_method_t;

#define IDIO_VTABLE_METHOD_FUNC(VTM)	((VTM)->func)
#define IDIO_VTABLE_METHOD_SIZE(VTM)	((VTM)->size)
#define IDIO_VTABLE_METHOD_DATA(VTM)	((VTM)->data)

/*
 * A vtable entry is the mapping from a vtable function name to a
 * vtable method.
 *
 * In addition we store:
 *
 * * whether the method was inherited from an ancestor, ie. has been
 *   cached and might need clearing
 *
 * * a count of usage which we can use to slowly bubble sort methods
 *   closer to the start of the array of vtable methods
 *
 *   What happens if count wraps?  2^31-1 calls?  Dunno.  It sits at
 *   (approximately) UINT_MAX?
 *
 * XXX name, an IDIO, is at risk of being GC'd unless it is kept in a
 * global table, idio_vtable_names.  Obviously, that global table has
 * to live until we've done any final reporting on vtables.
 */
struct idio_vtable_entry_s {
    struct idio_s *name;
    unsigned int inherited:1;
    unsigned int count:31;
    idio_vtable_method_t *method;
};

typedef struct idio_vtable_entry_s idio_vtable_entry_t;

#define IDIO_VTABLE_ENTRY_NAME(VTE)      ((VTE)->name)
#define IDIO_VTABLE_ENTRY_INHERITED(VTE) ((VTE)->inherited)
#define IDIO_VTABLE_ENTRY_COUNT(VTE)     ((VTE)->count)
#define IDIO_VTABLE_ENTRY_METHOD(VTE)    ((VTE)->method)

#define IDIO_VTABLE_FLAG_NONE		0
#define IDIO_VTABLE_FLAG_BASE		(1<<0)
#define IDIO_VTABLE_FLAG_DERIVED	(1<<0)

/*
 * A vtable is attached to all types -- including user-defined
 * (struct) types.  Idio values will reference their type-specific
 * vtable including bespoke ones for C/pointer types.  Fixnums,
 * constants etc. are obviously handled separately.
 *
 * Vtables are chained through a parent and are tagged with a
 * generation which is bumped whenever a modification is made
 * *anywhere* in the vtable tree.
 *
 * If this vtable's generation does not match the VM's generation then
 * we need to validate not just our own but our ancestor's methods.
 * There's a "quick" trick, here.  If none of my ancestor's
 * generations are newer than me then the vtable modification did not
 * affect this part of the tree and we can simply bump all the
 * generations (of this part of the tree) to the global value.
 */
struct idio_vtable_s {
    unsigned int flags;
    struct idio_vtable_s *parent;
    int gen;		/* generation */
    size_t size;	/* # entries, ie. methods */
    idio_vtable_entry_t **vte; /* array of size methods */
};

typedef struct idio_vtable_s idio_vtable_t;

#define IDIO_VTABLE_FLAGS(VT)	((VT)->flags)
#define IDIO_VTABLE_PARENT(VT)	((VT)->parent)
#define IDIO_VTABLE_GEN(VT)	((VT)->gen)
#define IDIO_VTABLE_SIZE(VT)	((VT)->size)
#define IDIO_VTABLE_VTE(VT,i)	((VT)->vte[i])

/**
 * struct idio_string_s - Idio ``string`` structure
 */
#define IDIO_STRING_FLAG_NONE		0
/*
 * Coincidentally, the nBYTE flags are the number of bytes required
 * for storage per code-point.  Do not rely on this, these are just
 * flags!
 */
#define IDIO_STRING_FLAG_1BYTE		(1<<0)
#define IDIO_STRING_FLAG_2BYTE		(1<<1)
#define IDIO_STRING_FLAG_4BYTE		(1<<2)
#define IDIO_STRING_FLAG_OCTET		(1<<3)
#define IDIO_STRING_FLAG_PATHNAME	(1<<4)
#define IDIO_STRING_FLAG_FD_PATHNAME	(1<<5)
#define IDIO_STRING_FLAG_FIFO_PATHNAME	(1<<6)

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
    /**
     * @blen: length in bytes
     *
     * The string is expected to be NUL-terminated.
     */
    size_t blen;		/* bytes */
    char *s;			/* C string */
} idio_symbol_t;

#define IDIO_SYMBOL_BLEN(S)	((S)->u.symbol.blen)
#define IDIO_SYMBOL_S(S)	((S)->u.symbol.s)
#define IDIO_SYMBOL_FLAGS(S)	((S)->tflags)

typedef struct idio_keyword_s {
    /**
     * @blen: length in bytes
     *
     * The string is expected to be NUL-terminated.
     */
    size_t blen;		/* bytes */
    char *s;			/* C string */
} idio_keyword_t;

#define IDIO_KEYWORD_BLEN(S)	((S)->u.keyword.blen)
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

#define IDIO_PAIR_TTTT(P)	IDIO_PAIR_T (IDIO_PAIR_TTT (P))
#define IDIO_PAIR_HTTTT(P)	IDIO_PAIR_H (IDIO_PAIR_TTTT (P))

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
 * typedef idio_ai_t - Idio ``array`` index (can be negative)
 * typedef idio_as_t - Idio ``array`` size
 */
typedef ssize_t idio_ai_t;
typedef size_t  idio_as_t;

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
    idio_as_t asize;
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
    idio_as_t usize;
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
    int (*comp_C) (void const *k1, void const *k2);	/* C equivalence function */
    idio_hi_t (*hash_C) (struct idio_s *h, void const *k); /* C hashing function */
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

/**
 * typedef idio_pc_t - Idio VM ``PC`` index
 */
typedef intptr_t idio_pc_t;

/**
 * typedef idio_xi_t - Idio VM execution environment index
 */
typedef uintptr_t idio_xi_t;

/*
 * If we wanted to store the arity and varargs boolean of a closure
 * (for a possible thunk? predicate) without increasing the size of
 * idio_closure_s then we should be able to sneak in an extra char in
 * idio_s (after tflags) for the arity -- with a special value of 255
 * for 255+ arguments (how many of those might we see?) and use a bit
 * in tflags for varargs.
 *
 * The closure's name is a reference into the VM's constants table
 * which shouldn't get reclaimed during the lifetime of the program.
 */
#ifdef IDIO_VM_PROF
typedef struct idio_closure_stats_s {
    int refcnt;
    uint64_t called;
    struct timespec call_time;
    struct timeval ru_utime;
    struct timeval ru_stime;
} idio_closure_stats_t;

#define IDIO_CLOSURE_REFCNT(C)     ((C)->u.closure->stats->refcnt)
#define IDIO_CLOSURE_CALLED(C)     ((C)->u.closure->stats->called)
#define IDIO_CLOSURE_CALL_TIME(C)  ((C)->u.closure->stats->call_time)
#define IDIO_CLOSURE_RU_UTIME(C)   ((C)->u.closure->stats->ru_utime)
#define IDIO_CLOSURE_RU_STIME(C)   ((C)->u.closure->stats->ru_stime)
#endif

typedef struct idio_closure_s {
    struct idio_s *grey;
    idio_xi_t xi;			/* xenv index */
    idio_pc_t code_pc;
    idio_pc_t code_len;
    struct idio_s *name;
    struct idio_s *frame;
    struct idio_s *env;
#ifdef IDIO_VM_PROF
    idio_closure_stats_t *stats;
#endif
} idio_closure_t;

#define IDIO_CLOSURE_GREY(C)       ((C)->u.closure->grey)
#define IDIO_CLOSURE_XI(C)         ((C)->u.closure->xi)
#define IDIO_CLOSURE_CODE_PC(C)    ((C)->u.closure->code_pc)
#define IDIO_CLOSURE_CODE_LEN(C)   ((C)->u.closure->code_len)
#define IDIO_CLOSURE_NAME(C)       ((C)->u.closure->name)
#define IDIO_CLOSURE_FRAME(C)      ((C)->u.closure->frame)
#define IDIO_CLOSURE_ENV(C)        ((C)->u.closure->env)
#ifdef IDIO_VM_PROF
#define IDIO_CLOSURE_STATS(C)      ((C)->u.closure->stats)
#endif

/*
 * idio_primitive_desc_t is for the static allocation in C of the
 * description of a primitive value.
 *
 * Yes, it contains much of an idio_primitive_t but it is never *used*
 * as an idio_primitive_t, the data is always copied in
 * idio_primitive_data()
 */
typedef struct idio_primitive_desc_s {
    struct idio_s *(*f) ();	/* don't declare args */
    char *name;
    size_t name_len;
    uint8_t arity;
    char varargs;
    char *sigstr;
    size_t sigstr_len;
    char *docstr;
    size_t docstr_len;
    char *source_file;
    size_t source_line;
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
    size_t name_len;
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
#define IDIO_PRIMITIVE_NAME_LEN(P)   ((P)->u.primitive->name_len)
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
    struct idio_s *symbols;	/* hash table of name to symbol info tuple */
    struct idio_s *identity;
} idio_module_t;

#define IDIO_MODULE_GREY(F)	((F)->u.module->grey)
#define IDIO_MODULE_NAME(F)	((F)->u.module->name)
#define IDIO_MODULE_EXPORTS(F)	((F)->u.module->exports)
#define IDIO_MODULE_IMPORTS(F)	((F)->u.module->imports)
#define IDIO_MODULE_SYMBOLS(F)	((F)->u.module->symbols)
#define IDIO_MODULE_IDENTITY(F)	((F)->u.module->identity)

#define IDIO_FRAME_FLAG_NONE	 0
#define IDIO_FRAME_FLAG_ETRACE   (1<<0)
#define IDIO_FRAME_FLAG_KTRACE   (1<<1)
#define IDIO_FRAME_FLAG_PROFILE  (1<<2)
#define IDIO_FRAME_FLAG_C_STRUCT (1<<3)

/**
 * typedef idio_fi_t - Idio ``frame`` arguments index
 * can be looped to a negative value
 */
typedef int idio_fi_t;

typedef struct idio_frame_s {
    struct idio_s *grey;
    struct idio_s *next;
    idio_fi_t nparams;		/* number in use */
    idio_fi_t nalloc;		/* number allocated */
    idio_xi_t xi;
    struct idio_s *names;	/* a ref into vm_constants[xi] */
    struct idio_s* *args;

    /*
     * For debugging purposes we can record the function and source
     * expression (index) as we roll along
     */
    struct idio_s* func;
    idio_ai_t src_expr;		/* a fixnum: -1 is no src_expr */
} idio_frame_t;

#define IDIO_FRAME_GREY(F)     ((F)->u.frame->grey)
#define IDIO_FRAME_NEXT(F)     ((F)->u.frame->next)
#define IDIO_FRAME_NPARAMS(F)  ((F)->u.frame->nparams)
#define IDIO_FRAME_NALLOC(F)   ((F)->u.frame->nalloc)
#define IDIO_FRAME_XI(F)       ((F)->u.frame->xi)
#define IDIO_FRAME_NAMES(F)    ((F)->u.frame->names)
#define IDIO_FRAME_ARGS(F,i)   ((F)->u.frame->args[i])
#define IDIO_FRAME_FUNC(F)     ((F)->u.frame->func)
#define IDIO_FRAME_SRC_EXPR(F) ((F)->u.frame->src_expr)
#define IDIO_FRAME_FLAGS(F)    ((F)->tflags)

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

/*
 * An int8_t exponent will work but five tests explicitly use an
 * exponent greater than 8 bits.  Things like asin won't generate
 * enough accuracy for the tests with 8 bit exponents.
 */
typedef int32_t IDIO_BE_T;
#define IDIO_BE_MAX INT32_MAX
#define IDIO_BE_MIN INT32_MIN
#define IDIO_BE_FMT PRId32

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
    IDIO_BE_T exp;
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
    ptrdiff_t (*puts) (struct idio_s *h, char const *s, size_t slen);
    int (*flush) (struct idio_s *h);
    off_t (*seek) (struct idio_s *h, off_t offset, int whence);
    void (*print) (struct idio_s *h, struct idio_s *o);
} idio_handle_methods_t;

#define IDIO_HANDLE_FLAG_NONE		0
#define IDIO_HANDLE_FLAG_READ		(1<<0)
#define IDIO_HANDLE_FLAG_WRITE		(1<<1)
#define IDIO_HANDLE_FLAG_CLOSED		(1<<2)
#define IDIO_HANDLE_FLAG_FILE		(1<<3)
#define IDIO_HANDLE_FLAG_PIPE		(1<<4)
#define IDIO_HANDLE_FLAG_STRING		(1<<5)

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
    struct idio_s* *fields;	/* an array of symbols */
} idio_struct_type_t;

#define IDIO_STRUCT_TYPE_GREY(ST)	((ST)->u.struct_type->grey)
#define IDIO_STRUCT_TYPE_NAME(ST)	((ST)->u.struct_type->name)
#define IDIO_STRUCT_TYPE_PARENT(ST)	((ST)->u.struct_type->parent)
#define IDIO_STRUCT_TYPE_SIZE(ST)	((ST)->u.struct_type->size)
#define IDIO_STRUCT_TYPE_FIELDS(ST,i)	((ST)->u.struct_type->fields[i])

typedef struct idio_struct_instance_s {
    struct idio_s *grey;
    struct idio_s *type;	/* a struct-type */
    size_t size;		/* number of fields *including parents* or number of slots */
    struct idio_s* *fields;	/* an array of values */
} idio_struct_instance_t;

#define IDIO_STRUCT_INSTANCE_GREY(SI)		((SI)->u.struct_instance->grey)
#define IDIO_STRUCT_INSTANCE_TYPE(SI)		((SI)->u.struct_instance->type)
#define IDIO_STRUCT_INSTANCE_SIZE(SI)		((SI)->u.struct_instance->size)
#define IDIO_STRUCT_INSTANCE_FIELDS(SI,i)	((SI)->u.struct_instance->fields[i])

/*
 * The stack will be an Idio array although the code assumes the stack
 * pointer is a fixnum.
 *
 * Often a stack pointer of -1 is used as a sentinal value so keep it
 * signed.
 */
typedef intptr_t idio_sp_t;

typedef struct idio_thread_s {
    struct idio_s *grey;
    idio_xi_t xi;			/* xenv index */
    idio_pc_t pc;
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

    sigjmp_buf jmp_buf;

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

    /*
     * holes is the extant set of holes as per delim-control.idio
     */
    struct idio_s *holes;
} idio_thread_t;

#define IDIO_THREAD_GREY(T)           ((T)->u.thread->grey)
#define IDIO_THREAD_XI(T)             ((T)->u.thread->xi)
#define IDIO_THREAD_PC(T)             ((T)->u.thread->pc)
#define IDIO_THREAD_STACK(T)          ((T)->u.thread->stack)
#define IDIO_THREAD_VAL(T)            ((T)->u.thread->val)
#define IDIO_THREAD_FRAME(T)          ((T)->u.thread->frame)
#define IDIO_THREAD_ENV(T)            ((T)->u.thread->env)
#define IDIO_THREAD_JMP_BUF(T)        ((T)->u.thread->jmp_buf)
#define IDIO_THREAD_FUNC(T)           ((T)->u.thread->func)
#define IDIO_THREAD_REG1(T)           ((T)->u.thread->reg1)
#define IDIO_THREAD_REG2(T)           ((T)->u.thread->reg2)
#define IDIO_THREAD_EXPR(T)           ((T)->u.thread->expr)
#define IDIO_THREAD_INPUT_HANDLE(T)   ((T)->u.thread->input_handle)
#define IDIO_THREAD_OUTPUT_HANDLE(T)  ((T)->u.thread->output_handle)
#define IDIO_THREAD_ERROR_HANDLE(T)   ((T)->u.thread->error_handle)
#define IDIO_THREAD_MODULE(T)	      ((T)->u.thread->module)
#define IDIO_THREAD_HOLES(T)	      ((T)->u.thread->holes)
#define IDIO_THREAD_FLAGS(T)          ((T)->tflags)

/*
 * A continuation needs to save everything important about the state
 * of the current thread.  Essentially everything except the *val*
 * register and function-oriented registers (*func*, *reg1* and
 * *reg2*).  *expr* will be picked up in due course from the byte
 * code.
 *
 * Also, the current jmp_buf.  In a single span of code the jmp_buf is
 * unchanged, however when we "load" a file we get a new, nested
 * jmp_buf. If a continuation from the "outer" idio_vm_run() is
 * invoked then we must use its contextually correct jmp_buf.
 *
 * We'll be duplicating the efforts of idio_vm_preserve_state() but we
 * can't call that as it modifies the stack.
 *
 * Notably, we'll also preserve the thread itself -- which seems a bit
 * non-intuitive.  Here, we are looking to restore the current-thread
 * itself -- rather than, necessarily, the state of the objects it
 * references.  We can then reset those objects to the values we have
 * separately preserved.  We could create a new thread value and
 * restore everything into that.  An extra pointer saves the malloc
 * (then free of the original) in an object of which only two may
 * exist.
 *
 * One element #define'd out whilst the problem is mused over are the
 * current handles.  We get into a mess if the continuation restores
 * (temporary) handles that were closed in with-handle-redir in
 * job-control.idio.
 */
#define IDIO_CONTINUATION_FLAG_NONE		0
#define IDIO_CONTINUATION_FLAG_DELIMITED	(1<<0)

typedef struct idio_continuation_s {
    struct idio_s *grey;
    idio_pc_t pc;
    idio_xi_t xi;
    struct idio_s *stack;
    struct idio_s *frame;
    struct idio_s *env;
    sigjmp_buf jmp_buf;
#ifdef IDIO_CONTINUATION_HANDLES
    struct idio_s *input_handle;
    struct idio_s *output_handle;
    struct idio_s *error_handle;
#endif
    struct idio_s *module;
    struct idio_s *holes;

    struct idio_s *thr;
} idio_continuation_t;

#define IDIO_CONTINUATION_GREY(K)		((K)->u.continuation->grey)
#define IDIO_CONTINUATION_PC(K)			((K)->u.continuation->pc)
#define IDIO_CONTINUATION_XI(K)			((K)->u.continuation->xi)
#define IDIO_CONTINUATION_STACK(K)		((K)->u.continuation->stack)
#define IDIO_CONTINUATION_FRAME(K)		((K)->u.continuation->frame)
#define IDIO_CONTINUATION_ENV(K)		((K)->u.continuation->env)
#define IDIO_CONTINUATION_JMP_BUF(K)		((K)->u.continuation->jmp_buf)
#ifdef IDIO_VM_DYNAMIC_REGISTERS
#define IDIO_CONTINUATION_TRAP_SP(K)		((K)->u.continuation->trap_sp)
#define IDIO_CONTINUATION_DYNAMIC_SP(K)		((K)->u.continuation->dynamic_sp)
#define IDIO_CONTINUATION_ENVIRON_SP(K)		((K)->u.continuation->environ_sp)
#endif
#ifdef IDIO_CONTINUATION_HANDLES
#define IDIO_CONTINUATION_INPUT_HANDLE(T)	((T)->u.continuation->input_handle)
#define IDIO_CONTINUATION_OUTPUT_HANDLE(T)	((T)->u.continuation->output_handle)
#define IDIO_CONTINUATION_ERROR_HANDLE(T)	((T)->u.continuation->error_handle)
#endif
#define IDIO_CONTINUATION_MODULE(T)		((T)->u.continuation->module)
#define IDIO_CONTINUATION_HOLES(T)		((T)->u.continuation->holes)
#define IDIO_CONTINUATION_THR(K)		((K)->u.continuation->thr)
#define IDIO_CONTINUATION_FLAGS(K)		((K)->tflags)

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
    struct idio_s *ptype;	/* for C_pointer: (name, ref) */
    char freep;
} idio_C_pointer_t;

typedef struct idio_C_type_s {
    union {
	char			C_char;
	signed char		C_schar;
	unsigned char		C_uchar;
	short int		C_short;
	unsigned short int	C_ushort;
	int			C_int;
	unsigned int		C_uint;
	long int		C_long;
	unsigned long int	C_ulong;
	long long int		C_longlong;
	unsigned long long int	C_ulonglong;
	float			C_float;
	double			C_double;
	long double	       *C_longdouble;
	idio_C_pointer_t       *C_pointer;
    } u;
} idio_C_type_t;

/*
 * The names of the following macros have a lower-case base_type part
 * to be in line with the c-api-gen code which re-uses the typedef'd
 * names rather than upper-case them.
 */
#define IDIO_C_TYPE_char(C)          ((C)->u.C_type.u.C_char)
#define IDIO_C_TYPE_schar(C)         ((C)->u.C_type.u.C_schar)
#define IDIO_C_TYPE_uchar(C)         ((C)->u.C_type.u.C_uchar)
#define IDIO_C_TYPE_short(C)         ((C)->u.C_type.u.C_short)
#define IDIO_C_TYPE_ushort(C)        ((C)->u.C_type.u.C_ushort)
#define IDIO_C_TYPE_int(C)           ((C)->u.C_type.u.C_int)
#define IDIO_C_TYPE_uint(C)          ((C)->u.C_type.u.C_uint)
#define IDIO_C_TYPE_long(C)          ((C)->u.C_type.u.C_long)
#define IDIO_C_TYPE_ulong(C)         ((C)->u.C_type.u.C_ulong)
#define IDIO_C_TYPE_longlong(C)      ((C)->u.C_type.u.C_longlong)
#define IDIO_C_TYPE_ulonglong(C)     ((C)->u.C_type.u.C_ulonglong)
#define IDIO_C_TYPE_float(C)         ((C)->u.C_type.u.C_float)
#define IDIO_C_TYPE_double(C)        ((C)->u.C_type.u.C_double)
#define IDIO_C_TYPE_longdouble(C)    (*((C)->u.C_type.u.C_longdouble))
#define IDIO_C_TYPE_POINTER_PTYPE(C) ((C)->u.C_type.u.C_pointer->ptype)
#define IDIO_C_TYPE_POINTER_P(C)     ((C)->u.C_type.u.C_pointer->p)
#define IDIO_C_TYPE_POINTER_FREEP(C) ((C)->u.C_type.u.C_pointer->freep)

typedef unsigned char IDIO_FLAGS_T;

typedef enum {
    IDIO_GC_FLAG_GCC_BLACK,
    IDIO_GC_FLAG_GCC_DGREY,
    IDIO_GC_FLAG_GCC_LGREY,
    IDIO_GC_FLAG_GCC_WHITE
} idio_gc_flag_gcc_enum;

typedef enum {
    IDIO_GC_FLAG_NOTFREE,
    IDIO_GC_FLAG_FREE
} idio_gc_flag_free_enum;

typedef enum {
    IDIO_GC_FLAG_NOTSTICKY,
    IDIO_GC_FLAG_STICKY
} idio_gc_flag_sticky_enum;

typedef enum {
    IDIO_GC_FLAG_NOFINALIZER,
    IDIO_GC_FLAG_FINALIZER
} idio_gc_flag_finalizer_enum;

/*
 * generic type flags
 */
typedef enum {
    IDIO_FLAG_NONE,
    IDIO_FLAG_CONST,
    IDIO_FLAG_TAINTED
} idio_flag_enum;

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
    idio_vtable_t *vtable;

    /*
     * The union will be word-aligned (or larger, see below) so we
     * have several bytes of room for "stuff".  (Once we use one
     * bit/char/whatever for flags then we'll get the space for the
     * full 4/8/16 byte alignment.)
     *
     * It doesn't *seem* to matter where in the structure these
     * bitfields live (other than together) suggesting the compiler is
     * quite good at deferencing the member fields.
     */
    idio_type_enum              type     :6; /* 40-ish types is < 64 */
    idio_gc_flag_gcc_enum       colour   :2;
    idio_gc_flag_free_enum      free     :1;
    idio_gc_flag_sticky_enum    sticky   :1;
    idio_gc_flag_finalizer_enum finalizer:1;

    idio_flag_enum              flags    :2; /* generic type flags */

    /*
     * type-specific flags (since we have room here)
     */
    IDIO_FLAGS_T tflags;

    /*
     * The garbage collection generation this was allocated in
     */
    unsigned char gen;

    /*
     * Rationale for union.  We need to decide whether the union
     * should embed the type-specific structure or have a pointer to
     * it.
     *
     * Far and away the most commonly used type is a pair(*) which
     * consists of three pointers (grey, head and tail).  If this
     * union is a pointer to such an structure then we use a pointer
     * here in the union and then two pointers from malloc(3) as well
     * as the three pointers in the pair.
     *
     * (*) Unless you start using bignums (two pointers) in which case
     * they may come to dominate.
     *
     * Of course, the moment we use the (three pointer) structure
     * directly in the union then the original pointer from the union
     * is shadowed by the three pointers of the pair directly here.
     * We still save the two malloc(3) pointers and the cost of
     * malloc(3)/free(3).
     *
     * Any other structure that is three pointers or less can then
     * also be used directly in the union with no extra cost.
     *
     * ----
     *
     * idio_C_type_t is the alignment culprit with long double being
     * the forcing type, here for a couple of examples (YMMV):
     *
     * 32-bit - 8 byte alignment (along with long long etc.)
     *
     * 64-bit - 16 byte alignment
     *
     * The alignment is both before and after.  It appears to be an
     * integer multiple of the largest element, so n*8 or n*16 >= max
     * (sizeof (union members)).
     *
     * In particular, the union, targeting 3 * sizeof (pointer),
     * actually becomes 2 * sizeof (long double), ie. 4 * sizeof
     * (pointer).  In other words there's a pointer's worth of empty
     * space at the end of the union.
     *
     * Maybe a struct-instance (three pointers and a size_t) could be
     * promoted to fit in the union directly.
     */
    union idio_s_u {
	idio_string_t           string;
	idio_substring_t        substring;
	idio_symbol_t           symbol;
	idio_keyword_t          keyword;
	idio_pair_t             pair;
	idio_array_t           *array;
	idio_hash_t            *hash;
	idio_closure_t         *closure;
	idio_primitive_t       *primitive;
	idio_bignum_t           bignum;
	idio_module_t          *module;
	idio_frame_t           *frame;
	idio_handle_t          *handle;
	idio_struct_type_t     *struct_type;
	idio_struct_instance_t *struct_instance;
	idio_thread_t	       *thread;
	idio_continuation_t    *continuation;
	idio_bitset_t	        bitset;
	idio_C_type_t           C_type;
    } u;
};

typedef struct idio_s idio_t;

#define IDIO_FLAGS(I)	((I)->flags)

/**
 * typedef IDIO - Idio *value*
 *
 * ``IDIO`` entities are the (references to) Idio values that are used
 * everywhere.  They are typedef'd to be pointers to &struct idio_s.
 *
 * NB volatility (see sigsetjmp()) affects automatic variables in the
 * function containing sigsetjmp() that might have been put into
 * registers, here, pointers.
 *
 * So we want the *pointer* to be volatile and care (slightly) less
 * about the idio_t, the thing the pointer points to -- which is
 * relatively unlikely to fit into a register anyway.
 */
typedef idio_t*			IDIO;
typedef idio_t* volatile	IDIO_v;

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
    idio_root_t *autos;
    IDIO free;
    IDIO used;
    IDIO grey;
    IDIO weak;
    int pause;
    unsigned char verbose;
    unsigned char gen;
    IDIO_FLAGS_T flags;		/* generic GC flags */
    struct stats {
	long long nfree; /* # on free list */
	long long tgets[IDIO_TYPE_MAX];
	long long igets; /* gets since last collection */
	long long mgets; /* max igets */
	long long reuse; /* objects from free list */
	long long allocs; /* # allocations */
	long long tbytes; /* # bytes ever allocated */
	long long nbytes; /* # bytes currently allocated */
	long long nused[IDIO_TYPE_MAX]; /* per-type usage */
	long long collections;	/* # times gc has been run */
	long long bounces;
	struct timeval mark_dur;
	struct timeval sweep_dur;
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
#define IDIO_TYPE_CONSTANT_UNICODE_MARK		((0x03 << IDIO_TYPE_BITS_SHIFT) | IDIO_TYPE_CONSTANT_MARK)
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
#define IDIO_TYPE_CONSTANT_UNICODEP(x)		((((intptr_t) x) & IDIO_TYPE_CONSTANT_MASK) == IDIO_TYPE_CONSTANT_UNICODE_MARK)

#define IDIO_FIXNUM_VAL(x)	(((intptr_t) x) >> IDIO_TYPE_BITS_SHIFT)
/*
 * left-shift of negative numbers is undefined behaviour so we need a
 * more robust implementation (cast to unsigned and cast back again)
 */
#define IDIO_FIXNUM(x)	((IDIO) ((intptr_t) (((uintptr_t) x) << IDIO_TYPE_BITS_SHIFT) | IDIO_TYPE_FIXNUM_MARK))
#define IDIO_FIXNUM_MIN		(INTPTR_MIN >> IDIO_TYPE_BITS_SHIFT)
#define IDIO_FIXNUM_MAX		(INTPTR_MAX >> IDIO_TYPE_BITS_SHIFT)

#define IDIO_CONSTANT_IDIO_VAL(x)	(((intptr_t) x) >> IDIO_TYPE_CONSTANT_BITS_SHIFT)
#define IDIO_CONSTANT_IDIO(x)		((const IDIO) (((intptr_t) x) << IDIO_TYPE_CONSTANT_BITS_SHIFT | IDIO_TYPE_CONSTANT_IDIO_MARK))
#define IDIO_CONSTANT_TOKEN_VAL(x)	(((intptr_t) x) >> IDIO_TYPE_CONSTANT_BITS_SHIFT)
#define IDIO_CONSTANT_TOKEN(x)		((const IDIO) (((intptr_t) x) << IDIO_TYPE_CONSTANT_BITS_SHIFT | IDIO_TYPE_CONSTANT_TOKEN_MARK))
#define IDIO_CONSTANT_I_CODE_VAL(x)	(((intptr_t) x) >> IDIO_TYPE_CONSTANT_BITS_SHIFT)
#define IDIO_CONSTANT_I_CODE(x)	((const IDIO) (((intptr_t) x) << IDIO_TYPE_CONSTANT_BITS_SHIFT | IDIO_TYPE_CONSTANT_I_CODE_MARK))

#define IDIO_UNICODE_VAL(x)		(((intptr_t) x) >> IDIO_TYPE_CONSTANT_BITS_SHIFT)
#define IDIO_UNICODE(x)			((const IDIO) (((intptr_t) x) << IDIO_TYPE_CONSTANT_BITS_SHIFT | IDIO_TYPE_CONSTANT_UNICODE_MARK))

/*
 * Idio instruction arrays.
 *
 * We generate our byte compiled code into these.  There's often
 * several floating around at once as, in order to determine how far
 * to jump over some not-immediately-relevant code to move onto the
 * next (source code) statement, we need to have already byte compiled
 * the not-immediately-relevant code.  Which we can then copy.
 *
 * XXX These are reference counted as they are used in both codegen.c
 * and vm.c.
 */
typedef struct idio_ia_s {
    int refcnt;
    idio_pc_t asize;		/* alloc()'d */
    idio_pc_t usize;		/* used */
    IDIO_I *ae;
} idio_ia_t;

typedef idio_ia_t* IDIO_IA_T;

#define IDIO_IA_REFCNT(A)	((A)->refcnt)
#define IDIO_IA_ASIZE(A)	((A)->asize)
#define IDIO_IA_USIZE(A)	((A)->usize)
#define IDIO_IA_AE(A,i)		((A)->ae[i])

void idio_gc_register_finalizer (IDIO o, void (*func) (IDIO o));
void idio_gc_deregister_finalizer (IDIO o);
void idio_run_finalizer (IDIO o);
void *idio_alloc (size_t s);
void idio_free (void *p);
void idio_gc_free (void *p, size_t size);
void *idio_realloc (void *p, size_t s);
IDIO idio_gc_get (idio_type_e type);
void idio_gc_alloc (void **p, size_t size);
void *idio_gc_realloc (void *p, size_t size);

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
#define IDIO_GC_REALLOC(p,s)	((p) = idio_gc_realloc ((void *)(p), s))
#define IDIO_GC_FREE(p,s)	(idio_gc_free ((void *)(p), s))

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
void IDIO_FPRINTF (FILE *stream, char const *format, ...);
#else
#define IDIO_FPRINTF(...)	((void) 0)
#endif
void idio_gc_stats_inc (idio_type_e type);
void idio_gc_protect (IDIO o);
void idio_gc_protect_auto (IDIO o);
void idio_gc_expose (IDIO o);
void idio_gc_add_weak_object (IDIO o);
void idio_gc_remove_weak_object (IDIO o);
void idio_gc_possibly_collect ();
#define IDIO_GC_COLLECT_GEN	0
#define IDIO_GC_COLLECT_ALL	1
void idio_gc_collect (idio_gc_t *idio_gc, int gen, char const *caller);
void idio_gc_collect_gen (char const *caller);
void idio_gc_collect_all (char const *caller);
void idio_gc_new_gen ();
int idio_gc_get_pause (char const *caller);
void idio_gc_pause (char const *caller);
void idio_gc_resume (char const *caller);
void idio_gc_reset (char const *caller, int pause);

int idio_vasprintf (char **strp, char const *fmt, va_list ap);
int idio_asprintf(char **strp, char const *fmt, ...);
char *idio_strcat (char *s1, size_t *s1sp, char const *s2, size_t s2s);
char *idio_strcat_free (char *s1, size_t *s1sp, char *s2, size_t s2s);

#define IDIO_STATIC_STR_LEN(s)	(s), sizeof (s) - 1

/*
 * IDIO_STRCAT should only be being used for static C strings for
 * which the longest is something like "\n output_handle=", some 16
 * chars.  The idio_strnlen (..., 20) magic number allows for some
 * growth whilst constraining the length.
 */
#define IDIO_STRCAT(s1,s1sp,s2)		{				\
	(s1) = idio_strcat ((s1), (s1sp), s2, idio_strnlen (s2, 20));	\
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
