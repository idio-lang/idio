/*
 * Copyright (c) 2015 Ian Fitchet <idf(at)idio-lang.org>
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

#define IDIO_TYPE_NONE            0
#define IDIO_TYPE_FIXNUM          1
#define IDIO_TYPE_CONSTANT        2
#define IDIO_TYPE_CHARACTER       3
#define IDIO_TYPE_STRING          4
#define IDIO_TYPE_SUBSTRING       5
#define IDIO_TYPE_SYMBOL          6
#define IDIO_TYPE_PAIR            7
#define IDIO_TYPE_ARRAY           8
#define IDIO_TYPE_HASH            9
#define IDIO_TYPE_CLOSURE         10
#define IDIO_TYPE_PRIMITIVE_C     11
#define IDIO_TYPE_BIGNUM          12
#define IDIO_TYPE_FRAME           13
#define IDIO_TYPE_HANDLE          14
#define IDIO_TYPE_STRUCT_TYPE     15
#define IDIO_TYPE_STRUCT_INSTANCE 16
#define IDIO_TYPE_C_INT8          18
#define IDIO_TYPE_C_UINT8         19
#define IDIO_TYPE_C_INT16         20
#define IDIO_TYPE_C_UINT16        21
#define IDIO_TYPE_C_INT32         22
#define IDIO_TYPE_C_UINT32        23
#define IDIO_TYPE_C_INT64         24
#define IDIO_TYPE_C_UINT64        25
#define IDIO_TYPE_C_FLOAT         26
#define IDIO_TYPE_C_DOUBLE        27
#define IDIO_TYPE_C_POINTER       28
#define IDIO_TYPE_C_VOID          29
#define IDIO_TYPE_C_TYPEDEF       30
#define IDIO_TYPE_C_STRUCT        31
#define IDIO_TYPE_C_INSTANCE      32
#define IDIO_TYPE_C_FFI           33
#define IDIO_TYPE_OPAQUE          34
#define IDIO_TYPE_MAX             35

typedef unsigned char idio_type_e;

#define IDIO_FLAG_NONE		0
#define IDIO_FLAG_GCC_SHIFT	0 /* GC colours -- two bits */
#define IDIO_FLAG_FREE_SHIFT	2 /* debug */
#define IDIO_FLAG_STICKY_SHIFT	3 /* memory pinning */
#define IDIO_FLAG_MACRO_SHIFT	4 /* (closure) is a macro */

#define IDIO_FLAG_GCC_MASK	(3 << IDIO_FLAG_GCC_SHIFT)
#define IDIO_FLAG_GCC_UMASK	(~ IDIO_FLAG_GCC_MASK)
#define IDIO_FLAG_GCC_BLACK	(0 << IDIO_FLAG_GCC_SHIFT)
#define IDIO_FLAG_GCC_DGREY	(1 << IDIO_FLAG_GCC_SHIFT)
#define IDIO_FLAG_GCC_LGREY	(2 << IDIO_FLAG_GCC_SHIFT)
#define IDIO_FLAG_GCC_WHITE	(3 << IDIO_FLAG_GCC_SHIFT)

#define IDIO_FLAG_FREE_MASK	(1 << IDIO_FLAG_FREE_SHIFT)
#define IDIO_FLAG_FREE_UMASK	(~ IDIO_FLAG_FREE_MASK)
#define IDIO_FLAG_NOTFREE	(0 << IDIO_FLAG_FREE_SHIFT)
#define IDIO_FLAG_FREE		(1 << IDIO_FLAG_FREE_SHIFT)

#define IDIO_FLAG_STICKY_MASK	(1 << IDIO_FLAG_STICKY_SHIFT)
#define IDIO_FLAG_STICKY_UMASK	(~ IDIO_FLAG_STICKY_MASK)
#define IDIO_FLAG_NOTSTICKY	(0 << IDIO_FLAG_STICKY_SHIFT)
#define IDIO_FLAG_STICKY	(1 << IDIO_FLAG_STICKY_SHIFT)

#define IDIO_FLAG_MACRO_MASK	(1 << IDIO_FLAG_MACRO_SHIFT)
#define IDIO_FLAG_MACRO_UMASK	(~ IDIO_FLAG_MACRO_MASK)
#define IDIO_FLAG_NOTMACRO	(0 << IDIO_FLAG_MACRO_SHIFT)
#define IDIO_FLAG_MACRO		(1 << IDIO_FLAG_MACRO_SHIFT)

