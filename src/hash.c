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
 * there is no next in chain.  
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

void idio_hash_error (char *m, IDIO loc)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);
    
    idio_error_printf (loc, "%s", m);
}

void idio_hash_error_key_not_found (IDIO key, IDIO loc)
{
    IDIO_ASSERT (key);
    IDIO_ASSERT (loc);
    IDIO_TYPE_ASSERT (string, loc);
    
    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("key not found", msh);
    
    IDIO c = idio_struct_instance (idio_condition_rt_hash_key_not_found_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       loc,
					       idio_S_nil,
					       key));
    idio_raise_condition (idio_S_true, c);
}

static int idio_assign_hash_he (IDIO h, idio_hi_t size)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (hash, h);

    IDIO_FPRINTF (stderr, "idio_assign_hash: %10p = reqd size {%d}\n", h, size);

    /*
     * We need to round size up to the nearest multiple of 2 for
     * which we need to find the MSB in size, calculating a possible
     * mask as we go.
     */
    idio_hi_t s = size;
    idio_hi_t m = 0;
    idio_hi_t mask;
    
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

    idio_hi_t i;
    for (i = 0; i < size; i++) {
	IDIO_HASH_HE_KEY (h, i) = idio_S_nil;
	IDIO_HASH_HE_VALUE (h, i) = idio_S_nil;
	IDIO_HASH_HE_NEXT (h, i) = size + 1;
    }

    return 1;
}

IDIO idio_hash (idio_hi_t size, int (*equal) (void *k1, void *k2), idio_hi_t (*hashf) (IDIO h, void *k), IDIO comp, IDIO hash)
{
    IDIO_C_ASSERT (size);
    /* IDIO_C_ASSERT (equal); */
    /* IDIO_C_ASSERT (hashf); */
    IDIO_ASSERT (comp);
    IDIO_ASSERT (hash);

    if (NULL == equal) {
	if (idio_S_nil == comp) {
	    idio_hash_error ("no comparator supplied", IDIO_C_LOCATION ("idio_hash"));
	}
    } else if (idio_S_nil != comp) {
	idio_hash_error ("two comparators supplied", IDIO_C_LOCATION ("idio_hash"));
    }

    if (NULL == hashf) {
	if (idio_S_nil == hash) {
	    idio_hash_error ("no hashing function supplied", IDIO_C_LOCATION ("idio_hash"));
	}
    } else if (idio_S_nil != hash) {
	idio_hash_error ("two hashing functions supplied", IDIO_C_LOCATION ("idio_hash"));
    }

    IDIO h = idio_gc_get (IDIO_TYPE_HASH);
    IDIO_GC_ALLOC (h->u.hash, sizeof (idio_hash_t));
    IDIO_HASH_GREY (h) = NULL;
    IDIO_HASH_EQUAL (h) = equal;
    IDIO_HASH_HASHF (h) = hashf;
    IDIO_HASH_COMP (h) = comp;
    IDIO_HASH_HASH (h) = hash;
    IDIO_HASH_FLAGS (h) = IDIO_HASH_FLAG_NONE;

    idio_assign_hash_he (h, size);

    return h;
}

IDIO idio_hash_copy (IDIO orig)
{
    IDIO_ASSERT (orig);
    IDIO_TYPE_ASSERT (hash, orig);

    IDIO new = idio_gc_get (IDIO_TYPE_HASH);
    IDIO_GC_ALLOC (new->u.hash, sizeof (idio_hash_t));
    IDIO_HASH_GREY (new) = NULL;
    IDIO_HASH_EQUAL (new) = IDIO_HASH_EQUAL (orig);
    IDIO_HASH_HASHF (new) = IDIO_HASH_HASHF (orig);
    IDIO_HASH_COMP (new) = IDIO_HASH_COMP (orig);
    IDIO_HASH_HASH (new) = IDIO_HASH_HASH (orig);
    IDIO_HASH_FLAGS (new) = IDIO_HASH_FLAGS (orig);

    idio_assign_hash_he (new, idio_hash_hcount (orig));

    idio_hi_t i;
    for (i = 0; i < IDIO_HASH_SIZE (orig); i++) {
	IDIO k = IDIO_HASH_HE_KEY (orig, i);
	if (! k) {
	    char em[BUFSIZ];
	    sprintf (em, "hash-copy: key #%zd is NULL", i);
	    idio_error_C (em, orig, IDIO_C_LOCATION ("idio_hash_copy"));
	}
	if (idio_S_nil != k) {
	    IDIO v = IDIO_HASH_HE_VALUE (orig, i);
	    idio_hash_put (new, k, v);
	}
    }
    
    return new;
}

/*
 * SRFI 69 -- allows a destructive merge
 */
IDIO idio_hash_merge (IDIO ht1, IDIO ht2)
{
    idio_hi_t i;
    for (i = 0; i < IDIO_HASH_SIZE (ht2); i++) {
	IDIO k = IDIO_HASH_HE_KEY (ht2, i);
	if (! k) {
	    char em[BUFSIZ];
	    sprintf (em, "hash-merge: key #%zd is NULL", i);
	    idio_error_C (em, ht2, IDIO_C_LOCATION ("idio_hash_merge"));
	}
	if (idio_S_nil != k) {
	    IDIO v = IDIO_HASH_HE_VALUE (ht2, i);
	    idio_hash_put (ht1, k, v);
	}
    }

    return ht1;
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

    if (IDIO_HASH_FLAGS (h) & IDIO_HASH_FLAG_STRING_KEYS) {
	idio_hi_t i;
	for (i = 0; i < IDIO_HASH_SIZE (h); i++) {
	    void *kv = IDIO_HASH_HE_KEY (h, i);
	    if (idio_S_nil != kv) {
		free (kv); 
	    }
	}
    }

    free (h->u.hash->he);
    free (h->u.hash);
}

