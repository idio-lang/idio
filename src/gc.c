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
    idio_gc_finalizer_hash = IDIO_HASH_EQP (idio_G_frame, 64);
    idio_collector_protect (idio_G_frame, idio_gc_finalizer_hash);
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
    idio_collector_expose (idio_G_frame, idio_gc_finalizer_hash);
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
 * idio_collector_get_alloc allocates another IDIO -- or pool thereof
 * -- and returns it
 */
IDIO idio_collector_get_alloc (idio_collector_t *collector)
{
    IDIO_C_ASSERT (collector);
    
    IDIO o = idio_alloc (sizeof (idio_t));
    o->next = NULL;
    
    collector->stats.nbytes += sizeof (idio_t);
    collector->stats.tbytes += sizeof (idio_t);
    
    return o;
}

/*
 * idio_collector_get finds the next available IDIO from the free list
 * or calls idio_collector_get_alloc to get one
 */
IDIO idio_collector_get (idio_collector_t *collector, idio_type_e type)
{
    IDIO_C_ASSERT (collector);
    IDIO_C_ASSERT (type);
    
    IDIO_C_ASSERT (type > IDIO_TYPE_NONE &&
		   type < IDIO_TYPE_MAX);

    collector->stats.nused[type]++;
    collector->stats.tgets[type]++;
    collector->stats.igets++;

    IDIO o = collector->free;
    if (NULL == o) {
	collector->stats.allocs++;
	o = idio_collector_get_alloc (collector);
    } else {
	collector->stats.reuse++;
	collector->free = o->next;
    }

    /* assign type late in case we've re-used a previous object */
    o->type = type;
    o->flags = IDIO_FLAG_NONE;
    
    IDIO_ASSERT (o);
    if (NULL != collector->used) {
	IDIO_ASSERT (collector->used);
    }
    o->next = collector->used;
    collector->used = o;

    return o;
}

/*
 * idio_get gets an IDIO of this type
 */
IDIO idio_get (IDIO f, idio_type_e type)
{
    IDIO_ASSERT (f);
    IDIO_C_ASSERT (type);
    
    idio_collector_t *collector = IDIO_COLLECTOR (f);
    
    return idio_collector_get (collector, type);
}

void idio_collector_alloc (idio_collector_t *collector, void **p, size_t size)
{
    IDIO_C_ASSERT (collector);
    
    *p = idio_alloc (size);
    collector->stats.nbytes += size;
    collector->stats.tbytes += size;
}

IDIO idio_clone_base (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    idio_collector_t *collector = IDIO_COLLECTOR (f);
    
    return idio_collector_get (collector, o->type);
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
	IDIO_C_ASSERT (idio_isa_C_pointer (f, ofunc));

	void (*p) (IDIO o) = IDIO_C_TYPE_POINTER_P (ofunc);
	(*p) (o);
    }

    idio_hash_delete (f, idio_gc_finalizer_hash, o);
}