typedef struct idio_string_s {
    size_t blen;		/* bytes */
    char *s;
} idio_string_t;

#define IDIO_STRING_BLEN(S)	((S)->u.string->blen)
#define IDIO_STRING_S(S)	((S)->u.string->s)

typedef struct idio_substring_s {
    /*
     * By rights, we should have a *grey here but we know we point to
     * a simple string and can just mark it as seen directly
     */
    struct idio_s *parent;
    size_t blen;		/* bytes */
    char *s;			/* no allocation, just a pointer into
				   parent's string */
} idio_substring_t;

#define IDIO_SUBSTRING_BLEN(S)	((S)->u.substring->blen)
#define IDIO_SUBSTRING_S(S)	((S)->u.substring->s)
#define IDIO_SUBSTRING_PARENT(S) ((S)->u.substring->parent)

typedef struct idio_symbol_s {
    char *s;			/* C string */
} idio_symbol_t;

#define IDIO_SYMBOL_S(S)	((S)->u.symbol->s)

typedef struct idio_pair_s {
    struct idio_s *grey;
    struct idio_s *h;
    struct idio_s *t;
} idio_pair_t;

#define IDIO_PAIR_GREY(P)	((P)->u.pair->grey)
#define IDIO_PAIR_H(P)		((P)->u.pair->h)
#define IDIO_PAIR_T(P)		((P)->u.pair->t)

typedef ptrdiff_t idio_index_t;

typedef struct idio_array_s {
    struct idio_s *grey;
    idio_index_t asize;		/* allocated size */
    idio_index_t usize;		/* used size */
    struct idio_s* *ae;		/* IDIO *a */
} idio_array_t;

#define IDIO_ARRAY_GREY(A)	((A)->u.array->grey)
#define IDIO_ARRAY_ASIZE(A)	((A)->u.array->asize)
#define IDIO_ARRAY_USIZE(A)	((A)->u.array->usize)
#define IDIO_ARRAY_AE(A,i)	((A)->u.array->ae[i])

typedef struct idio_hash_entry_s {
    struct idio_s *k;
    struct idio_s *v;
    size_t n;			/* next in chain */
} idio_hash_entry_t;

#define IDIO_HASH_FLAG_NONE		0
#define IDIO_HASH_FLAG_STRING_KEYS	(1<<0)

typedef struct idio_hash_s {
    struct idio_s *grey;
    size_t size;		/* nominal hash size */
    size_t mask;		/* bitmask for easy modulo arithmetic */
    int (*equal) (void *k1, void *k2);
    size_t (*hashf) (struct idio_s *h, void *k); /* hashing function */
    idio_hash_entry_t *he;
} idio_hash_t;

#define IDIO_HASH_GREY(H)	((H)->u.hash->grey)
#define IDIO_HASH_SIZE(H)	((H)->u.hash->size)
#define IDIO_HASH_MASK(H)	((H)->u.hash->mask)
#define IDIO_HASH_EQUAL(H)	((H)->u.hash->equal)
#define IDIO_HASH_HASHF(H)	((H)->u.hash->hashf)
#define IDIO_HASH_HE_KEY(H,i)	((H)->u.hash->he[i].k)
#define IDIO_HASH_HE_VALUE(H,i)	((H)->u.hash->he[i].v)
#define IDIO_HASH_HE_NEXT(H,i)	((H)->u.hash->he[i].n)
#define IDIO_HASH_FLAGS(H)	((H)->tflags)

typedef struct idio_closure_s {
    struct idio_s *grey;
    struct idio_s *frame;
    struct idio_s *args;
    struct idio_s *body;
} idio_closure_t;

#define IDIO_CLOSURE_GREY(C)	((C)->u.closure->grey)
#define IDIO_CLOSURE_FRAME(C)	((C)->u.closure->frame)
#define IDIO_CLOSURE_ARGS(C)	((C)->u.closure->args)
#define IDIO_CLOSURE_BODY(C)	((C)->u.closure->body)

typedef struct idio_primitive_C_s {
    struct idio_s *(*f) (struct idio_s *frame, struct idio_s *args);
    char *name;
} idio_primitive_C_t;

