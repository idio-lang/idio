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
 * c-type.c
 * 
 */

#include "idio.h"

IDIO idio_C_int8 (int8_t i)
{

    IDIO co = idio_gc_get (IDIO_TYPE_C_INT8);

    IDIO_GC_ALLOC (co->u.C_type, sizeof (idio_C_type_t));
    
    IDIO_C_TYPE_INT8 (co) = i;

    return co;
}

int idio_isa_C_int8 (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_INT8);
}

IDIO idio_C_uint8 (uint8_t i)
{

    IDIO co = idio_gc_get (IDIO_TYPE_C_UINT8);

    IDIO_GC_ALLOC (co->u.C_type, sizeof (idio_C_type_t));
    
    IDIO_C_TYPE_UINT8 (co) = i;

    return co;
}

int idio_isa_C_uint8 (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_UINT8);
}

IDIO idio_C_int16 (int16_t i)
{

    IDIO co = idio_gc_get (IDIO_TYPE_C_INT16);

    IDIO_GC_ALLOC (co->u.C_type, sizeof (idio_C_type_t));
    
    IDIO_C_TYPE_INT16 (co) = i;

    return co;
}

int idio_isa_C_int16 (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_INT16);
}

IDIO idio_C_uint16 (uint16_t i)
{

    IDIO co = idio_gc_get (IDIO_TYPE_C_UINT16);

    IDIO_GC_ALLOC (co->u.C_type, sizeof (idio_C_type_t));
    
    IDIO_C_TYPE_UINT16 (co) = i;

    return co;
}

int idio_isa_C_uint16 (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_UINT16);
}

IDIO idio_C_int32 (int32_t i)
{

    IDIO co = idio_gc_get (IDIO_TYPE_C_INT32);

    IDIO_GC_ALLOC (co->u.C_type, sizeof (idio_C_type_t));
    
    IDIO_C_TYPE_INT32 (co) = i;

    return co;
}

int idio_isa_C_int32 (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_INT32);
}

IDIO idio_C_uint32 (uint32_t i)
{

    IDIO co = idio_gc_get (IDIO_TYPE_C_UINT32);

    IDIO_GC_ALLOC (co->u.C_type, sizeof (idio_C_type_t));
    
    IDIO_C_TYPE_UINT32 (co) = i;

    return co;
}

int idio_isa_C_uint32 (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_UINT32);
}

IDIO idio_C_int64 (int64_t i)
{

    IDIO co = idio_gc_get (IDIO_TYPE_C_INT64);

    IDIO_GC_ALLOC (co->u.C_type, sizeof (idio_C_type_t));
    
    IDIO_C_TYPE_INT64 (co) = i;

    return co;
}

int idio_isa_C_int64 (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_INT64);
}

IDIO idio_C_uint64 (uint64_t i)
{

    IDIO co = idio_gc_get (IDIO_TYPE_C_UINT64);

    IDIO_GC_ALLOC (co->u.C_type, sizeof (idio_C_type_t));
    
    IDIO_C_TYPE_UINT64 (co) = i;

    return co;
}

int idio_isa_C_uint64 (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_UINT64);
}

IDIO idio_C_float (float fl)
{

    IDIO co = idio_gc_get (IDIO_TYPE_C_FLOAT);

    IDIO_GC_ALLOC (co->u.C_type, sizeof (idio_C_type_t));
    
    IDIO_C_TYPE_FLOAT (co) = fl;

    return co;
}

int idio_isa_C_float (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_FLOAT);
}

IDIO idio_C_double (double d)
{

    IDIO co = idio_gc_get (IDIO_TYPE_C_DOUBLE);

    IDIO_GC_ALLOC (co->u.C_type, sizeof (idio_C_type_t));
    
    IDIO_C_TYPE_DOUBLE (co) = d;

    return co;
}

int idio_isa_C_double (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_DOUBLE);
}

void idio_free_C_type (IDIO co)
{
    IDIO_ASSERT (co);

    idio_gc_stats_free (sizeof (idio_C_type_t));

    free (co->u.C_type);
}

IDIO idio_C_pointer (void *p)
{

    IDIO co = idio_gc_get (IDIO_TYPE_C_POINTER);

    IDIO_GC_ALLOC (co->u.C_type, sizeof (idio_C_type_t));
    IDIO_GC_ALLOC (IDIO_C_TYPE_POINTER (co), sizeof (idio_C_pointer_t));

    IDIO_C_TYPE_POINTER_P (co) = p;
    IDIO_C_TYPE_POINTER_FREEP (co) = 0;

    return co;
}