idio_hi_t idio_hash_hcount (IDIO h)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (hash, h);

    idio_hi_t count = 0;

    idio_hi_t i;
    for (i = 0 ; i < IDIO_HASH_SIZE (h); i++) {
	if (idio_S_nil != IDIO_HASH_HE_KEY (h, i)) {
	    count++;
	}
    }

    return count;
}

void idio_hash_resize (IDIO h)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (hash, h);

    idio_hi_t ohsize = h->u.hash->size;
    idio_hash_entry_t *ohe = h->u.hash->he;

    /*
     * if we said osize = ohash->size it would be including the
     * existing +16%, as we are growing this then we only want to
     * double not 2*116%
     */
    idio_hi_t osize = IDIO_HASH_MASK (h) + 1;

    idio_hi_t hcount = idio_hash_hcount (h);

    idio_hi_t nsize = osize;
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

    IDIO_FPRINTF (stderr, "idio_hash_resize: %10p = %zd/%zd/%zd -> %zd\n", h, hcount, osize, IDIO_HASH_SIZE (h), nsize);
    idio_assign_hash_he (h, nsize);

    idio_hi_t i;
    for (i = 0 ; i < ohsize; i++) {
	if (idio_S_nil != ohe[i].k) {
	    idio_hash_put (h, ohe[i].k, ohe[i].v);
	}
    }

    idio_gc_stats_free (osize * sizeof (idio_hash_entry_t));
    idio_gc_stats_free (osize * sizeof (idio_hash_entry_t));

    free (ohe);
}

idio_hi_t idio_hash_hashval_void (void *p)
{
    IDIO_C_ASSERT (p);
    
    unsigned long ul = (unsigned long) p;

    /*
     * all our objects are at least 16 bytes so pointer alignment
     * means the bottom 4-5 bits are always 0
    */
    idio_hi_t hv = idio_hash_hashval_uintmax_t (ul ^ (ul >> 5));
    return hv;
}

idio_hi_t idio_hash_hashval_uintmax_t (uintmax_t i)
{

    idio_hi_t hv = i ^ (i << 8) ^ (i << 16) ^ (i << 24);

    return hv;
}

idio_hi_t idio_hash_hashval_character (IDIO c)
{
    IDIO_ASSERT (c);
    
    return idio_hash_hashval_uintmax_t (IDIO_CHARACTER_VAL (c));
}

idio_hi_t idio_hash_hashval_string_C (idio_hi_t blen, const char *s_C)
{
    IDIO_C_ASSERT (s_C);
    
    idio_hi_t hv = idio_hash_hashval_uintmax_t (blen);

    /*
     * We could hash every character in the string.  However, a
     * hashing function's goal is to get an evenly distributed hash
     * value not necessarily to require every character be used.
     *
     * The trick here is to say that using 32 characters should be
     * sufficient to "sample" the uniqueness of the string.  If it
     * isn't (and examples are trivial) then we fall back to chaining
     * within the hash table and the use of eq?/strncmp to
     * differentitate between strings.
     *
     * Note that there are always pathological examples for whatever
     * hashing algorithm you choose such that we rely on eq?/strncmp
     * to differentiate between keys.
     */

    idio_hi_t skip = blen >> 5;
    if (0 == skip) {
	skip++;
    }

    idio_hi_t i;
    for (i = 0; i < blen ; i+=skip) {
	hv ^= (unsigned)(s_C[i]);
    }

    return hv;
}

idio_hi_t idio_hash_hashval_string (IDIO s)
{
    IDIO_ASSERT (s);
    
    return idio_hash_hashval_string_C (IDIO_STRING_BLEN (s), IDIO_STRING_S (s));
}

idio_hi_t idio_hash_hashval_symbol (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_hash_hashval_void (IDIO_SYMBOL_S (h)); 
}

idio_hi_t idio_hash_hashval_keyword (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_hash_hashval_void (IDIO_KEYWORD_S (h)); 
}

idio_hi_t idio_hash_hashval_pair (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_uintmax_t ((unsigned long) IDIO_PAIR_H (h) ^ (unsigned long) IDIO_PAIR_T (h));
}

idio_hi_t idio_hash_hashval_array (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_void (h->u.array);
}

idio_hi_t idio_hash_hashval_hash (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_void (h->u.hash);
}

idio_hi_t idio_idio_hash_hashval_closure (IDIO h)
{
    IDIO_ASSERT (h);

    idio_hi_t hv = idio_hash_hashval_uintmax_t (IDIO_CLOSURE_CODE (h));
    hv ^= idio_hash_hashval_void (IDIO_CLOSURE_ENV (h));
    return hv;
}

idio_hi_t idio_idio_hash_hashval_primitive (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_void (IDIO_PRIMITIVE_F (h));
}

idio_hi_t idio_idio_hash_hashval_module (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_void (IDIO_MODULE_NAME (h));
}

idio_hi_t idio_idio_hash_hashval_frame (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_void (h->u.frame);
}

idio_hi_t idio_idio_hash_hashval_bignum (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_void (IDIO_BIGNUM_SIG (h));
}

idio_hi_t idio_idio_hash_hashval_handle (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_void (IDIO_HANDLE_STREAM (h));
}

idio_hi_t idio_hash_hashval_C_struct (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_void (IDIO_C_STRUCT_METHODS (h));
}