void idio_mark (IDIO f, IDIO o, unsigned colour)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);
    
    idio_collector_t *collector = IDIO_COLLECTOR (f);

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
	IDIO_FRAME_COLLECTOR (f)->verbose++;
	idio_dump (f, o, 1);
	IDIO_FRAME_COLLECTOR (f)->verbose--;
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
	    IDIO_PAIR_GREY (o) = collector->grey;
	    collector->grey = o;
	    break;
	case IDIO_TYPE_ARRAY:
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_ARRAY_GREY (o) = collector->grey;
	    collector->grey = o;
	    break;
	case IDIO_TYPE_HASH:
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_HASH_GREY (o) = collector->grey;
	    collector->grey = o;
	    break;
	case IDIO_TYPE_CLOSURE:
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_CLOSURE_GREY (o) = collector->grey;
	    collector->grey = o;
	    break;
	case IDIO_TYPE_C_FFI:
	    IDIO_C_ASSERT (IDIO_C_FFI_GREY (o) != o);
	    IDIO_C_ASSERT (collector->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_C_FFI_GREY (o) = collector->grey;
	    collector->grey = o;
	    break;
	case IDIO_TYPE_C_STRUCT:
	    IDIO_C_ASSERT (IDIO_C_STRUCT_GREY (o) != o);
	    IDIO_C_ASSERT (collector->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_C_STRUCT_GREY (o) = collector->grey;
	    collector->grey = o;
	    break;
	case IDIO_TYPE_C_INSTANCE:
	    IDIO_C_ASSERT (IDIO_C_INSTANCE_GREY (o) != o);
	    IDIO_C_ASSERT (collector->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_C_INSTANCE_GREY (o) = collector->grey;
	    collector->grey = o;
	    break;
	case IDIO_TYPE_FRAME:
	    IDIO_C_ASSERT (IDIO_FRAME_GREY (o) != o);
	    IDIO_C_ASSERT (collector->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_FRAME_GREY (o) = collector->grey;
	    collector->grey = o;
	    break;
	case IDIO_TYPE_OPAQUE:
	    IDIO_C_ASSERT (IDIO_OPAQUE_GREY (o) != o);
	    IDIO_C_ASSERT (collector->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_OPAQUE_GREY (o) = collector->grey;
	    collector->grey = o;
	    break;
	case IDIO_TYPE_C_TYPEDEF:
	    IDIO_C_ASSERT (IDIO_C_TYPEDEF_GREY (o) != o);
	    IDIO_C_ASSERT (collector->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_C_TYPEDEF_GREY (o) = collector->grey;
	    collector->grey = o;
	    break;
	case IDIO_TYPE_BIGNUM:
	    IDIO_C_ASSERT (IDIO_BIGNUM_GREY (o) != o);
	    IDIO_C_ASSERT (collector->grey != o);
	    o->flags |= IDIO_FLAG_GCC_LGREY;
	    IDIO_BIGNUM_GREY (o) = collector->grey;
	    collector->grey = o;
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

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    IDIO o = collector->grey;

    if (NULL == o) {
	return;
    }

    size_t i;

    o->flags &= IDIO_FLAG_GCC_UMASK;
    o->flags = (o->flags & IDIO_FLAG_GCC_UMASK) | IDIO_FLAG_GCC_BLACK;
    
    switch (o->type) {
    case IDIO_TYPE_PAIR:
	collector->grey = IDIO_PAIR_GREY (o);
	idio_mark (f, IDIO_PAIR_H (o), colour);
	idio_mark (f, IDIO_PAIR_T (o), colour);
	break;
    case IDIO_TYPE_ARRAY:
	collector->grey = IDIO_ARRAY_GREY (o);
	for (i = 0; i < IDIO_ARRAY_ASIZE (o); i++) {
	    if (NULL != IDIO_ARRAY_AE (o, i)) {
		idio_mark (f, IDIO_ARRAY_AE (o, i), colour);
	    }
	}
	break;
    case IDIO_TYPE_HASH:
	collector->grey = IDIO_HASH_GREY (o);
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
	collector->grey = IDIO_CLOSURE_GREY (o);
	idio_mark (f, IDIO_CLOSURE_ARGS (o), colour);
	idio_mark (f, IDIO_CLOSURE_BODY (o), colour);
	idio_mark (f, IDIO_CLOSURE_FRAME (o), colour);
	break;
    case IDIO_TYPE_C_FFI:
	IDIO_C_ASSERT (collector->grey != IDIO_C_FFI_GREY (o));
	collector->grey = IDIO_C_FFI_GREY (o);
	idio_mark (f, IDIO_C_FFI_SYMBOL (o), colour);
	idio_mark (f, IDIO_C_FFI_RESULT (o), colour);
	idio_mark (f, IDIO_C_FFI_ARGS (o), colour);
	idio_mark (f, IDIO_C_FFI_NAME (o), colour);
	break;
    case IDIO_TYPE_C_STRUCT:
	IDIO_C_ASSERT (collector->grey != IDIO_C_STRUCT_GREY (o));
	collector->grey = IDIO_C_STRUCT_GREY (o);
	idio_mark (f, IDIO_C_STRUCT_SLOTS (o), colour);
	idio_mark (f, IDIO_C_STRUCT_METHODS (o), colour);
	idio_mark (f, IDIO_C_STRUCT_FRAME (o), colour);
	break;
    case IDIO_TYPE_C_INSTANCE:
	IDIO_C_ASSERT (collector->grey != IDIO_C_INSTANCE_GREY (o));
	collector->grey = IDIO_C_INSTANCE_GREY (o);
	idio_mark (f, IDIO_C_INSTANCE_C_STRUCT (o), colour);
	idio_mark (f, IDIO_C_INSTANCE_FRAME (o), colour);
	break;
    case IDIO_TYPE_FRAME:
	IDIO_C_ASSERT (collector->grey != IDIO_FRAME_GREY (o));
	collector->grey = IDIO_FRAME_GREY (o);
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
    case IDIO_TYPE_OPAQUE:
	IDIO_C_ASSERT (collector->grey != IDIO_OPAQUE_GREY (o));
	collector->grey = IDIO_OPAQUE_GREY (o);
	idio_mark (f, IDIO_OPAQUE_ARGS (o), colour);
	break;
    case IDIO_TYPE_C_TYPEDEF:
	IDIO_C_ASSERT (collector->grey != IDIO_C_TYPEDEF_GREY (o));
	collector->grey = IDIO_C_TYPEDEF_GREY (o);
	idio_mark (f, IDIO_C_TYPEDEF_SYM (o), colour);
	break;
    case IDIO_TYPE_BIGNUM:
	IDIO_C_ASSERT (collector->grey != IDIO_BIGNUM_GREY (o));
	collector->grey = IDIO_BIGNUM_GREY (o);
	idio_mark (f, IDIO_BIGNUM_SIG (o), colour);
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

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    idio_root_t *r = idio_alloc (sizeof (idio_root_t));
    r->next = collector->roots;
    collector->roots = r;
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

idio_collector_t *idio_collector_new ()
{
    idio_collector_t *c = idio_alloc (sizeof (idio_collector_t));
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
void IDIO_COLLECTOR_FPRINTF (idio_collector_t *collector, FILE *stream, const char *format, ...)
{
    IDIO_C_ASSERT (collector);
    IDIO_C_ASSERT (stream);
    IDIO_C_ASSERT (format);

    va_list fmt_args;
    va_start (fmt_args, format);
    
    if (collector->verbose) {
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

    if (IDIO_FRAME_COLLECTOR (f)->verbose) {
	vfprintf (stream, format, fmt_args);
    }
    
    va_end (fmt_args);
}
#endif

void idio_walk_tree (IDIO f)
{
    IDIO_ASSERT (f);

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    collector->verbose++;
    
    IDIO_FRAME_FPRINTF (f, stderr, "idio_walk_tree: \n");

    size_t ri = 0;
    idio_root_t *root = collector->roots;
    while (root) {
	fprintf (stderr, "ri %3zd: ", ri++);
	if (idio_G_frame == root->object) {
	    fprintf (stderr, "== idio_G_frame: ");
	}
	idio_dump (f, root->object, 16);
	root = root->next;
    }

    collector->verbose--;
}

void idio_collector_dump (IDIO f)
{
    IDIO_ASSERT (f);

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    IDIO_FRAME_FPRINTF (f, stderr, "\ndump\n");
    IDIO_FRAME_FPRINTF (f, stderr, "idio_collector_dump: self @%10p\n", collector);
    
    idio_root_t *root = collector->roots;
    while (root) {
	idio_root_dump (f, root);
	root = root->next;
    }

    IDIO_FRAME_FPRINTF (f, stderr, "idio_collector_dump: free list\n");
    IDIO o = collector->free;
    while (o) {
	/*
	  Can't actually dump the free objects as the code to print
	  objects out asserts whether they are free or not...

	  However, walking down the chain checks the chain is valid.
	 */
	o = o->next;
    }

    IDIO_FRAME_FPRINTF (f, stderr, "idio_collector_dump: used list\n");
    o = collector->used;
    while (o) {
	IDIO_ASSERT (o);
	idio_dump (f, o, 1);
	o = o->next;
    }
}

void idio_collector_protect (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    IDIO_FRAME_FPRINTF (f, stderr, "idio_collector_protect: %10p\n", o);

    idio_root_t *r = collector->roots;
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

void idio_collector_expose (IDIO f, IDIO o)
{
    IDIO_ASSERT (f);
    IDIO_ASSERT (o);

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    IDIO_FRAME_FPRINTF (f, stderr, "idio_collector_expose: %10p\n", o);

    int seen = 0;
    idio_root_t *r = collector->roots;
    while (r) {
	if (r->object == o) {
	    seen = 1;
	    r->object = idio_S_nil;
	    break;
	}
	r = r->next;
    }

    if (0 == seen) {
	fprintf (stderr, "idio_collector_expose: o %10p not previously protected\n", o);
	r = collector->roots;
	while (r) {
	    fprintf (stderr, "idio_collector_expose: currently protected: %10p %s\n", r->object, idio_type2string (r->object));
	    r = r->next;
	}
	IDIO_C_ASSERT (seen);
	return;
    }

    IDIO_FRAME_FPRINTF (f, stderr, "idio_collector_expose: %10p not protected\n", o);
    r = collector->roots;
    while (r) {
	idio_root_dump (f, r);
	r = r->next;
    }
}

void idio_collector_expose_all (IDIO f)
{
    IDIO_ASSERT (f);

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    IDIO_FRAME_FPRINTF (f, stderr, "idio_collector_expose_all\n");
    fprintf (stderr, "idio_collector_expose_all\n");
    idio_root_t *r = collector->roots;
    while (r) {
	r->object = idio_S_nil;
	r = r->next;
    }
}

void idio_collector_mark (IDIO f)
{
    IDIO_ASSERT (f);

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    IDIO_FRAME_FPRINTF (f, stderr, "idio_collector_mark: all used -> WHITE\n");
    IDIO o = collector->used;
    while (o) {
	idio_mark (f, o, IDIO_FLAG_GCC_WHITE);
	o = o->next;
    }    
    collector->grey = NULL;

    IDIO_FRAME_FPRINTF (f, stderr, "idio_collector_mark: roots -> BLACK\n");
    idio_root_t *root = collector->roots;
    while (root) {
	idio_root_mark (f, root, IDIO_FLAG_GCC_BLACK);
	root = root->next;
    }    

    IDIO_FRAME_FPRINTF (f, stderr, "idio_collector_mark: process grey list\n");
    while (collector->grey) {
	idio_process_grey (f, IDIO_FLAG_GCC_BLACK);
    }    

}

void idio_collector_sweep (IDIO f)
{
    IDIO_ASSERT (f);

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    IDIO_FRAME_FPRINTF (f, stderr, "idio_collector_sweep: clear free list\n");
    while (collector->free) {
	IDIO co = collector->free;
	collector->free = co->next;
	free (co);
    }

    IDIO_FRAME_FPRINTF (f, stderr, "idio_collector_sweep: used list\n");
    IDIO co = collector->used;
    IDIO po = NULL;
    IDIO no = NULL;
    while (co) {
	IDIO_ASSERT (co);
	if ((co->flags & IDIO_FLAG_FREE_MASK) == IDIO_FLAG_FREE) {
	    fprintf (stderr, "idio_collector_sweep: already free?: ");
	    IDIO_FRAME_COLLECTOR (f)->verbose++;
	    idio_dump (f, co, 1);
	    IDIO_FRAME_COLLECTOR (f)->verbose--;
	    fprintf (stderr, "\n");
	}
	
	no = co->next;

	if (((co->flags & IDIO_FLAG_STICKY_MASK) == IDIO_FLAG_NOTSTICKY) &&
	    IDIO_FLAG_GCC_WHITE == idio_bw (f, co)) {
	    collector->stats.nused[co->type]--;
	    IDIO_FRAME_FPRINTF (f, stderr, "idio_collector_sweep: freeing %10p %2d %s\n", co, co->type, idio_type2string (co));
	    if (po) {
		po->next = co->next;
	    } else {
		collector->used = co->next;
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
	    case IDIO_TYPE_C_FFI:
		idio_free_C_FFI (f, co);
		break;
	    case IDIO_TYPE_C_STRUCT:
		idio_free_C_struct (f, co);
		break;
	    case IDIO_TYPE_C_INSTANCE:
		idio_free_C_instance (f, co);
		break;
	    case IDIO_TYPE_OPAQUE:
		idio_free_opaque (f, co);
		break;
	    case IDIO_TYPE_C_TYPEDEF:
		idio_free_C_typedef (f, co);
		break;
	    default:
		IDIO_C_ASSERT (0);
		break;
	    }

	    co->flags = (co->flags & IDIO_FLAG_FREE_UMASK) | IDIO_FLAG_FREE;
	    co->next = collector->free;
	    collector->free = co;
	    
	} else {
	    po = co;
	}

	co = no;
    }
}

void idio_collector_collect (IDIO f)
{
    IDIO_ASSERT (f);

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    IDIO_C_ASSERT (collector->pause == 0);

    collector->stats.collections++;
    if (collector->stats.igets > collector->stats.mgets) {
	collector->stats.mgets = collector->stats.igets;
    }
    collector->stats.igets = 0;
    /* IDIO_FRAME_FPRINTF (f, stderr, "\nidio_collector_collect: %2d: pre-mark dump\n", collector->stats.collections); */
    /* idio_collector_dump (f); */
    /* IDIO_FRAME_FPRINTF (f, stderr, "\nidio_collector_collect: %2d: mark\n", collector->stats.collections); */
    idio_collector_mark (f);
    /* IDIO_FRAME_FPRINTF (f, stderr, "\nidio_collector_collect: %2d: post-mark dump\n", collector->stats.collections); */
    /* idio_collector_dump (f); */
    /* IDIO_FRAME_FPRINTF (f, stderr, "\nidio_collector_collect: %2d: sweep\n", collector->stats.collections); */
    idio_collector_sweep (f);
    /* IDIO_FRAME_FPRINTF (f, stderr, "\nidio_collector_collect: %2d: post-sweep dump\n", collector->stats.collections); */
    /* idio_collector_dump (f); */
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

void idio_collector_stats (IDIO f)
{
    IDIO_ASSERT (f);

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    char scales[] = " KMGT";
    unsigned long long count;
    int scale;
    
    fprintf (stderr, "idio_collector_stats: %4lld   collections\n", collector->stats.collections);

    count = collector->stats.bounces;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_collector_stats: %4lld%c  bounces\n", count, scales[scale]);

    int i;
    int tgets = 0;
    int nused = 0;
    for (i = 1; i < IDIO_TYPE_MAX; i++) {
	tgets += collector->stats.tgets[i];
	nused += collector->stats.nused[i];
    }
    count = tgets;
    scale = 0;
    idio_hcount (&count, &scale);
    fprintf (stderr, "idio_collector_stats: %4lld%c total GC requests\n", count, scales[scale]);
    count = nused;
    scale = 0;
    idio_hcount (&count, &scale);
    fprintf (stderr, "idio_collector_stats: %4lld%c current GC requests\n", count, scales[scale]);
    fprintf (stderr, "idio_collector_stats: %-10.10s %5.5s %4.4s %5.5s %4.4s\n", "type", "total", "%age", "used", "%age");
    for (i = 1; i < IDIO_TYPE_MAX; i++) {
	unsigned long long tgets_count = collector->stats.tgets[i];
	int tgets_scale = 0;
	idio_hcount (&tgets_count, &tgets_scale);
	unsigned long long nused_count = collector->stats.nused[i];
	int nused_scale = 0;
	idio_hcount (&nused_count, &nused_scale);
    
	fprintf (stderr, "idio_collector_stats: %-10.10s %4lld%c %3lld %4lld%c %3lld\n",
		 idio_type_enum2string (i),
		 tgets_count, scales[tgets_scale],
		 collector->stats.tgets[i] * 100 / tgets,
		 nused_count, scales[nused_scale],
		 collector->stats.nused[i] * 100 / nused
		 );
    }

    count = collector->stats.mgets;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_collector_stats: %4lld%c  max requests between GC\n", count, scales[scale]);

    count = collector->stats.reuse;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_collector_stats: %4lld%c  GC objects reused\n", count, scales[scale]);

    count = collector->stats.allocs;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_collector_stats: %4lld%c  system allocs\n", count, scales[scale]);

    count = collector->stats.tbytes;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_collector_stats: %4lld%cB total bytes referenced\n", count, scales[scale]);

    count = collector->stats.nbytes;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_collector_stats: %4lld%cB current bytes referenced\n", count, scales[scale]);

    int rc = 0;
    idio_root_t *root = collector->roots;
    collector->verbose++;
    while (root) {
	switch (root->object->type) {
	default:
	    idio_dump (f, root->object, 3);
	    rc++;
	    break;
	}
	root = root->next;
    }
    collector->verbose--;

    count = rc;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_collector_stats: %4lld%c  protected objects\n", count, scales[scale]);

    int fc = 0;
    IDIO o = collector->free;
    while (o) {
	fc++;
	o = o->next;
    }

    count = fc;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_collector_stats: %4lld%c  on free list\n", count, scales[scale]);

    int uc = 0;
    o = collector->used;
    collector->verbose++;
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
    collector->verbose--;

    count = uc;
    scale = 0;
    idio_hcount (&count, &scale);
    
    fprintf (stderr, "idio_collector_stats: %4lld%c  on used list\n", count, scales[scale]);
}

void idio_collector_pause (IDIO f)
{
    IDIO_ASSERT (f);

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    collector->pause++; 
}

void idio_collector_resume (IDIO f)
{
    IDIO_ASSERT (f);

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    collector->pause--; 
}

void idio_collector_ports_free (IDIO f)
{
    IDIO_ASSERT (f);

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    size_t pl = idio_array_size (f, collector->input_port);
    for (;pl; pl--) {
	idio_array_pop (f, collector->input_port);
    }

    pl = idio_array_size (f, collector->output_port);
    for (;pl; pl--) {
	idio_array_pop (f, collector->output_port);
    }
}

void idio_collector_free (IDIO f)
{
    IDIO_ASSERT (f);

    idio_collector_t *collector = IDIO_COLLECTOR (f);

    idio_collector_stats (f);

    /*
      Things with finalizers will try to use embedded references which
      may have been freed by collector_sweep (because we will remove
      all roots before we call it).

      We know ports have finalizers.
     */
    idio_collector_ports_free (f);
    
    /*
      Careful!  Still need a frame to call the collector functions
      with!
     */
    IDIO dummyf = idio_collector_frame (collector, 37, 0);
    IDIO_FRAME_COLLECTOR (dummyf) = collector;
    IDIO_FRAME_FORM (dummyf) = idio_S_nil;
    IDIO_FRAME_PFRAME (dummyf) = NULL;
    IDIO_FRAME_NAMESPACE (dummyf) = idio_S_nil;
    IDIO_FRAME_ENV (dummyf) = idio_S_nil;
    IDIO_FRAME_STACK (dummyf) = idio_S_nil;
    IDIO_FRAME_THREADS (dummyf) = idio_S_nil;
    IDIO_FRAME_ERROR (dummyf) = idio_S_nil;

    /* idio_collector_expose_all (f); */

    while (collector->roots) {
	idio_root_t *root = collector->roots;
	collector->roots = root->next;
	free (root);
    }    

    collector->roots = idio_root_new (f);
    collector->roots->object = dummyf;
    collector->roots->next = NULL;

    /* IDIO_FRAME_COLLECTOR (dummyf)->verbose++;  */
    idio_collector_collect (dummyf);
    /* idio_collector_dump (dummyf); */

    while (collector->free) {
	IDIO co = collector->free;
	collector->free = co->next;
	free (co);
    }

    free (collector->roots);
    free (collector);
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