#define IDIO_PRIMITIVE_C_F(P)		((P)->u.primitive_C->f)
#define IDIO_PRIMITIVE_C_NAME(P)	((P)->u.primitive_C->name)

#define IDIO_FRAME_FLAG_NONE	 0
#define IDIO_FRAME_FLAG_ETRACE   (1<<0)
#define IDIO_FRAME_FLAG_KTRACE   (1<<1)
#define IDIO_FRAME_FLAG_PROFILE  (1<<2)
#define IDIO_FRAME_FLAG_C_STRUCT (1<<3)

typedef struct idio_frame_s {
    struct idio_s *grey;
    struct idio_s *form;
    struct idio_s *namespace;
    struct idio_s *env;
    struct idio_s *pframe;	/* parent frame */
    struct idio_s *stack;
    struct idio_s *threads;
    struct idio_s *error;
} idio_frame_t;

#define IDIO_FRAME_GREY(F)	((F)->u.frame->grey)
#define IDIO_FRAME_FORM(F)	((F)->u.frame->form)
#define IDIO_FRAME_NAMESPACE(F)	((F)->u.frame->namespace)
#define IDIO_FRAME_ENV(F)	((F)->u.frame->env)
#define IDIO_FRAME_PFRAME(F)	((F)->u.frame->pframe)
#define IDIO_FRAME_STACK(F)	((F)->u.frame->stack)
#define IDIO_FRAME_THREADS(F)	((F)->u.frame->threads)
#define IDIO_FRAME_ERROR(F)	((F)->u.frame->error)
#define IDIO_FRAME_FLAGS(F)	((F)->tflags)

#define IDIO_BIGNUM_FLAG_NONE          (0)
#define IDIO_BIGNUM_FLAG_INTEGER       (1<<0)
#define IDIO_BIGNUM_FLAG_REAL          (1<<1)
#define IDIO_BIGNUM_FLAG_REAL_NEGATIVE (1<<2)
#define IDIO_BIGNUM_FLAG_REAL_INEXACT  (1<<3)
#define IDIO_BIGNUM_FLAG_NAN           (1<<4)

typedef struct idio_bignum_s {
    struct idio_s *grey;
    char *nums;
    int64_t exp;		/* exponent, a raw int64_t */
    struct idio_s *sig;		/* significand, an array of C_int64 */
} idio_bignum_t;

#define IDIO_BIGNUM_GREY(B)  ((B)->u.bignum->grey)
#define IDIO_BIGNUM_NUMS(B)  ((B)->u.bignum->nums)
#define IDIO_BIGNUM_FLAGS(B) ((B)->tflags)
#define IDIO_BIGNUM_EXP(B)   ((B)->u.bignum->exp)
#define IDIO_BIGNUM_SIG(B)   ((B)->u.bignum->sig)

typedef struct idio_handle_methods_s {
    void (*free) (struct idio_s *h);
    int (*readyp) (struct idio_s *h);
    int (*getc) (struct idio_s *h);
    int (*eofp) (struct idio_s *h);
    int (*close) (struct idio_s *h);
    int (*putc) (struct idio_s *h, int c);
    int (*puts) (struct idio_s *h, char *s, size_t l);
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
    void *stream;		/* file/string specific stream data */
    idio_handle_methods_t *methods; /* file/string specific methods */
    int lc;			/* lookahead char */
    off_t line;			/* 1+ */
    off_t pos;			/* position in file: 0+ */
    char *name;			/* filename or some other identifying data */
} idio_handle_t;

#define IDIO_HANDLE_STREAM(H)	((H)->u.handle->stream)
#define IDIO_HANDLE_METHODS(H)	((H)->u.handle->methods)
#define IDIO_HANDLE_LC(H)	((H)->u.handle->lc)
#define IDIO_HANDLE_LINE(H)	((H)->u.handle->line)
#define IDIO_HANDLE_POS(H)	((H)->u.handle->pos)
#define IDIO_HANDLE_NAME(H)	((H)->u.handle->name)
#define IDIO_HANDLE_FLAGS(H)	((H)->tflags)