idio_hi_t idio_hash_hashval_C_instance (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_void (IDIO_C_INSTANCE_P (h));
}

idio_hi_t idio_hash_hashval_C_FFI (IDIO h)
{
    IDIO_ASSERT (h);
    
    return idio_hash_hashval_void (IDIO_C_FFI_CIFP (h));
}

idio_hi_t idio_hash_hashval (IDIO h, void *kv)
{
    IDIO_ASSERT (h);

    IDIO k = (IDIO) kv;
    IDIO_ASSERT (k);

    idio_hi_t hv = IDIO_HASH_SIZE (h) + 1;
    idio_type_e type = idio_type (k);
    
    switch (type) {
    case IDIO_TYPE_FIXNUM:
    case IDIO_TYPE_CONSTANT:
    case IDIO_TYPE_CHARACTER:
	hv = idio_hash_hashval_uintmax_t ((uintptr_t) k);
	break;
    case IDIO_TYPE_STRING:
	hv = idio_hash_hashval_string_C (IDIO_STRING_BLEN (k), IDIO_STRING_S (k)); 
	break;
    case IDIO_TYPE_SUBSTRING:
	hv = idio_hash_hashval_string_C (IDIO_SUBSTRING_BLEN (k), IDIO_SUBSTRING_S (k));
	break;
    case IDIO_TYPE_SYMBOL:
	hv = idio_hash_hashval_symbol (k);  
	break;
    case IDIO_TYPE_KEYWORD:
	hv = idio_hash_hashval_keyword (k);  
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
    case IDIO_TYPE_PRIMITIVE:
	hv = idio_idio_hash_hashval_primitive (k);
	break;
    case IDIO_TYPE_MODULE:
	hv = idio_idio_hash_hashval_module (k);
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
    case IDIO_TYPE_C_INT:
	hv = idio_hash_hashval_uintmax_t ((uintmax_t) IDIO_C_TYPE_INT (k));
	break;
    case IDIO_TYPE_C_UINT:
	hv = idio_hash_hashval_uintmax_t ((uintmax_t) IDIO_C_TYPE_UINT (k));
	break;
    case IDIO_TYPE_C_FLOAT:
	hv = idio_hash_hashval_uintmax_t ((uintmax_t) IDIO_C_TYPE_FLOAT (k));
	break;
    case IDIO_TYPE_C_DOUBLE:
	hv = idio_hash_hashval_uintmax_t ((uintmax_t) IDIO_C_TYPE_DOUBLE (k));
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
	idio_error_C ("idio_hash_hashval: unexpected type", k, IDIO_C_LOCATION ("idio_hash_hashval"));
    }

    return (hv & IDIO_HASH_MASK (h));
}

idio_hi_t idio_hash_value (IDIO ht, void *kv)
{
    IDIO_ASSERT (ht);
    IDIO_TYPE_ASSERT (hash, ht);

    if (IDIO_HASH_HASHF (ht) != NULL) {
	return IDIO_HASH_HASHF (ht) (ht, kv);
    } else {
	IDIO ihv = idio_vm_invoke_C (idio_current_thread (), IDIO_LIST2 (IDIO_HASH_HASH (ht), (IDIO) kv));

	idio_hi_t hv = IDIO_HASH_SIZE (ht) + 1;

	if (idio_isa_fixnum (ihv)) {
	    hv = IDIO_FIXNUM_VAL (ihv);
	} else if (idio_isa_bignum (ihv)) {
	    /*
	     * idio_ht_t is a size_t so the fact that ptrdiff_t &
	     * size_t on non-segemented architectures are *the same
	     * size* means we can technically return a negative value
	     * here.
	     *
	     * Technically wrong is the worst kind of wrong.
	     */
	    hv = idio_bignum_ptrdiff_value (ihv);
	} else {
	    idio_error_param_type ("fixnum|bignum", ihv, IDIO_C_LOCATION ("idio_hash_value"));
	}

	return (hv & IDIO_HASH_MASK (ht));
    }

    /* notreached */
    return -1;
}

int idio_hash_equal (IDIO ht, void *kv1, void *kv2)
{
    IDIO_ASSERT (ht);
    IDIO_TYPE_ASSERT (hash, ht);

    if (IDIO_HASH_EQUAL (ht) != NULL) {
	return IDIO_HASH_EQUAL (ht) (kv1, kv2);
    } else {
	IDIO r = idio_vm_invoke_C (idio_current_thread (), IDIO_LIST3 (IDIO_HASH_COMP (ht), (IDIO) kv1, (IDIO) kv2));
	if (idio_S_false == r) {
	    return 0;
	} else {
	    return 1;
	}
    }
}

void idio_hash_verify_chain (IDIO h, void *kv)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (hash, h);

    if (idio_gc_verboseness (2)) {
	idio_hi_t ohv = idio_hash_value (h, kv);
	idio_hi_t nhv = ohv;
	fprintf (stderr, "idio_hash_verify_chain: %10p %" PRIuPTR "\n", kv, nhv);
	while (nhv < IDIO_HASH_SIZE (h)) {
	    void *nkv = IDIO_HASH_HE_KEY (h, nhv);
	    if (idio_S_nil != nkv) {
		idio_hi_t hv = idio_hash_value (h, nkv);
		if (hv != ohv) {
		    fprintf (stderr, "idio_hash_verify_chain: %10p %10p@%" PRIuPTR " hv %" PRIuPTR " != ohv %" PRIuPTR "\n", kv, nkv, nhv, hv, ohv);
		    abort ();
		}
	    }
	    fprintf (stderr, "idio_hash_verify_chain: %10p %10p %" PRIuPTR " -> %" PRIuPTR "\n", kv, nkv, nhv, IDIO_HASH_HE_NEXT (h, nhv));
	    nhv = IDIO_HASH_HE_NEXT (h, nhv);
	}
    }
}

