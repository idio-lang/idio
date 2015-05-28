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

static idio_gc_t *idio_gc;
static IDIO idio_gc_finalizer_hash = idio_S_nil;

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

void *idio_realloc (void *p, size_t s)
{
    p = realloc (p, s);
    IDIO_C_ASSERT (p);
    return p;
}

/*
 * idio_gc_get_alloc allocates another IDIO -- or pool thereof
 * -- and returns it
 */

#define IDIO_GC_ALLOC_POOL	1024

IDIO idio_gc_get_alloc ()
{
    IDIO o;
    int n;
    idio_gc->request = 1;
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

/*
 * idio_gc_get finds the next available IDIO from the free list
 * or calls idio_gc_get_alloc to get one
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
    o->flags = IDIO_FLAG_NONE;
    
    IDIO_ASSERT (o);
    if (NULL != idio_gc->used) {
	IDIO_ASSERT (idio_gc->used);
    }
    o->next = idio_gc->used;
    idio_gc->used = o;

    return o;
}

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

int idio_isa (IDIO o, idio_type_e type)
{
    IDIO_ASSERT (o);
    IDIO_C_ASSERT (type);
    
    switch ((intptr_t) o & 3) {
    case IDIO_TYPE_FIXNUM_MARK:
	return (IDIO_TYPE_FIXNUM == type);
    case IDIO_TYPE_CONSTANT_MARK:
	return (IDIO_TYPE_CONSTANT == type);
    case IDIO_TYPE_CHARACTER_MARK:
	return (IDIO_TYPE_CHARACTER == type);
    case IDIO_TYPE_POINTER_MARK:
	return (o->type == type);
    default:
	idio_error_message ("isa: unexpected object type %x", o);
	return 0;
    }
}

void idio_gc_stats_free (size_t n)
{
    idio_gc->stats.nbytes -= n;
}

void idio_register_finalizer (IDIO o, void (*func) (IDIO o))
{
    IDIO_ASSERT (o);
    IDIO_C_ASSERT (func);

    IDIO ofunc = idio_C_pointer (func);

    idio_hash_put (idio_gc_finalizer_hash, o, ofunc);
    o->flags |= IDIO_FLAG_FINALIZER;
}

void idio_deregister_finalizer (IDIO o)
{
    IDIO_ASSERT (o);

    idio_hash_delete (idio_gc_finalizer_hash, o);
    o->flags &= IDIO_FLAG_FINALIZER_UMASK;
}

void idio_finalizer_run (IDIO o)
{
    IDIO_ASSERT (o);

    if (idio_S_nil == o) {
	fprintf (stderr, "idio_finalizer_run: nil?\n");
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

void idio_mark (IDIO o, unsigned colour)
{
    IDIO_ASSERT (o);
    
    switch ((uintptr_t) o & 3) {
    case IDIO_TYPE_FIXNUM_MARK:
    case IDIO_TYPE_CONSTANT_MARK:
    case IDIO_TYPE_CHARACTER_MARK:
	return;
    case IDIO_TYPE_POINTER_MARK:
	break;
    default:
	fprintf (stderr, "idio_mark: u/k type %p\n", o);
	IDIO_C_ASSERT (0);
    }

    IDIO_FPRINTF (stderr, "idio_mark: mark %10p -> %10p t=%2d/%.5s f=%2x colour=%d\n", o, o->next, o->type, idio_type2string (o), o->flags, colour);

    if ((o->flags & IDIO_FLAG_FREE_UMASK) == IDIO_FLAG_FREE) {
	fprintf (stderr, "idio_mark: already free?: ");
	idio_gc->verbose++;
	idio_dump (o, 1);
	idio_gc->verbose--;
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
	    IDIO_FPRINTF (stderr, "idio_mark: object is already grey: %10p t=%2d %s f=%x\n", o, o->type, idio_type2string (o), o->flags); 
	    break;
	}

	switch (o->type) {
	case IDIO_TYPE_SUBSTRING:
	    o->flags = (o->flags & IDIO_FLAG_GCC_UMASK) | colour;
	    idio_mark (IDIO_SUBSTRING_PARENT (o), colour);
	    break;
	case IDIO_TYPE_PAIR:
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_PAIR_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_ARRAY:
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_ARRAY_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_HASH:
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_HASH_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_CLOSURE:
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_CLOSURE_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	/* case IDIO_TYPE_BIGNUM: */
	/*     IDIO_C_ASSERT (IDIO_BIGNUM_GREY (o) != o); */
	/*     IDIO_C_ASSERT (idio_gc->grey != o); */
	/*     o->flags |= IDIO_FLAG_GCC_LGREY; */
	/*     IDIO_BIGNUM_GREY (o) = idio_gc->grey; */
	/*     idio_gc->grey = o; */
	/*     break; */
	case IDIO_TYPE_MODULE:
	    IDIO_C_ASSERT (IDIO_MODULE_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_MODULE_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_FRAME:
	    IDIO_C_ASSERT (IDIO_FRAME_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_FRAME_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_STRUCT_TYPE:
	    IDIO_C_ASSERT (IDIO_STRUCT_TYPE_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_STRUCT_TYPE_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_STRUCT_INSTANCE:
	    IDIO_C_ASSERT (IDIO_STRUCT_INSTANCE_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_STRUCT_INSTANCE_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_THREAD:
	    IDIO_C_ASSERT (IDIO_THREAD_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_THREAD_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_C_TYPEDEF:
	    IDIO_C_ASSERT (IDIO_C_TYPEDEF_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_C_TYPEDEF_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_C_STRUCT:
	    IDIO_C_ASSERT (IDIO_C_STRUCT_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_C_STRUCT_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_C_INSTANCE:
	    IDIO_C_ASSERT (IDIO_C_INSTANCE_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_C_INSTANCE_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_C_FFI:
	    IDIO_C_ASSERT (IDIO_C_FFI_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_C_FFI_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	case IDIO_TYPE_OPAQUE:
	    IDIO_C_ASSERT (IDIO_OPAQUE_GREY (o) != o);
	    IDIO_C_ASSERT (idio_gc->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_OPAQUE_GREY (o) = idio_gc->grey;
	    idio_gc->grey = o;
	    break;
	default:
	    o->flags = (o->flags & IDIO_FLAG_GCC_UMASK) | colour;
	    break;
	}
	break;
    default:
	IDIO_C_ASSERT (0);
	IDIO_FPRINTF (stderr, "idio_mark: unexpected colour %d\n", colour);
	break;
    }
}

void idio_process_grey (unsigned colour)
{

    IDIO o = idio_gc->grey;

    if (NULL == o) {
	return;
    }

    size_t i;

    o->flags &= IDIO_FLAG_GCC_UMASK;
    o->flags = (o->flags & IDIO_FLAG_GCC_UMASK) | IDIO_FLAG_GCC_BLACK;
    
    switch (o->type) {
    case IDIO_TYPE_PAIR:
	idio_gc->grey = IDIO_PAIR_GREY (o);
	idio_mark (IDIO_PAIR_H (o), colour);
	idio_mark (IDIO_PAIR_T (o), colour);
	break;
    case IDIO_TYPE_ARRAY:
	idio_gc->grey = IDIO_ARRAY_GREY (o);
	for (i = 0; i < IDIO_ARRAY_ASIZE (o); i++) {
	    if (NULL != IDIO_ARRAY_AE (o, i)) {
		idio_mark (IDIO_ARRAY_AE (o, i), colour);
	    }
	}
	break;
    case IDIO_TYPE_HASH:
	idio_gc->grey = IDIO_HASH_GREY (o);
	for (i = 0; i < IDIO_HASH_SIZE (o); i++) {
	    if (0 == (IDIO_HASH_FLAGS (o) & IDIO_HASH_FLAG_STRING_KEYS)) {
		if (idio_S_nil != IDIO_HASH_HE_KEY (o, i)) {
		    idio_mark (IDIO_HASH_HE_KEY (o, i), colour);
		}
	    }
	    if (idio_S_nil != IDIO_HASH_HE_VALUE (o, i)) {
		idio_mark (IDIO_HASH_HE_VALUE (o, i), colour);
	    }
	}
	break;
    case IDIO_TYPE_CLOSURE:
	idio_gc->grey = IDIO_CLOSURE_GREY (o);
	idio_mark (IDIO_CLOSURE_ENV (o), colour);
	break;
    /* case IDIO_TYPE_BIGNUM: */
    /* 	IDIO_C_ASSERT (idio_gc->grey != IDIO_BIGNUM_GREY (o)); */
    /* 	idio_gc->grey = IDIO_BIGNUM_GREY (o); */
    /* 	idio_mark (IDIO_BIGNUM_SIG (o), colour); */
    /* 	break; */
    case IDIO_TYPE_MODULE:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_MODULE_GREY (o));
	idio_gc->grey = IDIO_MODULE_GREY (o);
	idio_mark (IDIO_MODULE_NAME (o), colour); 
	idio_mark (IDIO_MODULE_EXPORTS (o), colour); 
	idio_mark (IDIO_MODULE_IMPORTS (o), colour); 
	idio_mark (IDIO_MODULE_SYMBOLS (o), colour); 
	idio_mark (IDIO_MODULE_DEFINED (o), colour); 
	break;
    case IDIO_TYPE_FRAME:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_FRAME_GREY (o));
	idio_gc->grey = IDIO_FRAME_GREY (o);
	idio_mark (IDIO_FRAME_NEXT (o), colour); 
	idio_mark (IDIO_FRAME_ARGS (o), colour); 
	break;
    case IDIO_TYPE_STRUCT_TYPE:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_STRUCT_TYPE_GREY (o));
	idio_gc->grey = IDIO_STRUCT_TYPE_GREY (o);
	idio_mark (IDIO_STRUCT_TYPE_NAME (o), colour);
	idio_mark (IDIO_STRUCT_TYPE_PARENT (o), colour);
	idio_mark (IDIO_STRUCT_TYPE_FIELDS (o), colour);
	break;
    case IDIO_TYPE_STRUCT_INSTANCE:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_STRUCT_INSTANCE_GREY (o));
	idio_gc->grey = IDIO_STRUCT_INSTANCE_GREY (o);
	idio_mark (IDIO_STRUCT_INSTANCE_TYPE (o), colour);
	idio_mark (IDIO_STRUCT_INSTANCE_FIELDS (o), colour);
	break;
    case IDIO_TYPE_THREAD:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_THREAD_GREY (o));
	idio_gc->grey = IDIO_THREAD_GREY (o);
	idio_mark (IDIO_THREAD_STACK (o), colour);
	idio_mark (IDIO_THREAD_VAL (o), colour);
	idio_mark (IDIO_THREAD_ENV (o), colour);
	idio_mark (IDIO_THREAD_HANDLERSP (o), colour);
	idio_mark (IDIO_THREAD_DYNAMICS (o), colour);
	idio_mark (IDIO_THREAD_FUNC (o), colour);
	idio_mark (IDIO_THREAD_REG1 (o), colour);
	idio_mark (IDIO_THREAD_REG2 (o), colour);
	idio_mark (IDIO_THREAD_INPUT_HANDLE (o), colour);
	idio_mark (IDIO_THREAD_OUTPUT_HANDLE (o), colour);
	idio_mark (IDIO_THREAD_ERROR_HANDLE (o), colour);
	idio_mark (IDIO_THREAD_MODULE (o), colour);
	break;
    case IDIO_TYPE_C_TYPEDEF:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_C_TYPEDEF_GREY (o));
	idio_gc->grey = IDIO_C_TYPEDEF_GREY (o);
	idio_mark (IDIO_C_TYPEDEF_SYM (o), colour);
	break;
    case IDIO_TYPE_C_STRUCT:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_C_STRUCT_GREY (o));
	idio_gc->grey = IDIO_C_STRUCT_GREY (o);
	idio_mark (IDIO_C_STRUCT_FIELDS (o), colour);
	idio_mark (IDIO_C_STRUCT_METHODS (o), colour);
	idio_mark (IDIO_C_STRUCT_FRAME (o), colour);
	break;
    case IDIO_TYPE_C_INSTANCE:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_C_INSTANCE_GREY (o));
	idio_gc->grey = IDIO_C_INSTANCE_GREY (o);
	idio_mark (IDIO_C_INSTANCE_C_STRUCT (o), colour);
	idio_mark (IDIO_C_INSTANCE_FRAME (o), colour);
	break;
    case IDIO_TYPE_C_FFI:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_C_FFI_GREY (o));
	idio_gc->grey = IDIO_C_FFI_GREY (o);
	idio_mark (IDIO_C_FFI_SYMBOL (o), colour);
	idio_mark (IDIO_C_FFI_RESULT (o), colour);
	idio_mark (IDIO_C_FFI_ARGS (o), colour);
	idio_mark (IDIO_C_FFI_NAME (o), colour);
	break;
    case IDIO_TYPE_OPAQUE:
	IDIO_C_ASSERT (idio_gc->grey != IDIO_OPAQUE_GREY (o));
	idio_gc->grey = IDIO_OPAQUE_GREY (o);
	idio_mark (IDIO_OPAQUE_ARGS (o), colour);
	break;
    default:
	IDIO_C_ASSERT (0);
	IDIO_FPRINTF (stderr, "idio_process_grey: unexpected type %x\n", o->type);
	break;
    }
}

