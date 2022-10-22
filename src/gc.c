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
 * gc.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "idio-config.h"

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "bignum.h"
#include "bitset.h"
#include "c-type.h"
#include "closure.h"
#include "continuation.h"
#include "error.h"
#include "evaluate.h"
#include "frame.h"
#include "handle.h"
#include "hash.h"
#include "idio.h"
#include "idio-string.h"
#include "keyword.h"
#include "malloc.h"
#include "module.h"
#include "pair.h"
#include "primitive.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "util.h"
#include "vm.h"
#include "vtable.h"

static idio_gc_t *idio_gc0;
static idio_gc_t *idio_gc = NULL;
static IDIO idio_gc_finalizer_hash = idio_S_nil;

/**
 * idio_alloc() - Idio wrapper to allocate memory
 * @s: size in bytes
 *
 * You would normally call idio_gc_get() when allocating memory for an
 * ``IDIO`` value as it handles garbage collection housekeeping.
 *
 * idio_alloc() would be called for other memory allocations, eg. C
 * strings.
 *
 * If the C macro directive ``IDIO_DEBUG`` is enabled the allocated
 * memory will be initialised to ``0x5e`` -- an arbitrary not-all-ones
 * and not-all-zeroes value.
 *
 * Note that idio_alloc() actually calls malloc(3)!
 *
 * Return:
 * The allocated blob.
 */
void *idio_alloc (size_t const s)
{
#ifdef IDIO_MALLOC
    void *blob = idio_malloc_malloc (s);
#else
    void *blob = malloc (s);
#endif
    if (NULL == blob) {
	idio_error_alloc ("malloc");

	/* notreached */
	return NULL;
    }

#if IDIO_DEBUG
    /*
     * memset to something not all-zeroes and not all-ones to try to
     * catch assumptions about default memory bugs
     *
     * A for allocated
     */
    memset (blob, 0x41, s);
#endif

    return blob;
}

void *idio_realloc (void *p, size_t const s)
{
    void *np;

#ifdef IDIO_MALLOC
    np = idio_malloc_realloc (p, s);
#else
    np = realloc (p, s);
#endif
    if (NULL == np) {
	/*
	 * Test Case: ??
	 */
	idio_error_alloc ("realloc");

	/* notreached */
	return NULL;
    }

    return np;
}

/*
 * XXX has the caller decremented idio_gc->stats.nbytes by calling
 * idio_gc_stats_free()?
 */
void idio_free (void *p)
{
#ifdef IDIO_MALLOC
    idio_malloc_free (p);
#else
    free (p);
#endif
}

void idio_gc_free (void *p, size_t size)
{
    idio_free (p);
    idio_gc_stats_free (size);
}

/**
 * idio_gc_get_alloc() - get or allocate another ``IDIO`` value
 *
 * idio_gc_get_alloc() allocates another IDIO -- or pool thereof --
 * and returns it performing garbage collection housekeeping as it
 * goes.
 */

#define IDIO_GC_ALLOC_POOL	1E3
#define IDIO_GC_ALLOC_MANY	5E5

IDIO idio_gc_get_alloc ()
{
    IDIO o;
    int n;
    IDIO_GC_FLAGS (idio_gc0) |= IDIO_GC_FLAG_REQUEST;
    IDIO p = NULL;
    for (n = 0 ; n < IDIO_GC_ALLOC_POOL; n++) {
	idio_gc->stats.allocs++;
	o = idio_alloc (sizeof (idio_t));
	if (NULL == p) {
	    o->next = idio_gc->free;
	} else {
	    o->next = p;
	}
	p = o;

	idio_gc->stats.nbytes += sizeof (idio_t);
	idio_gc->stats.tbytes += sizeof (idio_t);
    }

    o = p;
    idio_gc->free = o->next;
    idio_gc->stats.nfree += IDIO_GC_ALLOC_POOL - 1;
    return o;
}

/**
 * idio_gc_get() - allocate memory for an Idio value
 * @type: the &typedef idio_type_e type
 *
 * idio_gc_get() finds the next available ``IDIO`` value from the free
 * list or calls idio_gc_get_alloc() to get one performing garbage
 * collection housekeeping.
 *
 * Return:
 * The ``IDIO`` value.
 *
 * Note that you must allocate memory for any data your ``IDIO`` value
 * references.
 */
IDIO idio_gc_get (idio_type_e type)
{
    IDIO_C_ASSERT (type);

    IDIO_C_ASSERT (type > IDIO_TYPE_NONE &&
		   type < IDIO_TYPE_MAX);

    idio_gc->stats.nused[type]++;
    idio_gc->stats.tgets[type]++;

    IDIO o = idio_gc->free;
    if (NULL == o) {
	o = idio_gc_get_alloc ();
    } else {
	idio_gc->stats.nfree--;
	idio_gc->stats.reuse++;
	idio_gc->free = o->next;
    }

    if (idio_gc->stats.igets++ > IDIO_GC_ALLOC_MANY) {
	IDIO_GC_FLAGS (idio_gc0) |= IDIO_GC_FLAG_REQUEST;
    }

    /* assign type late in case we've re-used a previous object */
    o->type      = type;
    o->vtable    = NULL;
    o->gen       = idio_gc->gen;
    o->colour    = IDIO_GC_FLAG_GCC_WHITE;
    o->free      = IDIO_GC_FLAG_NOTFREE;
    o->sticky    = IDIO_GC_FLAG_NOTSTICKY;
    o->finalizer = IDIO_GC_FLAG_NOFINALIZER;
    o->flags     = IDIO_FLAG_NONE;
    o->tflags    = 0;

    IDIO_ASSERT (o);
    if (NULL != idio_gc->used) {
	/*
	 * XXX have seen:
	 *
	 * idio: gc.c:170: idio_gc_get: Assertion `(idio_gc->used)->type' failed.
	 * Aborted
	 *
	 * here.  This might be a bogus assert with type==0 floating about.
	 */
	IDIO_ASSERT (idio_gc->used);
    }
    o->next = idio_gc->used;
    idio_gc->used = o;

    return o;
}

/**
 * idio_gc_alloc() - wrappered by IDIO_GC_ALLOC()
 * @p: pointer to be set
 * @size: size in bytes
 *
 */

void idio_gc_alloc (void **p, size_t const size)
{
    *p = idio_alloc (size);
    idio_gc->stats.nbytes += size;
    idio_gc->stats.tbytes += size;
}

void *idio_gc_realloc (void *p, size_t const size)
{
    idio_gc->stats.nbytes += size;
    idio_gc->stats.tbytes += size;
    return idio_realloc (p, size);
}

IDIO idio_clone_base (IDIO o)
{
    return idio_gc_get (idio_type (o));
}

/**
 * idio_isa() - base function behind idio_isa_X functions
 * @o: the IDIO object
 * @type: the &typedef idio_type_e type
 *
 * Return:
 * boolean
 */
int idio_isa (IDIO o, idio_type_e type)
{
    IDIO_ASSERT (o);
    IDIO_C_ASSERT (type);

    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
	return (IDIO_TYPE_FIXNUM == type);
    case IDIO_TYPE_CONSTANT_MARK:
	{
	    switch ((intptr_t) o & IDIO_TYPE_CONSTANT_MASK) {
	    case IDIO_TYPE_CONSTANT_IDIO_MARK:
		return (IDIO_TYPE_CONSTANT_IDIO == type);
	    case IDIO_TYPE_CONSTANT_TOKEN_MARK:
		return (IDIO_TYPE_CONSTANT_TOKEN == type);
	    case IDIO_TYPE_CONSTANT_I_CODE_MARK:
		return (IDIO_TYPE_CONSTANT_I_CODE == type);
	    case IDIO_TYPE_CONSTANT_UNICODE_MARK:
		return (IDIO_TYPE_CONSTANT_UNICODE == type);
	    default:
		/* inconceivable! */
		idio_error_printf (IDIO_C_FUNC_LOCATION_S ("CONSTANT"), "unexpected object mark type %#x", o);

		/* notreached */
		return 0;
	    }
	}
    case IDIO_TYPE_PLACEHOLDER_MARK:
	return (IDIO_TYPE_PLACEHOLDER == type);
    case IDIO_TYPE_POINTER_MARK:
	return (o->type == type);
    default:
	/* inconceivable! */
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected object mark type %#x", o);

	/* notreached */
	return 0;
    }
}

void idio_gc_stats_free (size_t const n)
{
    idio_gc->stats.nbytes -= n;
}

void idio_gc_register_finalizer (IDIO o, void (*func) (IDIO o))
{
    IDIO_ASSERT (o);
    IDIO_C_ASSERT (func);

    IDIO ofunc = idio_C_pointer (func);

    idio_hash_put (idio_gc_finalizer_hash, o, ofunc);
    o->finalizer = IDIO_GC_FLAG_FINALIZER;
}

void idio_gc_deregister_finalizer (IDIO o)
{
    IDIO_ASSERT (o);

    if (IDIO_GC_FLAG_FINALIZER == o->finalizer) {
	o->finalizer = IDIO_GC_FLAG_NOFINALIZER;
    } else {
	/* fprintf (stderr, "final del: already done?\n"); */
	/* idio_dump (idio_gc_finalizer_hash, 2); */
    }
}

static void idio_gc_finalizer_run (IDIO o)
{
    IDIO_ASSERT (o);

    if (idio_S_nil == o) {
	return;
    }

    if (idio_S_nil == idio_gc_finalizer_hash) {
	return;
    }

    IDIO ofunc = idio_hash_ref (idio_gc_finalizer_hash, o);
    if (idio_S_unspec != ofunc) {
	IDIO_TYPE_ASSERT (C_pointer, ofunc);

	void (*p) (IDIO o) = IDIO_C_TYPE_POINTER_P (ofunc);
	(*p) (o);

	idio_gc_deregister_finalizer (o);
    } else {
	fprintf (stderr, "gc-finalizer-run: no finalizer for %10p? (%s)\n", o, idio_type2string (o));
	idio_dump (idio_gc_finalizer_hash, 2);
	exit (4);
    }
}