void idio_hash_verify_all_keys (IDIO h)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (hash, h);

    IDIO_FPRINTF (stderr, "idio_hash_verify_all_keys:\n");
    idio_dump (h, 1);
    idio_hi_t hv = 0;
    for (hv = 0; hv < IDIO_HASH_SIZE (h); hv++) {
	void *kv = IDIO_HASH_HE_KEY (h, hv);
	if (idio_S_nil != kv) {
	    IDIO_FPRINTF (stderr, "idio_hash_verify_all_keys: hv=%zd %p ", hv, kv);
	    if (IDIO_HASH_FLAGS (h) & IDIO_HASH_FLAG_STRING_KEYS) {
		IDIO_FPRINTF (stderr, "\"%s\"", (char *) kv);
	    }
	    IDIO_FPRINTF (stderr, "\n");
	
	    idio_hash_verify_chain (h, kv);
	}
    }
    IDIO_FPRINTF (stderr, "idio_hash_verify_all_keys: done\n");
}

idio_hi_t idio_hash_find_free_slot (IDIO h)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (hash, h);

    /*
     * We want to start at the top to use free slots in the "cellar"
     * (attic?)  first however the post-loop "i--" means i would go
     * negative which means an unsigned loop variable (idio_hi_t)
     * would wrap and the test "var >= 0" would not work --
     * subsequently we ought to get a SEGV but YMMV.
     *
     * To avoid determining if a safer signed type, say, intptr_t ==
     * idio_hi_t/size_t on any given machine we can set the loop check
     * to be "var > 0" and repeat the test for the case "var == 0"
     * after the loop.
     *
     * Think of this as a special case...
     *
     * Of course, if we did reach i == 0 then our load factor is
     * effectively 1.00.  Which isn't good for other reasons.
     */
    idio_hi_t i;
    for (i = IDIO_HASH_SIZE (h) - 1; i > 0 ; i--) {
	if (idio_S_nil == IDIO_HASH_HE_KEY (h, i)) {
	    return i;
	}
    }

    /* i == 0 */
    if (idio_S_nil == IDIO_HASH_HE_KEY (h, i)) {
	return i;
    }

    return (IDIO_HASH_SIZE (h) + 1);
}

