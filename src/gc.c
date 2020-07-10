/*
 * Copyright (c) 2015, 2017, 2018, 2020 Ian Fitchet <idf(at)idio-lang.org>
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

#include "idio.h"

static idio_gc_t *idio_gc;
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
void *idio_alloc (size_t s)
{
    void *blob = malloc (s);
    if (NULL == blob) {
	idio_error_alloc ("malloc");

	/* notreached */
	return NULL;
    }

#if IDIO_DEBUG
    /*
     * memset to something not all-zeroes and not all-ones to try to
     * catch assumptions about default memory bugs
     */
    memset (blob, 0x5e, s);
#endif

    return blob;
}

void *idio_realloc (void *p, size_t s)
{
    p = realloc (p, s);

    if (NULL == p) {
	idio_error_alloc ("realloc");

	/* notreached */
	return NULL;
    }

    return p;
}

/**
 * idio_gc_get_alloc() - get or allocate another ``IDIO`` value
 *
 * idio_gc_get_alloc() allocates another IDIO -- or pool thereof --
 * and returns it performing garbage collection housekeeping as it
 * goes.
 */

#define IDIO_GC_ALLOC_POOL	1024

IDIO idio_gc_get_alloc ()
{
    IDIO o;
    int n;
    IDIO_GC_FLAGS (idio_gc) |= IDIO_GC_FLAG_REQUEST;
    IDIO p = NULL;
    for (n = 0 ; n < IDIO_GC_ALLOC_POOL; n++) {
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
    idio_gc->stats.igets++;

    if ((idio_gc->stats.igets & 0xffff) == 0) {
	IDIO_FPRINTF (stderr, "igets = %llu\n", idio_gc->stats.igets);
    }
    IDIO o = idio_gc->free;
    if (NULL == o) {
	idio_gc->stats.allocs++;
	o = idio_gc_get_alloc ();
    } else {
	idio_gc->stats.nfree--;
	idio_gc->stats.reuse++;
	idio_gc->free = o->next;
    }

    /* assign type late in case we've re-used a previous object */
    o->type = type;
    o->gc_flags = IDIO_GC_FLAG_NONE;
    o->flags = IDIO_FLAG_NONE;
    o->tflags = 0;

    /*
     * 0 is the sentinel value for a hashval.  Of course a hash value
     * could be 0 in which case for a small number of objects we
     * re-compute the hash.
     */
    IDIO_HASHVAL (o) = 0;

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

void idio_gc_alloc (void **p, size_t size)
{

    *p = idio_alloc (size);
    idio_gc->stats.nbytes += size;
    idio_gc->stats.tbytes += size;
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
	    case IDIO_TYPE_CONSTANT_CHARACTER_MARK:
		return (IDIO_TYPE_CONSTANT_CHARACTER == type);
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

void idio_gc_stats_free (size_t n)
{
    idio_gc->stats.nbytes -= n;
}

void idio_gc_register_finalizer (IDIO o, void (*func) (IDIO o))
{
    IDIO_ASSERT (o);
    IDIO_C_ASSERT (func);

    IDIO ofunc = idio_C_pointer (func);

    idio_hash_put (idio_gc_finalizer_hash, o, ofunc);
    o->gc_flags |= IDIO_GC_FLAG_FINALIZER;
}

void idio_gc_deregister_finalizer (IDIO o)
{
    IDIO_ASSERT (o);

    idio_hash_delete (idio_gc_finalizer_hash, o);
    o->gc_flags &= IDIO_GC_FLAG_FINALIZER_UMASK;
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

    IDIO ofunc = idio_hash_get (idio_gc_finalizer_hash, o);
    if (idio_S_unspec != ofunc) {
	IDIO_TYPE_ASSERT (C_pointer, ofunc);

	void (*p) (IDIO o) = IDIO_C_TYPE_POINTER_P (ofunc);
	(*p) (o);

	idio_hash_delete (idio_gc_finalizer_hash, o);
    }
}

void idio_gc_gcc_mark (IDIO o, unsigned colour)
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

    IDIO_FPRINTF (stderr, "idio_gc_gcc_mark: mark %10p -> %10p t=%2d/%.5s f=%2x colour=%d\n", o, o->next, o->type, idio_type2string (o), o->gc_flags, colour);

    if ((o->gc_flags & IDIO_GC_FLAG_FREE_UMASK) & IDIO_GC_FLAG_FREE) {
	fprintf (stderr, "idio_gc_gcc_mark: already free?: ");
	idio_gc->verbose++;
	idio_dump (o, 1);
	idio_gc->verbose--;
	fprintf (stderr, "\n");
    }

    switch (colour) {
    case IDIO_GC_FLAG_GCC_WHITE:
	o->gc_flags = (o->gc_flags & IDIO_GC_FLAG_GCC_UMASK) | colour;
	break;
    case IDIO_GC_FLAG_GCC_BLACK:
	if (o->gc_flags & IDIO_GC_FLAG_GCC_BLACK) {
	    break;
	}
	if (o->gc_flags & IDIO_GC_FLAG_GCC_LGREY) {
	    IDIO_FPRINTF (stderr, "idio_gc_gcc_mark: object is already grey: %10p t=%2d %s f=%x\n", o, o->type, idio_type2string (o), o->gc_flags);
	    break;
	}

	switch (o->type) {
	case IDIO_TYPE_NONE:
	    IDIO_C_ASSERT (0);
	    idio_error_C ("idio_gc_gcc_mark cannot process an IDIO_TYPE_NONE", IDIO_LIST1 (o), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return;
	    break;
	case IDIO_TYPE_SUBSTRING:
	    o->gc_flags = (o->gc_flags & IDIO_GC_FLAG_GCC_UMASK) | colour;
	    idio_gc_gcc_mark (IDIO_SUBSTRING_PARENT (o), colour);
	    break;
	case IDIO_TYPE_PAIR:
	    o->gc_flags |= IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_PAIR_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_ARRAY:
	    o->gc_flags |= IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_ARRAY_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_HASH:
	    o->gc_flags |= IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_HASH_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_CLOSURE:
	    o->gc_flags |= IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_CLOSURE_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_PRIMITIVE:
	    o->gc_flags |= IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_PRIMITIVE_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_MODULE:
	    IDIO_C_ASSERT (IDIO_MODULE_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->gc_flags |= IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_MODULE_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_FRAME:
	    IDIO_C_ASSERT (IDIO_FRAME_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->gc_flags |= IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_FRAME_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_HANDLE:
	    IDIO_C_ASSERT (IDIO_HANDLE_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->gc_flags |= IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_HANDLE_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_STRUCT_TYPE:
	    IDIO_C_ASSERT (IDIO_STRUCT_TYPE_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->gc_flags |= IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_STRUCT_TYPE_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_STRUCT_INSTANCE:
	    IDIO_C_ASSERT (IDIO_STRUCT_INSTANCE_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->gc_flags |= IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_STRUCT_INSTANCE_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_THREAD:
	    IDIO_C_ASSERT (IDIO_THREAD_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->gc_flags |= IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_THREAD_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_CONTINUATION:
	    IDIO_C_ASSERT (IDIO_CONTINUATION_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->gc_flags |= IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_CONTINUATION_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_C_TYPEDEF:
	    IDIO_C_ASSERT (IDIO_C_TYPEDEF_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->gc_flags |= IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_C_TYPEDEF_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_C_STRUCT:
	    IDIO_C_ASSERT (IDIO_C_STRUCT_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->gc_flags |= IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_C_STRUCT_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_C_INSTANCE:
	    IDIO_C_ASSERT (IDIO_C_INSTANCE_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->gc_flags |= IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_C_INSTANCE_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_C_FFI:
	    IDIO_C_ASSERT (IDIO_C_FFI_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->gc_flags |= IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_C_FFI_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_OPAQUE:
	    IDIO_C_ASSERT (IDIO_OPAQUE_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->gc_flags |= IDIO_GC_FLAG_GCC_LGREY;
	    IDIO_OPAQUE_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	default:
	    /*
	     * All other (non-compound) types just have the colour set
	     */
	    o->gc_flags = (o->gc_flags & IDIO_GC_FLAG_GCC_UMASK) | colour;
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

void idio_gc_process_grey (unsigned colour)
{

    IDIO o = idio_gc->grey;

    if (NULL == o) {
	return;
    }

    size_t i;

    o->gc_flags &= IDIO_GC_FLAG_GCC_UMASK;
    o->gc_flags = (o->gc_flags & IDIO_GC_FLAG_GCC_UMASK) | IDIO_GC_FLAG_GCC_BLACK;

    switch (o->type) {
    case IDIO_TYPE_PAIR:
	idio_gc->grey = IDIO_PAIR_GREY (o);
	idio_gc_gcc_mark (IDIO_PAIR_H (o), colour);
	idio_gc_gcc_mark (IDIO_PAIR_T (o), colour);
	break;
    case IDIO_TYPE_ARRAY:
	idio_gc->grey = IDIO_ARRAY_GREY (o);
	idio_gc_gcc_mark (IDIO_ARRAY_DV (o), colour);
	for (i = 0; i < IDIO_ARRAY_USIZE (o); i++) {
	    if (NULL != IDIO_ARRAY_AE (o, i)) {
		idio_gc_gcc_mark (IDIO_ARRAY_AE (o, i), colour);
	    }
	}
	break;
    case IDIO_TYPE_HASH:
	idio_gc->grey = IDIO_HASH_GREY (o);
	if (IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_WEAK_KEYS) {
	    for (i = 0; i < IDIO_HASH_SIZE (o); i++) {
		int k_freed = 0;

		/*
		 * Don't mark the key
		 */
		IDIO k = IDIO_HASH_HE_KEY (o, i);
		switch ((uintptr_t) k & IDIO_TYPE_MASK) {
		case IDIO_TYPE_FIXNUM_MARK:
		case IDIO_TYPE_CONSTANT_MARK:
		case IDIO_TYPE_PLACEHOLDER_MARK:
		    break;
		case IDIO_TYPE_POINTER_MARK:
		    if (k->type) {
			IDIO v = IDIO_HASH_HE_VALUE (o, i);
			switch ((uintptr_t) v & IDIO_TYPE_MASK) {
			case IDIO_TYPE_FIXNUM_MARK:
			case IDIO_TYPE_CONSTANT_MARK:
			case IDIO_TYPE_PLACEHOLDER_MARK:
			    break;
			case IDIO_TYPE_POINTER_MARK:
			    if (v->type) {
				if (idio_S_nil != v) {
				    idio_gc_gcc_mark (v, colour);
				}
			    } else {
				fprintf (stderr, "ig_pg k OK, v freed %p?\n", v);
			    }
			    break;
			default:
			    /* inconceivable! */
			    idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected object mark type %#x", v);

			    /* notreached */
			    return;
			}
		    } else {
			k_freed = 1;
			IDIO_HASH_HE_VALUE (o, i) = idio_S_nil;
		    }
		    break;
		default:
		    /* inconceivable! */
		    idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected object mark type %#x", k);

		    /* notreached */
		    return;
		}

		if (! k_freed) {
		    IDIO v = IDIO_HASH_HE_VALUE (o, i);
		    switch ((uintptr_t) v & IDIO_TYPE_MASK) {
		    case IDIO_TYPE_FIXNUM_MARK:
		    case IDIO_TYPE_CONSTANT_MARK:
		    case IDIO_TYPE_PLACEHOLDER_MARK:
			break;
		    case IDIO_TYPE_POINTER_MARK:
			if (v->type) {
			    if (idio_S_nil != v) {
				idio_gc_gcc_mark (v, colour);
			    }
			} else {
			    fprintf (stderr, "ig_pg v freed %p?\n", v);
			}
			break;
		    default:
			/* inconceivable! */
			idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected object mark type %#x", v);

			/* notreached */
			return;
		    }
		}
	    }
	} else {
	    for (i = 0; i < IDIO_HASH_SIZE (o); i++) {
		if (!(IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_STRING_KEYS)) {
		    IDIO k = IDIO_HASH_HE_KEY (o, i);
		    if (idio_S_nil != k) {
			idio_gc_gcc_mark (k, colour);
		    }
		}
		IDIO v = IDIO_HASH_HE_VALUE (o, i);
		if (idio_S_nil != v) {
		    idio_gc_gcc_mark (v, colour);
		}
	    }
	}
	idio_gc_gcc_mark (IDIO_HASH_COMP (o), colour);
	idio_gc_gcc_mark (IDIO_HASH_HASH (o), colour);
	break;
    case IDIO_TYPE_CLOSURE:
	idio_gc->grey = IDIO_CLOSURE_GREY (o);
	idio_gc_gcc_mark (IDIO_CLOSURE_FRAME (o), colour);
	idio_gc_gcc_mark (IDIO_CLOSURE_ENV (o), colour);
	break;
    case IDIO_TYPE_PRIMITIVE:
	idio_gc->grey = IDIO_PRIMITIVE_GREY (o);
	break;
    case IDIO_TYPE_MODULE:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_MODULE_GREY (o));
	idio_gc->grey = IDIO_MODULE_GREY (o);
	idio_gc_gcc_mark (IDIO_MODULE_NAME (o), colour);
	idio_gc_gcc_mark (IDIO_MODULE_EXPORTS (o), colour);
	idio_gc_gcc_mark (IDIO_MODULE_IMPORTS (o), colour);
	idio_gc_gcc_mark (IDIO_MODULE_SYMBOLS (o), colour);
	idio_gc_gcc_mark (IDIO_MODULE_VCI (o), colour);
	idio_gc_gcc_mark (IDIO_MODULE_VVI (o), colour);
	break;
    case IDIO_TYPE_FRAME:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_FRAME_GREY (o));
	idio_gc->grey = IDIO_FRAME_GREY (o);
	idio_gc_gcc_mark (IDIO_FRAME_NEXT (o), colour);
	idio_gc_gcc_mark (IDIO_FRAME_NAMES (o), colour);
	for (i = 0; i < IDIO_FRAME_NARGS (o); i++) {
	    idio_gc_gcc_mark (IDIO_FRAME_ARGS (o, i), colour);
	}
	break;
    case IDIO_TYPE_HANDLE:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_HANDLE_GREY (o));
	idio_gc->grey = IDIO_HANDLE_GREY (o);
	idio_gc_gcc_mark (IDIO_HANDLE_FILENAME (o), colour);
	idio_gc_gcc_mark (IDIO_HANDLE_PATHNAME (o), colour);
	break;
    case IDIO_TYPE_STRUCT_TYPE:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_STRUCT_TYPE_GREY (o));
	idio_gc->grey = IDIO_STRUCT_TYPE_GREY (o);
	idio_gc_gcc_mark (IDIO_STRUCT_TYPE_NAME (o), colour);
	idio_gc_gcc_mark (IDIO_STRUCT_TYPE_PARENT (o), colour);
	idio_gc_gcc_mark (IDIO_STRUCT_TYPE_FIELDS (o), colour);
	break;
    case IDIO_TYPE_STRUCT_INSTANCE:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_STRUCT_INSTANCE_GREY (o));
	idio_gc->grey = IDIO_STRUCT_INSTANCE_GREY (o);
	idio_gc_gcc_mark (IDIO_STRUCT_INSTANCE_TYPE (o), colour);
	idio_gc_gcc_mark (IDIO_STRUCT_INSTANCE_FIELDS (o), colour);
	break;
    case IDIO_TYPE_THREAD:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_THREAD_GREY (o));
	idio_gc->grey = IDIO_THREAD_GREY (o);
	idio_gc_gcc_mark (IDIO_THREAD_STACK (o), colour);
	idio_gc_gcc_mark (IDIO_THREAD_VAL (o), colour);
	idio_gc_gcc_mark (IDIO_THREAD_FRAME (o), colour);
	idio_gc_gcc_mark (IDIO_THREAD_ENV (o), colour);
	idio_gc_gcc_mark (IDIO_THREAD_TRAP_SP (o), colour);
	idio_gc_gcc_mark (IDIO_THREAD_DYNAMIC_SP (o), colour);
	idio_gc_gcc_mark (IDIO_THREAD_ENVIRON_SP (o), colour);
	idio_gc_gcc_mark (IDIO_THREAD_FUNC (o), colour);
	idio_gc_gcc_mark (IDIO_THREAD_REG1 (o), colour);
	idio_gc_gcc_mark (IDIO_THREAD_REG2 (o), colour);
	/*
	 * Don't mark the expr as it is in a weak keyed hash
	 */
	/* idio_gc_gcc_mark (IDIO_THREAD_EXPR (o), colour); */
	idio_gc_gcc_mark (IDIO_THREAD_INPUT_HANDLE (o), colour);
	idio_gc_gcc_mark (IDIO_THREAD_OUTPUT_HANDLE (o), colour);
	idio_gc_gcc_mark (IDIO_THREAD_ERROR_HANDLE (o), colour);
	idio_gc_gcc_mark (IDIO_THREAD_MODULE (o), colour);
	break;
    case IDIO_TYPE_CONTINUATION:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_CONTINUATION_GREY (o));
	idio_gc->grey = IDIO_CONTINUATION_GREY (o);
	idio_gc_gcc_mark (IDIO_CONTINUATION_STACK (o), colour);
	break;
    case IDIO_TYPE_C_TYPEDEF:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_C_TYPEDEF_GREY (o));
	idio_gc->grey = IDIO_C_TYPEDEF_GREY (o);
	idio_gc_gcc_mark (IDIO_C_TYPEDEF_SYM (o), colour);
	break;
    case IDIO_TYPE_C_STRUCT:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_C_STRUCT_GREY (o));
	idio_gc->grey = IDIO_C_STRUCT_GREY (o);
	idio_gc_gcc_mark (IDIO_C_STRUCT_FIELDS (o), colour);
	idio_gc_gcc_mark (IDIO_C_STRUCT_METHODS (o), colour);
	idio_gc_gcc_mark (IDIO_C_STRUCT_FRAME (o), colour);
	break;
    case IDIO_TYPE_C_INSTANCE:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_C_INSTANCE_GREY (o));
	idio_gc->grey = IDIO_C_INSTANCE_GREY (o);
	idio_gc_gcc_mark (IDIO_C_INSTANCE_C_STRUCT (o), colour);
	idio_gc_gcc_mark (IDIO_C_INSTANCE_FRAME (o), colour);
	break;
    case IDIO_TYPE_C_FFI:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_C_FFI_GREY (o));
	idio_gc->grey = IDIO_C_FFI_GREY (o);
	idio_gc_gcc_mark (IDIO_C_FFI_SYMBOL (o), colour);
	idio_gc_gcc_mark (IDIO_C_FFI_RESULT (o), colour);
	idio_gc_gcc_mark (IDIO_C_FFI_ARGS (o), colour);
	idio_gc_gcc_mark (IDIO_C_FFI_NAME (o), colour);
	break;
    case IDIO_TYPE_OPAQUE:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_OPAQUE_GREY (o));
	idio_gc->grey = IDIO_OPAQUE_GREY (o);
	idio_gc_gcc_mark (IDIO_OPAQUE_ARGS (o), colour);
	break;
    default:
	idio_error_C ("unexpected type", o, IDIO_C_FUNC_LOCATION ());

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

void idio_gc_dump_root (idio_root_t *root)
{
    IDIO_C_ASSERT (root);

    IDIO_FPRINTF (stderr, "idio_gc_dump_root: self @%10p ->%10p o=%10p ", root, root->next, root->object);
    switch (((intptr_t) root->object) & IDIO_TYPE_MASK) {
    case IDIO_TYPE_FIXNUM_MARK:
	IDIO_FPRINTF (stderr, "FIXNUM %d", ((intptr_t) root->object >> IDIO_TYPE_BITS_SHIFT));
	break;
    case IDIO_TYPE_CONSTANT_MARK:
	IDIO_FPRINTF (stderr, "SCONSTANT %d", ((intptr_t) root->object >> IDIO_TYPE_BITS_SHIFT));
	break;
    case IDIO_TYPE_POINTER_MARK:
	IDIO_FPRINTF (stderr, "IDIO %s", idio_type2string (root->object));
	break;
    default:
	IDIO_FPRINTF (stderr, "?? %p", root->object);
	break;
    }
    IDIO_FPRINTF (stderr, "\n");
}

void idio_gc_gcc_mark_root (idio_root_t *root, unsigned colour)
{
    IDIO_C_ASSERT (root);

    IDIO_FPRINTF (stderr, "idio_gc_gcc_mark_root: mark as %d\n", colour);
    idio_gc_gcc_mark (root->object, colour);
}

idio_gc_t *idio_gc_new ()
{
    idio_gc_t *c = idio_alloc (sizeof (idio_gc_t));
    c->next = NULL;
    c->roots = NULL;
    c->dynamic_roots = NULL;
    c->free = NULL;
    c->used = NULL;
    c->grey = NULL;
    c->pause = 0;
    c->verbose = 0;
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
    c->stats.dur.tv_sec = 0;
    c->stats.dur.tv_sec = 0;
    c->stats.dur.tv_usec = 0;

    return c;
}

#if IDIO_DEBUG > 2
/*
 * http://c-faq.com/varargs/handoff.html
 */
void IDIO_GC_VFPRINTF (FILE *stream, const char *format, va_list argp)
{
    IDIO_C_ASSERT (stream);
    IDIO_C_ASSERT (format);
    IDIO_C_ASSERT (argp);

    if (idio_gc->verbose > 2) {
	vfprintf (stream, format, argp);
    }
}

void IDIO_FPRINTF (FILE *stream, const char *format, ...)
{
    IDIO_C_ASSERT (stream);
    IDIO_C_ASSERT (format);

    va_list fmt_args;
    va_start (fmt_args, format);
    IDIO_GC_VFPRINTF (stream, format, fmt_args);
    va_end (fmt_args);
}
#endif

void idio_gc_walk_tree ()
{

    idio_gc->verbose++;

    IDIO_FPRINTF (stderr, "idio_walk_tree: \n");

    size_t ri = 0;
    idio_root_t *root = idio_gc->roots;
    while (root) {
	fprintf (stderr, "root #%3zd: ", ri++);
	if (idio_G_frame == root->object) {
	    fprintf (stderr, "== idio_G_frame: ");
	}
	idio_dump (root->object, 16);
	root = root->next;
    }

    idio_gc->verbose--;
}

void idio_gc_dump ()
{
    idio_gc->verbose = 3;

    IDIO_FPRINTF (stderr, "\ndump\n");
    IDIO_FPRINTF (stderr, "idio_gc_dump: self @%10p\n", idio_gc);

    size_t n = 0;
    idio_root_t *root = idio_gc->roots;
    while (root) {
	n++;
	idio_gc_dump_root (root);
	root = root->next;
    }
    if (n) {
	fprintf (stderr, "idio_gc_dump: %" PRIdPTR " roots\n", n);
    }

    IDIO o = idio_gc->free;
    n = 0;
    while (o) {
	/*
	  Can't actually dump the free objects as the code to print
	  objects out asserts whether they are free or not...

	  However, walking down the chain checks the chain is valid.
	 */
	o = o->next;
	n++;
    }
    IDIO_FPRINTF (stderr, "idio_gc_dump: %" PRIdPTR " on free list\n", n);
    IDIO_C_ASSERT (n == idio_gc->stats.nfree);

    o = idio_gc->used;
    n = 0;
    if (NULL != o) {
	fprintf (stderr, "idio_gc_dump: used list\n");
	while (NULL != o) {
	    IDIO_ASSERT (o);
	    if (n < 10) {
		idio_dump (o, 1);
	    }
	    o = o->next;
	    n++;
	}
	if (n >= 10) {
	    fprintf (stderr, "... +%zd more\n", n - 10);
	}
    }
    IDIO_FPRINTF (stderr, "idio_gc_dump: %" PRIdPTR " on used list\n", n);
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

    if (idio_S_nil == o) {
	idio_error_param_nil ("protect", IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    IDIO_FPRINTF (stderr, "idio_gc_protect: %10p\n", o);

    idio_root_t *r = idio_gc->roots;
    while (r) {
	if (idio_S_nil == r->object) {
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

    if (idio_S_nil == o) {
	idio_error_param_nil ("protect", IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    IDIO_FPRINTF (stderr, "idio_gc_protect_auto: %10p\n", o);

    idio_gc_protect (o);

    IDIO_PAIR_H (idio_gc->dynamic_roots) = idio_pair (o, IDIO_PAIR_H (idio_gc->dynamic_roots));
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

    IDIO_FPRINTF (stderr, "idio_gc_expose: %10p\n", o);

    int seen = 0;
    idio_root_t *r = idio_gc->roots;
    idio_root_t *p = NULL;
    while (r) {
	if (r->object == o) {
	    seen = 1;
	    if (p) {
		p->next = r->next;
	    } else {
		idio_gc->roots = r->next;
	    }
	    free (r);
	    break;
	} else {
	    p = r;
	}
	r = r->next;
    }

    if (0 == seen) {
	fprintf (stderr, "idio_gc_expose: o %10p not previously protected\n", o);
	idio_debug ("o = %s\n", o);

	fprintf (stderr, "idio_gc_expose: currently protected:\n");
	r = idio_gc->roots;
	while (r) {
	    fprintf (stderr, "  %10p %s\n", r->object, idio_type2string (r->object));
	    r = r->next;
	}
	IDIO_C_ASSERT (seen);
	return;
    }

    IDIO_FPRINTF (stderr, "idio_gc_expose: %10p no longer protected\n", o);
    r = idio_gc->roots;
    while (r) {
	idio_gc_dump_root (r);
	r = r->next;
    }
}

void idio_gc_expose_all ()
{

    IDIO_FPRINTF (stderr, "idio_gc_expose_all\n");
    idio_root_t *r = idio_gc->roots;
    size_t n = 0;
    while (r) {
	idio_dump (r->object, 1);
	/* r->object = idio_S_nil; */
	r = r->next;
	n++;
    }

    if (n) {
	fprintf (stderr, "idio_gc_expose_all for %zd objects\n", n);
    }
}

void idio_gc_expose_autos ()
{

    IDIO_FPRINTF (stderr, "idio_gc_expose_autos\n");

    IDIO dr = idio_gc->dynamic_roots;
    IDIO d = IDIO_PAIR_H (dr);
    while (idio_S_nil != d) {
	idio_gc_expose (IDIO_PAIR_H (d));
	d = IDIO_PAIR_T (d);
    }

    idio_gc_expose (dr);

    /* just for completeness */
    idio_gc->dynamic_roots = idio_S_nil;
}

static IDIO idio_gc_find_frame_id = idio_S_nil;

void idio_gc_find_frame_capture (IDIO frame)
{
    idio_gc_find_frame_id = frame;
}

void idio_gc_find_frame_print_id (IDIO id, int depth)
{
    for (int i = 0; i < depth; i++) {
	fprintf (stderr, "  ");
    }

    fprintf (stderr, "idio_gc_find_frame_r: %10p %-12s in ", id, idio_type2string (id));
}

void idio_gc_find_frame_r (IDIO id, int depth)
{
    if (depth > 20) {
	fprintf (stderr, "depth!\n");
	return;
    }

    int ri = 0;
    idio_root_t *root = idio_gc->roots;
    while (root) {
	if (root->object == id) {
	    idio_gc_find_frame_print_id (id, depth);
	    fprintf (stderr, "root #%d: ", ri);
	    idio_gc_dump_root (root);
	    fprintf (stderr, "\n");
	    return;
	}
	root = root->next;
	ri++;
    }

    IDIO o = idio_gc->used;
    IDIO no = NULL;
    int nobj = 0;
    size_t i;
    IDIO ro = idio_S_nil;
    int found = 0;
    while (o) {
	IDIO_ASSERT (o);
	nobj++;

	no = o->next;

	switch (o->type) {
	case IDIO_TYPE_PAIR:
	    if (IDIO_PAIR_H (o) == id) {
		idio_gc_find_frame_print_id (id, depth);
		idio_debug ("pair h %.100s\n", o);
		idio_gc_find_frame_r (o, depth + 1);
		fprintf (stderr, "\n");
		found = 1;
	    } else if (IDIO_PAIR_T (o) == id) {
		idio_gc_find_frame_print_id (id, depth);
		idio_debug ("pair t %.100s\n", o);
		idio_gc_find_frame_r (o, depth + 1);
		fprintf (stderr, "\n");
		found = 1;
	    }
	    break;
	case IDIO_TYPE_ARRAY:
	    for (i = 0; i < IDIO_ARRAY_USIZE (o); i++) {
		if (NULL != IDIO_ARRAY_AE (o, i)) {
		    if (IDIO_ARRAY_AE (o, i) == id) {
			idio_gc_find_frame_print_id (id, depth);
			fprintf (stderr, "array #%zu ", i);
			idio_debug ("%.100s\n", o);
			idio_gc_find_frame_r (o, depth + 1);
			fprintf (stderr, "\n");
			found = 1;
		    }
		}
	    }
	    break;
	case IDIO_TYPE_HASH:
	    for (i = 0; i < IDIO_HASH_SIZE (o); i++) {
		if (!((IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_STRING_KEYS) ||
		      (IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_WEAK_KEYS))) {
		    if (idio_S_nil != IDIO_HASH_HE_KEY (o, i)) {
			if (IDIO_HASH_HE_KEY (o, i) == id) {
			    idio_gc_find_frame_print_id (id, depth);
			    fprintf (stderr, "hash key #%zu\n", i);
			    idio_gc_find_frame_r (o, depth + 1);
			    fprintf (stderr, "\n");
			    found = 1;
			}
		    }
		} else {
		    if (idio_S_nil != IDIO_HASH_HE_KEY (o, i)) {
			if (IDIO_HASH_HE_KEY (o, i) == id) {
			    idio_gc_find_frame_print_id (id, depth);
			    fprintf (stderr, "hash W/S key #%zu\n", i);
			    idio_gc_find_frame_r (o, depth + 1);
			    fprintf (stderr, "\n");
			    found = 1;
			}
		    }
		}
		if (idio_S_nil != IDIO_HASH_HE_VALUE (o, i)) {
		    if (IDIO_HASH_HE_VALUE (o, i) == id) {
			idio_gc_find_frame_print_id (id, depth);
			fprintf (stderr, "hash value #%zu\n", i);
			idio_gc_find_frame_r (o, depth + 1);
			fprintf (stderr, "\n");
			found = 1;
		    }
		}
	    }
	    break;
	case IDIO_TYPE_CLOSURE:
	    if (IDIO_CLOSURE_FRAME (o) == id) {
		idio_gc_find_frame_print_id (id, depth);
		idio_debug ("clos frame %s\n", o);
		id = o;
		idio_gc_find_frame_r (o, depth + 1);
		fprintf (stderr, "\n");
		found = 1;
	    }
	    if (IDIO_CLOSURE_ENV (o) == id) {
		idio_gc_find_frame_print_id (id, depth);
		idio_debug ("clos env? %s\n", o);
		id = o;
		idio_gc_find_frame_r (o, depth + 1);
		fprintf (stderr, "\n");
		found = 1;
	    }
	    break;
	case IDIO_TYPE_FRAME:
	    if (o == id) {
		idio_gc_find_frame_print_id (id, depth);
		idio_debug ("frame itself %s\n", o);
		if (idio_S_nil != IDIO_FRAME_NEXT (o)) {
		    id = IDIO_FRAME_NEXT (o);
		    /* idio_gc_find_frame_r (o, depth + 1); */
		    fprintf (stderr, "\n");
		    found = 1;
		} else {
		    fprintf (stderr, "dead end?");
		    break;
		}
	    } else if (IDIO_FRAME_NEXT (o) == id) {
		idio_gc_find_frame_print_id (id, depth);
		idio_debug ("frame next %s\n", o);
		id = o;
		idio_gc_find_frame_r (o, depth + 1);
		fprintf (stderr, "\n");
		found = 1;
	    } else if (IDIO_FRAME_NAMES (o) == id) {
		idio_gc_find_frame_print_id (id, depth);
		idio_debug ("frame names %s\n", o);
		id = o;
		idio_gc_find_frame_r (o, depth + 1);
		fprintf (stderr, "\n");
		found = 1;
	    } else {
		for (i = 0; i < IDIO_FRAME_NARGS (o); i++) {
		    if (IDIO_FRAME_ARGS (o, i) == id) {
			idio_gc_find_frame_print_id (id, depth);
			fprintf (stderr, "frame arg #%zu ", i);
			idio_debug ("%.100s\n", o);
			idio_gc_find_frame_r (o, depth + 1);
			fprintf (stderr, "\n");
			found = 1;
		    }
		}
	    }
	    break;
	case IDIO_TYPE_THREAD:
	    if (IDIO_THREAD_STACK (o) == id) {
		idio_gc_find_frame_print_id (id, depth);
		idio_debug ("thr stack %s\n", o);
		id = o;
		idio_gc_find_frame_r (o, depth + 1);
		fprintf (stderr, "\n");
		found = 1;
	    } else if (IDIO_THREAD_VAL (o) == id) {
		idio_gc_find_frame_print_id (id, depth);
		idio_debug ("thr val %s\n", o);
		id = o;
		idio_gc_find_frame_r (o, depth + 1);
		fprintf (stderr, "\n");
		found = 1;
	    } else if (IDIO_THREAD_FRAME (o) == id) {
		idio_gc_find_frame_print_id (id, depth);
		idio_debug ("thr frame %s\n", o);
		id = o;
		idio_gc_find_frame_r (o, depth + 1);
		fprintf (stderr, "\n");
		found = 1;
	    } else if (IDIO_THREAD_ENV (o) == id) {
		idio_gc_find_frame_print_id (id, depth);
		idio_debug ("thr env %s\n", o);
		id = o;
		idio_gc_find_frame_r (o, depth + 1);
		fprintf (stderr, "\n");
		found = 1;
	    } else if (IDIO_THREAD_FUNC (o) == id) {
		idio_gc_find_frame_print_id (id, depth);
		idio_debug ("thr func %s\n", o);
		id = o;
		idio_gc_find_frame_r (o, depth + 1);
		fprintf (stderr, "\n");
		found = 1;
	    } else if (IDIO_THREAD_REG1 (o) == id) {
		idio_gc_find_frame_print_id (id, depth);
		idio_debug ("thr reg1 %s\n", o);
		id = o;
		idio_gc_find_frame_r (o, depth + 1);
		fprintf (stderr, "\n");
		found = 1;
	    } else if (IDIO_THREAD_REG2 (o) == id) {
		idio_gc_find_frame_print_id (id, depth);
		idio_debug ("thr reg2 %s\n", o);
		id = o;
		idio_gc_find_frame_r (o, depth + 1);
		fprintf (stderr, "\n");
		found = 1;
	    } else if (IDIO_THREAD_EXPR (o) == id) {
		idio_gc_find_frame_print_id (id, depth);
		idio_debug ("thr expr %s\n", o);
		id = o;
		idio_gc_find_frame_r (o, depth + 1);
		fprintf (stderr, "\n");
		found = 1;
	    }
	    break;
	case IDIO_TYPE_CONTINUATION:
	    if (IDIO_CONTINUATION_STACK (o) == id) {
		idio_gc_find_frame_print_id (id, depth);
		idio_debug ("k stack %s\n", o);
		id = o;
		idio_gc_find_frame_r (o, depth + 1);
		fprintf (stderr, "\n");
		found = 1;
	    }
	    break;
	}

	o = no;
    }

    if (0 == found) {
	idio_gc_find_frame_print_id (id, depth);
	fprintf (stderr, "??: %d objs: done\n", nobj);
    }
}

void idio_gc_find_frame ()
{
    if (idio_S_nil == idio_gc_find_frame_id) {
	return;
    }

    if (IDIO_TYPE_NONE == idio_gc_find_frame_id->type) {
	fprintf (stderr, "igff: frame freed\n");
	idio_gc_find_frame_id = idio_S_nil;
	return;
    }

    idio_gc_find_frame_print_id (idio_gc_find_frame_id, 0);
    idio_debug ("%s\n", idio_gc_find_frame_id);
    idio_gc_find_frame_r (idio_gc_find_frame_id, 0);
}

void idio_gc_mark ()
{

    IDIO_FPRINTF (stderr, "idio_gc_mark: all used -> WHITE %ux\n", IDIO_GC_FLAG_GCC_WHITE);
    IDIO o = idio_gc->used;
    while (o) {
	idio_gc_gcc_mark (o, IDIO_GC_FLAG_GCC_WHITE);
	o = o->next;
    }
    idio_gc->grey = NULL;

    IDIO_FPRINTF (stderr, "idio_gc_mark: roots -> BLACK %x\n", IDIO_GC_FLAG_GCC_BLACK);
    idio_root_t *root = idio_gc->roots;
    while (root) {
	idio_gc_gcc_mark_root (root, IDIO_GC_FLAG_GCC_BLACK);
	root = root->next;
    }

    IDIO_FPRINTF (stderr, "idio_gc_mark: process grey list\n");
    while (idio_gc->grey) {
	idio_gc_process_grey (IDIO_GC_FLAG_GCC_BLACK);
    }
}

#ifdef IDIO_VM_PERF
static struct timespec idio_gc_all_closure_t;
static struct timespec idio_gc_all_primitive_t;

void idio_gc_closure_stats (IDIO c)
{
    if (IDIO_CLOSURE_CALLED (c) > 0) {
	idio_gc_all_closure_t.tv_sec += IDIO_CLOSURE_CALL_TIME (c).tv_sec;
	idio_gc_all_closure_t.tv_nsec += IDIO_CLOSURE_CALL_TIME (c).tv_nsec;
	if (idio_gc_all_closure_t.tv_nsec > IDIO_VM_NS) {
	    idio_gc_all_closure_t.tv_nsec -= IDIO_VM_NS;
	    idio_gc_all_closure_t.tv_sec += 1;
	}

	IDIO name = idio_S_unspec;
	if (0 == (IDIO_GC_FLAGS (idio_gc) & IDIO_GC_FLAG_FINISH)) {
	    name = idio_vm_closure_name (c);
	}
	if (IDIO_CLOSURE_CALL_TIME (c).tv_sec ||
	    IDIO_CLOSURE_CALL_TIME (c).tv_nsec ||
	    idio_S_unspec != name) {
	    fprintf (idio_vm_perf_FILE, "%+5lds gc_sweep_free Clos %6td %8" PRIu64, idio_vm_elapsed (), IDIO_CLOSURE_CODE_PC (c), IDIO_CLOSURE_CALLED (c));

	    /*
	     * A little more complicated to get the closure's name
	     */
	    if (idio_S_unspec != name) {
		idio_debug_FILE (idio_vm_perf_FILE, " %-40s", name);
	    } else {
		fprintf (idio_vm_perf_FILE, " %-40s", "--");
	    }

	    fprintf (idio_vm_perf_FILE, " %5ld.%09ld", IDIO_CLOSURE_CALL_TIME (c).tv_sec, IDIO_CLOSURE_CALL_TIME (c).tv_nsec);

	    double call_time = (IDIO_PRIMITIVE_CALL_TIME (c).tv_sec * IDIO_VM_NS + IDIO_PRIMITIVE_CALL_TIME (c).tv_nsec) / IDIO_PRIMITIVE_CALLED (c);
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
	    fprintf (idio_vm_perf_FILE, "\n");
	}
    }
}

void idio_gc_primitive_stats (IDIO p)
{
    if (IDIO_PRIMITIVE_CALLED (p)) {
	idio_gc_all_primitive_t.tv_sec += IDIO_PRIMITIVE_CALL_TIME (p).tv_sec;
	idio_gc_all_primitive_t.tv_nsec += IDIO_PRIMITIVE_CALL_TIME (p).tv_nsec;
	if (idio_gc_all_primitive_t.tv_nsec > IDIO_VM_NS) {
	    idio_gc_all_primitive_t.tv_nsec -= IDIO_VM_NS;
	    idio_gc_all_primitive_t.tv_sec += 1;
	}

	fprintf (idio_vm_perf_FILE, "%+5lds gc_sweep_free Prim %10p %8" PRIu64 " %-40s %5ld.%09ld", idio_vm_elapsed (), p, IDIO_PRIMITIVE_CALLED (p), IDIO_PRIMITIVE_NAME (p), IDIO_PRIMITIVE_CALL_TIME (p).tv_sec, IDIO_PRIMITIVE_CALL_TIME (p).tv_nsec);

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
	fprintf (idio_vm_perf_FILE, " %6.f %s", call_time, units);
	fprintf (idio_vm_perf_FILE, "\n");
    }
}
#endif

void idio_gc_sweep_free_value (IDIO vo)
{
    IDIO_ASSERT (vo);

    if (idio_S_nil == vo) {
	idio_error_param_nil ("vo", IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }

    if (vo->gc_flags & IDIO_GC_FLAG_FINALIZER) {
	idio_gc_finalizer_run (vo);
    }

    switch (vo->type) {
    case IDIO_TYPE_NONE:
	IDIO_C_ASSERT (0);
	idio_error_C ("idio_gc_sweep_free_value cannot process an IDIO_TYPE_NONE", IDIO_LIST1 (vo), IDIO_C_FUNC_LOCATION ());

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
	idio_properties_delete (vo);
	idio_free_closure (vo);
	break;
    case IDIO_TYPE_PRIMITIVE:
	idio_properties_delete (vo);
	idio_free_primitive (vo);
	break;
    case IDIO_TYPE_BIGNUM:
	idio_free_bignum (vo);
	break;
    case IDIO_TYPE_MODULE:
	idio_free_module (vo);
	break;
    case IDIO_TYPE_FRAME:
	/* fprintf (stderr, "igsfv: frame %p\n", vo); */
	idio_free_frame (vo);
	break;
    case IDIO_TYPE_HANDLE:
	idio_free_handle (vo);
	break;
    case IDIO_TYPE_C_INT:
    case IDIO_TYPE_C_UINT:
    case IDIO_TYPE_C_FLOAT:
    case IDIO_TYPE_C_DOUBLE:
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
    case IDIO_TYPE_C_TYPEDEF:
	idio_free_C_typedef (vo);
	break;
    case IDIO_TYPE_C_STRUCT:
	idio_free_C_struct (vo);
	break;
    case IDIO_TYPE_C_INSTANCE:
	idio_free_C_instance (vo);
	break;
    case IDIO_TYPE_C_FFI:
	idio_free_C_FFI (vo);
	break;
    case IDIO_TYPE_OPAQUE:
	idio_free_opaque (vo);
	break;
    default:
	idio_error_C ("unexpected type", vo, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
	break;
    }
}

void idio_gc_sweep ()
{
    while (idio_gc->stats.nfree > 0x1000) {
    	IDIO fo = idio_gc->free;
	idio_gc->free = fo->next;
	free (fo);
	idio_gc->stats.nfree--;
    }

    size_t nobj = 0;
    size_t freed = 0;

    IDIO_FPRINTF (stderr, "idio_gc_sweep: used list\n");
    IDIO co = idio_gc->used;
    IDIO po = NULL;
    IDIO no = NULL;
    while (co) {
	IDIO_ASSERT (co);
	nobj++;
	if ((co->gc_flags & IDIO_GC_FLAG_FREE_MASK) == IDIO_GC_FLAG_FREE) {
	    fprintf (stderr, "idio_gc_sweep: already free?: ");
	    idio_gc->verbose++;
	    idio_dump (co, 1);
	    idio_gc->verbose--;
	    fprintf (stderr, "\n");
	}

	no = co->next;

	if (((co->gc_flags & IDIO_GC_FLAG_STICKY_MASK) == IDIO_GC_FLAG_NOTSTICKY) &&
	    ((co->gc_flags & IDIO_GC_FLAG_GCC_MASK) == IDIO_GC_FLAG_GCC_WHITE)) {
	    idio_gc->stats.nused[co->type]--;
	    /* fprintf (stderr, "idio_gc_sweep: freeing %10p %2d %s\n", co, co->type, idio_type2string (co)); */
	    if (po) {
		po->next = co->next;
	    } else {
		idio_gc->used = co->next;
	    }

	    IDIO_C_ASSERT (IDIO_TYPE_NONE != co->type);
	    idio_gc_sweep_free_value (co);

	    co->type = IDIO_TYPE_NONE;
	    IDIO_HASHVAL (co) = 0;
	    co->gc_flags = (co->gc_flags & IDIO_GC_FLAG_FREE_UMASK) | IDIO_GC_FLAG_FREE;
	    co->next = idio_gc->free;
	    idio_gc->free = co;
	    idio_gc->stats.nfree++;
	    freed++;
	} else {
	    IDIO_FPRINTF (stderr, "idio_gc_sweep: keeping %10p %x == %x %x == %x\n", co, co->gc_flags & IDIO_GC_FLAG_STICKY_MASK, IDIO_GC_FLAG_NOTSTICKY, co->gc_flags & IDIO_GC_FLAG_GCC_MASK, IDIO_GC_FLAG_GCC_WHITE);
	    po = co;
	}

	co = no;
    }

    /*
    if (nobj) {
	fprintf (stderr, "idio_gc_sweep: freed %zd/%zd %.1f%%, left %zd\n", freed, nobj, freed * 100.0 / nobj, nobj - freed);
    } else {
	fprintf (stderr, "idio_gc_sweep: freed %zd/%zd ??%%, left %zd\n", freed, nobj, nobj - freed);
    }
    */
}

void idio_gc_stats ();

void idio_gc_possibly_collect ()
{
    if (idio_gc->pause == 0 &&
	((IDIO_GC_FLAGS (idio_gc) & IDIO_GC_FLAG_REQUEST) ||
	 idio_gc->stats.igets > 0x1ffff)) {
	idio_gc_collect ("idio_gc_possibly_collect");
    }
}

void idio_gc_collect (char *caller)
{
    /* idio_gc_walk_tree (); */
    /* idio_gc_stats ();   */

    if (idio_gc->pause) {
	return;
    }

    idio_gc_find_frame ();

    IDIO_GC_FLAGS (idio_gc) &= ~ IDIO_GC_FLAG_REQUEST;

    struct timeval t0;
    gettimeofday (&t0, NULL);

    idio_gc->stats.collections++;
    if (idio_gc->stats.igets > idio_gc->stats.mgets) {
	idio_gc->stats.mgets = idio_gc->stats.igets;
    }
    idio_gc->stats.igets = 0;
    idio_gc_mark ();
    idio_gc_sweep ();

    idio_hash_tidy_weak_references ();

    struct timeval t1;
    gettimeofday (&t1, NULL);

    time_t s = t1.tv_sec - t0.tv_sec;
    suseconds_t us = t1.tv_usec - t0.tv_usec;

    if (us < 0) {
	us += 1000000;
	s -= 1;
    }

    IDIO_FPRINTF (stderr, "idio-gc-collect: GC time %ld.%03ld\n", s, us / 1000);

    idio_gc->stats.dur.tv_usec += us;
    if (idio_gc->stats.dur.tv_usec > 1000000) {
	idio_gc->stats.dur.tv_usec -= 1000000;
	idio_gc->stats.dur.tv_sec += 1;
    }
    idio_gc->stats.dur.tv_sec += s;
}

void idio_hcount (unsigned long long *bytes, int *scale)
{
    if (*bytes < 10000) {
	return;
    }
    *scale += 1;
    *bytes /= 1000;
    idio_hcount (bytes, scale);
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

void idio_gc_stats ()
{
    FILE *fh = stderr;

#ifdef IDIO_VM_PERF
    fh = idio_vm_perf_FILE;
#endif

    char scales[] = " KMGT";
    unsigned long long count;
    int scale;

    fprintf (fh, "idio_gc_stats: %4lld   collections\n", idio_gc->stats.collections);

    count = idio_gc->stats.bounces;
    scale = 0;
    idio_hcount (&count, &scale);

    fprintf (fh, "idio_gc_stats: %4lld%c  bounces\n", count, scales[scale]);

    int i;
    int tgets = 0;
    int nused = 0;
    for (i = 1; i < IDIO_TYPE_MAX; i++) {
	tgets += idio_gc->stats.tgets[i];
	nused += idio_gc->stats.nused[i];
    }
    count = tgets;
    scale = 0;
    idio_hcount (&count, &scale);
    fprintf (fh, "idio_gc_stats: %4lld%c total GC requests\n", count, scales[scale]);
    count = nused;
    scale = 0;
    idio_hcount (&count, &scale);
    fprintf (fh, "idio_gc_stats: %4lld%c current GC requests\n", count, scales[scale]);
    fprintf (fh, "idio_gc_stats: %-15.15s %5.5s %4.4s %5.5s %4.4s\n", "type", "total", "%age", "used", "%age");
    int types_unused = 0;
    for (i = 1; i < IDIO_TYPE_MAX; i++) {
	unsigned long long tgets_count = idio_gc->stats.tgets[i];

	if (tgets_count) {
	    int tgets_scale = 0;
	    idio_hcount (&tgets_count, &tgets_scale);
	    unsigned long long nused_count = idio_gc->stats.nused[i];
	    int nused_scale = 0;
	    idio_hcount (&nused_count, &nused_scale);

	    fprintf (fh, "idio_gc_stats: %-15.15s %4lld%c %3lld %4lld%c %3lld\n",
		     idio_type_enum2string (i),
		     tgets_count, scales[tgets_scale],
		     tgets ? idio_gc->stats.tgets[i] * 100 / tgets : -1,
		     nused_count, scales[nused_scale],
		     nused ? idio_gc->stats.nused[i] * 100 / nused : -1
		     );
	} else {
	    types_unused++;
	}
    }
    if (types_unused) {
	fprintf (fh, "idio_gc_stats: %d types unused\n", types_unused);
    }

    count = idio_gc->stats.mgets;
    scale = 0;
    idio_hcount (&count, &scale);

    fprintf (fh, "idio_gc_stats: %4lld%c  max requests between GC\n", count, scales[scale]);

    count = idio_gc->stats.reuse;
    scale = 0;
    idio_hcount (&count, &scale);

    fprintf (fh, "idio_gc_stats: %4lld%c  GC objects reused\n", count, scales[scale]);

    count = idio_gc->stats.allocs;
    scale = 0;
    idio_hcount (&count, &scale);

    fprintf (fh, "idio_gc_stats: %4lld%c  system allocs\n", count, scales[scale]);

    count = idio_gc->stats.tbytes;
    scale = 0;
    idio_hcount (&count, &scale);

    fprintf (fh, "idio_gc_stats: %4lld%cB total bytes referenced\n", count, scales[scale]);

    count = idio_gc->stats.nbytes;
    scale = 0;
    idio_hcount (&count, &scale);

    fprintf (fh, "idio_gc_stats: %4lld%cB current bytes referenced\n", count, scales[scale]);

    int rc = 0;
    idio_root_t *root = idio_gc->roots;
    idio_gc->verbose++;
    while (root) {
	switch (root->object->type) {
	default:
	    rc++;
	    break;
	}
	root = root->next;
    }
    idio_gc->verbose--;

    count = rc;
    scale = 0;
    idio_hcount (&count, &scale);

    fprintf (fh, "idio_gc_stats: %4lld%c  protected objects\n", count, scales[scale]);

    int fc = 0;
    IDIO o = idio_gc->free;
    while (o) {
	fc++;
	o = o->next;
    }

    count = fc;
    scale = 0;
    idio_hcount (&count, &scale);

    fprintf (fh, "idio_gc_stats: %4lld%c  on free list\n", count, scales[scale]);

    int uc = 0;
    o = idio_gc->used;
    idio_gc->verbose++;
    while (o) {
	IDIO_ASSERT (o);
	uc++;
	if (o->next &&
	    o->next->type == 0) {
	    /* IDIO_ASSERT (o->next); */
	    fprintf (fh, "bad type %10p\n", o->next);
	    o->next = o->next->next;
	}
	/* idio_dump (o, 160); */

	switch (o->type) {
	case IDIO_TYPE_CLOSURE:
	    idio_gc_closure_stats (o);
	    break;
	case IDIO_TYPE_PRIMITIVE:
	    idio_gc_primitive_stats (o);
	    break;
	}

	o = o->next;
    }
    idio_gc->verbose--;

    fprintf (fh, "idio_gc_stats: all closures   %4ld.%09ld\n", idio_gc_all_closure_t.tv_sec, idio_gc_all_closure_t.tv_nsec);
    fprintf (fh, "idio_gc_stats: all primitives %4ld.%09ld\n", idio_gc_all_primitive_t.tv_sec, idio_gc_all_primitive_t.tv_nsec);

    count = uc;
    scale = 0;
    idio_hcount (&count, &scale);

    fprintf (fh, "idio_gc_stats: %4lld%c  on used list\n", count, scales[scale]);

    fprintf (fh, "idio-gc-stats: GC time %ld.%03ld\n", idio_gc->stats.dur.tv_sec, (long) idio_gc->stats.dur.tv_usec / 1000);

}

/**
 * idio_gc_get_pause() - get the "pause" state of the garbage collector
 *
 */
int idio_gc_get_pause (char *caller)
{
    return idio_gc->pause;
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
void idio_gc_pause (char *caller)
{
    idio_gc->pause++;
}

/**
 * idio_gc_resume() - resume the garbage collector
 *
 * Calls to idio_gc_resume() will de-nest.
 *
 * See also idio_gc_pause().
 */
void idio_gc_resume (char *caller)
{
    idio_gc->pause--;
    if (idio_gc->pause < 0) {
	idio_gc->pause = 0;
    }
}

/**
 * idio_gc_reset() - re-pause the garbage collector
 *
 */
void idio_gc_reset (char *caller, int pause)
{
    idio_gc->pause = pause;
}

void idio_gc_ports_free ()
{
}

void idio_gc_free ()
{
    /*
      Things with finalizers will try to use embedded references which
      may have been freed by gc_sweep (because we will remove
      all roots before we call it).

      We know ports have finalizers.
     */
    idio_gc_ports_free ();

    while (idio_gc->roots) {
	idio_root_t *root = idio_gc->roots;
	idio_gc->roots = root->next;
	if (idio_S_nil == root->object) {
	    idio_error_error_message ("root->object is #n at %s:%d", __FILE__, __LINE__);

	    /* notreached */
	    return;
	}
	free (root);
    }

    if (idio_gc->pause) {
	IDIO_FPRINTF (stderr, "idio_gc_free: pause is non-zero: %" PRIdPTR ", zeroing\n", idio_gc->pause);
	idio_gc->pause = 0;
    }

    /*
     * Having exposed everything running a collection should free
     * everything...
     */
    idio_gc_collect ("idio_gc_free");

    size_t n = 0;
    while (idio_gc->free) {
	IDIO co = idio_gc->free;
	idio_gc->free = co->next;
	free (co);
	n++;
    }
    IDIO_FPRINTF (stderr, "idio_gc_free: %" PRIdPTR " on free list\n", n);
    IDIO_C_ASSERT (n == idio_gc->stats.nfree);

    n = 0;
    while (idio_gc->used) {
	IDIO co = idio_gc->used;
	idio_gc->used = co->next;
	free (co);
	n++;
    }
    IDIO_FPRINTF (stderr, "idio_gc_free: %" PRIdPTR " on used list\n", n);

    free (idio_gc);
}

/* #ifndef asprintf */
/* /\* */
/*   http://stackoverflow.com/questions/3774417/sprintf-with-automatic-memory-allocation */
/*  *\/ */
/* int */
/* vasprintf(char **strp, const char *fmt, va_list ap) */
/* { */
/*     size_t size = vsnprintf(NULL, 0, fmt, ap) + 1; */
/*     char *buffer = calloc(1, size); */

/*     if (!buffer) */
/*         return -1; */

/*     return vsnprintf(buffer, size, fmt, ap); */
/* } */

/* int */
/* asprintf(char **strp, const char *fmt, ...) */
/* { */
/*     int error; */
/*     va_list ap; */

/*     va_start(ap, fmt); */
/*     error = vasprintf(strp, fmt, ap); */
/*     va_end(ap); */

/*     return error; */
/* } */
/* #endif */

/**
 * idio_strcat() - concatenate two (non-static) C strings
 * @s1: first string
 * @s2: second string
 *
 * If @s2 is non-NULL then @s1 is resized to have @s2 concatenated.
 *
 * Return:
 * Returns @s1 (original or new)
 *
 * See also idio_strcat_free().
 */
char *idio_strcat (char *s1, const char *s2)
{
    IDIO_C_ASSERT (s1);

    if (NULL == s2) {
	return s1;
    }

    char *r = idio_realloc (s1, strlen (s1) + strlen (s2) + 1);
    if (NULL != r) {
	strcat (r, s2);
    }

    return r;
}

/**
 * idio_strcat_free() - cancatenate two (non-static) C strings
 * @s1: first string
 * @s2: second string
 *
 * Calls idio_strcat() then free()s @s2.
 *
 * Return:
 * Returns @s1 (original or new)
 */
char *idio_strcat_free (char *s1, char *s2)
{
    IDIO_C_ASSERT (s1);

    if (NULL == s2) {
	return s1;
    }

    char *r = idio_strcat (s1, s2);
    free (s2);

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

IDIO_DEFINE_PRIMITIVE0 ("gc/collect", gc_collect, (void))
{
    idio_gc_collect ("gc/collect");

    return idio_S_unspec;
}

void idio_init_gc ()
{
    idio_gc = idio_gc_new ();

    idio_gc->verbose = 0;

    IDIO dr = idio_pair (idio_S_nil, idio_S_nil);
    idio_gc_protect (dr);
    idio_gc->dynamic_roots = dr;

    if (0) {
	size_t n = 0;
	size_t sum = 0;

#define IDIO_GC_STRUCT_SIZE(s)	{					\
	    size_t size = sizeof (struct s);				\
	    sum += size;						\
	    n++;							\
	    fprintf (stderr, "sizeof (struct %-24s = %zd\n", #s ")", size); \
	}

	IDIO_GC_STRUCT_SIZE (idio_s);

	n = 0;
	sum = 0;

	IDIO_GC_STRUCT_SIZE (idio_string_s);
	IDIO_GC_STRUCT_SIZE (idio_substring_s);
	IDIO_GC_STRUCT_SIZE (idio_symbol_s);
	IDIO_GC_STRUCT_SIZE (idio_keyword_s);
	IDIO_GC_STRUCT_SIZE (idio_pair_s);
	IDIO_GC_STRUCT_SIZE (idio_array_s);
	IDIO_GC_STRUCT_SIZE (idio_hash_s);
	IDIO_GC_STRUCT_SIZE (idio_closure_s);
	IDIO_GC_STRUCT_SIZE (idio_primitive_s);
	IDIO_GC_STRUCT_SIZE (idio_bignum_s);
	IDIO_GC_STRUCT_SIZE (idio_module_s);
	IDIO_GC_STRUCT_SIZE (idio_frame_s);
	IDIO_GC_STRUCT_SIZE (idio_handle_s);
	IDIO_GC_STRUCT_SIZE (idio_struct_type_s);
	IDIO_GC_STRUCT_SIZE (idio_struct_instance_s);
	IDIO_GC_STRUCT_SIZE (idio_thread_s);
	IDIO_GC_STRUCT_SIZE (idio_C_type_s);
	IDIO_GC_STRUCT_SIZE (idio_C_typedef_s);
	IDIO_GC_STRUCT_SIZE (idio_C_struct_s);
	IDIO_GC_STRUCT_SIZE (idio_C_instance_s);
	IDIO_GC_STRUCT_SIZE (idio_C_FFI_s);
	IDIO_GC_STRUCT_SIZE (idio_opaque_s);
	IDIO_GC_STRUCT_SIZE (idio_continuation_s);

	fprintf (stderr, "sum = %zd, avg = %zd\n", sum, sum / n);

	/*
	 * deduct sizeof (idio_thread_t) as there aren't may of them
	 * in use and it skews the stats of regular user objects
	 */
	sum -= 96;
	fprintf (stderr, "sum = %zd, avg = %zd\n", sum, sum / n);

    }

    idio_gc_finalizer_hash = IDIO_HASH_EQP (64);
    idio_gc_protect (idio_gc_finalizer_hash);
    idio_hash_add_weak_table (idio_gc_finalizer_hash);

#ifdef IDIO_VM_PERF
    idio_gc_all_closure_t.tv_sec = 0;
    idio_gc_all_closure_t.tv_nsec = 0;
    idio_gc_all_primitive_t.tv_sec = 0;
    idio_gc_all_primitive_t.tv_nsec = 0;
#endif
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

    int n = 0;
    idio_ai_t hi;
    for (hi = 0; hi < IDIO_HASH_SIZE (idio_gc_finalizer_hash); hi++) {
	IDIO k = IDIO_HASH_HE_KEY (idio_gc_finalizer_hash, hi);
	IDIO_ASSERT (k);
	if (idio_S_nil != k) {
	    n++;

	    /* apply the finalizer */
	    IDIO C_p = IDIO_HASH_HE_VALUE (idio_gc_finalizer_hash, hi);
	    void (*func) (IDIO o) = IDIO_C_TYPE_POINTER_P (C_p);
	    (*func) (k);

	    /* expunge the key/value pair from this hash */
	    idio_hash_delete (idio_gc_finalizer_hash, k);
	}
    }
    idio_hash_remove_weak_table (idio_gc_finalizer_hash);
}

void idio_final_gc ()
{
    idio_gc_stats ();

    IDIO_GC_FLAGS (idio_gc) |= IDIO_GC_FLAG_FINISH;

    idio_gc_run_all_finalizers ();

    /* unprotect the finalizer hash itself */
    idio_gc_expose (idio_gc_finalizer_hash);
    /*  prevent it being used */
    idio_gc_finalizer_hash = idio_S_nil;

    idio_gc_expose_autos ();

    idio_gc_expose_all ();
    idio_gc_collect ("idio_final_gc");
#ifdef IDIO_DEBUG
    idio_gc_dump ();
#endif
    idio_gc_free ();
}