IDIO idio_C_pointer_free_me (void *p)
{

    IDIO co = idio_C_pointer (p);

    IDIO_C_TYPE_POINTER_FREEP (co) = 1;

    return co;
}

int idio_isa_C_pointer (IDIO co)
{
    IDIO_ASSERT (co);

    return idio_isa (co, IDIO_TYPE_C_POINTER);
}

void idio_free_C_pointer (IDIO co)
{
    IDIO_ASSERT (co);

    idio_gc_stats_free (sizeof (idio_C_pointer_t));

    if (IDIO_C_TYPE_POINTER_FREEP (co)) {
	free (IDIO_C_TYPE_POINTER_P (co));
    }

    free (co->u.C_type->u.C_pointer);
    free (co->u.C_type);
}

int idio_isa_C_number (IDIO co)
{
    IDIO_ASSERT (co);

    switch (co->type) {
    case IDIO_TYPE_C_INT64:
    case IDIO_TYPE_C_UINT64:
    case IDIO_TYPE_C_INT32:
    case IDIO_TYPE_C_UINT32:
    case IDIO_TYPE_C_INT8:
    case IDIO_TYPE_C_UINT8:
    case IDIO_TYPE_C_INT16:
    case IDIO_TYPE_C_UINT16:
    case IDIO_TYPE_C_FLOAT:
    case IDIO_TYPE_C_DOUBLE:
	return 1;
    }

    return 0;
}