IDIO idio_hash_put (IDIO h, void *kv, IDIO v)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (hash, h);

    if (idio_S_nil == kv) {
	idio_error_param_nil ("key", IDIO_C_LOCATION ("idio_hash_put"));
	return idio_S_nil;
    }

    idio_hi_t hv = idio_hash_hv_follow_chain (h, kv);

    if (hv > IDIO_HASH_SIZE (h)) {
	/* XXX this was just done in idio_hash_hv_follow_chain !!! */
	hv = idio_hash_value (h, kv);
    }

    IDIO_FPRINTF (stderr, "idio_hash_put: %10p starting @%" PRIuPTR " -> %" PRIuPTR "\n", kv, hv, IDIO_HASH_HE_NEXT (h, hv));

    /* current object @hv */
    IDIO ck = IDIO_HASH_HE_KEY (h, hv);

    if (idio_S_nil == ck) {
	IDIO_HASH_HE_KEY (h, hv) = kv;
	IDIO_HASH_HE_VALUE (h, hv) = v;
	IDIO_HASH_HE_NEXT (h, hv) = IDIO_HASH_SIZE (h) + 1;
	idio_hash_verify_chain (h, kv);
	return kv;
    }

    if (idio_hash_equal (h, ck, kv)) {
	IDIO_HASH_HE_VALUE (h, hv) = v;
	idio_hash_verify_chain (h, kv);
	return kv;
    }

    idio_hi_t fhv = idio_hash_find_free_slot (h);
    if (fhv > IDIO_HASH_SIZE (h)) {
	IDIO_FPRINTF (stderr, "idio_hash_put: no free slot -> resize\n");
	idio_hash_resize (h);
	idio_hash_put (h, kv, v);
	return kv;
    }
    IDIO_FPRINTF (stderr, "idio_hash_put: fhv %" PRIuPTR "\n", fhv);

    idio_hi_t ckhv = idio_hash_value (h, ck);

    /* either me or him is going to go to the end of the chain */
    if (ckhv != hv) {
	/* he's in the wrong place */
	IDIO_FPRINTF (stderr, "idio_hash_put: %10p hv(k@%" PRIuPTR " ck=%10p)=%" PRIuPTR "; mv -> %" PRIuPTR "\n", kv, hv, ck, ckhv, fhv);

	/* who points to him? */
	idio_hi_t phv = ckhv;
	idio_hi_t nhv = IDIO_HASH_HE_NEXT (h, phv);
	while (nhv != hv) {
	    phv = nhv;
	    nhv = IDIO_HASH_HE_NEXT (h, phv);
	    IDIO_FPRINTF (stderr, "idio_hash_put: %10p step %" PRIuPTR " -> %" PRIuPTR "\n", kv, phv, nhv);

	    if (nhv > IDIO_HASH_SIZE (h)) {
		fprintf (stderr, "idio_hash_put: nhv %" PRIuPTR " > size %" PRIuPTR " k=%10p\n", nhv, IDIO_HASH_SIZE (h), kv);
		fprintf (stderr, "idio_hash_put: hv %" PRIuPTR " ckhv %" PRIuPTR " phv %" PRIuPTR "\n", hv, ckhv, phv);
		idio_hash_verify_chain (h, ck);
		idio_hash_verify_chain (h, kv);
		idio_error_printf (IDIO_C_LOCATION ("idio_hash_put"), "oh dear");
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
	idio_hi_t phv = hv;
	idio_hi_t nhv = IDIO_HASH_HE_NEXT (h, phv);
	while (nhv < IDIO_HASH_SIZE (h)) {
	    phv = nhv;
	    nhv = IDIO_HASH_HE_NEXT (h, phv);
	    IDIO_FPRINTF (stderr, "idio_hash_put: %10p step %" PRIuPTR " -> %" PRIuPTR "\n", kv, phv, nhv);
	}

	IDIO_FPRINTF (stderr, "idio_hash_put: %10p append chain @%" PRIuPTR " -> %" PRIuPTR "\n", kv, phv, fhv);

	/* point them at fhv */
	IDIO_HASH_HE_NEXT (h, phv) = fhv;

	IDIO_HASH_HE_KEY (h, fhv) = kv;
	IDIO_HASH_HE_VALUE (h, fhv) = v;
	IDIO_HASH_HE_NEXT (h, fhv) = IDIO_HASH_SIZE (h) + 1;

	idio_hash_verify_chain (h, kv);
    }

    return kv;
}

idio_hi_t idio_hash_hv_follow_chain (IDIO h, void *kv)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (hash, h);

    if (idio_S_nil == kv) {
	idio_error_param_nil ("key", IDIO_C_LOCATION ("idio_hash_hv_follow_chain"));

	/* notreached */
	return IDIO_HASH_SIZE (h) + 1;
    }

    idio_hi_t hv = idio_hash_value (h, kv);

    if (hv > IDIO_HASH_SIZE (h)) {
	idio_error_printf (IDIO_C_LOCATION ("idio_hash_hv_follow_chain"), "hv %" PRIuPTR " > size %" PRIuPTR " kv=%10p", hv, IDIO_HASH_SIZE (h), kv);
    }

    IDIO_FPRINTF (stderr, "idio_hash_hv_follow_chain: %10p starting @%" PRIuPTR "\n", kv, hv);
    if (IDIO_HASH_FLAGS (h) & IDIO_HASH_FLAG_STRING_KEYS) {
	IDIO_FPRINTF (stderr, "SK kv=%s\n", (char *) kv);
    }

    idio_hi_t chv = hv;
    IDIO ck = IDIO_HASH_HE_KEY (h, chv);
    
    IDIO_FPRINTF (stderr, "%10p: @%zd %10p -> %zd\n", kv, chv, ck, IDIO_HASH_HE_NEXT (h, chv));
    if (idio_S_nil != ck) {
	if (IDIO_HASH_FLAGS (h) & IDIO_HASH_FLAG_STRING_KEYS) {
	    IDIO_FPRINTF (stderr, "SK ck=%s\n", (char *) ck);
	} else {
	    IDIO_ASSERT (ck);
	}
    }

    while (! idio_hash_equal (h, ck, kv) &&
	   IDIO_HASH_HE_NEXT (h, chv) < IDIO_HASH_SIZE (h)) {
	chv = IDIO_HASH_HE_NEXT (h, chv);
	ck = IDIO_HASH_HE_KEY (h, chv);
	IDIO_FPRINTF (stderr, "%10p: @%zd %10p -> %zd\n", kv, chv, ck, IDIO_HASH_HE_NEXT (h, chv));
	if (idio_S_nil != ck &&
	    IDIO_HASH_FLAGS (h) & IDIO_HASH_FLAG_STRING_KEYS) {
	    IDIO_FPRINTF (stderr, "SK ck=%s\n", (char *) ck);
	}
    }

    if (! idio_hash_equal (h, ck, kv)) {
	IDIO_FPRINTF (stderr, "idio_hash_hv_follow_chain: %10p not found\n", kv);
	return (IDIO_HASH_SIZE (h) + 1);
    }

    IDIO_FPRINTF (stderr, "idio_hash_hv_follow_chain: %10p found @%" PRIuPTR "\n", kv, chv);

    return chv;
}