void idio_gc_gcc_mark (idio_gc_t *gc, IDIO o, idio_gc_flag_gcc_enum colour)
{
    /* IDIO_ASSERT (o); */
    switch ((uintptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_PLACEHOLDER_MARK:
	return;
    case IDIO_TYPE_POINTER_MARK:
	if (IDIO_TYPE_NONE == o->type) {
	    fprintf (stderr, "ig_mark: bad type %p\n", o);
	    o->type = IDIO_TYPE_STRING;
	    idio_debug ("(string) o = %s\n", o);
	    o->type = IDIO_TYPE_NONE;
	    return;
	}
	break;
    default:
	/* inconceivable! */
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected object mark type %#x", o);

	/* notreached */
	return;
    }

    if (IDIO_GC_FLAG_FREE == o->free) {
	fprintf (stderr, "idio_gc_gcc_mark: already free?: ");
	gc->verbose++;
	idio_dump (o, 1);
	gc->verbose--;
	fprintf (stderr, "\n");
    }

    switch (colour) {
    case IDIO_GC_FLAG_GCC_WHITE:
	o->colour = colour;
	break;
    case IDIO_GC_FLAG_GCC_BLACK:
	if (IDIO_GC_FLAG_GCC_BLACK == o->colour) {
	    break;
	}
	if (IDIO_GC_FLAG_GCC_LGREY == o->colour) {
	    break;
	}

	switch (o->type) {
	case IDIO_TYPE_NONE:
	    IDIO_C_ASSERT (0);
	    idio_coding_error_C ("idio_gc_gcc_mark cannot process an IDIO_TYPE_NONE", IDIO_LIST1 (o), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	    break;
	case IDIO_TYPE_SUBSTRING:
	    o->colour = colour;
	    idio_gc_gcc_mark (gc, IDIO_SUBSTRING_PARENT (o), colour);
	    break;
	case IDIO_TYPE_PAIR:
	    o->colour = IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_PAIR_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_ARRAY:
	    o->colour = IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_ARRAY_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_HASH:
	    o->colour = IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_HASH_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_CLOSURE:
	    o->colour = IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_CLOSURE_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_PRIMITIVE:
	    o->colour = IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_PRIMITIVE_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_MODULE:
	    IDIO_C_ASSERT (IDIO_MODULE_GREY (o) != o);
	    IDIO_C_ASSERT (gc->grey != o);
	    o->colour = IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_MODULE_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_FRAME:
	    IDIO_C_ASSERT (IDIO_FRAME_GREY (o) != o);
	    IDIO_C_ASSERT (gc->grey != o);
	    o->colour = IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_FRAME_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_HANDLE:
	    IDIO_C_ASSERT (IDIO_HANDLE_GREY (o) != o);
	    IDIO_C_ASSERT (gc->grey != o);
	    o->colour = IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_HANDLE_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_STRUCT_TYPE:
	    IDIO_C_ASSERT (IDIO_STRUCT_TYPE_GREY (o) != o);
	    IDIO_C_ASSERT (gc->grey != o);
	    o->colour = IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_STRUCT_TYPE_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_STRUCT_INSTANCE:
	    IDIO_C_ASSERT (IDIO_STRUCT_INSTANCE_GREY (o) != o);
	    IDIO_C_ASSERT (gc->grey != o);
	    o->colour = IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_STRUCT_INSTANCE_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_THREAD:
	    IDIO_C_ASSERT (IDIO_THREAD_GREY (o) != o);
	    IDIO_C_ASSERT (gc->grey != o);
	    o->colour = IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_THREAD_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_CONTINUATION:
	    IDIO_C_ASSERT (IDIO_CONTINUATION_GREY (o) != o);
	    IDIO_C_ASSERT (gc->grey != o);
	    o->colour = IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_CONTINUATION_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	default:
	    /*
	     * All other (non-compound) types just have the colour set
	     */
	    o->colour = colour;
	    break;
	}
	break;
    default:
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected colour %d", colour);

	/* notreached */
	return;
	break;
    }
}

void idio_gc_process_grey (idio_gc_t *gc, unsigned colour)
{

    IDIO o = gc->grey;

    if (NULL == o) {
	return;
    }

    size_t i;

    o->colour = IDIO_GC_FLAG_GCC_BLACK;

    switch (o->type) {
    case IDIO_TYPE_PAIR:
	gc->grey = IDIO_PAIR_GREY (o);
	idio_gc_gcc_mark (gc, IDIO_PAIR_H (o), colour);
	idio_gc_gcc_mark (gc, IDIO_PAIR_T (o), colour);
	break;
    case IDIO_TYPE_ARRAY:
	gc->grey = IDIO_ARRAY_GREY (o);
	idio_gc_gcc_mark (gc, IDIO_ARRAY_DV (o), colour);
	for (i = 0; i < IDIO_ARRAY_USIZE (o); i++) {
	    if (NULL != IDIO_ARRAY_AE (o, i)) {
		idio_gc_gcc_mark (gc, IDIO_ARRAY_AE (o, i), colour);
	    }
	}
	break;
    case IDIO_TYPE_HASH:
	gc->grey = IDIO_HASH_GREY (o);
	for (i = 0; i < IDIO_HASH_SIZE (o); i++) {
	    idio_hash_entry_t *he = IDIO_HASH_HA (o, i);
	    for ( ; NULL != he; he = IDIO_HASH_HE_NEXT (he)) {
		if (!(IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_STRING_KEYS)) {
		    IDIO k = IDIO_HASH_HE_KEY (he);
		    if (idio_S_nil != k) {
			idio_gc_gcc_mark (gc, k, colour);
		    }
		}
		IDIO v = IDIO_HASH_HE_VALUE (he);
		if (idio_S_nil != v) {
		    idio_gc_gcc_mark (gc, v, colour);
		}
	    }
	}
	idio_gc_gcc_mark (gc, IDIO_HASH_COMP (o), colour);
	idio_gc_gcc_mark (gc, IDIO_HASH_HASH (o), colour);
	break;
    case IDIO_TYPE_CLOSURE:
	gc->grey = IDIO_CLOSURE_GREY (o);
	idio_gc_gcc_mark (gc, IDIO_CLOSURE_FRAME (o), colour);
	idio_gc_gcc_mark (gc, IDIO_CLOSURE_ENV (o), colour);
	break;
    case IDIO_TYPE_PRIMITIVE:
	gc->grey = IDIO_PRIMITIVE_GREY (o);
	break;
    case IDIO_TYPE_MODULE:
	IDIO_C_ASSERT (gc->grey != IDIO_MODULE_GREY (o));
	gc->grey = IDIO_MODULE_GREY (o);
	idio_gc_gcc_mark (gc, IDIO_MODULE_NAME (o), colour);
	idio_gc_gcc_mark (gc, IDIO_MODULE_EXPORTS (o), colour);
	idio_gc_gcc_mark (gc, IDIO_MODULE_IMPORTS (o), colour);
	idio_gc_gcc_mark (gc, IDIO_MODULE_SYMBOLS (o), colour);
	break;
    case IDIO_TYPE_FRAME:
	IDIO_C_ASSERT (gc->grey != IDIO_FRAME_GREY (o));
	gc->grey = IDIO_FRAME_GREY (o);
	idio_gc_gcc_mark (gc, IDIO_FRAME_NEXT (o), colour);
	idio_gc_gcc_mark (gc, IDIO_FRAME_NAMES (o), colour);
	for (i = 0; i < (size_t) IDIO_FRAME_NALLOC (o); i++) {
	    idio_gc_gcc_mark (gc, IDIO_FRAME_ARGS (o, i), colour);
	}
	break;
    case IDIO_TYPE_HANDLE:
	IDIO_C_ASSERT (gc->grey != IDIO_HANDLE_GREY (o));
	gc->grey = IDIO_HANDLE_GREY (o);
	idio_gc_gcc_mark (gc, IDIO_HANDLE_FILENAME (o), colour);
	idio_gc_gcc_mark (gc, IDIO_HANDLE_PATHNAME (o), colour);
	break;
    case IDIO_TYPE_STRUCT_TYPE:
	IDIO_C_ASSERT (gc->grey != IDIO_STRUCT_TYPE_GREY (o));
	gc->grey = IDIO_STRUCT_TYPE_GREY (o);
	idio_gc_gcc_mark (gc, IDIO_STRUCT_TYPE_NAME (o), colour);
	idio_gc_gcc_mark (gc, IDIO_STRUCT_TYPE_PARENT (o), colour);
	{
	    size_t size = IDIO_STRUCT_TYPE_SIZE (o);
	    size_t i;
	    for (i = 0; i < size ; i++) {
		idio_gc_gcc_mark (gc, IDIO_STRUCT_TYPE_FIELDS (o, i), colour);
	    }
	}
	break;
    case IDIO_TYPE_STRUCT_INSTANCE:
	IDIO_C_ASSERT (gc->grey != IDIO_STRUCT_INSTANCE_GREY (o));
	gc->grey = IDIO_STRUCT_INSTANCE_GREY (o);
	idio_gc_gcc_mark (gc, IDIO_STRUCT_INSTANCE_TYPE (o), colour);
	{
	    size_t size = IDIO_STRUCT_INSTANCE_SIZE (o);
	    size_t i;
	    for (i = 0; i < size; i++) {
		idio_gc_gcc_mark (gc, IDIO_STRUCT_INSTANCE_FIELDS (o, i), colour);
	    }
	}
	break;
    case IDIO_TYPE_THREAD:
	IDIO_C_ASSERT (gc->grey != IDIO_THREAD_GREY (o));
	gc->grey = IDIO_THREAD_GREY (o);
	idio_gc_gcc_mark (gc, IDIO_THREAD_STACK (o), colour);
	idio_gc_gcc_mark (gc, IDIO_THREAD_VAL (o), colour);
	idio_gc_gcc_mark (gc, IDIO_THREAD_FRAME (o), colour);
	idio_gc_gcc_mark (gc, IDIO_THREAD_ENV (o), colour);
	idio_gc_gcc_mark (gc, IDIO_THREAD_FUNC (o), colour);
	idio_gc_gcc_mark (gc, IDIO_THREAD_REG1 (o), colour);
	idio_gc_gcc_mark (gc, IDIO_THREAD_REG2 (o), colour);
	idio_gc_gcc_mark (gc, IDIO_THREAD_EXPR (o), colour);
	idio_gc_gcc_mark (gc, IDIO_THREAD_INPUT_HANDLE (o), colour);
	idio_gc_gcc_mark (gc, IDIO_THREAD_OUTPUT_HANDLE (o), colour);
	idio_gc_gcc_mark (gc, IDIO_THREAD_ERROR_HANDLE (o), colour);
	idio_gc_gcc_mark (gc, IDIO_THREAD_MODULE (o), colour);
	idio_gc_gcc_mark (gc, IDIO_THREAD_HOLES (o), colour);
	break;
    case IDIO_TYPE_CONTINUATION:
	IDIO_C_ASSERT (gc->grey != IDIO_CONTINUATION_GREY (o));
	gc->grey = IDIO_CONTINUATION_GREY (o);
	idio_gc_gcc_mark (gc, IDIO_CONTINUATION_STACK (o), colour);
	idio_gc_gcc_mark (gc, IDIO_CONTINUATION_FRAME (o), colour);
	idio_gc_gcc_mark (gc, IDIO_CONTINUATION_ENV (o), colour);
#ifdef IDIO_CONTINUATION_HANDLES
	idio_gc_gcc_mark (gc, IDIO_CONTINUATION_INPUT_HANDLE (o), colour);
	idio_gc_gcc_mark (gc, IDIO_CONTINUATION_OUTPUT_HANDLE (o), colour);
	idio_gc_gcc_mark (gc, IDIO_CONTINUATION_ERROR_HANDLE (o), colour);
#endif
	idio_gc_gcc_mark (gc, IDIO_CONTINUATION_MODULE (o), colour);
	idio_gc_gcc_mark (gc, IDIO_CONTINUATION_HOLES (o), colour);
	idio_gc_gcc_mark (gc, IDIO_CONTINUATION_THR (o), colour);
	break;
    case IDIO_TYPE_C_POINTER:
	idio_gc_gcc_mark (gc, IDIO_C_TYPE_POINTER_PTYPE (o), colour);
	break;
    default:
	idio_coding_error_C ("unexpected type", o, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
	break;
    }
}

idio_root_t *idio_gc_new_root ()
{

    idio_root_t *r = idio_alloc (sizeof (idio_root_t));
    r->next = idio_gc->roots;
    idio_gc->roots = r;
    r->object = idio_S_nil;
    return r;
}

idio_root_t *idio_gc_new_auto ()
{

    idio_root_t *r = idio_alloc (sizeof (idio_root_t));
    r->next = idio_gc->autos;
    idio_gc->autos = r;
    r->object = idio_S_nil;
    return r;
}

void idio_gc_dump_root (idio_root_t *root)
{
    IDIO_C_ASSERT (root);
}

void idio_gc_gcc_mark_root (idio_gc_t *gc, idio_root_t *r, unsigned colour)
{
    IDIO_C_ASSERT (r);

    idio_gc_gcc_mark (gc, r->object, colour);
}

idio_gc_t *idio_gc_obj_new ()
{
    idio_gc_t *r = idio_gc;
    if (NULL != r) {
	while (NULL != r->next) {
	    r = r->next;
	}
    }

    idio_gc_t *c = idio_alloc (sizeof (idio_gc_t));
    c->next = NULL;
    c->roots = NULL;
    c->autos = NULL;
    c->free = NULL;
    c->used = NULL;
    c->grey = NULL;
    c->weak = NULL;
    c->pause = 0;
    c->verbose = 0;
    if (NULL == r) {
	c->gen = 1;
    } else {
	c->gen = r->gen + 1;
    }
    IDIO_GC_FLAGS (c) = IDIO_GC_FLAG_NONE;

    c->stats.nfree = 0;
    int i;
    for (i = 0; i < IDIO_TYPE_MAX; i++) {
	c->stats.tgets[i] = 0;
    }
    c->stats.igets = 0;
    c->stats.mgets = 0;
    c->stats.reuse = 0;
    c->stats.allocs = 0;
    c->stats.tbytes = 0;
    c->stats.nbytes = 0;
    for (i = 0; i < IDIO_TYPE_MAX; i++) {
	c->stats.nused[i] = 0;
    }
    c->stats.collections = 0;
    c->stats.bounces = 0;
    c->stats.mark_dur.tv_sec = 0;
    c->stats.mark_dur.tv_usec = 0;
    c->stats.sweep_dur.tv_sec = 0;
    c->stats.sweep_dur.tv_usec = 0;
    c->stats.ru_utime.tv_sec = 0;
    c->stats.ru_utime.tv_usec = 0;
    c->stats.ru_stime.tv_sec = 0;
    c->stats.ru_stime.tv_usec = 0;

    if (NULL != r) {
	r->next = c;
    }

    return c;
}

void idio_gc_new_gen ()
{
    idio_gc = idio_gc_obj_new ();
}

#if IDIO_DEBUG > 2
/*
 * http://c-faq.com/varargs/handoff.html
 */
void IDIO_GC_VFPRINTF (FILE *stream, char const *format, va_list argp)
{
    IDIO_C_ASSERT (stream);
    IDIO_C_ASSERT (format);

    /*
     * Raspbian's cc says: error: used struct type value where scalar is required
     */
    /* IDIO_C_ASSERT (argp); */

    if (idio_gc->verbose > 2) {
	vfprintf (stream, format, argp);
    }
}

void IDIO_FPRINTF (FILE *stream, char const *format, ...)
{
    IDIO_C_ASSERT (stream);
    IDIO_C_ASSERT (format);

    va_list fmt_args;
    va_start (fmt_args, format);
    IDIO_GC_VFPRINTF (stream, format, fmt_args);
    va_end (fmt_args);
}
#endif

void idio_gc_dump ()
{
    idio_gc_t *gc = idio_gc0;

    while (NULL != gc) {
	size_t n = 0;
	idio_root_t *r = gc->roots;
	while (NULL != r) {
	    n++;
	    idio_gc_dump_root (r);
	    r = r->next;
	}
	if (n) {
	    fprintf (stderr, "idio_gc_dump: %" PRIdPTR " roots\n", n);
	}

	IDIO o = gc->free;
	n = 0;
	while (NULL != o) {
	    /*
	      Can't actually dump the free objects as the code to print
	      objects out asserts whether they are free or not...

	      However, walking down the chain checks the chain is valid.
	    */
	    o = o->next;
	    n++;
	}
	IDIO_C_ASSERT (n == (size_t) gc->stats.nfree);

	o = gc->used;
	n = 0;
	if (NULL != o) {
	    fprintf (stderr, "idio_gc_dump: used list\n");
	    while (NULL != o) {
		IDIO_ASSERT (o);
		if (n < 10) {
		    idio_dump (o, 0);
		}
		o = o->next;
		n++;
	    }
	    if (n >= 10) {
		fprintf (stderr, "... +%zd more\n", n - 10);
	    }
	}
	gc = gc->next;
    }
}

/**
 * idio_gc_protect() - protect an ``IDIO`` value from the garbage collector
 * @o: the ``IDIO`` value
 *
 * If you've allocated an ``IDIO`` value and are returning it back to
 * Idio then you **MUST NOT** use this function.  Idio will or will
 * not preserve the value depending on how the value is used in Idio.
 *
 * However, if you've allocated an ``IDIO`` value and intend to pass
 * control back to Idio with the intention that it return control to
 * you then you **must** call idio_gc_protect() otherwise the garbage
 * collector may have de-allocated your value before you try to use
 * it.  These tend to be in idio_init_X functions in X.c and before
 * calls to idio_vm_run().
 *
 * This is normally used to protect (hidden) lists of ``IDIO`` values,
 * eg. the symbol table.  The ``IDIO`` value that is the symbol table,
 * a *hash*, probably, isn't accessible from Idio itself but is
 * otherwise a regular value.
 *
 * See also idio_gc_expose().
 */
void idio_gc_protect (IDIO o)
{
    IDIO_ASSERT (o);

    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_POINTER_MARK:
	break;
    default:
	/* nothing to protect! */
	return;
    }

    idio_root_t *r = idio_gc->roots;
    while (NULL != r) {
	if (o == r->object) {
	    fprintf (stderr, "root dupe %p\n", o);
	    return;
	} else if (idio_S_nil == r->object) {
	    r->object = o;
	    return;
	}
	r = r->next;
    }

    r = idio_gc_new_root ();
    r->object = o;
}

/*
 * idio_gc_protect_auto - protect C auto objects
 * @o: object to protect
 *
 * Many C-created IDIO objects have global names for which an explicit
 * idio_gc_expose() can be called.
 *
 * C auto objects (eg. signal condition types) can be protected here
 * and will be exposed during GC shutdown.
 *
 * You should only be calling this for objects which are expected to
 * live for the lifetime of the process.
 *
 * Return:
 * void
 */
void idio_gc_protect_auto (IDIO o)
{
    IDIO_ASSERT (o);

    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_POINTER_MARK:
	break;
    default:
	/* nothing to protect! */
	return;
    }

    idio_root_t *a = idio_gc->autos;
    while (NULL != a) {
	if (o == a->object) {
	    fprintf (stderr, "auto dupe %p\n", o);
	    return;
	} else if (idio_S_nil == a->object) {
	    a->object = o;
	    return;
	}
	a = a->next;
    }

    a = idio_gc_new_auto ();
    a->object = o;
}

/**
 * idio_gc_expose() - expose an ``IDIO`` value that was previously protected
 * @o: the ``IDIO`` value
 *
 * This is normally used to expose the ``IDIO`` values protected
 * during startup and called from the corresponding idio_final_X
 * functions in X.c
 *
 * See also idio_gc_protect().
 */
void idio_gc_expose (IDIO o)
{
    IDIO_ASSERT (o);

    switch ((intptr_t) o & IDIO_TYPE_MASK) {
    case IDIO_TYPE_POINTER_MARK:
	break;
    default:
	/* nothing to expose! */
	return;
    }

    int seen = 0;
    idio_gc_t *gc = idio_gc0;
    idio_root_t *r;
    while (! seen &&
	   NULL != gc) {
	r = gc->roots;
	idio_root_t *p = NULL;
	while (NULL != r) {
	    if (o == r->object) {
		seen = 1;
		if (p) {
		    p->next = r->next;
		} else {
		    gc->roots = r->next;
		}
		idio_free (r);
		break;
	    } else {
		p = r;
	    }
	    r = r->next;
	}
	if (seen) {
	    break;
	} else {
	    gc = gc->next;
	}
    }

    if (0 == seen) {
	fprintf (stderr, "idio_gc_expose: o %10p not previously protected\n", o);
	idio_debug ("o = %s\n", o);

	fprintf (stderr, "idio_gc_expose: #%d currently protected:\n", gc->gen);
	gc = idio_gc0;
	while (NULL != gc) {
	    r = gc->roots;
	    while (NULL != r) {
		fprintf (stderr, " %d %10p %s\n", gc->gen, r->object, idio_type2string (r->object));
		r = r->next;
	    }
	    gc = gc->next;
	}
	IDIO_C_ASSERT (seen);
	return;
    }

    r = gc->roots;
    while (NULL != r) {
	idio_gc_dump_root (r);
	r = r->next;
    }
}

void idio_gc_expose_roots ()
{
    idio_gc_t *gc = idio_gc0;
    while (NULL != gc) {
#ifdef IDIO_GC_DEBUG
	fprintf (stderr, "gc-expose-roots #%d\n", gc->gen);
#endif
	idio_root_t *r = gc->roots;
#ifdef IDIO_GC_DEBUG
	size_t rc = 0;
#endif
	while (NULL != r) {
	    /*
	     * Calling idio_dump (r->object, 1); on shutdown
	     * is...unwise.  Ultimately, you may end up dumping a
	     * struct-instance, say, which may require running the VM
	     * for its as-string function.
	     *
	     * Instead, just a quick note.
	     */
#ifdef IDIO_GC_DEBUG
	    fprintf (stderr, "gc-expose-roots: %10p %-15s ", r->object, idio_type2string (r->object));
	    switch (idio_type (r->object)) {
	    case IDIO_TYPE_STRUCT_INSTANCE:
		{
		    IDIO sit = IDIO_STRUCT_INSTANCE_TYPE (r->object);
		    idio_debug ("%s", IDIO_STRUCT_TYPE_NAME (sit));
		    if (idio_struct_type_isa (sit, idio_evaluate_eenv_type)) {
			idio_debug (" %s", idio_struct_instance_ref_direct(r->object, IDIO_EENV_ST_DESC));
		    }
		}
		break;
	    }
	    fprintf (stderr, "\n");
	    rc++;
#endif
	    idio_root_t *n = r->next;
	    idio_free (r);
	    r = n;
	}

#ifdef IDIO_GC_DEBUG
	if (rc) {
	    fprintf (stderr, "gc-expose-roots #%d: for %zd roots\n", gc->gen, rc);
	}
#endif

	gc->roots = NULL;
	gc = gc->next;
    }
}

void idio_gc_expose_autos ()
{
    idio_gc_t *gc = idio_gc0;
    while (NULL != gc) {
	idio_root_t *a = gc->autos;
	while (NULL != a) {
	    idio_root_t *n = a->next;
	    idio_free (a);
	    a = n;
	}
	gc->autos = NULL;
	gc = gc->next;
    }
}

void idio_gc_add_weak_object (IDIO o)
{
    IDIO_ASSERT (o);

    IDIO_TYPE_ASSERT (hash, o);

    if (idio_gc->used == o) {
	idio_gc->used = o->next;
    } else {
	IDIO p = idio_gc->used;
	while (NULL != p &&
	       p->next != o) {
	    p = p->next;
	}
	if (NULL != p) {
	    p->next = o->next;
	} else {
	    fprintf (stderr, "add-weak-obj: %p not on used list?\n", o);
	    abort ();
	}
    }

    o->next = idio_gc->weak;
    idio_gc->weak = o;
}

void idio_gc_remove_weak_object (IDIO o)
{
    IDIO_ASSERT (o);

    IDIO_TYPE_ASSERT (hash, o);

    idio_gc_t *gc = idio_gc0;
    while (NULL != gc) {
	if (gc->weak == o) {
	    gc->weak = o->next;
	    break;
	} else {
	    IDIO p = gc->weak;
	    while (NULL != p &&
		   p->next != o) {
		p = p->next;
	    }
	    if (NULL != p) {
		p->next = o->next;
		break;
	    }
	}
	gc = gc->next;
    }

    if (NULL != gc) {
	o->next = gc->used;
	gc->used = o;
    } else {
	fprintf (stderr, "remove weak obj: NULL?\n");
	abort ();
    }
}

/*
 * This follows in the style of "Implementation - Second try" of Bruno
 * Haible's (commonly referenced) paper:
 * https://www.haible.de/bruno/papers/cs/weak/WeakDatastructures-writeup.html
 *
 * The third, more efficient try, requires "a bit more effort" and
 * only affects large numbers of weak objects.
 *
 * We can review if it proves to be a problem.
 */
void idio_gc_mark_weak (idio_gc_t *gc)
{
    /*
     * Weak hash table values only get marked if the key has been
     * marked in the regular mark phase.
     *
     * Pass 1: loop until we stop marking values because the key was
     * marked -- we loop partly as the value (of a weak key) or its
     * descendents could be a weak key itself.
     */
    IDIO o = gc->weak;
    int modified = 1;
    while (modified) {
	modified = 0;
	while (NULL != o) {
	    switch ((uintptr_t) o & IDIO_TYPE_MASK) {
	    case IDIO_TYPE_FIXNUM_MARK:
	    case IDIO_TYPE_CONSTANT_MARK:
	    case IDIO_TYPE_PLACEHOLDER_MARK:
		break;
	    case IDIO_TYPE_POINTER_MARK:
		switch (o->type) {
		case IDIO_TYPE_HASH:
		    {
			idio_hi_t i;
			for (i = 0; i < IDIO_HASH_SIZE (o); i++) {
			    idio_hash_entry_t *he = IDIO_HASH_HA (o, i);
			    for ( ; NULL != he; he = IDIO_HASH_HE_NEXT (he)) {
				IDIO k = IDIO_HASH_HE_KEY (he);
				switch ((uintptr_t) k & IDIO_TYPE_MASK) {
				case IDIO_TYPE_FIXNUM_MARK:
				case IDIO_TYPE_CONSTANT_MARK:
				case IDIO_TYPE_PLACEHOLDER_MARK:
				    break;
				case IDIO_TYPE_POINTER_MARK:
				    if (IDIO_GC_FLAG_GCC_BLACK == k->colour) {
					idio_gc_gcc_mark (gc, IDIO_HASH_HE_VALUE (he), IDIO_GC_FLAG_GCC_BLACK);
					modified++;
				    }
				    break;
				default:
				    /* inconceivable! */
				    idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected weak key type %#x", k);

				    /* notreached */
				    return;
				}
			    }
			}
		    }
		    break;
		default:
		    /* inconceivable! */
		    idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected weak object type %#x", o);

		    /* notreached */
		    return;
		}
		break;
	    default:
		/* inconceivable! */
		idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected weak object type %#x", o);

		/* notreached */
		return;
	    }

	    o = o->next;
	}

	/*
	 * (Re-)processing the grey list seems like a good idea at
	 * this point to flush through the values -- which might be
	 * keys elsewhere.
	 */
	while (NULL != gc->grey) {
	    idio_gc_process_grey (gc, IDIO_GC_FLAG_GCC_BLACK);
	}
    }

    /*
     * Pass 2: ditch unmarked key/value pairs
     */
    o = gc->weak;
    while (NULL != o) {
	switch ((uintptr_t) o & IDIO_TYPE_MASK) {
	case IDIO_TYPE_FIXNUM_MARK:
	case IDIO_TYPE_CONSTANT_MARK:
	case IDIO_TYPE_PLACEHOLDER_MARK:
	    break;
	case IDIO_TYPE_POINTER_MARK:
	    switch (o->type) {
	    case IDIO_TYPE_HASH:
		{
		    idio_hi_t i;
		    for (i = 0; i < IDIO_HASH_SIZE (o); i++) {
			idio_hash_entry_t *hel = IDIO_HASH_HA (o, i);
			idio_hash_entry_t *he = hel;

			while (NULL != he) {
			    idio_hash_entry_t *hen = IDIO_HASH_HE_NEXT (he);
			    IDIO k = IDIO_HASH_HE_KEY (he);
			    switch ((uintptr_t) k & IDIO_TYPE_MASK) {
			    case IDIO_TYPE_FIXNUM_MARK:
			    case IDIO_TYPE_CONSTANT_MARK:
			    case IDIO_TYPE_PLACEHOLDER_MARK:
				break;
			    case IDIO_TYPE_POINTER_MARK:
				if (IDIO_GC_FLAG_GCC_BLACK == k->colour) {
				    idio_gc_gcc_mark (gc, IDIO_HASH_HE_VALUE (he), IDIO_GC_FLAG_GCC_BLACK);
				} else {
				    if (IDIO_GC_FLAG_FINALIZER == k->finalizer) {
					idio_gc_finalizer_run (k);
				    }

				    if (he == hel) {
					hel = IDIO_HASH_HE_NEXT (he);
					IDIO_HASH_HA (o, i) = hel;
					hen = hel;
				    } else {
					idio_hash_entry_t *hep = hel;
					while (IDIO_HASH_HE_NEXT (hep) != he) {
					    hep = IDIO_HASH_HE_NEXT (hep);
					}
					IDIO_HASH_HE_NEXT (hep) = hen;
				    }

				    IDIO_GC_FREE (he, sizeof (idio_hash_entry_t));
				    IDIO_HASH_COUNT (o) -= 1;
				}
				break;
			    default:
				/* inconceivable! */
				idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected weak key type %#x", k);

				/* notreached */
				return;
			    }

			    he = hen;
			}
		    }
		}
		break;
	    default:
		/* inconceivable! */
		idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected weak object type %#x", o);

		/* notreached */
		return;
	    }
	    break;
	default:
	    /* inconceivable! */
	    idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected weak object type %#x", o);

	    /* notreached */
	    return;
	}

	o = o->next;
    }

    while (NULL != gc->grey) {
	idio_gc_process_grey (gc, IDIO_GC_FLAG_GCC_BLACK);
    }

#ifdef IDIO_DEBUG
    /*
     * Pass 3: verify - because I'm of a nervous disposition
     */
    int lost = 0;
    o = gc->weak;
    while (NULL != o) {
	switch ((uintptr_t) o & IDIO_TYPE_MASK) {
	case IDIO_TYPE_FIXNUM_MARK:
	case IDIO_TYPE_CONSTANT_MARK:
	case IDIO_TYPE_PLACEHOLDER_MARK:
	    break;
	case IDIO_TYPE_POINTER_MARK:
	    switch (o->type) {
	    case IDIO_TYPE_HASH:
		{
		    idio_hi_t i;
		    for (i = 0; i < IDIO_HASH_SIZE (o); i++) {
			idio_hash_entry_t *hel = IDIO_HASH_HA (o, i);
			idio_hash_entry_t *he = hel;
			for ( ; NULL != he; he = IDIO_HASH_HE_NEXT (he)) {
			    IDIO k = IDIO_HASH_HE_KEY (he);
			    switch ((uintptr_t) k & IDIO_TYPE_MASK) {
			    case IDIO_TYPE_FIXNUM_MARK:
			    case IDIO_TYPE_CONSTANT_MARK:
			    case IDIO_TYPE_PLACEHOLDER_MARK:
				break;
			    case IDIO_TYPE_POINTER_MARK:
				if (IDIO_GC_FLAG_GCC_BLACK != k->colour) {
				    fprintf (stderr, "lost key %10p %10p in chain %5zu\n", o, k, i);
				    lost++;
				}
				break;
			    default:
				/* inconceivable! */
				idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected weak key type %#x", k);

				/* notreached */
				return;
			    }
			}
		    }
		    if (lost) {
			idio_dump (o, 1);
		    }
		}
		break;
	    default:
		/* inconceivable! */
		idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected weak object type %#x", o);

		/* notreached */
		return;
	    }
	    break;
	default:
	    /* inconceivable! */
	    idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected weak object type %#x", o);

	    /* notreached */
	    return;
	}

	o = o->next;
    }
#endif
}

void idio_gc_mark (idio_gc_t *gc)
{

    IDIO o = gc->used;
    while (NULL != o) {
	idio_gc_gcc_mark (gc, o, IDIO_GC_FLAG_GCC_WHITE);
	o = o->next;
    }
    gc->grey = NULL;

    idio_root_t *r = gc->roots;
    while (NULL != r) {
	idio_gc_gcc_mark_root (gc, r, IDIO_GC_FLAG_GCC_BLACK);
	r = r->next;
    }

    idio_root_t *a = gc->autos;
    while (NULL != a) {
	idio_gc_gcc_mark_root (gc, a, IDIO_GC_FLAG_GCC_BLACK);
	a = a->next;
    }

    while (NULL != gc->grey) {
	idio_gc_process_grey (gc, IDIO_GC_FLAG_GCC_BLACK);
    }

    idio_gc_mark_weak (gc);
}

#ifdef IDIO_VM_PROF
static struct timespec idio_gc_all_closure_t;
static struct timespec idio_gc_all_primitive_t;
static double idio_gc_all_closure_ru;
static double idio_gc_all_primitive_ru;

void idio_gc_closure_stats (IDIO c)
{
    /*
     * XXX IDIO_CLOSURE_REFCNT(c) is one because we're running this
     * before the actual idio_gc_sweep_free_value()
     */
    if (IDIO_CLOSURE_CALLED (c) &&
	1 == IDIO_CLOSURE_REFCNT (c)) {
	idio_gc_all_closure_t.tv_sec += IDIO_CLOSURE_CALL_TIME (c).tv_sec;
	idio_gc_all_closure_t.tv_nsec += IDIO_CLOSURE_CALL_TIME (c).tv_nsec;
	if (idio_gc_all_closure_t.tv_nsec > IDIO_VM_NS) {
	    idio_gc_all_closure_t.tv_nsec -= IDIO_VM_NS;
	    idio_gc_all_closure_t.tv_sec += 1;
	}

	fprintf (idio_vm_perf_FILE, "%+5jds gc_sweep_free Clos [%zu]@%-6zu %8" PRIu64, (intmax_t) idio_vm_elapsed (), IDIO_CLOSURE_XI (c), IDIO_CLOSURE_CODE_PC (c), IDIO_CLOSURE_CALLED (c));

	idio_debug_FILE (idio_vm_perf_FILE, " %-40s", idio_vm_closure_name (c));

	fprintf (idio_vm_perf_FILE, " %5jd.%09ld", (intmax_t) IDIO_CLOSURE_CALL_TIME (c).tv_sec, IDIO_CLOSURE_CALL_TIME (c).tv_nsec);

	double call_time = (IDIO_CLOSURE_CALL_TIME (c).tv_sec * IDIO_VM_NS + IDIO_CLOSURE_CALL_TIME (c).tv_nsec) / IDIO_CLOSURE_CALLED (c);
	fprintf (idio_vm_perf_FILE, " %11.f", call_time);
	char *units = "ns";
	if (call_time > 10000) {
	    units = "us";
	    call_time /= 1000;
	    if (call_time > 10000) {
		units = "ms";
		call_time /= 1000;
		if (call_time > 10000) {
		    units = "s";
		    call_time /= 1000;
		}
	    }
	}
	fprintf (idio_vm_perf_FILE, " %6.f %s", call_time, units);

	double ru_t =
	    IDIO_CLOSURE_RU_UTIME (c).tv_sec * IDIO_VM_US + IDIO_CLOSURE_RU_UTIME (c).tv_usec +
	    IDIO_CLOSURE_RU_STIME (c).tv_sec * IDIO_VM_US + IDIO_CLOSURE_RU_STIME (c).tv_usec;

	ru_t /= IDIO_VM_US;

	fprintf (idio_vm_perf_FILE, " %11.6f", ru_t);

	idio_gc_all_closure_ru += ru_t;

	fprintf (idio_vm_perf_FILE, "\n");

	IDIO_CLOSURE_CALLED (c) = 0;
    }
}

void idio_gc_primitive_stats (IDIO p)
{
    if (IDIO_PRIMITIVE_CALLED (p)) {
	fprintf (idio_vm_perf_FILE, "%+5jds gc_sweep_free Prim %10p %8" PRIu64 " %-40s %5jd.%09ld", (intmax_t) idio_vm_elapsed (), p, IDIO_PRIMITIVE_CALLED (p), IDIO_PRIMITIVE_NAME (p), (intmax_t) IDIO_PRIMITIVE_CALL_TIME (p).tv_sec, IDIO_PRIMITIVE_CALL_TIME (p).tv_nsec);

	double call_time = (IDIO_PRIMITIVE_CALL_TIME (p).tv_sec * IDIO_VM_NS + IDIO_PRIMITIVE_CALL_TIME (p).tv_nsec) / IDIO_PRIMITIVE_CALLED (p);
	fprintf (idio_vm_perf_FILE, " %11.f", call_time);
	char *units = "ns";
	if (call_time > 10000) {
	    units = "us";
	    call_time /= 1000;
	    if (call_time > 10000) {
		units = "ms";
		call_time /= 1000;
		if (call_time > 10000) {
		    units = "s";
		    call_time /= 1000;
		}
	    }
	}
	fprintf (idio_vm_perf_FILE, " %6.f %2s", call_time, units);

	double ru_t =
	    IDIO_PRIMITIVE_RU_UTIME (p).tv_sec * IDIO_VM_US + IDIO_PRIMITIVE_RU_UTIME (p).tv_usec +
	    IDIO_PRIMITIVE_RU_STIME (p).tv_sec * IDIO_VM_US + IDIO_PRIMITIVE_RU_STIME (p).tv_usec;

	ru_t /= IDIO_VM_US;

	fprintf (idio_vm_perf_FILE, " %9.6f", ru_t);

	if (strcmp ("load", IDIO_PRIMITIVE_NAME (p))) {
	    idio_gc_all_primitive_t.tv_sec += IDIO_PRIMITIVE_CALL_TIME (p).tv_sec;
	    idio_gc_all_primitive_t.tv_nsec += IDIO_PRIMITIVE_CALL_TIME (p).tv_nsec;
	    if (idio_gc_all_primitive_t.tv_nsec > IDIO_VM_NS) {
		idio_gc_all_primitive_t.tv_nsec -= IDIO_VM_NS;
		idio_gc_all_primitive_t.tv_sec += 1;
	    }

	    idio_gc_all_primitive_ru += ru_t;
	}

	fprintf (idio_vm_perf_FILE, "\n");

	IDIO_PRIMITIVE_CALLED (p) = 0;
    }
}
#endif

void idio_gc_sweep_free_value (IDIO vo)
{
    IDIO_ASSERT (vo);

    if (idio_S_nil == vo) {
	idio_error_param_nil ("idio_gc_sweep_free_value", "vo", IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    if (IDIO_GC_FLAG_FINALIZER == vo->finalizer) {
	idio_gc_finalizer_run (vo);
    }

    switch (vo->type) {
    case IDIO_TYPE_NONE:
	IDIO_C_ASSERT (0);
	idio_coding_error_C ("idio_gc_sweep_free_value cannot process an IDIO_TYPE_NONE", IDIO_LIST1 (vo), IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
	break;
    case IDIO_TYPE_STRING:
	idio_free_string (vo);
	break;
    case IDIO_TYPE_SUBSTRING:
	idio_free_substring (vo);
	break;
    case IDIO_TYPE_SYMBOL:
	idio_free_symbol (vo);
	break;
    case IDIO_TYPE_KEYWORD:
	idio_free_keyword (vo);
	break;
    case IDIO_TYPE_PAIR:
	idio_free_pair (vo);
	break;
    case IDIO_TYPE_ARRAY:
	idio_free_array (vo);
	break;
    case IDIO_TYPE_HASH:
	idio_free_hash (vo);
	break;
    case IDIO_TYPE_CLOSURE:
#ifdef IDIO_VM_PROF
	idio_gc_closure_stats (vo);
#endif
	if (0 == (IDIO_GC_FLAGS (idio_gc0) & IDIO_GC_FLAG_FINISH)) {
	    idio_delete_properties (vo);
	}
	idio_free_closure (vo);
	break;
    case IDIO_TYPE_PRIMITIVE:
#ifdef IDIO_VM_PROF
	idio_gc_primitive_stats (vo);
#endif
	if (0 == (IDIO_GC_FLAGS (idio_gc0) & IDIO_GC_FLAG_FINISH)) {
	    idio_delete_properties (vo);
	}
	idio_free_primitive (vo);
	break;
    case IDIO_TYPE_BIGNUM:
	idio_free_bignum (vo);
	break;
    case IDIO_TYPE_MODULE:
	idio_free_module (vo);
	break;
    case IDIO_TYPE_FRAME:
	idio_free_frame (vo);
	break;
    case IDIO_TYPE_HANDLE:
	idio_free_handle (vo);
	break;
    case IDIO_TYPE_C_CHAR:
    case IDIO_TYPE_C_SCHAR:
    case IDIO_TYPE_C_UCHAR:
    case IDIO_TYPE_C_SHORT:
    case IDIO_TYPE_C_USHORT:
    case IDIO_TYPE_C_INT:
    case IDIO_TYPE_C_UINT:
    case IDIO_TYPE_C_LONG:
    case IDIO_TYPE_C_ULONG:
    case IDIO_TYPE_C_LONGLONG:
    case IDIO_TYPE_C_ULONGLONG:
    case IDIO_TYPE_C_FLOAT:
    case IDIO_TYPE_C_DOUBLE:
	break;
    case IDIO_TYPE_C_LONGDOUBLE:
	idio_free_C_longdouble (vo);
	break;
    case IDIO_TYPE_C_POINTER:
	idio_free_C_pointer (vo);
	break;
    case IDIO_TYPE_STRUCT_TYPE:
	idio_free_struct_type (vo);
	break;
    case IDIO_TYPE_STRUCT_INSTANCE:
	idio_free_struct_instance (vo);
	break;
    case IDIO_TYPE_THREAD:
	idio_free_thread (vo);
	break;
    case IDIO_TYPE_CONTINUATION:
	idio_free_continuation (vo);
	break;
    case IDIO_TYPE_BITSET:
	idio_free_bitset (vo);
	break;
    default:
	idio_coding_error_C ("unexpected type", vo, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
	break;
    }
}

size_t idio_gc_sweep (idio_gc_t *gc)
{
    while (gc->stats.nfree > IDIO_GC_ALLOC_MANY) {
    	IDIO fo = gc->free;
	gc->free = fo->next;
	IDIO_GC_FREE (fo, sizeof (idio_t));
	gc->stats.nfree--;
    }

    size_t nobj = 0;
    size_t freed = 0;

    IDIO co = gc->used;
    IDIO po = NULL;
    IDIO no = NULL;
    while (NULL != co) {
	IDIO_ASSERT (co);
	nobj++;
	if (IDIO_GC_FLAG_FREE == co->free) {
	    fprintf (stderr, "idio_gc_sweep: already free?: ");
	    gc->verbose++;
	    idio_dump (co, 1);
	    gc->verbose--;
	    fprintf (stderr, "\n");
	}

	no = co->next;

	if ((IDIO_GC_FLAG_NOTSTICKY == co->sticky) &&
	    (IDIO_GC_FLAG_GCC_WHITE == co->colour)) {
	    gc->stats.nused[co->type]--;
	    if (po) {
		po->next = co->next;
	    } else {
		gc->used = co->next;
	    }

	    IDIO_C_ASSERT (IDIO_TYPE_NONE != co->type);
	    idio_gc_sweep_free_value (co);

	    co->type = IDIO_TYPE_NONE;
	    co->free = IDIO_GC_FLAG_FREE;
	    co->next = gc->free;
	    gc->free = co;
	    gc->stats.nfree++;
	    freed++;
	} else {
	    po = co;
	}

	co = no;
    }

#ifdef IDIO_GC_DEBUG
    pid_t pid = getpid ();
    if (pid == idio_pid) {
	fprintf (stderr, "[%" PRIdMAX "] gc-sweep #%d: saw %7zd obj; freed %6zd ", (intmax_t) pid, gc->gen, nobj, freed);
	double freed100 = freed * 100.0;
	if (nobj) {
	    fprintf (stderr, "%5.1f%% ", freed100 / nobj);
	} else {
	    fprintf (stderr, "    0%% ");
	}
	if (gc->stats.igets) {
	    fprintf (stderr, "%5.1f%% ", freed100 / gc->stats.igets);
	} else {
	    fprintf (stderr, "    0%% ");
	}
	fprintf (stderr, "; %7zd obj rem\n", nobj - freed);
    }
#endif

    return nobj;
}

static FILE *idio_gc_stats_FILE = NULL;

void idio_gc_stats ()
{
#ifdef IDIO_DEBUG
    fprintf (stderr, "gc-stats ");
#endif
    if (NULL == idio_gc_stats_FILE) {
	idio_gc_stats_FILE = fopen ("idio-gc-stats", "w");
    } else {
	idio_gc_stats_FILE = fopen ("idio-gc-stats", "a");
    }

    if (NULL == idio_gc_stats_FILE) {
	perror ("fopen (idio-gc-stats)");
	return;
    }

#ifdef IDIO_GC_DEBUG
    fprintf (idio_gc_stats_FILE, "gc-stats sizeof (struct idio_s)  %zu\n", sizeof (struct idio_s));
    fprintf (idio_gc_stats_FILE, "gc-stats sizeof (union idio_s_u) %zu\n", sizeof (union idio_s_u));
#endif

    char scales[] = " KMGT";
    unsigned long long count;
    int scale;

    idio_gc_t *gc = idio_gc0;
    while (NULL != gc) {
	fprintf (idio_gc_stats_FILE, "gc-stats #%d: %4lld   collections\n", gc->gen, gc->stats.collections);

	count = gc->stats.bounces;
	scale = 0;
	idio_hcount (&count, &scale);

	fprintf (idio_gc_stats_FILE, "gc-stats: %4lld%c  bounces\n", count, scales[scale]);

	int i;
	long long tgets = 0;
	long long nused = 0;
	for (i = 1; i < IDIO_TYPE_MAX; i++) {
	    tgets += gc->stats.tgets[i];
	    nused += gc->stats.nused[i];
	}
	count = tgets;
	scale = 0;
	idio_hcount (&count, &scale);
	fprintf (idio_gc_stats_FILE, "gc-stats: %4lld%c total GC requests\n", count, scales[scale]);
	count = nused;
	scale = 0;
	idio_hcount (&count, &scale);
	fprintf (idio_gc_stats_FILE, "gc-stats: %4lld%c current GC requests\n", count, scales[scale]);
	fprintf (idio_gc_stats_FILE, "gc-stats: %-15.15s %5.5s %4.4s %5.5s %4.4s", "type", "total", "%age", "used", "%age");
#ifdef IDIO_GC_DEBUG
	fprintf (idio_gc_stats_FILE, " %4.4s", "szof");
#endif
	fprintf (idio_gc_stats_FILE, "\n");
	int types_unused = 0;
	for (i = 1; i < IDIO_TYPE_MAX; i++) {
	    unsigned long long tgets_count = gc->stats.tgets[i];

	    if (tgets_count) {
		int tgets_scale = 0;
		idio_hcount (&tgets_count, &tgets_scale);
		unsigned long long nused_count = gc->stats.nused[i];
		int nused_scale = 0;
		idio_hcount (&nused_count, &nused_scale);

		fprintf (idio_gc_stats_FILE, "gc-stats: %-15.15s %4lld%c %4lld %4lld%c %4lld",
			 idio_type_enum2string (i),
			 tgets_count, scales[tgets_scale],
			 tgets ? gc->stats.tgets[i] * 100 / tgets : -1,
			 nused_count, scales[nused_scale],
			 nused ? gc->stats.nused[i] * 100 / nused : -1
		    );
#ifdef IDIO_GC_DEBUG
		size_t size = sizeof (IDIO);
		switch (i) {
		case IDIO_TYPE_STRING:          size = sizeof (idio_string_t);          break;
		case IDIO_TYPE_SUBSTRING:       size = sizeof (idio_substring_t);       break;
		case IDIO_TYPE_SYMBOL:          size = sizeof (idio_symbol_t);          break;
		case IDIO_TYPE_KEYWORD:         size = sizeof (idio_keyword_t);         break;
		case IDIO_TYPE_PAIR:            size = sizeof (idio_pair_t);            break;
		case IDIO_TYPE_ARRAY:           size = sizeof (idio_array_t);           break;
		case IDIO_TYPE_HASH:            size = sizeof (idio_hash_t);            break;
		case IDIO_TYPE_CLOSURE:         size = sizeof (idio_closure_t);         break;
		case IDIO_TYPE_PRIMITIVE:       size = sizeof (idio_primitive_t);       break;
		case IDIO_TYPE_BIGNUM:          size = sizeof (idio_bignum_t);          break;
		case IDIO_TYPE_MODULE:          size = sizeof (idio_module_t);          break;
		case IDIO_TYPE_FRAME:           size = sizeof (idio_frame_t);           break;
		case IDIO_TYPE_HANDLE:          size = sizeof (idio_handle_t);          break;
		case IDIO_TYPE_STRUCT_TYPE:     size = sizeof (idio_struct_type_t);     break;
		case IDIO_TYPE_STRUCT_INSTANCE: size = sizeof (idio_struct_instance_t); break;
		case IDIO_TYPE_THREAD:          size = sizeof (idio_thread_t);          break;
		case IDIO_TYPE_CONTINUATION:    size = sizeof (idio_continuation_t);    break;
		case IDIO_TYPE_BITSET:          size = sizeof (idio_bitset_t);          break;
		case IDIO_TYPE_C_CHAR:
		case IDIO_TYPE_C_SCHAR:
		case IDIO_TYPE_C_UCHAR:
		case IDIO_TYPE_C_SHORT:
		case IDIO_TYPE_C_USHORT:
		case IDIO_TYPE_C_INT:
		case IDIO_TYPE_C_UINT:
		case IDIO_TYPE_C_LONG:
		case IDIO_TYPE_C_ULONG:
		case IDIO_TYPE_C_LONGLONG:
		case IDIO_TYPE_C_ULONGLONG:
		case IDIO_TYPE_C_FLOAT:
		case IDIO_TYPE_C_DOUBLE:
		case IDIO_TYPE_C_LONGDOUBLE:
		case IDIO_TYPE_C_POINTER:
		    size = sizeof (idio_C_type_t);
		    break;
		}
		fprintf (idio_gc_stats_FILE, " %4zd", size);
#endif
		fprintf (idio_gc_stats_FILE, "\n");
	    } else {
		types_unused++;
	    }
	}
	if (types_unused) {
	    fprintf (idio_gc_stats_FILE, "gc-stats: %d types unused\n", types_unused);
	    if (5 != types_unused) {
		/*
		 * These should be other constants (noting that we've
		 * only tracked user-ish constants above: fixnum and
		 * unicode) and the PLACEHOLDER type.
		 *
		 * Also C/void which we haven't found a use for
		 * (yet?).
		 */
		for (i = 1; i < IDIO_TYPE_MAX; i++) {
		    if (0 == gc->stats.tgets[i]) {
			fprintf (idio_gc_stats_FILE, "gc-stats: %2d %s\n", i, idio_type_enum2string (i));
		    }
		}
	    }
	}

	count = gc->stats.mgets;
	scale = 0;
	idio_hcount (&count, &scale);

	fprintf (idio_gc_stats_FILE, "gc-stats: %4lld%c  max requests between GC\n", count, scales[scale]);

	count = gc->stats.reuse;
	scale = 0;
	idio_hcount (&count, &scale);

	fprintf (idio_gc_stats_FILE, "gc-stats: %4lld%c  GC objects reused\n", count, scales[scale]);

	count = gc->stats.allocs;
	scale = 0;
	idio_hcount (&count, &scale);

	fprintf (idio_gc_stats_FILE, "gc-stats: %4lld%c  system allocs\n", count, scales[scale]);

	count = gc->stats.tbytes;
	scale = 0;
	idio_hcount (&count, &scale);

	fprintf (idio_gc_stats_FILE, "gc-stats: %4lld%cB total bytes referenced\n", count, scales[scale]);

	count = gc->stats.nbytes;
	scale = 0;
	idio_hcount (&count, &scale);

	fprintf (idio_gc_stats_FILE, "gc-stats: %4lld%cB current bytes referenced\n", count, scales[scale]);

	int rc = 0;
	idio_root_t *r = gc->roots;
	while (NULL != r) {
	    switch (r->object->type) {
	    default:
		rc++;
		break;
	    }
	    r = r->next;
	}

	count = rc;
	scale = 0;
	idio_hcount (&count, &scale);

	fprintf (idio_gc_stats_FILE, "gc-stats: %4lld%c  roots\n", count, scales[scale]);

	int ac = 0;
	idio_root_t *a = gc->autos;
	while (NULL != a) {
	    switch (a->object->type) {
	    default:
		ac++;
		break;
	    }
	    a = a->next;
	}

	count = ac;
	scale = 0;
	idio_hcount (&count, &scale);

	fprintf (idio_gc_stats_FILE, "gc-stats: %4lld%c  autos\n", count, scales[scale]);

	int fc = 0;
	IDIO o = gc->free;
	while (NULL != o) {
	    fc++;
	    o = o->next;
	}

	count = fc;
	scale = 0;
	idio_hcount (&count, &scale);

	fprintf (idio_gc_stats_FILE, "gc-stats: %4lld%c  on free list\n", count, scales[scale]);

	int uc = 0;
	o = gc->used;
	gc->verbose++;
	while (NULL != o) {
	    IDIO_ASSERT (o);
	    uc++;
	    if (o->next &&
		o->next->type == 0) {
		fprintf (idio_gc_stats_FILE, "bad type %10p\n", o->next);
		o->next = o->next->next;
	    }

	    o = o->next;
	}
	gc->verbose--;

#ifdef IDIO_VM_PROF
	fprintf (idio_gc_stats_FILE, "gc-stats: all closures   %4jd.%09ld %7.3f\n", (intmax_t) idio_gc_all_closure_t.tv_sec, idio_gc_all_closure_t.tv_nsec, idio_gc_all_closure_ru);
	fprintf (idio_gc_stats_FILE, "gc-stats: all primitives %4jd.%09ld %7.3f\n", (intmax_t) idio_gc_all_primitive_t.tv_sec, idio_gc_all_primitive_t.tv_nsec, idio_gc_all_primitive_ru);
#endif

	count = uc;
	scale = 0;
	idio_hcount (&count, &scale);

	fprintf (idio_gc_stats_FILE, "gc-stats: %4lld%c  on used list\n", count, scales[scale]);

	struct rusage ru;
	if (getrusage (RUSAGE_SELF, &ru) < 0) {
	    perror ("gc-stats: getrusage");
	}

	double ru_t =
	    gc->stats.ru_utime.tv_sec * IDIO_VM_US + gc->stats.ru_utime.tv_usec +
	    gc->stats.ru_stime.tv_sec * IDIO_VM_US + gc->stats.ru_stime.tv_usec;

	ru_t /= IDIO_VM_US;

	count = ru.ru_maxrss;
#if defined (__APPLE__) && defined (__MACH__)
	/*
	 * bytes except Mac OS X 10.5.8 which claims pages but seems
	 * to set it to zero
	 */
	scale = 0;
#elif defined (__sun) && defined (__SVR4)
	/*
	 * pages but in all implementations the value of set to zero
	 */
	scale = 0;
#else
	/*
	 * KB
	 */
	scale = 1;
#endif
	idio_hcount (&count, &scale);

	fprintf (idio_gc_stats_FILE, "gc-stats: GC time dur mark/sweep %ld.%03lds/%ld.%03lds ru u+s %6.3f; max RSS %lld%cB\n",
		 (long) gc->stats.mark_dur.tv_sec, (long) gc->stats.mark_dur.tv_usec / 1000,
		 (long) gc->stats.sweep_dur.tv_sec, (long) gc->stats.sweep_dur.tv_usec / 1000,
		 ru_t,
		 count,
		 scales[scale]);

	gc = gc->next;
    }

    fclose (idio_gc_stats_FILE);
}

void idio_gc_possibly_collect ()
{
    /*
     * How often to run the garbage collector?
     *
     * If you don't run it all all then we'll consume a lot of virtual
     * memory.
     *
     * If you run it very often we'll spend more time winging up and
     * down linked lists than doing any real work.
     *
     * There's no good answer.
     *
     * We're normally here because IDIO_GC_FLAG_REQUEST has been set
     * in idio_gc_get_alloc() because there's nothing left on the free
     * list.
     */
    if (idio_gc0->pause == 0 &&
	((IDIO_GC_FLAGS (idio_gc0) & IDIO_GC_FLAG_REQUEST))) {
#ifdef IDIO_GC_DEBUG
	pid_t pid = getpid ();
	if (pid == idio_pid) {
	    fprintf (stderr, "[%" PRIdMAX "] possibly-collect: reqd=%d igets=%6llu allocs=%7llu free=%6lld\n",
		     (intmax_t) pid,
		     (IDIO_GC_FLAGS (idio_gc0) & IDIO_GC_FLAG_REQUEST) ? 1 : 0,
		     idio_gc->stats.igets,
		     idio_gc->stats.allocs,
		     idio_gc->stats.nfree);
	}
#endif
	idio_gc_collect (idio_gc0, IDIO_GC_COLLECT_ALL, "gc-possibly-collect");
    }
}

void idio_gc_collect (idio_gc_t *gc, int gen, char const *caller)
{
    /* idio_gc_stats ();   */

    if (idio_gc0->pause) {
	return;
    }

    gc->stats.collections++;
    IDIO_GC_FLAGS (idio_gc0) &= ~ IDIO_GC_FLAG_REQUEST;

    struct timeval t0;
    struct timeval t_mark;
    struct timeval t_sweep;

    /* elapsed */
    time_t s_mark = 0;
    suseconds_t us_mark = 0;
    time_t s_sweep = 0;
    suseconds_t us_sweep = 0;

    struct rusage ru0;
    if (getrusage (RUSAGE_SELF, &ru0) < 0) {
	perror ("gc-collect: getrusage");
    }

#ifdef IDIO_GC_DEBUG
    size_t nobj = 0;
#endif

    idio_gc_t *gc0 = gc;
    while (NULL != gc) {
	if (gettimeofday (&t0, NULL) < 0) {
	    perror ("gc-collect: gettimeofday");
	}

	idio_gc_mark (gc);

	if (gettimeofday (&t_mark, NULL) < 0) {
	    perror ("gc-collect: gettimeofday");
	}

	s_mark += t_mark.tv_sec - t0.tv_sec;
	us_mark += t_mark.tv_usec - t0.tv_usec;

	if (us_mark < 0) {
	    us_mark += 1000000;
	    s_mark -= 1;
	}

	if (IDIO_GC_COLLECT_GEN == gen) {
	    break;
	}

	gc = gc->next;
    }
    gc = gc0;

    while (NULL != gc) {
	if (gettimeofday (&t0, NULL) < 0) {
	    perror ("gc-collect: gettimeofday");
	}

#ifdef IDIO_GC_DEBUG
	nobj += idio_gc_sweep (gc);
#else
	idio_gc_sweep (gc);
#endif

	if (gettimeofday (&t_sweep, NULL) < 0) {
	    perror ("gc-collect: gettimeofday");
	}

	s_sweep += t_sweep.tv_sec - t_mark.tv_sec;
	us_sweep += t_sweep.tv_usec - t_mark.tv_usec;

	if (us_sweep < 0) {
	    us_sweep += 1000000;
	    s_sweep -= 1;
	}

	if (gc->stats.igets > gc->stats.mgets) {
	    gc->stats.mgets = gc->stats.igets;
	}
	gc->stats.igets = 0;

	if (IDIO_GC_COLLECT_GEN == gen) {
	    break;
	}

	gc = gc->next;
    }
    gc = gc0;

    struct rusage ru1;
    if (getrusage (RUSAGE_SELF, &ru1) < 0) {
	perror ("gc-collect: getrusage");
    }

    gc->stats.mark_dur.tv_usec += us_mark;
    if (gc->stats.mark_dur.tv_usec > 1000000) {
	gc->stats.mark_dur.tv_usec -= 1000000;
	gc->stats.mark_dur.tv_sec += 1;
    }
    gc->stats.mark_dur.tv_sec += s_mark;

    gc->stats.sweep_dur.tv_usec += us_sweep;
    if (gc->stats.sweep_dur.tv_usec > 1000000) {
	gc->stats.sweep_dur.tv_usec -= 1000000;
	gc->stats.sweep_dur.tv_sec += 1;
    }
    gc->stats.sweep_dur.tv_sec += s_sweep;

#ifdef IDIO_GC_DEBUG
    /*
     * Child processes can report GC metrics(*) which give an
     * anomalous appearance of a collection being repeated so report
     * (and limit to) the (main) PID to help clarify/differentiate
     * matters.
     *
     * (*) Somewhat worse, the child processes, even though they are
     * shortlived expressions in the test suite, can trigger a GC
     * which we have to wait for.
     *
     * Doubly worse because if that trigger occurs for one child
     * process then it's quite likely to occur for many of the child
     * processes.  Doh!
     */
    pid_t pid = getpid ();
    if (pid == idio_pid) {
	fprintf (stderr, "[%" PRIdMAX "] gc-collect %s %d.%-3lld %-23s ",
		 (intmax_t) pid,
		 IDIO_GC_COLLECT_GEN == gen ? "gen" : "all",
		 gc->gen,
		 gc->stats.collections,
		 caller);

	fprintf (stderr, "m/s %ld.%03lds/%ld.%03lds",
		 (long) s_mark, (long) us_mark / 1000,
		 (long) s_sweep, (long) us_sweep / 1000);

	int rc = 0;
	idio_root_t *r = gc->roots;
	while (NULL != r) {
	    switch (r->object->type) {
	    default:
		rc++;
		break;
	    }
	    r = r->next;
	}
	fprintf (stderr, " %2d roots ", rc);

	int ac = 0;
	idio_root_t *a = gc->autos;
	while (NULL != a) {
	    switch (a->object->type) {
	    default:
		ac++;
		break;
	    }
	    a = a->next;
	}
	fprintf (stderr, " %6d autos", ac);

	if (us_mark) {
	    fprintf (stderr, " %6zu in %6ldus = %5.1f", nobj, (long) us_mark, 1.0 * nobj / us_mark);
	}

	fprintf (stderr, "\n");
    }
#endif

    /* rusage ru_utime */
    time_t s = ru1.ru_utime.tv_sec - ru0.ru_utime.tv_sec;
    suseconds_t us = ru1.ru_utime.tv_usec - ru0.ru_utime.tv_usec;

    if (us < 0) {
	us += 1000000;
	s -= 1;
    }

    gc->stats.ru_utime.tv_usec += us;
    if (gc->stats.ru_utime.tv_usec > 1000000) {
	gc->stats.ru_utime.tv_usec -= 1000000;
	gc->stats.ru_utime.tv_sec += 1;
    }
    gc->stats.ru_utime.tv_sec += s;

    /* rusage ru_stime */
    s = ru1.ru_stime.tv_sec - ru0.ru_stime.tv_sec;
    us = ru1.ru_stime.tv_usec - ru0.ru_stime.tv_usec;

    if (us < 0) {
	us += 1000000;
	s -= 1;
    }

    gc->stats.ru_stime.tv_usec += us;
    if (gc->stats.ru_stime.tv_usec > 1000000) {
	gc->stats.ru_stime.tv_usec -= 1000000;
	gc->stats.ru_stime.tv_sec += 1;
    }
    gc->stats.ru_stime.tv_sec += s;
}

void idio_gc_collect_gen (char const *caller)
{
    idio_gc_collect (idio_gc, IDIO_GC_COLLECT_GEN, caller);
}

void idio_gc_collect_all (char const *caller)
{
    idio_gc_collect (idio_gc0, IDIO_GC_COLLECT_ALL, caller);
}

void idio_hcount (unsigned long long *bp, int *ip)
{
    if (*bp < 10000) {
	return;
    }
    *ip += 1;
    *bp /= 1000;
    idio_hcount (bp, ip);
}

void idio_gc_stats_inc (idio_type_e type)
{
    if (type > IDIO_TYPE_MAX) {
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "bad type %hhu", type);

	/* notreached */
	return;
    } else {
	idio_gc->stats.tgets[type]++;
    }
}

/**
 * idio_gc_get_pause() - get the "pause" state of the garbage collector
 *
 */
int idio_gc_get_pause (char const *caller)
{
    return idio_gc0->pause;
}

/**
 * idio_gc_pause() - pause the garbage collector
 *
 * Use this function sparingly as it's hard to predict how much you
 * will allocate inside your critical block.
 *
 * Calls to idio_gc_pause() will nest.
 *
 * See also idio_gc_resume().
 */
void idio_gc_pause (char const *caller)
{
    idio_gc0->pause++;
}

/**
 * idio_gc_resume() - resume the garbage collector
 *
 * Calls to idio_gc_resume() will de-nest.
 *
 * See also idio_gc_pause().
 */
void idio_gc_resume (char const *caller)
{
    idio_gc0->pause--;
    if (idio_gc0->pause < 0) {
	idio_gc0->pause = 0;
    }
}

/**
 * idio_gc_reset() - re-pause the garbage collector
 *
 */
void idio_gc_reset (char const *caller, int pause)
{
    idio_gc0->pause = pause;
}

void idio_gc_obj_free ()
{
    if (idio_gc0->pause) {
	idio_gc0->pause = 0;
    }

    idio_gc_t *gc = idio_gc0;
    while (NULL != gc) {
	size_t n = 0;
	while (NULL != gc->free) {
	    IDIO co = gc->free;
	    gc->free = co->next;
	    IDIO_GC_FREE (co, sizeof (idio_t));
	    n++;
	}
	IDIO_C_ASSERT (n == (size_t) gc->stats.nfree);

	n = 0;
	while (NULL != gc->used) {
	    IDIO co = gc->used;
	    gc->used = co->next;
	    IDIO_GC_FREE (co, sizeof (idio_t));
	    n++;
	}

	if (n) {
	    fprintf (stderr, "gc-obj-free #%d: %zu remained in use\n", gc->gen, n);
	}

	idio_gc_t *ngc = gc->next;
	idio_free (gc);
	gc = ngc;
    }
}

int idio_vasprintf (char **strp, char const *fmt, va_list ap)
{
#ifdef IDIO_MALLOC
    return idio_malloc_vasprintf (strp, fmt, ap);
#else
    return vasprintf (strp, fmt, ap);
#endif
}

/*
 * idio_asprintf() exists as a convenience function to abstract the
 * error check from dozens of calls to asprintf()
 */
int idio_asprintf(char **strp, char const *fmt, ...)
{
    int r;
    va_list ap;

    va_start (ap, fmt);
    r = idio_vasprintf (strp, fmt, ap);
    va_end (ap);

    if (-1 == r) {
	idio_error_alloc ("asprintf");

	/* notreached */
	return -1;
    }

    return r;
}

/**
 * idio_strcat() - concatenate two (non-static) length defined strings
 * @s1: first string
 * @s1sp: first string length pointer
 * @s2: second string
 * @s2s: second string length
 *
 * If @s2 is non-NULL then @s1 is resized to have @s2 concatenated.
 *
 * Return:
 * Returns @s1 (original or new)
 *
 * See also idio_strcat_free().
 */
char *idio_strcat (char *s1, size_t *s1sp, char const *s2, size_t const s2s)
{
    IDIO_C_ASSERT (s1);

    if (NULL == s2) {
	return s1;
    }

    char *r = idio_realloc (s1, *s1sp + s2s + 1);
    if (NULL == r) {
	idio_error_alloc ("realloc");

	/* notreached */
	return NULL;
    }

    memcpy (r + *s1sp, s2, s2s);
    *s1sp += s2s;
    r[*s1sp] = '\0';

    return r;
}

/**
 * idio_strcat_free() - cancatenate two (non-static) C strings
 * @s1: first string
 * @s1sp: first string length pointer
 * @s2: second string
 * @s2s: second string length
 *
 * Calls idio_strcat() then free()s @s2.
 *
 * Return:
 * Returns @s1 (original or new)
 */
char *idio_strcat_free (char *s1, size_t *s1sp, char *s2, size_t const s2s)
{
    IDIO_C_ASSERT (s1);

    if (NULL == s2) {
	return s1;
    }

    char *r = idio_strcat (s1, s1sp, s2, s2s);
    idio_gc_free (s2, s2s);

    return r;
}

int idio_gc_verboseness (int n)
{
    return (idio_gc->verbose >= n);
}

void idio_gc_set_verboseness (int n)
{
    idio_gc->verbose = n;
}

IDIO_DEFINE_PRIMITIVE0_DS ("gc/collect", gc_collect, (void), "", "\
invoke the garbage collector			\n\
						\n\
:return: ``#<unspec>``				\n\
")
{
    IDIO_GC_FLAGS (idio_gc0) |= IDIO_GC_FLAG_REQUEST;
    idio_gc_collect_all ("gc/collect");

    return idio_S_unspec;
}

void idio_gc_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (gc_collect);
}

static void idio_gc_run_all_finalizers ()
{
    if (idio_S_nil == idio_gc_finalizer_hash) {
	return;
    }

    IDIO keys = idio_hash_keys_to_list (idio_gc_finalizer_hash);
    while (idio_S_nil != keys) {
	IDIO k = IDIO_PAIR_H (keys);

	/* apply the finalizer */
	IDIO C_p = idio_hash_ref (idio_gc_finalizer_hash, k);
	void (*func) (IDIO o) = IDIO_C_TYPE_POINTER_P (C_p);
	(*func) (k);

	keys = IDIO_PAIR_T (keys);
    }

    idio_gc_remove_weak_object (idio_gc_finalizer_hash);
}

void idio_final_gc ()
{
    if (idio_vm_reports) {
	idio_gc_stats ();
#ifdef IDIO_DEBUG
#ifdef IDIO_MALLOC
	idio_malloc_stats ("final-gc");
#endif
#endif
    }

    IDIO_GC_FLAGS (idio_gc0) |= IDIO_GC_FLAG_FINISH;

    idio_gc_run_all_finalizers ();

    /* unprotect the finalizer hash itself */
    idio_gc_expose (idio_gc_finalizer_hash);
    /*  prevent it being used */
    idio_gc_finalizer_hash = idio_S_nil;

    idio_gc_expose_autos ();

    idio_gc_expose_roots ();

    idio_gc_collect_all ("final-gc");
#ifdef IDIO_DEBUG
    idio_gc_dump ();
#endif
    idio_gc_obj_free ();
}

void idio_init_gc ()
{
    idio_module_table_register (idio_gc_add_primitives, idio_final_gc, NULL);

    idio_gc = idio_gc_obj_new ();
    idio_gc0 = idio_gc;

    idio_gc->verbose = 0;

    /*
     * Hmm, debugging something else I noticed that as file handles
     * come and go, they actually go and are therefore deregistered
     * quite early on which provokes a resize from 64 back down to
     * 8...
     */
    idio_gc_finalizer_hash = IDIO_HASH_EQP (64);
    idio_gc_protect (idio_gc_finalizer_hash);
    idio_gc_add_weak_object (idio_gc_finalizer_hash);

#ifdef IDIO_VM_PROF
    idio_gc_all_closure_t.tv_sec = 0;
    idio_gc_all_closure_t.tv_nsec = 0;
    idio_gc_all_primitive_t.tv_sec = 0;
    idio_gc_all_primitive_t.tv_nsec = 0;
    idio_gc_all_closure_ru = 0;
    idio_gc_all_primitive_ru = 0;
#endif
}