idio_root_t *idio_root_new ()
{

    idio_root_t *r = idio_alloc (sizeof (idio_root_t));
    r->next = idio_gc->roots;
    idio_gc->roots = r;
    r->object = idio_S_nil;
    return r;
}

void idio_root_dump (idio_root_t *root)
{
    IDIO_C_ASSERT (root);

    IDIO_FPRINTF (stderr, "idio_root_dump: self @%10p ->%10p o=%10p ", root, root->next, root->object);
    switch (((intptr_t) root->object) & 3) {
    case IDIO_TYPE_FIXNUM_MARK:
	IDIO_FPRINTF (stderr, "FIXNUM %d", ((intptr_t) root->object >> 2));
	break;
    case IDIO_TYPE_CONSTANT_MARK:
	IDIO_FPRINTF (stderr, "SCONSTANT %d", ((intptr_t) root->object >> 2));
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

void idio_root_mark (idio_root_t *root, unsigned colour)
{
    IDIO_C_ASSERT (root);

    IDIO_FPRINTF (stderr, "idio_root_mark: mark as %d\n", colour);
    idio_mark (root->object, colour);
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
    c->request = 0;

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
	idio_root_dump (root);
	root = root->next;
    }
    IDIO_FPRINTF (stderr, "idio_gc_dump: %" PRIdPTR " roots\n", n);
    
    IDIO_FPRINTF (stderr, "idio_gc_dump: free list\n");
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

    IDIO_FPRINTF (stderr, "idio_gc_dump: used list\n");
    o = idio_gc->used;
    n = 0;
    while (o) {
	IDIO_ASSERT (o);
	idio_dump (o, 1);
	o = o->next;
	n++;
    }
    IDIO_FPRINTF (stderr, "idio_gc_dump: %" PRIdPTR " on used list\n", n);
}

void idio_gc_protect (IDIO o)
{
    IDIO_ASSERT (o);

    if (idio_S_nil == o) {
	idio_error_param_nil ("protect");
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

    r = idio_root_new ();
    r->object = o;
}

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
	r = idio_gc->roots;
	while (r) {
	    fprintf (stderr, "idio_gc_expose: currently protected: %10p %s\n", r->object, idio_type2string (r->object));
	    r = r->next;
	}
	IDIO_C_ASSERT (seen);
	return;
    }

    IDIO_FPRINTF (stderr, "idio_gc_expose: %10p no longer protected\n", o);
    r = idio_gc->roots;
    while (r) {
	idio_root_dump (r);
	r = r->next;
    }
}