int idio_hash_exists_key (IDIO h, void *kv)
{
    IDIO_ASSERT (h);

    if (idio_S_nil == h) {
	return 0;
    }

    if (idio_S_nil == kv) {
	idio_error_param_nil ("key", IDIO_C_LOCATION ("idio_hash_exists_key"));
	return IDIO_HASH_SIZE (h) + 1;
    }

    idio_hi_t hv = idio_hash_hv_follow_chain (h, kv);

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
	idio_error_param_nil ("key", IDIO_C_LOCATION ("idio_hash_exists"));
	return idio_S_nil;
    }

    IDIO_TYPE_ASSERT (hash, h);

    idio_hi_t hv = idio_hash_hv_follow_chain (h, kv);

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
	idio_error_param_nil ("key", IDIO_C_LOCATION ("idio_hash_get"));
	return idio_S_nil;
    }

    IDIO_TYPE_ASSERT (hash, h);

    IDIO_FPRINTF (stderr, "idio_hash_get: %10p\n", kv);
    idio_hi_t hv = idio_hash_hv_follow_chain (h, kv);

    if (hv > IDIO_HASH_SIZE (h)) {
	return idio_S_unspec;
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
	idio_error_param_nil ("key", IDIO_C_LOCATION ("idio_hash_delete"));
	return 0;
    }

    idio_hi_t hv = idio_hash_value (h, kv);

    if (hv > IDIO_HASH_SIZE (h)) {
	idio_error_printf (IDIO_C_LOCATION ("idio_hash_delete"), "hv %" PRIuPTR " > size %" PRIuPTR, hv, IDIO_HASH_SIZE (h));
    }

    IDIO_FPRINTF (stderr, "idio_hash_delete: %10p starting @%" PRIuPTR "\n", kv, hv);

    idio_hi_t phv = 0;
    idio_hi_t chv = hv;
    IDIO ck = IDIO_HASH_HE_KEY (h, chv);
    while (! idio_hash_equal (h, ck, kv) &&
	   IDIO_HASH_HE_NEXT (h, chv) < IDIO_HASH_SIZE (h)) {
	phv = chv;
	chv = IDIO_HASH_HE_NEXT (h, chv);
	ck = IDIO_HASH_HE_KEY (h, chv);
	IDIO_FPRINTF (stderr, "idio_hash_delete: %10p stepped to %" PRIuPTR " -> @%" PRIuPTR " -> %" PRIuPTR "\n", ck, phv, chv, IDIO_HASH_HE_NEXT (h, chv));
    }

    if (idio_S_nil == ck) {
	IDIO_FPRINTF (stderr, "idio_hash_delete: %10p nil found @%" PRIuPTR "?\n", kv, chv);
	return 0;
    }

    if (! idio_hash_equal (h, ck, kv)) {
	IDIO_FPRINTF (stderr, "idio_hash_delete: %10p not found\n", kv);
	return 0;
    }

    idio_hi_t nhv = IDIO_HASH_HE_NEXT (h, chv);
    IDIO_FPRINTF (stderr, "idio_hash_delete: %10p deleting from %" PRIuPTR " -> @%" PRIuPTR " -> %" PRIuPTR "\n", ck, phv, chv, nhv);

    if (0 == phv) {
	/* head of chain */
	if (nhv < IDIO_HASH_SIZE (h)) {
	    IDIO_FPRINTF (stderr, "idio_hash_delete: %10p head of chain: @%" PRIuPTR " <- %" PRIuPTR "\n", ck, chv, nhv);
	    IDIO_HASH_HE_KEY (h, chv) = IDIO_HASH_HE_KEY (h, nhv);
	    IDIO_HASH_HE_VALUE (h, chv) = IDIO_HASH_HE_VALUE (h, nhv);
	    IDIO_HASH_HE_NEXT (h, chv) = IDIO_HASH_HE_NEXT (h, nhv);
	
	    IDIO_FPRINTF (stderr, "idio_hash_delete: %10p head of chain: nullify %" PRIuPTR "\n", ck, nhv);
	    IDIO_HASH_HE_KEY (h, nhv) = idio_S_nil;
	    IDIO_HASH_HE_VALUE (h, nhv) = idio_S_nil;
	    IDIO_HASH_HE_NEXT (h, nhv) = IDIO_HASH_SIZE (h) + 1;
	} else {
	    IDIO_FPRINTF (stderr, "idio_hash_delete: %10p head of chain: nullify %" PRIuPTR "\n", ck, chv);
	    IDIO_HASH_HE_KEY (h, chv) = idio_S_nil;
	    IDIO_HASH_HE_VALUE (h, chv) = idio_S_nil;
	    IDIO_HASH_HE_NEXT (h, chv) = IDIO_HASH_SIZE (h) + 1;
	}
    } else {
	IDIO_FPRINTF (stderr, "idio_hash_delete: %10p link %" PRIuPTR " -> %" PRIuPTR "\n", ck, phv, nhv);
	IDIO_HASH_HE_NEXT (h, phv) = nhv;
    
	IDIO_FPRINTF (stderr, "idio_hash_delete: %10p nullify %" PRIuPTR "\n", ck, chv);
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

    idio_hi_t i;
    for (i = 0; i < IDIO_HASH_SIZE (h); i++) {
	IDIO k = IDIO_HASH_HE_KEY (h, i);
	if (! k) {
	    char em[BUFSIZ];
	    sprintf (em, "hash-keys-to-list: key #%zd is NULL", i);
	    idio_error_C (em, h, IDIO_C_LOCATION ("idio_hash_keys_to_list"));
	}
	if (idio_S_nil != k) {
	    if (IDIO_HASH_FLAGS (h) & IDIO_HASH_FLAG_STRING_KEYS) {
		r = idio_pair (idio_string_C ((char *) k), r);
	    } else {
		IDIO_ASSERT (k);
		r = idio_pair (k, r);
	    }
	}
    }

    return r;
}