#define IDIO_HANDLE_INPUTP(H)	(IDIO_HANDLE_FLAGS(H) & IDIO_HANDLE_FLAG_READ)
#define IDIO_HANDLE_OUTPUTP(H)	(IDIO_HANDLE_FLAGS(H) & IDIO_HANDLE_FLAG_WRITE)
#define IDIO_HANDLE_CLOSEDP(H)	(IDIO_HANDLE_FLAGS(H) & IDIO_HANDLE_FLAG_CLOSED)
#define IDIO_HANDLE_FILEP(H)	(IDIO_HANDLE_FLAGS(H) & IDIO_HANDLE_FLAG_FILE)
#define IDIO_HANDLE_STRINGP(H)	(IDIO_HANDLE_FLAGS(H) & IDIO_HANDLE_FLAG_STRING)

#define IDIO_HANDLE_M_FREE(H)	(IDIO_HANDLE_METHODS (H)->free)
#define IDIO_HANDLE_M_READYP(H)	(IDIO_HANDLE_METHODS (H)->readyp)
#define IDIO_HANDLE_M_GETC(H)	(IDIO_HANDLE_METHODS (H)->getc)
#define IDIO_HANDLE_M_EOFP(H)	(IDIO_HANDLE_METHODS (H)->eofp)
#define IDIO_HANDLE_M_CLOSE(H)	(IDIO_HANDLE_METHODS (H)->close)
#define IDIO_HANDLE_M_PUTC(H)	(IDIO_HANDLE_METHODS (H)->putc)
#define IDIO_HANDLE_M_PUTS(H)	(IDIO_HANDLE_METHODS (H)->puts)
#define IDIO_HANDLE_M_FLUSH(H)	(IDIO_HANDLE_METHODS (H)->flush)
#define IDIO_HANDLE_M_SEEK(H)	(IDIO_HANDLE_METHODS (H)->seek)
#define IDIO_HANDLE_M_PRINT(H)	(IDIO_HANDLE_METHODS (H)->print)

typedef struct idio_struct_type_s {
    struct idio_s *grey;
    struct idio_s *name;	/* a string */
    struct idio_s *parent;	/* a struct-type */
    struct idio_s *slots;	/* an array of strings */
} idio_struct_type_t;

#define IDIO_STRUCT_TYPE_GREY(S)	((S)->u.struct_type->grey)
#define IDIO_STRUCT_TYPE_NAME(S)	((S)->u.struct_type->name)
#define IDIO_STRUCT_TYPE_PARENT(S)	((S)->u.struct_type->parent)
#define IDIO_STRUCT_TYPE_SLOTS(S)	((S)->u.struct_type->slots)

typedef struct idio_struct_instance_s {
    struct idio_s *grey;
    struct idio_s *type;	/* a struct-type */
    struct idio_s *slots;	/* an array */
} idio_struct_instance_t;

#define IDIO_STRUCT_INSTANCE_GREY(I)	((I)->u.struct_instance->grey)
#define IDIO_STRUCT_INSTANCE_TYPE(I)	((I)->u.struct_instance->type)
#define IDIO_STRUCT_INSTANCE_SLOTS(I)	((I)->u.struct_instance->slots)

typedef struct idio_C_pointer_s {
    void *p;
    char freep;
} idio_C_pointer_t;

typedef struct idio_C_type_s {
    union {
	int8_t            C_int8;
	uint8_t           C_uint8;
	int16_t           C_int16;
	uint16_t          C_uint16;
	int32_t           C_int32;
	uint32_t          C_uint32;
	int64_t           C_int64;
	uint64_t          C_uint64;
	float             C_float;
	double            C_double;
	idio_C_pointer_t *C_pointer;
    } u;
} idio_C_type_t;

#define IDIO_C_TYPE_INT8(C)          ((C)->u.C_type->u.C_int8)
#define IDIO_C_TYPE_UINT8(C)         ((C)->u.C_type->u.C_uint8)
#define IDIO_C_TYPE_INT16(C)         ((C)->u.C_type->u.C_int16)
#define IDIO_C_TYPE_UINT16(C)        ((C)->u.C_type->u.C_uint16)
#define IDIO_C_TYPE_INT32(C)         ((C)->u.C_type->u.C_int32)
#define IDIO_C_TYPE_UINT32(C)        ((C)->u.C_type->u.C_uint32)
#define IDIO_C_TYPE_INT64(C)         ((C)->u.C_type->u.C_int64)
#define IDIO_C_TYPE_UINT64(C)        ((C)->u.C_type->u.C_uint64)
#define IDIO_C_TYPE_FLOAT(C)         ((C)->u.C_type->u.C_float)
#define IDIO_C_TYPE_DOUBLE(C)        ((C)->u.C_type->u.C_double)
#define IDIO_C_TYPE_POINTER(C)       ((C)->u.C_type->u.C_pointer)
#define IDIO_C_TYPE_POINTER_P(C)     ((C)->u.C_type->u.C_pointer->p)
#define IDIO_C_TYPE_POINTER_FREEP(C) ((C)->u.C_type->u.C_pointer->freep)