IDIO idio_C_number_cast (IDIO co, int type)
{
    IDIO_ASSERT (co);
    IDIO_C_ASSERT (type);

    if (! IDIO_TYPE_POINTERP (co)) {
	char em[BUFSIZ];
	sprintf (em, "idio_C_number_cast: conversion not possible from %s %d to %d", idio_type2string (co), co->type, type);
	idio_error_add_C (em);
	return idio_S_nil;
    }

    IDIO r = idio_S_nil;
    int fail = 0;
    
    switch (type) {
    case IDIO_TYPE_C_INT8:
	{
	    switch (co->type) {
	    case IDIO_TYPE_C_INT8: r = idio_C_int8 ((int8_t) IDIO_C_TYPE_INT8 (co)); break;
	    case IDIO_TYPE_C_UINT8: r = idio_C_int8 ((int8_t) IDIO_C_TYPE_UINT8 (co)); break;
	    case IDIO_TYPE_C_INT16: r = idio_C_int8 ((int8_t) IDIO_C_TYPE_INT16 (co)); break;
	    case IDIO_TYPE_C_UINT16: r = idio_C_int8 ((int8_t) IDIO_C_TYPE_UINT16 (co)); break;
	    case IDIO_TYPE_C_INT32: r = idio_C_int8 ((int8_t) IDIO_C_TYPE_INT32 (co)); break;
	    case IDIO_TYPE_C_UINT32: r = idio_C_int8 ((int8_t) IDIO_C_TYPE_UINT32 (co)); break;
	    case IDIO_TYPE_C_INT64: r = idio_C_int8 ((int8_t) IDIO_C_TYPE_INT64 (co)); break;
	    case IDIO_TYPE_C_UINT64: r = idio_C_int8 ((int8_t) IDIO_C_TYPE_UINT64 (co)); break;
	    case IDIO_TYPE_C_FLOAT: r = idio_C_int8 ((int8_t) IDIO_C_TYPE_FLOAT (co)); break;
	    case IDIO_TYPE_C_DOUBLE: r = idio_C_int8 ((int8_t) IDIO_C_TYPE_DOUBLE (co)); break;
	    default:
		fail = 1;
		break;
	    }
	    break;
	}
    case IDIO_TYPE_C_UINT8:
	{
	    switch (co->type) {
	    case IDIO_TYPE_C_INT8: r = idio_C_uint8 ((uint8_t) IDIO_C_TYPE_INT8 (co)); break;
	    case IDIO_TYPE_C_UINT8: r = idio_C_uint8 ((uint8_t) IDIO_C_TYPE_UINT8 (co)); break;
	    case IDIO_TYPE_C_INT16: r = idio_C_uint8 ((uint8_t) IDIO_C_TYPE_INT16 (co)); break;
	    case IDIO_TYPE_C_UINT16: r = idio_C_uint8 ((uint8_t) IDIO_C_TYPE_UINT16 (co)); break;
	    case IDIO_TYPE_C_INT32: r = idio_C_uint8 ((uint8_t) IDIO_C_TYPE_INT32 (co)); break;
	    case IDIO_TYPE_C_UINT32: r = idio_C_uint8 ((uint8_t) IDIO_C_TYPE_UINT32 (co)); break;
	    case IDIO_TYPE_C_INT64: r = idio_C_uint8 ((uint8_t) IDIO_C_TYPE_INT64 (co)); break;
	    case IDIO_TYPE_C_UINT64: r = idio_C_uint8 ((uint8_t) IDIO_C_TYPE_UINT64 (co)); break;
	    case IDIO_TYPE_C_FLOAT: r = idio_C_uint8 ((uint8_t) IDIO_C_TYPE_FLOAT (co)); break;
	    case IDIO_TYPE_C_DOUBLE: r = idio_C_uint8 ((uint8_t) IDIO_C_TYPE_DOUBLE (co)); break;
	    default:
		fail = 1;
		break;
	    }
	    break;
	}
    case IDIO_TYPE_C_INT16:
	{
	    switch (co->type) {
	    case IDIO_TYPE_C_INT8: r = idio_C_int16 ((int16_t) IDIO_C_TYPE_INT8 (co)); break;
	    case IDIO_TYPE_C_UINT8: r = idio_C_int16 ((int16_t) IDIO_C_TYPE_UINT8 (co)); break;
	    case IDIO_TYPE_C_INT16: r = idio_C_int16 ((int16_t) IDIO_C_TYPE_INT16 (co)); break;
	    case IDIO_TYPE_C_UINT16: r = idio_C_int16 ((int16_t) IDIO_C_TYPE_UINT16 (co)); break;
	    case IDIO_TYPE_C_INT32: r = idio_C_int16 ((int16_t) IDIO_C_TYPE_INT32 (co)); break;
	    case IDIO_TYPE_C_UINT32: r = idio_C_int16 ((int16_t) IDIO_C_TYPE_UINT32 (co)); break;
	    case IDIO_TYPE_C_INT64: r = idio_C_int16 ((int16_t) IDIO_C_TYPE_INT64 (co)); break;
	    case IDIO_TYPE_C_UINT64: r = idio_C_int16 ((int16_t) IDIO_C_TYPE_UINT64 (co)); break;
	    case IDIO_TYPE_C_FLOAT: r = idio_C_int16 ((int16_t) IDIO_C_TYPE_FLOAT (co)); break;
	    case IDIO_TYPE_C_DOUBLE: r = idio_C_int16 ((int16_t) IDIO_C_TYPE_DOUBLE (co)); break;
	    default:
		fail = 1;
		break;
	    }
	    break;
	}
    case IDIO_TYPE_C_UINT16:
	{
	    switch (co->type) {
	    case IDIO_TYPE_C_INT8: r = idio_C_uint16 ((uint16_t) IDIO_C_TYPE_INT8 (co)); break;
	    case IDIO_TYPE_C_UINT8: r = idio_C_uint16 ((uint16_t) IDIO_C_TYPE_UINT8 (co)); break;
	    case IDIO_TYPE_C_INT16: r = idio_C_uint16 ((uint16_t) IDIO_C_TYPE_INT16 (co)); break;
	    case IDIO_TYPE_C_UINT16: r = idio_C_uint16 ((uint16_t) IDIO_C_TYPE_UINT16 (co)); break;
	    case IDIO_TYPE_C_INT32: r = idio_C_uint16 ((uint16_t) IDIO_C_TYPE_INT32 (co)); break;
	    case IDIO_TYPE_C_UINT32: r = idio_C_uint16 ((uint16_t) IDIO_C_TYPE_UINT32 (co)); break;
	    case IDIO_TYPE_C_INT64: r = idio_C_uint16 ((uint16_t) IDIO_C_TYPE_INT64 (co)); break;
	    case IDIO_TYPE_C_UINT64: r = idio_C_uint16 ((uint16_t) IDIO_C_TYPE_UINT64 (co)); break;
	    case IDIO_TYPE_C_FLOAT: r = idio_C_uint16 ((uint16_t) IDIO_C_TYPE_FLOAT (co)); break;
	    case IDIO_TYPE_C_DOUBLE: r = idio_C_uint16 ((uint16_t) IDIO_C_TYPE_DOUBLE (co)); break;
	    default:
		fail = 1;
		break;
	    }
	    break;
	}
    case IDIO_TYPE_C_INT32:
	{
	    switch (co->type) {
	    case IDIO_TYPE_C_INT8: r = idio_C_int32 ((int32_t) IDIO_C_TYPE_INT8 (co)); break;
	    case IDIO_TYPE_C_UINT8: r = idio_C_int32 ((int32_t) IDIO_C_TYPE_UINT8 (co)); break;
	    case IDIO_TYPE_C_INT16: r = idio_C_int32 ((int32_t) IDIO_C_TYPE_INT16 (co)); break;
	    case IDIO_TYPE_C_UINT16: r = idio_C_int32 ((int32_t) IDIO_C_TYPE_UINT16 (co)); break;
	    case IDIO_TYPE_C_INT32: r = idio_C_int32 ((int32_t) IDIO_C_TYPE_INT32 (co)); break;
	    case IDIO_TYPE_C_UINT32: r = idio_C_int32 ((int32_t) IDIO_C_TYPE_UINT32 (co)); break;
	    case IDIO_TYPE_C_INT64: r = idio_C_int32 ((int32_t) IDIO_C_TYPE_INT64 (co)); break;
	    case IDIO_TYPE_C_UINT64: r = idio_C_int32 ((int32_t) IDIO_C_TYPE_UINT64 (co)); break;
	    case IDIO_TYPE_C_FLOAT: r = idio_C_int32 ((int32_t) IDIO_C_TYPE_FLOAT (co)); break;
	    case IDIO_TYPE_C_DOUBLE: r = idio_C_int32 ((int32_t) IDIO_C_TYPE_DOUBLE (co)); break;
	    default:
		fail = 1;
		break;
	    }
	    break;
	}
    case IDIO_TYPE_C_UINT32:
	{
	    switch (co->type) {
	    case IDIO_TYPE_C_INT8: r = idio_C_uint32 ((uint32_t) IDIO_C_TYPE_INT8 (co)); break;
	    case IDIO_TYPE_C_UINT8: r = idio_C_uint32 ((uint32_t) IDIO_C_TYPE_UINT8 (co)); break;
	    case IDIO_TYPE_C_INT16: r = idio_C_uint32 ((uint32_t) IDIO_C_TYPE_INT16 (co)); break;
	    case IDIO_TYPE_C_UINT16: r = idio_C_uint32 ((uint32_t) IDIO_C_TYPE_UINT16 (co)); break;
	    case IDIO_TYPE_C_INT32: r = idio_C_uint32 ((uint32_t) IDIO_C_TYPE_INT32 (co)); break;
	    case IDIO_TYPE_C_UINT32: r = idio_C_uint32 ((uint32_t) IDIO_C_TYPE_UINT32 (co)); break;
	    case IDIO_TYPE_C_INT64: r = idio_C_uint32 ((uint32_t) IDIO_C_TYPE_INT64 (co)); break;
	    case IDIO_TYPE_C_UINT64: r = idio_C_uint32 ((uint32_t) IDIO_C_TYPE_UINT64 (co)); break;
	    case IDIO_TYPE_C_FLOAT: r = idio_C_uint32 ((uint32_t) IDIO_C_TYPE_FLOAT (co)); break;
	    case IDIO_TYPE_C_DOUBLE: r = idio_C_uint32 ((uint32_t) IDIO_C_TYPE_DOUBLE (co)); break;
	    default:
		fail = 1;
		break;
	    }
	    break;
	}
    case IDIO_TYPE_C_INT64:
	{
	    switch (co->type) {
	    case IDIO_TYPE_C_INT8: r = idio_C_int64 ((int64_t) IDIO_C_TYPE_INT8 (co)); break;
	    case IDIO_TYPE_C_UINT8: r = idio_C_int64 ((int64_t) IDIO_C_TYPE_UINT8 (co)); break;
	    case IDIO_TYPE_C_INT16: r = idio_C_int64 ((int64_t) IDIO_C_TYPE_INT16 (co)); break;
	    case IDIO_TYPE_C_UINT16: r = idio_C_int64 ((int64_t) IDIO_C_TYPE_UINT16 (co)); break;
	    case IDIO_TYPE_C_INT32: r = idio_C_int64 ((int64_t) IDIO_C_TYPE_INT32 (co)); break;
	    case IDIO_TYPE_C_UINT32: r = idio_C_int64 ((int64_t) IDIO_C_TYPE_UINT32 (co)); break;
	    case IDIO_TYPE_C_INT64: r = idio_C_int64 ((int64_t) IDIO_C_TYPE_INT64 (co)); break;
	    case IDIO_TYPE_C_UINT64: r = idio_C_int64 ((int64_t) IDIO_C_TYPE_UINT64 (co)); break;
	    case IDIO_TYPE_C_FLOAT: r = idio_C_int64 ((int64_t) IDIO_C_TYPE_FLOAT (co)); break;
	    case IDIO_TYPE_C_DOUBLE: r = idio_C_int64 ((int64_t) IDIO_C_TYPE_DOUBLE (co)); break;
	    default:
		fail = 1;
		break;
	    }
	    break;
	}
    case IDIO_TYPE_C_UINT64:
	{
	    switch (co->type) {
	    case IDIO_TYPE_C_INT8: r = idio_C_uint64 ((uint64_t) IDIO_C_TYPE_INT8 (co)); break;
	    case IDIO_TYPE_C_UINT8: r = idio_C_uint64 ((uint64_t) IDIO_C_TYPE_UINT8 (co)); break;
	    case IDIO_TYPE_C_INT16: r = idio_C_uint64 ((uint64_t) IDIO_C_TYPE_INT16 (co)); break;
	    case IDIO_TYPE_C_UINT16: r = idio_C_uint64 ((uint64_t) IDIO_C_TYPE_UINT16 (co)); break;
	    case IDIO_TYPE_C_INT32: r = idio_C_uint64 ((uint64_t) IDIO_C_TYPE_INT32 (co)); break;
	    case IDIO_TYPE_C_UINT32: r = idio_C_uint64 ((uint64_t) IDIO_C_TYPE_UINT32 (co)); break;
	    case IDIO_TYPE_C_INT64: r = idio_C_uint64 ((uint64_t) IDIO_C_TYPE_INT64 (co)); break;
	    case IDIO_TYPE_C_UINT64: r = idio_C_uint64 ((uint64_t) IDIO_C_TYPE_UINT64 (co)); break;
	    case IDIO_TYPE_C_FLOAT: r = idio_C_uint64 ((uint64_t) IDIO_C_TYPE_FLOAT (co)); break;
	    case IDIO_TYPE_C_DOUBLE: r = idio_C_uint64 ((uint64_t) IDIO_C_TYPE_DOUBLE (co)); break;
	    default:
		fail = 1;
		break;
	    }
	    break;
	}
    case IDIO_TYPE_C_FLOAT:
	{
	    switch (co->type) {
	    case IDIO_TYPE_C_INT8: r = idio_C_float ((float) IDIO_C_TYPE_INT8 (co)); break;
	    case IDIO_TYPE_C_UINT8: r = idio_C_float ((float) IDIO_C_TYPE_UINT8 (co)); break;
	    case IDIO_TYPE_C_INT16: r = idio_C_float ((float) IDIO_C_TYPE_INT16 (co)); break;
	    case IDIO_TYPE_C_UINT16: r = idio_C_float ((float) IDIO_C_TYPE_UINT16 (co)); break;
	    case IDIO_TYPE_C_INT32: r = idio_C_float ((float) IDIO_C_TYPE_INT32 (co)); break;
	    case IDIO_TYPE_C_UINT32: r = idio_C_float ((float) IDIO_C_TYPE_UINT32 (co)); break;
	    case IDIO_TYPE_C_INT64: r = idio_C_float ((float) IDIO_C_TYPE_INT64 (co)); break;
	    case IDIO_TYPE_C_UINT64: r = idio_C_float ((float) IDIO_C_TYPE_UINT64 (co)); break;
	    case IDIO_TYPE_C_FLOAT: r = idio_C_float ((float) IDIO_C_TYPE_FLOAT (co)); break;
	    case IDIO_TYPE_C_DOUBLE: r = idio_C_float ((float) IDIO_C_TYPE_DOUBLE (co)); break;
	    default:
		fail = 1;
		break;
	    }
	    break;
	}
    case IDIO_TYPE_C_DOUBLE:
	{
	    switch (co->type) {
	    case IDIO_TYPE_C_INT8: r = idio_C_double ((double) IDIO_C_TYPE_INT8 (co)); break;
	    case IDIO_TYPE_C_UINT8: r = idio_C_double ((double) IDIO_C_TYPE_UINT8 (co)); break;
	    case IDIO_TYPE_C_INT16: r = idio_C_double ((double) IDIO_C_TYPE_INT16 (co)); break;
	    case IDIO_TYPE_C_UINT16: r = idio_C_double ((double) IDIO_C_TYPE_UINT16 (co)); break;
	    case IDIO_TYPE_C_INT32: r = idio_C_double ((double) IDIO_C_TYPE_INT32 (co)); break;
	    case IDIO_TYPE_C_UINT32: r = idio_C_double ((double) IDIO_C_TYPE_UINT32 (co)); break;
	    case IDIO_TYPE_C_INT64: r = idio_C_double ((double) IDIO_C_TYPE_INT64 (co)); break;
	    case IDIO_TYPE_C_UINT64: r = idio_C_double ((double) IDIO_C_TYPE_UINT64 (co)); break;
	    case IDIO_TYPE_C_FLOAT: r = idio_C_double ((double) IDIO_C_TYPE_FLOAT (co)); break;
	    case IDIO_TYPE_C_DOUBLE: r = idio_C_double ((double) IDIO_C_TYPE_DOUBLE (co)); break;
	    default:
		fail = 1;
		break;
	    }
	    break;
	}
    case IDIO_TYPE_C_POINTER:
	{
	    switch (co->type) {
	    case IDIO_TYPE_C_INT8: r = idio_C_pointer ((void *) &(IDIO_C_TYPE_INT8 (co))); break;
	    case IDIO_TYPE_C_UINT8: r = idio_C_pointer ((void *) &(IDIO_C_TYPE_UINT8 (co))); break;
	    case IDIO_TYPE_C_INT16: r = idio_C_pointer ((void *) &(IDIO_C_TYPE_INT16 (co))); break;
	    case IDIO_TYPE_C_UINT16: r = idio_C_pointer ((void *) &(IDIO_C_TYPE_UINT16 (co))); break;
	    case IDIO_TYPE_C_INT32: r = idio_C_pointer ((void *) &(IDIO_C_TYPE_INT32 (co))); break;
	    case IDIO_TYPE_C_UINT32: r = idio_C_pointer ((void *) &(IDIO_C_TYPE_UINT32 (co))); break;
	    case IDIO_TYPE_C_INT64: r = idio_C_pointer ((void *) &(IDIO_C_TYPE_INT64 (co))); break;
	    case IDIO_TYPE_C_UINT64: r = idio_C_pointer ((void *) &(IDIO_C_TYPE_UINT64 (co))); break;
	    case IDIO_TYPE_C_FLOAT: r = idio_C_pointer ((void *) &(IDIO_C_TYPE_FLOAT (co))); break;
	    case IDIO_TYPE_C_DOUBLE: r = idio_C_pointer ((void *) &(IDIO_C_TYPE_DOUBLE (co))); break;
	    case IDIO_TYPE_C_POINTER: r = co; break;
	    case IDIO_TYPE_STRING:
	    case IDIO_TYPE_SUBSTRING:
		{
		    char *s_C = idio_string_as_C (co);
		    r = idio_string_C ((void *) s_C);
		    free (s_C);
		    break;
		}
	    case IDIO_TYPE_C_INSTANCE:
		r = idio_C_pointer (IDIO_C_INSTANCE_P (co));
		break;
	    default:
		fail = 1;
		break;
	    }
	    break;
	}
    case IDIO_TYPE_STRING:
	{
	    switch (co->type) {
	    case IDIO_TYPE_C_POINTER: r = idio_C_pointer (IDIO_C_TYPE_POINTER_P (co)); break;
	    case IDIO_TYPE_STRING: r = idio_C_pointer ((void *) IDIO_STRING_S (co)); break;
	    case IDIO_TYPE_SUBSTRING: r = idio_C_pointer ((void *) IDIO_SUBSTRING_S (co)); break;
	    default:
		fail = 1;
		break;
	    }
	    break;
	}
    default:
	fail = 1;
	break;
    }

    if (fail) {
	char em[BUFSIZ];
	sprintf (em, "idio_C_number_cast: conversion not possible from %s %d to %d", idio_type2string (co), co->type, type);
	idio_error_add_C (em);
    }

    return r;
}