IDIO idio_hash_values_to_list (IDIO h)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (hash, h);

    IDIO r = idio_S_nil;

    idio_hi_t i;
    for (i = 0; i < IDIO_HASH_SIZE (h); i++) {
	IDIO k = IDIO_HASH_HE_KEY (h, i);
	if (! k) {
	    char em[BUFSIZ];
	    sprintf (em, "hash-values-to-list: key #%zd is NULL", i);
	    idio_error_C (em, h, IDIO_C_LOCATION ("idio_hash_values_to_list"));
	}
	if (idio_S_nil != k) {
	    r = idio_pair (IDIO_HASH_HE_VALUE (h, i), r);
	}
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("hash?", hash_p, (IDIO o))
{
    IDIO_ASSERT (o);

    IDIO r = idio_S_false;

    if (idio_isa_hash (o)) {
	r = idio_S_true;
    }
    
    return r;
}

/*
 * SRFI-69
 *
 * make-hash-table [ equiv-func [ hash-func ]]
 *
 * hash-func defaults to hash-table-hash
 * equiv-func defaults to equal?
 *
 * If either is #n use the default.
 *
 * As an accelerator if comp is the *symbol* eq? eqv? equal? then use
 * the underlying C function.  So:
 *
 *  make-hash eq?
 *
 *  make-hash 'eq?
 *
 * are not (necessarily) the same.
 */
IDIO idio_hash_make_hash (IDIO args)
{
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    idio_hi_t size = 32;
    int (*equal) (void *k1, void *k2) = idio_equalp;
    idio_hi_t (*hashf) (IDIO h, void *k) = idio_hash_hashval;
    IDIO comp = idio_S_nil;
    IDIO hash = idio_S_nil;

    if (idio_S_nil != args) {
	comp = IDIO_PAIR_H (args);

	if (idio_S_nil != comp) {
	    equal = NULL;
	    if (idio_S_eqp == comp) {
		equal = idio_eqp;
		comp = idio_S_nil;
	    } else if (idio_S_eqvp == comp) {
		equal = idio_eqvp;
		comp = idio_S_nil;
	    } else if (idio_S_equalp == comp) {
		equal = idio_equalp;
		comp = idio_S_nil;
	    }
	}

	args = IDIO_PAIR_T (args);
    }

    if (idio_S_nil != args) {
	hash = IDIO_PAIR_H (args);

	if (idio_S_nil != hash) {
	    hashf = NULL;
	}
	
	args = IDIO_PAIR_T (args);
    }

    /*
     * SRFI-69 -- remaining args are implementation specific
     */

    IDIO ht = idio_hash (size, equal, hashf, comp, hash);
    
    return ht;
}

IDIO_DEFINE_PRIMITIVE0V ("make-hash", make_hash, (IDIO args))
{
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    return idio_hash_make_hash (args);
}

IDIO idio_hash_alist_to_hash (IDIO alist, IDIO args)
{
    IDIO_ASSERT (alist);
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (list, alist);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    IDIO ht = idio_hash_make_hash (args);
    
    while (idio_S_nil != alist) {
	IDIO p = IDIO_PAIR_H (alist);

	if (idio_isa_pair (p)) {
	    IDIO k = IDIO_PAIR_H (p);

	    /*
	     * SRFI 69: If some key occurs multiple times in alist,
	     * the value in the first association will take precedence
	     * over later ones.
	     */
	    if (idio_hash_exists_key (ht, k) == 0) {
		idio_hash_put (ht, k, IDIO_PAIR_T (p));
	    }
	} else {
	    idio_error_param_type ("not a pair in alist", p, IDIO_C_LOCATION ("alist->hash"));
	}

	alist = IDIO_PAIR_T (alist);
    }

    return ht;
}

IDIO_DEFINE_PRIMITIVE1V ("alist->hash", alist2hash, (IDIO alist, IDIO args))
{
    IDIO_ASSERT (alist);
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (list, alist);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    return idio_hash_alist_to_hash (alist, args);
}

IDIO_DEFINE_PRIMITIVE1 ("hash-equivalence-function", hash_equivalence_function, (IDIO ht))
{
    IDIO_ASSERT (ht);
    IDIO_VERIFY_PARAM_TYPE (hash, ht);

    IDIO r = idio_S_unspec;

    if (IDIO_HASH_EQUAL (ht) != NULL) {
	if (IDIO_HASH_EQUAL (ht) == idio_eqp) {
	    r = idio_S_eqp;
	} else if (IDIO_HASH_EQUAL (ht) == idio_eqvp) {
	    r = idio_S_eqvp;
	} else if (IDIO_HASH_EQUAL (ht) == idio_equalp) {
	    r = idio_S_equalp;
	}
    } else {
	r = IDIO_HASH_COMP (ht);
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("hash-hash-function", hash_hash_function, (IDIO ht))
{
    IDIO_ASSERT (ht);
    IDIO_VERIFY_PARAM_TYPE (hash, ht);

    IDIO r = idio_S_unspec;

    if (IDIO_HASH_HASHF (ht) != NULL) {
	if (IDIO_HASH_HASHF (ht) == idio_hash_hashval) {
	    r = idio_S_nil;
	}
    } else {
	r = IDIO_HASH_HASH (ht);
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1 ("hash-size", hash_size, (IDIO ht))
{
    IDIO_ASSERT (ht);
    IDIO_VERIFY_PARAM_TYPE (hash, ht);

    /*
     * Can we create a hash table with entries than IDIO_FIXNUM_MAX?
     * You imagine something else would have fallen over first...
     *
     * Better safe than sorry, though.  Call idio_integer() rather
     * than idio_fixnum().
     */
    return idio_integer (idio_hash_hcount (ht));
}

IDIO idio_hash_ref (IDIO ht, IDIO key, IDIO args)
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (key);
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (hash, ht);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    IDIO r = idio_hash_get (ht, key);

    if (idio_S_unspec == r) {
	if (idio_S_nil != args) {
	    IDIO dv = IDIO_PAIR_H (args);
	    r = idio_vm_invoke_C (idio_current_thread (), dv);
	} else {
	    idio_hash_error_key_not_found (key, IDIO_C_LOCATION ("hash-ref"));
	}
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE2V ("hash-ref", hash_ref, (IDIO ht, IDIO key, IDIO args))
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (key);
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (hash, ht);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    return idio_hash_ref (ht, key, args);
}

IDIO_DEFINE_PRIMITIVE3 ("hash-set!", hash_set, (IDIO ht, IDIO key, IDIO v))
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (key);
    IDIO_ASSERT (v);
    IDIO_VERIFY_PARAM_TYPE (hash, ht);

    idio_hash_put (ht, key, v);

    return idio_S_unspec;
}

/*
 * SRFI 69 -- not an error to delete a non-existent key
 */
IDIO_DEFINE_PRIMITIVE2 ("hash-delete!", hash_delete, (IDIO ht, IDIO key))
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (key);
    IDIO_VERIFY_PARAM_TYPE (hash, ht);

    idio_hash_delete (ht, key);
    
    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE2 ("hash-exists?", hash_existsp, (IDIO ht, IDIO key))
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (key);
    IDIO_VERIFY_PARAM_TYPE (hash, ht);

    IDIO r = idio_S_false;
    
    if (idio_hash_exists_key (ht, key)) {
	r = idio_S_true;
    }
    
    return r;
}

/*
 * SRFI 69:
 *
 * Semantically equivalent to, but may be implemented more efficiently
 * than, the following code:

   (hash-table-set! hash-table key
		    (function (hash-table-ref hash-table key thunk)))

 *
 * That is, call {func} on the existing value and set the key to the
 * returned value
 */
IDIO_DEFINE_PRIMITIVE3V ("hash-update!", hash_update, (IDIO ht, IDIO key, IDIO func, IDIO args))
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (key);
    IDIO_ASSERT (func);
    IDIO_ASSERT (args);
    IDIO_VERIFY_PARAM_TYPE (hash, ht);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    IDIO cv = idio_hash_ref (ht, key, args);

    IDIO nv = idio_vm_invoke_C (idio_current_thread (), IDIO_LIST2 (func, cv));
    
    idio_hash_put (ht, key, nv);
    
    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1 ("hash-keys", hash_keys, (IDIO ht))
{
    IDIO_ASSERT (ht);
    IDIO_VERIFY_PARAM_TYPE (hash, ht);

    return idio_hash_keys_to_list (ht);
}

IDIO_DEFINE_PRIMITIVE1 ("hash-values", hash_values, (IDIO ht))
{
    IDIO_ASSERT (ht);
    IDIO_VERIFY_PARAM_TYPE (hash, ht);

    return idio_hash_values_to_list (ht);
}

IDIO_DEFINE_PRIMITIVE2 ("hash-walk", hash_walk, (IDIO ht, IDIO func))
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (func);
    IDIO_VERIFY_PARAM_TYPE (hash, ht);

    idio_hi_t i;
    for (i = 0; i < IDIO_HASH_SIZE (ht); i++) {
	IDIO k = IDIO_HASH_HE_KEY (ht, i);
	if (! k) {
	    char em[BUFSIZ];
	    sprintf (em, "hash-walk: key #%zd is NULL", i);
	    idio_error_C (em, ht, IDIO_C_LOCATION ("hash-walk"));
	}
	if (idio_S_nil != k) {
	    IDIO v = IDIO_HASH_HE_VALUE (ht, i);
	    idio_vm_invoke_C (idio_current_thread (), IDIO_LIST3 (func, k, v));
	}
    }

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE3 ("hash-fold", hash_fold, (IDIO ht, IDIO func, IDIO val))
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (func);
    IDIO_ASSERT (val);
    IDIO_VERIFY_PARAM_TYPE (hash, ht);

    idio_hi_t i;
    for (i = 0; i < IDIO_HASH_SIZE (ht); i++) {
	IDIO k = IDIO_HASH_HE_KEY (ht, i);
	if (! k) {
	    char em[BUFSIZ];
	    sprintf (em, "hash-fold: key #%zd is NULL", i);
	    idio_error_C (em, ht, IDIO_C_LOCATION ("hash-fold"));
	}
	if (idio_S_nil != k) {
	    IDIO v = IDIO_HASH_HE_VALUE (ht, i);
	    val = idio_vm_invoke_C (idio_current_thread (), IDIO_LIST4 (func, k, v, val));
	}
    }

    return val;
}

IDIO_DEFINE_PRIMITIVE1 ("hash-copy", hash_copy, (IDIO ht))
{
    IDIO_ASSERT (ht);
    IDIO_VERIFY_PARAM_TYPE (hash, ht);

    return idio_hash_copy (ht);
}

IDIO_DEFINE_PRIMITIVE2 ("hash-merge!", hash_merge, (IDIO ht1, IDIO ht2))
{
    IDIO_ASSERT (ht1);
    IDIO_ASSERT (ht2);
    IDIO_VERIFY_PARAM_TYPE (hash, ht1);
    IDIO_VERIFY_PARAM_TYPE (hash, ht2);

    return idio_hash_merge (ht1, ht2);
}

void idio_init_hash ()
{
}

void idio_hash_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (hash_p);
    IDIO_ADD_PRIMITIVE (make_hash);
    IDIO_ADD_PRIMITIVE (alist2hash);
    IDIO_ADD_PRIMITIVE (hash_size);
    IDIO_ADD_PRIMITIVE (hash_equivalence_function);
    IDIO_ADD_PRIMITIVE (hash_hash_function);
    IDIO_ADD_PRIMITIVE (hash_ref);
    IDIO_ADD_PRIMITIVE (hash_set);
    IDIO_ADD_PRIMITIVE (hash_delete);
    IDIO_ADD_PRIMITIVE (hash_existsp);
    IDIO_ADD_PRIMITIVE (hash_update);
    IDIO_ADD_PRIMITIVE (hash_keys);
    IDIO_ADD_PRIMITIVE (hash_values);
    IDIO_ADD_PRIMITIVE (hash_walk);
    IDIO_ADD_PRIMITIVE (hash_fold);
    IDIO_ADD_PRIMITIVE (hash_copy);
    IDIO_ADD_PRIMITIVE (hash_merge);
}

void idio_final_hash ()
{
}