typedef struct idio_C_typedef_s {
    struct idio_s *grey;
    struct idio_s *sym;		/* a symbol */
} idio_C_typedef_t;

#define IDIO_C_TYPEDEF_GREY(C) ((C)->u.C_typedef->grey)
#define IDIO_C_TYPEDEF_SYM(C)  ((C)->u.C_typedef->sym)

typedef struct idio_C_struct_s {
    struct idio_s *grey;
    struct idio_s *slots;
    struct idio_s *methods;
    struct idio_s *frame;
    size_t size;
} idio_C_struct_t;

#define IDIO_C_STRUCT_GREY(C)    ((C)->u.C_struct->grey)
#define IDIO_C_STRUCT_SLOTS(C)   ((C)->u.C_struct->slots)
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

typedef struct idio_continuation_s {
    struct idio_s *grey;
    struct idio_s *func;
    struct idio_s *args;
    struct idio_s *frame;
    struct idio_s *k;
} idio_continuation_t;

typedef struct idio_s {
    struct idio_s *next;

    /*
     * The union will be word-aligned (or larger) so we have 4-8 bytes
     * of room for "info"
     */
    idio_type_e type;
    unsigned char flags;	/* generic type flags */
    unsigned char tflags;	/* type-specific flags (since we have
				   room here) */

    union idio_s_u {
	idio_string_t          *string;
	idio_substring_t       *substring;
	idio_symbol_t          *symbol;
	idio_pair_t            *pair;
	idio_array_t           *array;
	idio_hash_t            *hash;
	idio_closure_t         *closure;
	idio_primitive_C_t     *primitive_C;
	idio_bignum_t          *bignum;
	idio_frame_t           *frame;
	idio_handle_t          *handle;
	idio_struct_type_t     *struct_type;
	idio_struct_instance_t *struct_instance;
                               
	idio_C_type_t          *C_type;
                               
	idio_C_typedef_t       *C_typedef;
	idio_C_struct_t        *C_struct;
	idio_C_instance_t      *C_instance;
	idio_C_FFI_t           *C_FFI;
	idio_opaque_t          *opaque;
	idio_continuation_t    *continuation;
    } u;
} idio_t;
					 
typedef idio_t* IDIO;

typedef struct idio_root_s {
    struct idio_root_s *next;
    struct idio_s *object;
} idio_root_t;

typedef struct idio_gc_s {
    struct idio_gc_s *next;
    idio_root_t *roots;
    IDIO namespace;
    IDIO symbols;
    IDIO ports;			/* SCM/S9fES ports */
    IDIO input_port;		/* SCM/S9fES ports */
    IDIO output_port;		/* SCM/S9fES ports */
    IDIO error_port;		/* SCM/S9fES ports */
    IDIO C_typedefs;
    IDIO free;
    IDIO used;
    IDIO grey;
    unsigned int pause;
    unsigned char verbose;
    struct stats {
	unsigned long long tgets[IDIO_TYPE_MAX];
	unsigned long long igets;
	unsigned long long mgets;
	unsigned long long reuse;
	unsigned long long allocs;	/* # allocations */
	unsigned long long tbytes;	/* # bytes ever allocated */
	unsigned long long nbytes;	/* # bytes currently allocated */
	unsigned long long nused[IDIO_TYPE_MAX]; /* per-type usage */
	unsigned long long collections;	/* # times gc has been run */
	unsigned long long bounces;
    }  stats;
} idio_gc_t;

/*
  the alignment of a structure slot of type TYPE can be determined by
  looking at the offset of such a slot in a structure where the first
  element is a single char
*/

#define IDIO_C_STRUCT_ALIGNMENT(TYPE)	offsetof (struct { char __gc1_c_struct_1; TYPE __gc1_c_struct_2; }, __gc1_c_struct_2)