void idio_gc_expose_all ()
{

    IDIO_FPRINTF (stderr, "idio_gc_expose_all\n");
    fprintf (stderr, "idio_gc_expose_all\n");
    idio_root_t *r = idio_gc->roots;
    while (r) {
	r->object = idio_S_nil;
	r = r->next;
    }
}

void idio_gc_mark ()
{

    IDIO_FPRINTF (stderr, "idio_gc_mark: all used -> WHITE %ux\n", IDIO_FLAG_GCC_WHITE);
    IDIO o = idio_gc->used;
    while (o) {
	idio_mark (o, IDIO_FLAG_GCC_WHITE);
	o = o->next;
    }    
    idio_gc->grey = NULL;

    IDIO_FPRINTF (stderr, "idio_gc_mark: roots -> BLACK %x\n", IDIO_FLAG_GCC_BLACK);
    idio_root_t *root = idio_gc->roots;
    while (root) {
	idio_root_mark (root, IDIO_FLAG_GCC_BLACK);
	root = root->next;
    }    

    IDIO_FPRINTF (stderr, "idio_gc_mark: process grey list\n");
    while (idio_gc->grey) {
	idio_process_grey (IDIO_FLAG_GCC_BLACK);
    }    

}

void idio_gc_sweep_free_value (IDIO vo)
{
    IDIO_ASSERT (vo);

    if (idio_S_nil == vo) {
	fprintf (stderr, "idio_gc_sweep_free_value: nil??\n");
	return;
    }

    if (vo->flags & IDIO_FLAG_FINALIZER) {
	idio_finalizer_run (vo);
    }

    /* idio_free_lex (vo); */

    switch (vo->type) {
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
	idio_free_C_type (vo);
	break;
    case IDIO_TYPE_C_POINTER:
	idio_free_C_pointer (vo);
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
	idio_free_closure (vo);
	break;
    case IDIO_TYPE_PRIMITIVE:
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
    case IDIO_TYPE_STRUCT_TYPE:
	idio_free_struct_type (vo);
	break;
    case IDIO_TYPE_STRUCT_INSTANCE:
	idio_free_struct_instance (vo);
	break;
    case IDIO_TYPE_THREAD:
	idio_free_thread (vo);
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
	IDIO_C_ASSERT (0);
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
	if ((co->flags & IDIO_FLAG_FREE_MASK) == IDIO_FLAG_FREE) {
	    fprintf (stderr, "idio_gc_sweep: already free?: ");
	    idio_gc->verbose++;
	    idio_dump (co, 1);
	    idio_gc->verbose--;
	    fprintf (stderr, "\n");
	}
	
	no = co->next;

	if (((co->flags & IDIO_FLAG_STICKY_MASK) == IDIO_FLAG_NOTSTICKY) &&
	    ((co->flags & IDIO_FLAG_GCC_MASK) == IDIO_FLAG_GCC_WHITE)) {
	    idio_gc->stats.nused[co->type]--;
	    IDIO_FPRINTF (stderr, "idio_gc_sweep: freeing %10p %2d %s\n", co, co->type, idio_type2string (co));
	    if (po) {
		po->next = co->next;
	    } else {
		idio_gc->used = co->next;
	    }

	    idio_gc_sweep_free_value (co);

	    co->flags = (co->flags & IDIO_FLAG_FREE_UMASK) | IDIO_FLAG_FREE;
	    co->next = idio_gc->free;
	    idio_gc->free = co;
	    idio_gc->stats.nfree++;
	    freed++;
	} else {
	    IDIO_FPRINTF (stderr, "idio_gc_sweep: keeping %10p %x == %x %x == %x\n", co, co->flags & IDIO_FLAG_STICKY_MASK, IDIO_FLAG_NOTSTICKY, co->flags & IDIO_FLAG_GCC_MASK, IDIO_FLAG_GCC_WHITE);
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
	(idio_gc->request ||
	 idio_gc->stats.igets > 0x1ffff)) {
	idio_gc_collect ();
    }
}

void idio_gc_collect ()
{
    /* idio_gc_walk_tree (); */
    /* idio_gc_stats ();   */
    
    if (idio_gc->pause) {
	/* fprintf (stderr, "GC paused\n"); */
	return;
    }

    idio_gc->request = 0;
    
    struct timeval t0;
    gettimeofday (&t0, NULL);

    idio_gc->stats.collections++;
    if (idio_gc->stats.igets > idio_gc->stats.mgets) {
	idio_gc->stats.mgets = idio_gc->stats.igets;
    }
    idio_gc->stats.igets = 0;
    /* IDIO_FPRINTF (stderr, "\nidio_gc_collect: %2d: pre-mark dump\n", idio_gc->stats.collections); */
    /* idio_gc_dump (f); */
    /* IDIO_FPRINTF (stderr, "\nidio_gc_collect: %2d: mark\n", idio_gc->stats.collections); */
    idio_gc_mark ();
    /* IDIO_FPRINTF (stderr, "\nidio_gc_collect: %2d: post-mark dump\n", idio_gc->stats.collections); */
    /* idio_gc_dump (f); */
    /* IDIO_FPRINTF (stderr, "\nidio_gc_collect: %2d: sweep\n", idio_gc->stats.collections); */
    idio_gc_sweep ();
    /* IDIO_FPRINTF (stderr, "\nidio_gc_collect: %2d: post-sweep dump\n", idio_gc->stats.collections); */
    /* idio_gc_dump (f); */

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
	idio_error_message ("GC stats: bad type %#x", type);
    } else {
	idio_gc->stats.tgets[type]++;
    }
}

