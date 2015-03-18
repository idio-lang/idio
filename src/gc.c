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
 * gc.c
 * 
 */

#include "idio.h"

static IDIO idio_gc_finalizer_hash = idio_S_nil;

void idio_init_gc ()
{
    /*
     * Two bootstrap functions to get the global frame
     */
    idio_gc_t *gc = idio_gc_new ();
    idio_G_frame = idio_gc_frame (gc, 0, 0);
    idio_gc_protect (idio_G_frame, idio_G_frame);

    gc->verbose++;

    idio_gc_finalizer_hash = IDIO_HASH_EQP (idio_G_frame, 64);
    idio_gc_protect (idio_G_frame, idio_gc_finalizer_hash);
}

void idio_final_gc ()
{
    idio_index_t hi;
    for (hi = 0; hi < IDIO_HASH_SIZE (idio_gc_finalizer_hash); hi++) {
	IDIO k = IDIO_HASH_HE_KEY (idio_G_frame, hi);
	if (idio_S_nil != k) {
	    /* apply the finalizer */
	    idio_apply (idio_G_frame,
			IDIO_HASH_HE_VALUE (idio_gc_finalizer_hash, hi),
			idio_pair (idio_G_frame, k, idio_S_nil));

	    /* expunge the key/value pair from this hash */
	    IDIO_HASH_HE_VALUE (idio_gc_finalizer_hash, hi) = idio_S_nil;
	    IDIO_HASH_HE_KEY (idio_gc_finalizer_hash, hi) = idio_S_nil;
	}
    }

    /* unprotect the finalizer hash itself */
    idio_gc_expose (idio_G_frame, idio_gc_finalizer_hash);
}

/*
 * idio_alloc actually calls malloc(3)!
 */
void *idio_alloc (size_t s)
{
    void *blob = malloc (s);
    IDIO_C_ASSERT (blob);

    /*
     * memset to something not all-zeroes and not all-ones to try to
     * catch assumptions about default memory bugs
     */
    return memset (blob, 0x5e, s);
}

/*
 * idio_gc_get_alloc allocates another IDIO -- or pool thereof
 * -- and returns it
 */
IDIO idio_gc_get_alloc (idio_gc_t *gc)
{
    IDIO_C_ASSERT (gc);
    
    IDIO o = idio_alloc (sizeof (idio_t));
    o->next = NULL;
    
    gc->stats.nbytes += sizeof (idio_t);
    gc->stats.tbytes += sizeof (idio_t);
    
    return o;
}

/*
 * idio_gc_get finds the next available IDIO from the free list
 * or calls idio_gc_get_alloc to get one
 */
IDIO idio_gc_get (idio_gc_t *gc, idio_type_e type)
{
    IDIO_C_ASSERT (gc);
    IDIO_C_ASSERT (type);
    
    IDIO_C_ASSERT (type > IDIO_TYPE_NONE &&
		   type < IDIO_TYPE_MAX);

    gc->stats.nused[type]++;
    gc->stats.tgets[type]++;
    gc->stats.igets++;

    IDIO o = gc->free;
    if (NULL == o) {
	gc->stats.allocs++;
	o = idio_gc_get_alloc (gc);
    } else {
	gc->stats.reuse++;
	gc->free = o->next;
    }

    /* assign type late in case we've re-used a previous object */
    o->type = type;
    o->flags = IDIO_FLAG_NONE;
    
    IDIO_ASSERT (o);
    if (NULL != gc->used) {
	IDIO_ASSERT (gc->used);
    }
    o->next = gc->used;
    gc->used = o;

    return o;
}

/*
 * idio_get gets an IDIO of this type
 */
IDIO idio_get (IDIO f, idio_type_e type)
{
    IDIO_ASSERT (f);
    IDIO_C_ASSERT (type);
    
    idio_gc_t *gc = IDIO_GC (f);
    
    return idio_gc_get (gc, type);
}

void idio_gc_alloc (idio_gc_t *gc, void **p, size_t size)
{
    IDIO_C_ASSERT (gc);
    
    *p = idio_alloc (size);
    gc->stats.nbytes += size;
    gc->stats.tbytes += size;
}

IDIO idio_clone_base (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    idio_gc_t *gc = IDIO_GC (f);
    
    return idio_gc_get (gc, o->type);
}

int idio_isa (IDIO f, IDIO o, idio_type_e type)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);
    IDIO_C_ASSERT (type);
    
    return (o->type == type);
}