/*
 * Fixnums and small constants

 * On a 32 bit processor a pointer to an Idio type structure will be
 * word-aligned meaning the bottom two bits will always be 00.  We can
 * use this to identify up to three other types which require less
 * than one word of data.

 * The three types are:

 * - fixnums: small integers (for small being at least +/- 2**29)

 * - small constants the user can see and use: #t, #f, #eof etc..
     Negative values cannot be evaluated.

 * - characters: as a distinct type from fixnums to avoid the
     awkwardness of trying to evaluate: 1 + ®

 * All three will then have a minimum of 30 bits.
 */

#define IDIO_TYPE_MASK		0x3

#define IDIO_TYPE_POINTER_MARK	0x00
#define IDIO_TYPE_FIXNUM_MARK	0x01
#define IDIO_TYPE_CONSTANT_MARK	0x02
#define IDIO_TYPE_CHARACTER_MARK	0x03

#define IDIO_TYPE_POINTERP(x)	((((intptr_t) x) & IDIO_TYPE_MASK) == IDIO_TYPE_POINTER_MARK)
#define IDIO_TYPE_FIXNUMP(x)	((((intptr_t) x) & IDIO_TYPE_MASK) == IDIO_TYPE_FIXNUM_MARK)
#define IDIO_TYPE_CONSTANTP(x)	((((intptr_t) x) & IDIO_TYPE_MASK) == IDIO_TYPE_CONSTANT_MARK)
#define IDIO_TYPE_CHARACTERP(x)	((((intptr_t) x) & IDIO_TYPE_MASK) == IDIO_TYPE_CHARACTER_MARK)

#define IDIO_FIXNUM_VAL(x)	(((intptr_t) x) >> 2)
#define IDIO_FIXNUM(x)		((IDIO) ((x) << 2 | IDIO_TYPE_FIXNUM_MARK))
#define IDIO_FIXNUM_MIN		(INTPTR_MIN >> 2)
#define IDIO_FIXNUM_MAX		(INTPTR_MAX >> 2)

#define IDIO_CONSTANT_VAL(x)	(((intptr_t) x) >> 2)
#define IDIO_CONSTANT(x)	((const IDIO) ((x) << 2 | IDIO_TYPE_CONSTANT_MARK))

#define IDIO_CHARACTER_VAL(x)	(((intptr_t) x) >> 2)
#define IDIO_CHARACTER(x)	((const IDIO) (((intptr_t) x) << 2 | IDIO_TYPE_CHARACTER_MARK))

void idio_init_gc ();
void idio_final_gc ();
void idio_register_finalizer (IDIO o, void (*func) (IDIO o));
void idio_deregister_finalizer (IDIO o);
void idio_run_finalizer (IDIO o);
void *idio_alloc (size_t s);
void *idio_realloc (void *p, size_t s);
IDIO idio_gc_get (idio_type_e type);
void idio_gc_alloc (void **p, size_t size);
#define IDIO_GC_ALLOC(p,s)	(idio_gc_alloc ((void **)&(p), s))
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
#ifdef IDIO_DEBUG
void IDIO_FPRINTF (FILE *stream, const char *format, ...);
#endif
void idio_gc_dump ();
void idio_gc_stats_inc (idio_type_e type);
void idio_gc_protect (IDIO o);
void idio_gc_expose (IDIO o);
void idio_gc_expose_all ();
void idio_gc_mark ();
void idio_gc_sweep ();
void idio_gc_collect ();
void idio_gc_pause ();
void idio_gc_resume ();
void idio_gc_free ();

char *idio_strcat (char *s1, const char *s2);
char *idio_strcat_free (char *s1, char *s2);

#define IDIO_STRCAT(s1,s2)	((s1) = idio_strcat ((s1), (s2)))
#define IDIO_STRCAT_FREE(s1,s2)	((s1) = idio_strcat_free ((s1), (s2)))

int idio_gc_verboseness (int n);

/*
  XXX delete me
 */
#define idio_expr_dump(e)	(idio_expr_dump_ ((e), (#e), 1))
#define idio_expr_dumpn(e,d)	(idio_expr_dump_ ((e), (#e), (d)))
void idio_expr_dump_ (IDIO e, const char *en, int depth);

#endif

/* Local Variables: */
/* mode: C/l */
/* coding: utf-8-unix */
/* End: */