void idio_gc_stats ()
{

    char scales[] = " KMGT";
    unsigned long long count;
    int scale;
    
    fprintf (stderr, "idio_gc_stats: %4lld   collections\n", idio_gc->stats.collections);

    count = idio_gc->stats.bounces;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_gc_stats: %4lld%c  bounces\n", count, scales[scale]);

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
    fprintf (stderr, "idio_gc_stats: %4lld%c total GC requests\n", count, scales[scale]);
    count = nused;
    scale = 0;
    idio_hcount (&count, &scale);
    fprintf (stderr, "idio_gc_stats: %4lld%c current GC requests\n", count, scales[scale]);
    fprintf (stderr, "idio_gc_stats: %-10.10s %5.5s %4.4s %5.5s %4.4s\n", "type", "total", "%age", "used", "%age");
    for (i = 1; i < IDIO_TYPE_MAX; i++) {
	unsigned long long tgets_count = idio_gc->stats.tgets[i];
	int tgets_scale = 0;
	idio_hcount (&tgets_count, &tgets_scale);
	unsigned long long nused_count = idio_gc->stats.nused[i];
	int nused_scale = 0;
	idio_hcount (&nused_count, &nused_scale);
    
	fprintf (stderr, "idio_gc_stats: %-10.10s %4lld%c %3lld %4lld%c %3lld\n",
		 idio_type_enum2string (i),
		 tgets_count, scales[tgets_scale],
		 tgets ? idio_gc->stats.tgets[i] * 100 / tgets : -1,
		 nused_count, scales[nused_scale],
		 nused ? idio_gc->stats.nused[i] * 100 / nused : -1
		 );
    }

    count = idio_gc->stats.mgets;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_gc_stats: %4lld%c  max requests between GC\n", count, scales[scale]);

    count = idio_gc->stats.reuse;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_gc_stats: %4lld%c  GC objects reused\n", count, scales[scale]);

    count = idio_gc->stats.allocs;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_gc_stats: %4lld%c  system allocs\n", count, scales[scale]);

    count = idio_gc->stats.tbytes;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_gc_stats: %4lld%cB total bytes referenced\n", count, scales[scale]);

    count = idio_gc->stats.nbytes;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_gc_stats: %4lld%cB current bytes referenced\n", count, scales[scale]);

    int rc = 0;
    idio_root_t *root = idio_gc->roots;
    idio_gc->verbose++;
    while (root) {
	switch (root->object->type) {
	default:
	    fprintf (stderr, "%p ", root->object);
	    idio_debug ("root object: %s\n", root->object);
	    rc++;
	    break;
	}
	root = root->next;
    }
    idio_gc->verbose--;

    count = rc;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_gc_stats: %4lld%c  protected objects\n", count, scales[scale]);

    int fc = 0;
    IDIO o = idio_gc->free;
    while (o) {
	fc++;
	o = o->next;
    }

    count = fc;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_gc_stats: %4lld%c  on free list\n", count, scales[scale]);

    int uc = 0;
    o = idio_gc->used;
    idio_gc->verbose++;
    while (o) {
	IDIO_ASSERT (o);
	uc++;
	if (o->next &&
	    o->next->type == 0) {
	    /* IDIO_ASSERT (o->next); */
	    fprintf (stderr, "bad type %10p\n", o->next);
	    o->next = o->next->next;
	}
	/* idio_dump (o, 160); */
	o = o->next;
    }
    idio_gc->verbose--;

    count = uc;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_gc_stats: %4lld%c  on used list\n", count, scales[scale]);

    fprintf (stderr, "idio-gc-stats: GC time %ld.%03ld\n", idio_gc->stats.dur.tv_sec, (long) idio_gc->stats.dur.tv_usec / 1000);

}

