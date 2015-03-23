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
 * hash.c
 *
 * We use a form of Coalesced hashing
 * (http://en.wikipedia.org/wiki/Coalesced_hashing) to avoid buckets
 * -- everything lives in the hash with some clever number crunching
 * to chain same-hash values together.
 *
 * To speed up modulo arithmetic we don't (necessarily) allocate the
 * request hash size.  Instead we round up to the nearest 2**n and can
 * use a bitmask of (2**n)-1 to do modulo arithmetic.
 *
 * Coalesced hashing prefers you have a "cellar" for clashes so we end
 * up allocating (1/8) more anyway.
 *
 * A hash entry will be a key, a value and a number indicating the
 * index of the next same-hash value in the chain.
 *
 * We use hsize+1, ie. beyond the allocated size, as a marker that
 * there is no next in chain.  It ought to be possible, if unwise, to
 * create a hash table that is one element more than half the size of
 * available memory which would require a size_t, not a
 * ssize_t/ptrdiff_t/intmax_t, therefore we can't use -1 as a flag.
 *
 * We should avoid the problem of someone allocating an array of
 * UINT_MAX elements and therefore not being able to use hsize+1 as a
 * marker as malloc's own data structures will get in the way and
 * prevent allocation of something so big.  Probably.
 *
 * With a coalesced hash the internal API (which leaks out -- grr!!)
 * is slightly different.  Having determing the hash value of a key
 * (idio_hash_hashval ()) we need to walk the chain of same-hash-value
 * keys until we find the one equal to us or walk off the end of the
 * chain (idio_hash_hv_follow_chain ()).
 *
 * To add fun to the mix there are several symbol tables (symbols,
 * tags, C_typedefs etc.) where we start with a C string.  For these
 * we need a _C variant as uniqueness is determined by strncmp ().
 */

#include "idio.h"

static int idio_assign_hash_he (IDIO h, size_t size)
{
    IDIO_ASSERT (h);

    IDIO_TYPE_ASSERT (hash, h);

    IDIO_FPRINTF (stderr, "idio_assign_hash: %10p = reqd size {%d}\n", h, size);

    /*
     * We need to round size up to the nearest multiple of 2 for
     * which we need to find the MSB in size, calculating a possible
     * mask as we go.
     */
    size_t s = size;
    size_t m = 0;
    size_t mask;
    
    while (s) {
	s >>= 1;
	m = (m << 1) | 1;
    }
    /*
     * This gives the following sample results:

     s=6 => m=0x7
     s=8 => m=0xf
     s=10 => m=0xf

     * 8 is the awkward one, a power of two, as we went round the loop
     * one more time than the mask needs.  It means we'll be
     * allocating sixteen slots when the user asked for eight.  This
     * is only really a problem if you're asking for a large array
     * when it becomes twice the problem you first thought of.
     *
     * We could try a quick check to see if this is a power of two:
     * shift the mask down and AND it with the size.  If the answer
     * is zero then it was a power of two.
    */
    
    if (size & (m >> 1)) {
	mask = m;
    } else {
	mask = (m >> 1);
    }

    /*
     * Now we can calculate our preferred size which is just mask+1
     * (cf. 2**n vs. (2**n)-1)
     */
    size = mask + 1;

    /*
      http://en.wikipedia.org/wiki/Coalesced_hashing

      add a cellar of 16% (1/0.86), approx 1/8
     */
    size += (size >> 3);

    IDIO_FPRINTF (stderr, "idio_assign_hash: %10p = true size {%d}\n", h, size);

    IDIO_GC_ALLOC (h->u.hash->he, size * sizeof (idio_hash_entry_t));

    IDIO_HASH_MASK (h) = mask;
    IDIO_HASH_SIZE (h) = size;

    size_t i;
    for (i = 0; i < size; i++) {
	IDIO_HASH_HE_KEY (h, i) = idio_S_nil;
	IDIO_HASH_HE_VALUE (h, i) = idio_S_nil;
	IDIO_HASH_HE_NEXT (h, i) = size + 1;
    }

    return 1;
}

IDIO idio_hash (size_t size, int (*equal) (void *k1, void *k2), size_t (*hashf) (IDIO h, void *k))
{
    IDIO_C_ASSERT (size);
    IDIO_C_ASSERT (equal);
    IDIO_C_ASSERT (hashf);

    IDIO h = idio_gc_get (IDIO_TYPE_HASH);
    IDIO_GC_ALLOC (h->u.hash, sizeof (idio_hash_t));
    IDIO_HASH_GREY (h) = NULL;
    IDIO_HASH_EQUAL (h) = equal;
    IDIO_HASH_HASHF (h) = hashf;
    IDIO_HASH_FLAGS (h) = IDIO_HASH_FLAG_NONE;

    idio_assign_hash_he (h, size);

    return h;
}

int idio_isa_hash (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_isa (h, IDIO_TYPE_HASH);
}

void idio_free_hash (IDIO h)
{
    IDIO_ASSERT (h);

    IDIO_TYPE_ASSERT (hash, h);

    idio_gc_stats_free (sizeof (idio_hash_t));
    idio_gc_stats_free (IDIO_HASH_SIZE (h) * sizeof (idio_hash_entry_t));

    free (h->u.hash->he);
    free (h->u.hash);
}

size_t idio_hash_hcount (IDIO h)
{
    IDIO_ASSERT (h);

    IDIO_TYPE_ASSERT (hash, h);

    size_t count = 0;

    size_t i;
    for (i = 0 ; i < IDIO_HASH_SIZE (h); i++) {
	if (IDIO_IS_SET (IDIO_HASH_HE_KEY (h, i))) {
	    count++;
	}
    }

    return count;
}

void idio_hash_resize (IDIO h)
{
    IDIO_ASSERT (h);

    IDIO_TYPE_ASSERT (hash, h);

    size_t ohsize = h->u.hash->size;
    idio_hash_entry_t *ohe = h->u.hash->he;

    /*
     * if we said osize = ohash->size it would be including the
     * existing +16%, as we are growing this then we only want to
     * double not 2*116%
     */
    size_t osize = IDIO_HASH_MASK (h) + 1;

    size_t hcount = idio_hash_hcount (h);

    size_t nsize = osize;
    if (nsize <= hcount) {
	while (nsize <= hcount) {
	    nsize <<= 1;
	}
    } else {
	while (nsize > hcount) {
	    nsize >>= 1;
	}
	nsize <<= 1;
    }

    IDIO_FPRINTF (stderr, "idio_hash_resize: %10p = {%d} -> {%d}\n", h, IDIO_HASH_SIZE (h), nsize);
    idio_assign_hash_he (h, nsize);

    size_t i;
    for (i = 0 ; i < ohsize; i++) {
	if (IDIO_IS_SET (ohe[i].k)) {
	    idio_hash_put (h, ohe[i].k, ohe[i].v);
	}
    }

    idio_gc_stats_free (osize * sizeof (idio_hash_entry_t));
    idio_gc_stats_free (osize * sizeof (idio_hash_entry_t));

    free (ohe);
}

size_t idio_hash_hashval_void (void *p)
{
    IDIO_C_ASSERT (p);
    
    unsigned long ul = (unsigned long) p;

    /*
     * all our objects are at least 16 bytes so pointer alignment
     * means the bottom 4-5 bits are always 0
    */
    size_t hv = idio_hash_hashval_uintmax_t (ul ^ (ul >> 5));
    return hv;
}

size_t idio_hash_hashval_uintmax_t (uintmax_t i)
{

    size_t hv = i ^ (i << 8) ^ (i << 16) ^ (i << 24);

    return hv;
}

size_t idio_hash_hashval_character (IDIO c)
{
    IDIO_ASSERT (c);
    
    return idio_hash_hashval_uintmax_t (IDIO_CHARACTER_VAL (c));
}

size_t idio_hash_hashval_string_C (size_t blen, const char *s_C)
{
    IDIO_C_ASSERT (s_C);
    
    size_t hv = idio_hash_hashval_uintmax_t (blen);
    size_t skip = blen >> 5;
    skip++;

    size_t i;
    for (i = 0; i < blen ; i+=skip) {
	hv ^= (unsigned)(s_C[i]);
    }

    return hv;
}

size_t idio_hash_hashval_string (IDIO s)
{
    IDIO_ASSERT (s);
    
    return idio_hash_hashval_string_C (IDIO_STRING_BLEN (s), IDIO_STRING_S (s));
}

size_t idio_hash_hashval_symbol (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_hash_hashval_void (h->u.symbol); 
}

size_t idio_hash_hashval_pair (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_uintmax_t ((unsigned long) IDIO_PAIR_H (h) ^ (unsigned long) IDIO_PAIR_T (h));
}

size_t idio_hash_hashval_array (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_void (h->u.array);
}

size_t idio_hash_hashval_hash (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_void (h->u.hash);
}

size_t idio_idio_hash_hashval_closure (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_void (IDIO_CLOSURE_BODY (h));
}

size_t idio_idio_hash_hashval_primitive_C (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_void (IDIO_PRIMITIVE_C_F (h));
}

size_t idio_idio_hash_hashval_frame (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_void (IDIO_FRAME_ENV (h));
}

size_t idio_idio_hash_hashval_bignum (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_void (IDIO_BIGNUM_SIG (h));
}

size_t idio_idio_hash_hashval_handle (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_void (IDIO_HANDLE_STREAM (h));
}

size_t idio_hash_hashval_C_struct (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_void (IDIO_C_STRUCT_METHODS (h));
}

size_t idio_hash_hashval_C_instance (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_void (IDIO_C_INSTANCE_P (h));
}

size_t idio_hash_hashval_C_FFI (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_void (IDIO_C_FFI_CIFP (h));
}

size_t idio_hash_hashval (IDIO h, void *kv)
{
    IDIO_ASSERT (h);

    IDIO k = (IDIO) kv;
    IDIO_ASSERT (k);

    size_t hv = IDIO_HASH_SIZE (h) + 1;
    
    switch (idio_type (k)) {
    case IDIO_TYPE_STRING:
	hv = idio_hash_hashval_string_C (IDIO_STRING_BLEN (k), IDIO_STRING_S (k)); 
	break;
    case IDIO_TYPE_SUBSTRING:
	hv = idio_hash_hashval_string_C (IDIO_SUBSTRING_BLEN (k), IDIO_SUBSTRING_S (k));
	break;
    case IDIO_TYPE_SYMBOL:
	hv = idio_hash_hashval_symbol (k);  
	break;
    case IDIO_TYPE_PAIR:
	hv = idio_hash_hashval_pair (k);
	break;
    case IDIO_TYPE_ARRAY:
	hv = idio_hash_hashval_array (k);
	break;
    case IDIO_TYPE_HASH:
	hv = idio_hash_hashval_hash (k);
	break;
    case IDIO_TYPE_CLOSURE:
	hv = idio_idio_hash_hashval_closure (k);
	break;
    case IDIO_TYPE_PRIMITIVE_C:
	hv = idio_idio_hash_hashval_primitive_C (k);
	break;
    case IDIO_TYPE_FRAME:
	hv = idio_idio_hash_hashval_frame (k);
	break;
    case IDIO_TYPE_BIGNUM:
	hv = idio_idio_hash_hashval_bignum (k);
	break;
    case IDIO_TYPE_HANDLE:
	hv = idio_idio_hash_hashval_handle (k);
	break;
    case IDIO_TYPE_C_INT8:
	hv = idio_hash_hashval_uintmax_t (IDIO_C_TYPE_INT8 (k));
	break;
    case IDIO_TYPE_C_UINT8:
	hv = idio_hash_hashval_uintmax_t (IDIO_C_TYPE_UINT8 (k));
	break;
    case IDIO_TYPE_C_INT16:
	hv = idio_hash_hashval_uintmax_t (IDIO_C_TYPE_INT16 (k));
	break;
    case IDIO_TYPE_C_UINT16:
	hv = idio_hash_hashval_uintmax_t (IDIO_C_TYPE_UINT16 (k));
	break;
    case IDIO_TYPE_C_INT32:
	hv = idio_hash_hashval_uintmax_t (IDIO_C_TYPE_INT32 (k));
	break;
    case IDIO_TYPE_C_UINT32:
	hv = idio_hash_hashval_uintmax_t (IDIO_C_TYPE_UINT32 (k));
	break;
    case IDIO_TYPE_C_INT64:
	hv = idio_hash_hashval_uintmax_t (IDIO_C_TYPE_INT64 (k));
	break;
    case IDIO_TYPE_C_UINT64:
	hv = idio_hash_hashval_uintmax_t (IDIO_C_TYPE_UINT64 (k));
	break;
    case IDIO_TYPE_C_FLOAT:
	hv = idio_hash_hashval_uintmax_t (IDIO_C_TYPE_FLOAT (k));
	break;
    case IDIO_TYPE_C_DOUBLE:
	hv = idio_hash_hashval_uintmax_t (IDIO_C_TYPE_DOUBLE (k));
	break;
    case IDIO_TYPE_C_POINTER:
	hv = idio_hash_hashval_void (IDIO_C_TYPE_POINTER_P (k));
	break;
    case IDIO_TYPE_C_STRUCT:
	hv = idio_hash_hashval_C_struct (k);
	break;
    case IDIO_TYPE_C_INSTANCE:
	hv = idio_hash_hashval_C_instance (k);
	break;
    case IDIO_TYPE_C_FFI:
	hv = idio_hash_hashval_C_FFI (k);
	break;
    default:
	fprintf (stderr, "idio_hash_hashval: type n/k\n");
	IDIO_C_ASSERT (0);
    }

    return (hv & IDIO_HASH_MASK (h));
}

void idio_hash_verify_chain (IDIO h, void *kv)
{
    IDIO_ASSERT (h);

    IDIO_TYPE_ASSERT (hash, h);

    if (idio_gc_verboseness (2)) {
	size_t ohv = IDIO_HASH_HASHF (h) (h, kv);
	size_t nhv = ohv;
	fprintf (stderr, "idio_hash_verify_chain: %10p %lu\n", kv, nhv);
	while (nhv < IDIO_HASH_SIZE (h) &&
	       IDIO_IS_SET (IDIO_HASH_HE_KEY (h, nhv))) {
	    size_t hv = IDIO_HASH_HASHF (h) (h, IDIO_HASH_HE_KEY (h, nhv));
	    if (hv != ohv) {
		fprintf (stderr, "idio_hash_verify_chain: %10p %10p@%lu hv %lu != ohv %lu\n", kv, IDIO_HASH_HE_KEY (h, nhv), nhv, hv, ohv);
		abort ();
	    }
	    fprintf (stderr, "idio_hash_verify_chain: %10p %10p %lu -> %lu\n", kv, IDIO_HASH_HE_KEY (h, nhv), nhv, IDIO_HASH_HE_NEXT (h, nhv));
	    nhv = IDIO_HASH_HE_NEXT (h, nhv);
	}
    }
}

size_t idio_hash_find_free_slot (IDIO h)
{
    IDIO_ASSERT (h);

    IDIO_TYPE_ASSERT (hash, h);

    size_t i;
    for (i = 0; i < IDIO_HASH_SIZE (h); i++) {
	if (idio_S_nil == IDIO_HASH_HE_KEY (h, i)) {
	    return i;
	}
    }

    IDIO_FPRINTF (stderr, "no free slots\n");
    return (IDIO_HASH_SIZE (h) + 1);
}

IDIO idio_hash_put (IDIO h, void *kv, IDIO v)
{
    IDIO_ASSERT (h);

    IDIO_TYPE_ASSERT (hash, h);

    if (idio_S_nil == kv) {
	idio_error_param_nil ("key");
	return idio_S_nil;
    }

    size_t hv = idio_hash_hv_follow_chain (h, kv);

    if (hv > IDIO_HASH_SIZE (h)) {
	/* XXX this was just done in idio_hash_hv_follow_chain !!! */
	hv = IDIO_HASH_HASHF (h) (h, kv);
    }

    IDIO_FPRINTF (stderr, "idio_hash_put: %10p starting @%lu -> %lu\n", kv, hv, IDIO_HASH_HE_NEXT (h, hv));

    /* current object @hv */
    IDIO ck = IDIO_HASH_HE_KEY (h, hv);

    if (idio_S_nil == ck) {
	if (idio_gc_verboseness (1)) {
	    fprintf (stderr, "idio_hash_put: ck == nil\n");
	}
	IDIO_HASH_HE_KEY (h, hv) = kv;
	IDIO_HASH_HE_VALUE (h, hv) = v;
	IDIO_HASH_HE_NEXT (h, hv) = IDIO_HASH_SIZE (h) + 1;
	idio_hash_verify_chain (h, kv);
	return kv;
    }

    if (IDIO_HASH_EQUAL (h) (ck, kv)) {
	if (idio_gc_verboseness (1)) {
	    fprintf (stderr, "idio_hash_put: ck == kv\n");
	}
	IDIO_HASH_HE_VALUE (h, hv) = v;
	idio_hash_verify_chain (h, kv);
	return kv;
    }

    size_t fhv = idio_hash_find_free_slot (h);
    if (fhv > IDIO_HASH_SIZE (h)) {
	if (idio_gc_verboseness (1)) {
	    fprintf (stderr, "idio_hash_put: fhv %lu > size %lu (hash is full)\n", fhv, IDIO_HASH_SIZE (h));
	}
	idio_hash_resize (h);
	idio_hash_put (h, kv, v);
	return kv;
    }
    IDIO_FPRINTF (stderr, "idio_hash_put: fhv %lu\n", fhv);

    size_t ckhv = IDIO_HASH_HASHF (h) (h, ck);

    /* either me or him is going to go to the end of the chain */
    if (ckhv != hv) {
	/* he's in the wrong place */
	IDIO_FPRINTF (stderr, "idio_hash_put: %10p hv(k@%lu ck=%10p)=%lu; mv -> %lu\n", kv, hv, ck, ckhv, fhv);

	/* who points to him? */
	size_t phv = ckhv;
	size_t nhv = IDIO_HASH_HE_NEXT (h, phv);
	while (nhv != hv) {
	    phv = nhv;
	    nhv = IDIO_HASH_HE_NEXT (h, phv);
	    IDIO_FPRINTF (stderr, "idio_hash_put: %10p step %lu -> %lu\n", kv, phv, nhv);

	    if (nhv > IDIO_HASH_SIZE (h)) {
		fprintf (stderr, "idio_hash_put: nhv %lu > size %lu k=%10p\n", nhv, IDIO_HASH_SIZE (h), kv);
		fprintf (stderr, "idio_hash_put: hv %lu ckhv %lu phv %lu\n", hv, ckhv, phv);
		idio_hash_verify_chain (h, ck);
		idio_hash_verify_chain (h, kv);
		IDIO_C_ASSERT (0);
		IDIO_C_EXIT (1);
	    }
	}

	/* point them at fhv */
	IDIO_HASH_HE_NEXT (h, phv) = fhv;

	/* shift ck */
	IDIO_HASH_HE_KEY (h, fhv) = IDIO_HASH_HE_KEY (h, hv);
	IDIO_HASH_HE_VALUE (h, fhv) = IDIO_HASH_HE_VALUE (h, hv);
	IDIO_HASH_HE_NEXT (h, fhv) = IDIO_HASH_HE_NEXT (h, hv);

	/* insert k */
	IDIO_HASH_HE_KEY (h, hv) = kv;
	IDIO_HASH_HE_VALUE (h, hv) = v;
	IDIO_HASH_HE_NEXT (h, hv) = IDIO_HASH_SIZE (h) + 1;

	idio_hash_verify_chain (h, ck);
	idio_hash_verify_chain (h, kv);
    } else {
	/* I go to the end of the chain */

	/* find the end of the chain */
	size_t phv = hv;
	size_t nhv = IDIO_HASH_HE_NEXT (h, phv);
	while (nhv < IDIO_HASH_SIZE (h)) {
	    phv = nhv;
	    nhv = IDIO_HASH_HE_NEXT (h, phv);
	    IDIO_FPRINTF (stderr, "idio_hash_put: %10p step %lu -> %lu\n", kv, phv, nhv);
	}

	IDIO_FPRINTF (stderr, "idio_hash_put: %10p append chain @%lu -> %lu\n", kv, phv, fhv);

	/* point them at fhv */
	IDIO_HASH_HE_NEXT (h, phv) = fhv;

	IDIO_HASH_HE_KEY (h, fhv) = kv;
	IDIO_HASH_HE_VALUE (h, fhv) = v;
	IDIO_HASH_HE_NEXT (h, fhv) = IDIO_HASH_SIZE (h) + 1;

	idio_hash_verify_chain (h, kv);
    }

    return kv;
}

size_t idio_hash_hv_follow_chain (IDIO h, void *kv)
{
    IDIO_ASSERT (h);

    IDIO_TYPE_ASSERT (hash, h);

    if (idio_S_nil == kv) {
	idio_error_param_nil ("key");
	IDIO_C_ASSERT (0);
	return IDIO_HASH_SIZE (h) + 1;
    }

    size_t hv = IDIO_HASH_HASHF (h) (h, kv);

    if (hv > IDIO_HASH_SIZE (h)) {
	fprintf (stderr, "idio_hash_hv_follow_chain: hv %lu > size %lu kv=%10p\n", hv, IDIO_HASH_SIZE (h), kv);
	IDIO_C_ASSERT (0);
	IDIO_C_EXIT (1);
    }

    IDIO_FPRINTF (stderr, "idio_hash_hv_follow_chain: %10p starting @%lu\n", kv, hv);

    size_t chv = hv;
    IDIO ck = IDIO_HASH_HE_KEY (h, chv);

    IDIO_FPRINTF (stderr, "%10p: @%zd %10p -> %zd\n", kv, chv, ck, IDIO_HASH_HE_NEXT (h, chv));

    while (! IDIO_HASH_EQUAL (h) (ck, kv) &&
	   IDIO_HASH_HE_NEXT (h, chv) < IDIO_HASH_SIZE (h)) {
	chv = IDIO_HASH_HE_NEXT (h, chv);
	ck = IDIO_HASH_HE_KEY (h, chv);
	IDIO_FPRINTF (stderr, "%10p: @%zd %10p -> %zd\n", kv, chv, ck, IDIO_HASH_HE_NEXT (h, chv));
    }

    if (! IDIO_HASH_EQUAL (h) (ck, kv)) {
	IDIO_FPRINTF (stderr, "idio_hash_hv_follow_chain: %10p not found\n", kv);
	return (IDIO_HASH_SIZE (h) + 1);
    }

    IDIO_FPRINTF (stderr, "idio_hash_hv_follow_chain: %10p found @%lu\n", kv, chv);

    return chv;
}

int idio_hash_exists_key (IDIO h, void *kv)
{
    IDIO_ASSERT (h);

    if (idio_S_nil == h) {
	return 0;
    }

    if (idio_S_nil == kv) {
	idio_error_param_nil ("key");
	return IDIO_HASH_SIZE (h) + 1;
    }

    size_t hv = idio_hash_hv_follow_chain (h, kv);

    if (hv > IDIO_HASH_SIZE (h)) {
	return 0;
    } else {
	return 1;
    }
}

IDIO idio_hash_exists (IDIO h, void *kv)
{
    IDIO_ASSERT (h);

    if (idio_S_nil == kv) {
	idio_error_param_nil ("key");
	return idio_S_nil;
    }

    IDIO_TYPE_ASSERT (hash, h);

    size_t hv = idio_hash_hv_follow_chain (h, kv);

    if (hv > IDIO_HASH_SIZE (h)) {
	return idio_S_nil;
    }

    return kv;

    /*
    IDIO_C_ASSERT (k == IDIO_HASH_HE_KEY (h, hv));
    
    return IDIO_HASH_HE_KEY (h, hv);
    */
}

IDIO idio_hash_get (IDIO h, void *kv)
{
    IDIO_ASSERT (h);

    if (idio_S_nil == kv) {
	idio_error_param_nil ("key");
	return idio_S_nil;
    }

    IDIO_TYPE_ASSERT (hash, h);

    IDIO_FPRINTF (stderr, "idio_hash_get: %10p\n", kv);
    size_t hv = idio_hash_hv_follow_chain (h, kv);

    if (hv > IDIO_HASH_SIZE (h)) {
	return idio_S_nil;
    }

    return IDIO_HASH_HE_VALUE (h, hv);
}

int idio_hash_delete (IDIO h, void *kv)
{
    IDIO_ASSERT (h);

    if (idio_S_nil == h) {
	return 0;
    }

    IDIO_TYPE_ASSERT (hash, h);

    if (idio_S_nil == kv) {
	idio_error_param_nil ("key");
	return 0;
    }

    size_t hv = IDIO_HASH_HASHF (h) (h, kv);

    if (hv > IDIO_HASH_SIZE (h)) {
	fprintf (stderr, "idio_hash_delete: hv %lu > size %lu\n", hv, IDIO_HASH_SIZE (h));
	IDIO_C_ASSERT (0);
	IDIO_C_EXIT (1);
    }

    IDIO_FPRINTF (stderr, "idio_hash_delete: %10p starting @%lu\n", kv, hv);

    size_t phv = 0;
    size_t chv = hv;
    IDIO ck = IDIO_HASH_HE_KEY (h, chv);
    while (! IDIO_HASH_EQUAL (h) (ck, kv) &&
	   IDIO_HASH_HE_NEXT (h, chv) < IDIO_HASH_SIZE (h)) {
	phv = chv;
	chv = IDIO_HASH_HE_NEXT (h, chv);
	ck = IDIO_HASH_HE_KEY (h, chv);
	IDIO_FPRINTF (stderr, "idio_hash_delete: %10p stepped to %lu -> @%lu -> %lu\n", ck, phv, chv, IDIO_HASH_HE_NEXT (h, chv));
    }

    if (idio_S_nil == ck) {
	IDIO_FPRINTF (stderr, "idio_hash_delete: %10p nil found @%lu?\n", kv, chv);
	return 0;
    }

    if (! IDIO_HASH_EQUAL (h) (ck, kv)) {
	IDIO_FPRINTF (stderr, "idio_hash_delete: %10p not found\n", kv);
	return 0;
    }

    size_t nhv = IDIO_HASH_HE_NEXT (h, chv);
    IDIO_FPRINTF (stderr, "idio_hash_delete: %10p deleting from %lu -> @%lu -> %lu\n", ck, phv, chv, nhv);

    if (0 == phv) {
	/* head of chain */
	if (nhv < IDIO_HASH_SIZE (h)) {
	    IDIO_FPRINTF (stderr, "idio_hash_delete: %10p head of chain: @%lu <- %lu\n", ck, chv, nhv);
	    IDIO_HASH_HE_KEY (h, chv) = IDIO_HASH_HE_KEY (h, nhv);
	    IDIO_HASH_HE_VALUE (h, chv) = IDIO_HASH_HE_VALUE (h, nhv);
	    IDIO_HASH_HE_NEXT (h, chv) = IDIO_HASH_HE_NEXT (h, nhv);
	
	    IDIO_FPRINTF (stderr, "idio_hash_delete: %10p head of chain: nullify %lu\n", ck, nhv);
	    IDIO_HASH_HE_KEY (h, nhv) = idio_S_nil;
	    IDIO_HASH_HE_VALUE (h, nhv) = idio_S_nil;
	    IDIO_HASH_HE_NEXT (h, nhv) = IDIO_HASH_SIZE (h) + 1;
	} else {
	    IDIO_FPRINTF (stderr, "idio_hash_delete: %10p head of chain: nullify %lu\n", ck, chv);
	    IDIO_HASH_HE_KEY (h, chv) = idio_S_nil;
	    IDIO_HASH_HE_VALUE (h, chv) = idio_S_nil;
	    IDIO_HASH_HE_NEXT (h, chv) = IDIO_HASH_SIZE (h) + 1;
	}
    } else {
	IDIO_FPRINTF (stderr, "idio_hash_delete: %10p link %lu -> %lu\n", ck, phv, nhv);
	IDIO_HASH_HE_NEXT (h, phv) = nhv;
    
	IDIO_FPRINTF (stderr, "idio_hash_delete: %10p nullify %lu\n", ck, chv);
	IDIO_HASH_HE_KEY (h, chv) = idio_S_nil;
	IDIO_HASH_HE_VALUE (h, chv) = idio_S_nil;
	IDIO_HASH_HE_NEXT (h, chv) = IDIO_HASH_SIZE (h) + 1;
    }

    idio_hash_verify_chain (h, kv);

    return 1;
}

IDIO idio_hash_keys_to_list (IDIO h)
{
    IDIO_ASSERT (h);

    IDIO_TYPE_ASSERT (hash, h);

    IDIO r = idio_S_nil;

    size_t i;
    for (i = 0; i < IDIO_HASH_SIZE (h); i++) {
	IDIO k = IDIO_HASH_HE_KEY (h, i);
	if (! k) {
	    fprintf (stderr, "h = %s\n", idio_as_string (h, 2));
	    idio_error_message ("key is NULL");
	}
	IDIO_ASSERT (k);
	if (idio_S_nil != k) {
	    r = idio_pair (k, r);
	}
    }

    return r;
}