int idio_isa_nil (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);

    return (idio_S_nil == o);
}

int idio_isa_boolean (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);

    return (idio_S_true == o ||
	    idio_S_false == o);
}

void idio_register_finalizer (IDIO f, IDIO o, void (*func) (IDIO o))
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);
    IDIO_C_ASSERT (func);

    IDIO ofunc = idio_C_pointer (f, func);

    idio_hash_put (f, idio_gc_finalizer_hash, o, ofunc);
}

void idio_deregister_finalizer (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);

    idio_hash_delete (f, idio_gc_finalizer_hash, o);
}

void idio_finalizer_run (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);

    IDIO ofunc = idio_hash_get (f, idio_gc_finalizer_hash, o);
    if (idio_S_nil != ofunc) {
	IDIO_TYPE_ASSERT (C_pointer, ofunc);

	void (*p) (IDIO o) = IDIO_C_TYPE_POINTER_P (ofunc);
	(*p) (o);
    }

    idio_hash_delete (f, idio_gc_finalizer_hash, o);
}

void idio_mark (IDIO f, IDIO o, unsigned colour)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);
    
    idio_gc_t *gc = IDIO_GC (f);

    switch ((uintptr_t) o & 3) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
	return;
    case IDIO_TYPE_POINTER_MARK:
	break;
    default:
	fprintf (stderr, "idio_mark: u/k type %p\n", o);
	IDIO_C_ASSERT (0);
    }

    IDIO_FRAME_FPRINTF (f, stderr, "idio_mark: mark %10p -> %10p t=%2d/%.5s f=%2x colour=%d\n", o, o->next, o->type, idio_type2string (o), o->flags, colour);

    if ((o->flags & IDIO_FLAG_FREE_UMASK) == IDIO_FLAG_FREE) {
	fprintf (stderr, "idio_mark: already free?: ");
	IDIO_FRAME_GC (f)->verbose++;
	idio_dump (f, o, 1);
	IDIO_FRAME_GC (f)->verbose--;
	fprintf (stderr, "\n");
    }

    switch (colour) {
    case IDIO_FLAG_GCC_WHITE:
	o->flags = (o->flags & IDIO_FLAG_GCC_UMASK) | colour;
	break;
    case IDIO_FLAG_GCC_BLACK:
	if (o->flags & IDIO_FLAG_GCC_BLACK) {
	    break;
	}
	if (o->flags & IDIO_FLAG_GCC_LGREY) {
	    IDIO_FRAME_FPRINTF (f, stderr, "idio_mark: object is already grey: %10p %2d %s\n", o, o->type, idio_type2string (o)); 
	    break;
	}

	switch (o->type) {
	case IDIO_TYPE_SUBSTRING:
	    o->flags = (o->flags & IDIO_FLAG_GCC_UMASK) | colour;
	    idio_mark (f, IDIO_SUBSTRING_PARENT (o), colour);
	    break;
	case IDIO_TYPE_PAIR:
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_PAIR_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_ARRAY:
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_ARRAY_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_HASH:
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_HASH_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_CLOSURE:
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_CLOSURE_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_BIGNUM:
	    IDIO_C_ASSERT (IDIO_BIGNUM_GREY (o) != o);
	    IDIO_C_ASSERT (gc->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_BIGNUM_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_FRAME:
	    IDIO_C_ASSERT (IDIO_FRAME_GREY (o) != o);
	    IDIO_C_ASSERT (gc->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_FRAME_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_STRUCT_TYPE:
	    IDIO_C_ASSERT (IDIO_STRUCT_TYPE_GREY (o) != o);
	    IDIO_C_ASSERT (gc->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_STRUCT_TYPE_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_STRUCT_INSTANCE:
	    IDIO_C_ASSERT (IDIO_STRUCT_INSTANCE_GREY (o) != o);
	    IDIO_C_ASSERT (gc->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_STRUCT_INSTANCE_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_C_TYPEDEF:
	    IDIO_C_ASSERT (IDIO_C_TYPEDEF_GREY (o) != o);
	    IDIO_C_ASSERT (gc->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_C_TYPEDEF_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_C_STRUCT:
	    IDIO_C_ASSERT (IDIO_C_STRUCT_GREY (o) != o);
	    IDIO_C_ASSERT (gc->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_C_STRUCT_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_C_INSTANCE:
	    IDIO_C_ASSERT (IDIO_C_INSTANCE_GREY (o) != o);
	    IDIO_C_ASSERT (gc->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_C_INSTANCE_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_C_FFI:
	    IDIO_C_ASSERT (IDIO_C_FFI_GREY (o) != o);
	    IDIO_C_ASSERT (gc->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_C_FFI_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	case IDIO_TYPE_OPAQUE:
	    IDIO_C_ASSERT (IDIO_OPAQUE_GREY (o) != o);
	    IDIO_C_ASSERT (gc->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_OPAQUE_GREY (o) = gc->grey;
	    gc->grey = o;
	    break;
	default:
	    o->flags = (o->flags & IDIO_FLAG_GCC_UMASK) | colour;
	    break;
	}
	break;
    default:
	IDIO_C_ASSERT (0);
	IDIO_FRAME_FPRINTF (f, stderr, "idio_mark: unexpected colour %d\n", colour);
	break;
    }
}

void idio_process_grey (IDIO f, unsigned colour)
{
    IDIO_ASSERT (f);

    idio_gc_t *gc = IDIO_GC (f);

    IDIO o = gc->grey;

    if (NULL == o) {
	return;
    }

    size_t i;

    o->flags &= IDIO_FLAG_GCC_UMASK;
    o->flags = (o->flags & IDIO_FLAG_GCC_UMASK) | IDIO_FLAG_GCC_BLACK;
    
    switch (o->type) {
    case IDIO_TYPE_PAIR:
	gc->grey = IDIO_PAIR_GREY (o);
	idio_mark (f, IDIO_PAIR_H (o), colour);
	idio_mark (f, IDIO_PAIR_T (o), colour);
	break;
    case IDIO_TYPE_ARRAY:
	gc->grey = IDIO_ARRAY_GREY (o);
	for (i = 0; i < IDIO_ARRAY_ASIZE (o); i++) {
	    if (NULL != IDIO_ARRAY_AE (o, i)) {
		idio_mark (f, IDIO_ARRAY_AE (o, i), colour);
	    }
	}
	break;
    case IDIO_TYPE_HASH:
	gc->grey = IDIO_HASH_GREY (o);
	for (i = 0; i < IDIO_HASH_SIZE (o); i++) {
	    if (idio_S_nil != IDIO_HASH_HE_KEY (o, i)) {
		idio_mark (f, IDIO_HASH_HE_KEY (o, i), colour);
	    }
	    if (idio_S_nil != IDIO_HASH_HE_VALUE (o, i)) {
		idio_mark (f, IDIO_HASH_HE_VALUE (o, i), colour);
	    }
	}
	break;
    case IDIO_TYPE_CLOSURE:
	gc->grey = IDIO_CLOSURE_GREY (o);
	idio_mark (f, IDIO_CLOSURE_ARGS (o), colour);
	idio_mark (f, IDIO_CLOSURE_BODY (o), colour);
	idio_mark (f, IDIO_CLOSURE_FRAME (o), colour);
	break;
    case IDIO_TYPE_BIGNUM:
	IDIO_C_ASSERT (gc->grey != IDIO_BIGNUM_GREY (o));
	gc->grey = IDIO_BIGNUM_GREY (o);
	idio_mark (f, IDIO_BIGNUM_SIG (o), colour);
	break;
    case IDIO_TYPE_FRAME:
	IDIO_C_ASSERT (gc->grey != IDIO_FRAME_GREY (o));
	gc->grey = IDIO_FRAME_GREY (o);
	idio_mark (f, IDIO_FRAME_FORM (o), colour); 
	idio_mark (f, IDIO_FRAME_NAMESPACE (o), colour); 
	idio_mark (f, IDIO_FRAME_ENV (o), colour); 
	if (NULL != IDIO_FRAME_PFRAME (o)) {
	    idio_mark (f, IDIO_FRAME_PFRAME (o), colour);
	}
	idio_mark (f, IDIO_FRAME_STACK (o), colour);
	idio_mark (f, IDIO_FRAME_THREADS (o), colour);
	idio_mark (f, IDIO_FRAME_ERROR (o), colour);
	break;
    case IDIO_TYPE_STRUCT_TYPE:
	IDIO_C_ASSERT (gc->grey != IDIO_STRUCT_TYPE_GREY (o));
	gc->grey = IDIO_STRUCT_TYPE_GREY (o);
	idio_mark (f, IDIO_STRUCT_TYPE_NAME (o), colour);
	idio_mark (f, IDIO_STRUCT_TYPE_PARENT (o), colour);
	idio_mark (f, IDIO_STRUCT_TYPE_SLOTS (o), colour);
	break;
    case IDIO_TYPE_STRUCT_INSTANCE:
	IDIO_C_ASSERT (gc->grey != IDIO_STRUCT_INSTANCE_GREY (o));
	gc->grey = IDIO_STRUCT_INSTANCE_GREY (o);
	idio_mark (f, IDIO_STRUCT_INSTANCE_TYPE (o), colour);
	idio_mark (f, IDIO_STRUCT_INSTANCE_SLOTS (o), colour);
	break;
    case IDIO_TYPE_C_TYPEDEF:
	IDIO_C_ASSERT (gc->grey != IDIO_C_TYPEDEF_GREY (o));
	gc->grey = IDIO_C_TYPEDEF_GREY (o);
	idio_mark (f, IDIO_C_TYPEDEF_SYM (o), colour);
	break;
    case IDIO_TYPE_C_STRUCT:
	IDIO_C_ASSERT (gc->grey != IDIO_C_STRUCT_GREY (o));
	gc->grey = IDIO_C_STRUCT_GREY (o);
	idio_mark (f, IDIO_C_STRUCT_SLOTS (o), colour);
	idio_mark (f, IDIO_C_STRUCT_METHODS (o), colour);
	idio_mark (f, IDIO_C_STRUCT_FRAME (o), colour);
	break;
    case IDIO_TYPE_C_INSTANCE:
	IDIO_C_ASSERT (gc->grey != IDIO_C_INSTANCE_GREY (o));
	gc->grey = IDIO_C_INSTANCE_GREY (o);
	idio_mark (f, IDIO_C_INSTANCE_C_STRUCT (o), colour);
	idio_mark (f, IDIO_C_INSTANCE_FRAME (o), colour);
	break;
    case IDIO_TYPE_C_FFI:
	IDIO_C_ASSERT (gc->grey != IDIO_C_FFI_GREY (o));
	gc->grey = IDIO_C_FFI_GREY (o);
	idio_mark (f, IDIO_C_FFI_SYMBOL (o), colour);
	idio_mark (f, IDIO_C_FFI_RESULT (o), colour);
	idio_mark (f, IDIO_C_FFI_ARGS (o), colour);
	idio_mark (f, IDIO_C_FFI_NAME (o), colour);
	break;
    case IDIO_TYPE_OPAQUE:
	IDIO_C_ASSERT (gc->grey != IDIO_OPAQUE_GREY (o));
	gc->grey = IDIO_OPAQUE_GREY (o);
	idio_mark (f, IDIO_OPAQUE_ARGS (o), colour);
	break;
    default:
	IDIO_C_ASSERT (0);
	IDIO_FRAME_FPRINTF (f, stderr, "idio_process_grey: unexpected type %x\n", o->type);
	break;
    }
}

unsigned idio_bw (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);
    
    return (o->flags & IDIO_FLAG_GCC_MASK);
}

idio_root_t *idio_root_new (IDIO f)
{
    IDIO_ASSERT (f);

    idio_gc_t *gc = IDIO_GC (f);

    idio_root_t *r = idio_alloc (sizeof (idio_root_t));
    r->next = gc->roots;
    gc->roots = r;
    r->object = idio_S_nil;
    return r;
}

void idio_root_dump (IDIO f, idio_root_t *root)
{
    IDIO_ASSERT (f);

    IDIO_FRAME_FPRINTF (f, stderr, "idio_root_dump: self @%10p ->%10p o=%10p %d\n", root, root->next, root->object, root->object ? root->object->type : -1);
}

void idio_root_mark (IDIO f, idio_root_t *root, unsigned colour)
{
    IDIO_ASSERT (f);
    IDIO_C_ASSERT (root);

    IDIO_FRAME_FPRINTF (f, stderr, "idio_root_mark: mark as %d\n", colour);
    idio_mark (f, root->object, colour);
}

idio_gc_t *idio_gc_new ()
{
    idio_gc_t *c = idio_alloc (sizeof (idio_gc_t));
    c->next = NULL;
    c->roots = NULL;
    c->free = NULL;
    c->used = NULL;
    c->grey = NULL;
    c->pause = 0;
    c->verbose = 0;

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

    c->symbols = NULL;
    c->ports = NULL;
    c->input_port = NULL;
    c->output_port = NULL;
    c->error_port = NULL;
    c->namespace = NULL;
    
    return c;
}

#ifdef IDIO_DEBUG
void IDIO_GC_FPRINTF (idio_gc_t *gc, FILE *stream, const char *format, ...)
{
    IDIO_C_ASSERT (gc);
    IDIO_C_ASSERT (stream);
    IDIO_C_ASSERT (format);

    va_list fmt_args;
    va_start (fmt_args, format);
    
    if (gc->verbose) {
	vfprintf (stream, format, fmt_args);
    }

    va_end (fmt_args);
}

void IDIO_FRAME_FPRINTF (IDIO f, FILE *stream, const char *format, ...)
{
    IDIO_ASSERT (f);
    IDIO_C_ASSERT (stream);
    IDIO_C_ASSERT (format);

    va_list fmt_args;
    va_start (fmt_args, format);

    if (IDIO_FRAME_GC (f)->verbose) {
	vfprintf (stream, format, fmt_args);
    }
    
    va_end (fmt_args);
}
#endif

void idio_walk_tree (IDIO f)
{
    IDIO_ASSERT (f);

    idio_gc_t *gc = IDIO_GC (f);

    gc->verbose++;
    
    IDIO_FRAME_FPRINTF (f, stderr, "idio_walk_tree: \n");

    size_t ri = 0;
    idio_root_t *root = gc->roots;
    while (root) {
	fprintf (stderr, "ri %3zd: ", ri++);
	if (idio_G_frame == root->object) {
	    fprintf (stderr, "== idio_G_frame: ");
	}
	idio_dump (f, root->object, 16);
	root = root->next;
    }

    gc->verbose--;
}

void idio_gc_dump (IDIO f)
{
    IDIO_ASSERT (f);

    idio_gc_t *gc = IDIO_GC (f);

    IDIO_FRAME_FPRINTF (f, stderr, "\ndump\n");
    IDIO_FRAME_FPRINTF (f, stderr, "idio_gc_dump: self @%10p\n", gc);
    
    idio_root_t *root = gc->roots;
    while (root) {
	idio_root_dump (f, root);
	root = root->next;
    }

    IDIO_FRAME_FPRINTF (f, stderr, "idio_gc_dump: free list\n");
    IDIO o = gc->free;
    while (o) {
	/*
	  Can't actually dump the free objects as the code to print
	  objects out asserts whether they are free or not...

	  However, walking down the chain checks the chain is valid.
	 */
	o = o->next;
    }

    IDIO_FRAME_FPRINTF (f, stderr, "idio_gc_dump: used list\n");
    o = gc->used;
    while (o) {
	IDIO_ASSERT (o);
	idio_dump (f, o, 1);
	o = o->next;
    }
}

void idio_gc_protect (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);

    idio_gc_t *gc = IDIO_GC (f);

    IDIO_FRAME_FPRINTF (f, stderr, "idio_gc_protect: %10p\n", o);

    idio_root_t *r = gc->roots;
    while (r) {
	if (idio_S_nil == r->object) {
	    r->object = o;
	    return;
	}
	r = r->next;
    }

    r = idio_root_new (f);
    r->object = o;
}

void idio_gc_expose (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);

    idio_gc_t *gc = IDIO_GC (f);

    IDIO_FRAME_FPRINTF (f, stderr, "idio_gc_expose: %10p\n", o);

    int seen = 0;
    idio_root_t *r = gc->roots;
    while (r) {
	if (r->object == o) {
	    seen = 1;
	    r->object = idio_S_nil;
	    break;
	}
	r = r->next;
    }

    if (0 == seen) {
	fprintf (stderr, "idio_gc_expose: o %10p not previously protected\n", o);
	r = gc->roots;
	while (r) {
	    fprintf (stderr, "idio_gc_expose: currently protected: %10p %s\n", r->object, idio_type2string (r->object));
	    r = r->next;
	}
	IDIO_C_ASSERT (seen);
	return;
    }

    IDIO_FRAME_FPRINTF (f, stderr, "idio_gc_expose: %10p not protected\n", o);
    r = gc->roots;
    while (r) {
	idio_root_dump (f, r);
	r = r->next;
    }
}

void idio_gc_expose_all (IDIO f)
{
    IDIO_ASSERT (f);

    idio_gc_t *gc = IDIO_GC (f);

    IDIO_FRAME_FPRINTF (f, stderr, "idio_gc_expose_all\n");
    fprintf (stderr, "idio_gc_expose_all\n");
    idio_root_t *r = gc->roots;
    while (r) {
	r->object = idio_S_nil;
	r = r->next;
    }
}

void idio_gc_mark (IDIO f)
{
    IDIO_ASSERT (f);

    idio_gc_t *gc = IDIO_GC (f);

    IDIO_FRAME_FPRINTF (f, stderr, "idio_gc_mark: all used -> WHITE\n");
    IDIO o = gc->used;
    while (o) {
	idio_mark (f, o, IDIO_FLAG_GCC_WHITE);
	o = o->next;
    }    
    gc->grey = NULL;

    IDIO_FRAME_FPRINTF (f, stderr, "idio_gc_mark: roots -> BLACK\n");
    idio_root_t *root = gc->roots;
    while (root) {
	idio_root_mark (f, root, IDIO_FLAG_GCC_BLACK);
	root = root->next;
    }    

    IDIO_FRAME_FPRINTF (f, stderr, "idio_gc_mark: process grey list\n");
    while (gc->grey) {
	idio_process_grey (f, IDIO_FLAG_GCC_BLACK);
    }    

}

void idio_gc_sweep (IDIO f)
{
    IDIO_ASSERT (f);

    idio_gc_t *gc = IDIO_GC (f);

    IDIO_FRAME_FPRINTF (f, stderr, "idio_gc_sweep: clear free list\n");
    while (gc->free) {
	IDIO co = gc->free;
	gc->free = co->next;
	free (co);
    }

    IDIO_FRAME_FPRINTF (f, stderr, "idio_gc_sweep: used list\n");
    IDIO co = gc->used;
    IDIO po = NULL;
    IDIO no = NULL;
    while (co) {
	IDIO_ASSERT (co);
	if ((co->flags & IDIO_FLAG_FREE_MASK) == IDIO_FLAG_FREE) {
	    fprintf (stderr, "idio_gc_sweep: already free?: ");
	    IDIO_FRAME_GC (f)->verbose++;
	    idio_dump (f, co, 1);
	    IDIO_FRAME_GC (f)->verbose--;
	    fprintf (stderr, "\n");
	}
	
	no = co->next;

	if (((co->flags & IDIO_FLAG_STICKY_MASK) == IDIO_FLAG_NOTSTICKY) &&
	    IDIO_FLAG_GCC_WHITE == idio_bw (f, co)) {
	    gc->stats.nused[co->type]--;
	    IDIO_FRAME_FPRINTF (f, stderr, "idio_gc_sweep: freeing %10p %2d %s\n", co, co->type, idio_type2string (co));
	    if (po) {
		po->next = co->next;
	    } else {
		gc->used = co->next;
	    }

	    idio_finalizer_run (f, co);

	    /* idio_free_lex (f, co); */

	    switch (co->type) {
	    case IDIO_TYPE_C_INT8:
	    case IDIO_TYPE_C_UINT8:
	    case IDIO_TYPE_C_INT16:
	    case IDIO_TYPE_C_UINT16:
	    case IDIO_TYPE_C_INT32:
	    case IDIO_TYPE_C_UINT32:
	    case IDIO_TYPE_C_INT64:
	    case IDIO_TYPE_C_UINT64:
	    case IDIO_TYPE_C_FLOAT:
	    case IDIO_TYPE_C_DOUBLE:
		idio_free_C_type (f, co);
		break;
	    case IDIO_TYPE_C_POINTER:
		idio_free_C_pointer (f, co);
		break;
	    case IDIO_TYPE_STRING:
		idio_free_string (f, co);
		break;
	    case IDIO_TYPE_SUBSTRING:
		idio_free_substring (f, co);
		break;
	    case IDIO_TYPE_SYMBOL:
		idio_free_symbol (f, co);
		break;
	    case IDIO_TYPE_PAIR:
		idio_free_pair (f, co);
		break;
	    case IDIO_TYPE_ARRAY:
		idio_free_array (f, co);
		break;
	    case IDIO_TYPE_HASH:
		idio_free_hash (f, co);
		break;
	    case IDIO_TYPE_CLOSURE:
		idio_free_closure (f, co);
		break;
	    case IDIO_TYPE_PRIMITIVE_C:
		idio_free_primitive_C (f, co);
		break;
	    case IDIO_TYPE_BIGNUM:
		idio_free_bignum (f, co);
		break;
	    case IDIO_TYPE_FRAME:
		idio_free_frame (f, co);
		break;
	    case IDIO_TYPE_HANDLE:
		idio_free_handle (f, co);
		break;
	    case IDIO_TYPE_STRUCT_TYPE:
		idio_free_struct_type (f, co);
		break;
	    case IDIO_TYPE_STRUCT_INSTANCE:
		idio_free_struct_instance (f, co);
		break;
	    case IDIO_TYPE_C_TYPEDEF:
		idio_free_C_typedef (f, co);
		break;
	    case IDIO_TYPE_C_STRUCT:
		idio_free_C_struct (f, co);
		break;
	    case IDIO_TYPE_C_INSTANCE:
		idio_free_C_instance (f, co);
		break;
	    case IDIO_TYPE_C_FFI:
		idio_free_C_FFI (f, co);
		break;
	    case IDIO_TYPE_OPAQUE:
		idio_free_opaque (f, co);
		break;
	    default:
		IDIO_C_ASSERT (0);
		break;
	    }

	    co->flags = (co->flags & IDIO_FLAG_FREE_UMASK) | IDIO_FLAG_FREE;
	    co->next = gc->free;
	    gc->free = co;
	    
	} else {
	    po = co;
	}

	co = no;
    }
}

void idio_gc_collect (IDIO f)
{
    IDIO_ASSERT (f);

    idio_gc_t *gc = IDIO_GC (f);

    IDIO_C_ASSERT (gc->pause == 0);

    gc->stats.collections++;
    if (gc->stats.igets > gc->stats.mgets) {
	gc->stats.mgets = gc->stats.igets;
    }
    gc->stats.igets = 0;
    /* IDIO_FRAME_FPRINTF (f, stderr, "\nidio_gc_collect: %2d: pre-mark dump\n", gc->stats.collections); */
    /* idio_gc_dump (f); */
    /* IDIO_FRAME_FPRINTF (f, stderr, "\nidio_gc_collect: %2d: mark\n", gc->stats.collections); */
    idio_gc_mark (f);
    /* IDIO_FRAME_FPRINTF (f, stderr, "\nidio_gc_collect: %2d: post-mark dump\n", gc->stats.collections); */
    /* idio_gc_dump (f); */
    /* IDIO_FRAME_FPRINTF (f, stderr, "\nidio_gc_collect: %2d: sweep\n", gc->stats.collections); */
    idio_gc_sweep (f);
    /* IDIO_FRAME_FPRINTF (f, stderr, "\nidio_gc_collect: %2d: post-sweep dump\n", gc->stats.collections); */
    /* idio_gc_dump (f); */
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

void idio_gc_stats (IDIO f)
{
    IDIO_ASSERT (f);

    idio_gc_t *gc = IDIO_GC (f);

    char scales[] = " KMGT";
    unsigned long long count;
    int scale;
    
    fprintf (stderr, "idio_gc_stats: %4lld   collections\n", gc->stats.collections);

    count = gc->stats.bounces;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_gc_stats: %4lld%c  bounces\n", count, scales[scale]);

    int i;
    int tgets = 0;
    int nused = 0;
    for (i = 1; i < IDIO_TYPE_MAX; i++) {
	tgets += gc->stats.tgets[i];
	nused += gc->stats.nused[i];
    }
    count = tgets;
    scale = 0;
    idio_hcount (&count, &scale);
    fprintf (stderr, "idio_gc_stats: %4lld%c total GC requests\n", count, scales[scale]);
    count = nused;
    scale = 0;
    idio_hcount (&count, &scale);
    fprintf (stderr, "idio_gc_stats: %4lld%c current GC requests\n", count, scales[scale]);
    fprintf (stderr, "idio_gc_stats: %-10.10s %5.5s %4.4s %5.5s %4.4s\n", "type", "total", "%age", "used", "%age");
    for (i = 1; i < IDIO_TYPE_MAX; i++) {
	unsigned long long tgets_count = gc->stats.tgets[i];
	int tgets_scale = 0;
	idio_hcount (&tgets_count, &tgets_scale);
	unsigned long long nused_count = gc->stats.nused[i];
	int nused_scale = 0;
	idio_hcount (&nused_count, &nused_scale);
    
	fprintf (stderr, "idio_gc_stats: %-10.10s %4lld%c %3lld %4lld%c %3lld\n",
		 idio_type_enum2string (i),
		 tgets_count, scales[tgets_scale],
		 gc->stats.tgets[i] * 100 / tgets,
		 nused_count, scales[nused_scale],
		 gc->stats.nused[i] * 100 / nused
		 );
    }

    count = gc->stats.mgets;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_gc_stats: %4lld%c  max requests between GC\n", count, scales[scale]);

    count = gc->stats.reuse;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_gc_stats: %4lld%c  GC objects reused\n", count, scales[scale]);

    count = gc->stats.allocs;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_gc_stats: %4lld%c  system allocs\n", count, scales[scale]);

    count = gc->stats.tbytes;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_gc_stats: %4lld%cB total bytes referenced\n", count, scales[scale]);

    count = gc->stats.nbytes;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_gc_stats: %4lld%cB current bytes referenced\n", count, scales[scale]);

    int rc = 0;
    idio_root_t *root = gc->roots;
    gc->verbose++;
    while (root) {
	switch (root->object->type) {
	default:
	    idio_dump (f, root->object, 3);
	    rc++;
	    break;
	}
	root = root->next;
    }
    gc->verbose--;

    count = rc;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_gc_stats: %4lld%c  protected objects\n", count, scales[scale]);

    int fc = 0;
    IDIO o = gc->free;
    while (o) {
	fc++;
	o = o->next;
    }

    count = fc;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_gc_stats: %4lld%c  on free list\n", count, scales[scale]);

    int uc = 0;
    o = gc->used;
    gc->verbose++;
    while (o) {
	IDIO_ASSERT (o);
	uc++;
	if (o->next &&
	    o->next->type == 0) {
	    /* IDIO_ASSERT (o->next); */
	    fprintf (stderr, "bad type %10p\n", o->next);
	    o->next = o->next->next;
	}
	/* idio_dump (f, o, 160); */
	o = o->next;
    }
    gc->verbose--;

    count = uc;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_gc_stats: %4lld%c  on used list\n", count, scales[scale]);
}

void idio_gc_pause (IDIO f)
{
    IDIO_ASSERT (f);

    idio_gc_t *gc = IDIO_GC (f);

    gc->pause++; 
}

void idio_gc_resume (IDIO f)
{
    IDIO_ASSERT (f);

    idio_gc_t *gc = IDIO_GC (f);

    gc->pause--; 
}

void idio_gc_ports_free (IDIO f)
{
    IDIO_ASSERT (f);

    idio_gc_t *gc = IDIO_GC (f);

    size_t pl = idio_array_size (f, gc->input_port);
    for (;pl; pl--) {
	idio_array_pop (f, gc->input_port);
    }

    pl = idio_array_size (f, gc->output_port);
    for (;pl; pl--) {
	idio_array_pop (f, gc->output_port);
    }
}

void idio_gc_free (IDIO f)
{
    IDIO_ASSERT (f);

    idio_gc_t *gc = IDIO_GC (f);

    idio_gc_stats (f);

    /*
      Things with finalizers will try to use embedded references which
      may have been freed by gc_sweep (because we will remove
      all roots before we call it).

      We know ports have finalizers.
     */
    idio_gc_ports_free (f);
    
    /*
      Careful!  Still need a frame to call the gc functions
      with!
     */
    IDIO dummyf = idio_gc_frame (gc, 37, 0);
    IDIO_FRAME_GC (dummyf) = gc;
    IDIO_FRAME_FORM (dummyf) = idio_S_nil;
    IDIO_FRAME_PFRAME (dummyf) = NULL;
    IDIO_FRAME_NAMESPACE (dummyf) = idio_S_nil;
    IDIO_FRAME_ENV (dummyf) = idio_S_nil;
    IDIO_FRAME_STACK (dummyf) = idio_S_nil;
    IDIO_FRAME_THREADS (dummyf) = idio_S_nil;
    IDIO_FRAME_ERROR (dummyf) = idio_S_nil;

    /* idio_gc_expose_all (f); */

    while (gc->roots) {
	idio_root_t *root = gc->roots;
	gc->roots = root->next;
	free (root);
    }    

    gc->roots = idio_root_new (f);
    gc->roots->object = dummyf;
    gc->roots->next = NULL;

    /* IDIO_FRAME_GC (dummyf)->verbose++;  */
    idio_gc_collect (dummyf);
    /* idio_gc_dump (dummyf); */

    while (gc->free) {
	IDIO co = gc->free;
	gc->free = co->next;
	free (co);
    }

    free (gc->roots);
    free (gc);
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

char *idio_strcat (char *s1, const char *s2)
{
    IDIO_C_ASSERT (s1);

    if (NULL == s2) {
	return s1;
    }
    
    char *r = realloc (s1, strlen (s1) + strlen (s2) + 1);
    if (NULL != r) {
	strcat (r, s2);
    }

    return r;
}

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