void idio_gc_pause ()
{

    idio_gc->pause++; 
}

void idio_gc_resume ()
{

    idio_gc->pause--; 
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
	    idio_error_param_nil ("root->object?");
	}
	free (root);
    }    

    /*
     * Having exposed everything running a collection should free
     * everything...
     */
    idio_gc_collect ();

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

int idio_gc_verboseness (int n)
{
    return (idio_gc->verbose >= n);
}

void idio_gc_set_verboseness (int n)
{
    idio_gc->verbose = n;
}

void idio_init_gc ()
{
    idio_gc = idio_gc_new ();

    idio_gc->verbose = 0;

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

	sum -= 96;		/* thread */
	fprintf (stderr, "sum = %zd, avg = %zd\n", sum, sum / n); 

    }
    
    idio_gc_finalizer_hash = IDIO_HASH_EQP (64);
    idio_gc_protect (idio_gc_finalizer_hash);
    IDIO_HASH_FLAGS (idio_gc_finalizer_hash) |= IDIO_HASH_FLAG_STRING_KEYS;
}

void idio_run_all_finalizers ()
{
    if (idio_S_nil == idio_gc_finalizer_hash) {
	return;
    }
    
    idio_ai_t hi;
    for (hi = 0; hi < IDIO_HASH_SIZE (idio_gc_finalizer_hash); hi++) {
	IDIO k = IDIO_HASH_HE_KEY (idio_gc_finalizer_hash, hi);
	if (idio_S_nil != k) {
	    /* apply the finalizer */
	    idio_apply (IDIO_HASH_HE_VALUE (idio_gc_finalizer_hash, hi),
			idio_pair (k, idio_S_nil));

	    /* expunge the key/value pair from this hash */
	    idio_hash_delete (idio_gc_finalizer_hash, k);
	}
    }
}

void idio_final_gc ()
{
    idio_run_all_finalizers ();

    /* unprotect the finalizer hash itself */
    idio_gc_expose (idio_gc_finalizer_hash);
    /*  prevent it being used */
    idio_gc_finalizer_hash = idio_S_nil;

    fprintf (stderr, "\n\n\nFINAL GC\n\n\n");
    idio_gc_stats ();
    idio_gc_collect ();
    idio_gc_dump ();
    idio_gc_free ();
}

/*
  XXX delete me
 */

void idio_expr_dump_ (IDIO e, const char *en, int detail)
{
    fprintf (stderr, "%20s=", en);
    idio_gc->verbose++;
    idio_dump (e, detail);
    idio_gc->verbose--;
}




