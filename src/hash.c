/*
 * Copyright (c) 2015, 2017, 2020 Ian Fitchet <idf(at)idio-lang.org>
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
 * We used to use a form of Coalesced hashing
 * (http://en.wikipedia.org/wiki/Coalesced_hashing) but my
 * implementation was "poor."
 *
 * Back to basics.
 */

#include "idio.h"

void idio_hash_verify_chain (IDIO h, void *kv, int reqd);
void idio_hash_verify_all_keys (IDIO h);
size_t idio_hash_find_free_slot (IDIO h);
size_t idio_hash_hv_follow_chain (IDIO h, void *k);
IDIO idio_hash_weak_tables = idio_S_nil;

void idio_hash_error (char *m, IDIO c_location)
{
    IDIO_C_ASSERT (m);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    idio_error_printf (c_location, "%s", m);
}

void idio_hash_error_key_not_found (IDIO key, IDIO c_location)
{
    IDIO_ASSERT (key);
    IDIO_ASSERT (c_location);
    IDIO_TYPE_ASSERT (string, c_location);

    IDIO msh = idio_open_output_string_handle_C ();
    idio_display_C ("key not found", msh);

    IDIO location = idio_vm_source_location ();

    IDIO c = idio_struct_instance (idio_condition_rt_hash_key_not_found_error_type,
				   IDIO_LIST4 (idio_get_output_string (msh),
					       location,
					       c_location,
					       key));
    idio_raise_condition (idio_S_true, c);

    /* notreached */
}

static int idio_assign_hash_he (IDIO h, idio_hi_t size)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (hash, h);

    /*
     * We need to round size up to the nearest multiple of 2 for
     * which we need to find the MSB in size, calculating a possible
     * mask as we go.
     */
    idio_hi_t s = size;
    idio_hi_t mask = 0;

    while (s) {
	s >>= 1;
	mask = (mask << 1) | 1;
    }

    /*
     * This gives the following sample results:
     *
     * s=6 => mask=0x7
     * s=8 => mask=0xf
     * s=10 => mask=0xf
     *
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

    if (0 == (size & (mask >> 1))) {
	mask >>= 1;
    }

    /*
     * Now we can calculate our preferred size which is just mask+1
     * (cf. 2**n vs. (2**n)-1)
     */
    size = mask + 1;

    IDIO_GC_ALLOC (h->u.hash->ha, size * sizeof (idio_hash_entry_t));

    IDIO_HASH_MASK (h) = mask;
    IDIO_HASH_SIZE (h) = size;

    idio_hi_t i;
    for (i = 0; i < size; i++) {
	IDIO_HASH_HA (h, i) = NULL;
    }

    return 1;
}

/*
 * create a hash table of at least ``size`` elements -- see
 * idio_assign_hash_he() for how ``size`` may be increased.
 *
 * Either a C or Idio function can be supplied for:
 *
 * a. equality: comp_C and comp, respectively
 *
 * b. hashing: hash_C and hash, respectively
 *
 * You cannot supply both.  Use NULL for the C-variant to ignore it.
 */
IDIO idio_hash (idio_hi_t size, int (*comp_C) (void *k1, void *k2), idio_hi_t (*hash_C) (IDIO h, void *k), IDIO comp, IDIO hash)
{
    IDIO_C_ASSERT (size);
    IDIO_ASSERT (comp);
    IDIO_ASSERT (hash);

    if (NULL == comp_C) {
	if (idio_S_nil == comp) {
	    idio_hash_error ("no comparator supplied", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else if (idio_S_nil != comp) {
	idio_hash_error ("two comparators supplied", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    if (NULL == hash_C) {
	if (idio_S_nil == hash) {
	    idio_hash_error ("no hashing function supplied", IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    } else if (idio_S_nil != hash) {
	idio_hash_error ("two hashing functions supplied", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO h = idio_gc_get (IDIO_TYPE_HASH);
    IDIO_GC_ALLOC (h->u.hash, sizeof (idio_hash_t));
    IDIO_HASH_GREY (h) = NULL;
    IDIO_HASH_COUNT (h) = 0;
    IDIO_HASH_COMP_C (h) = comp_C;
    IDIO_HASH_HASH_C (h) = hash_C;
    IDIO_HASH_COMP (h) = comp;
    IDIO_HASH_HASH (h) = hash;
    IDIO_HASH_FLAGS (h) = IDIO_HASH_FLAG_NONE;

    idio_assign_hash_he (h, size);

    return h;
}

/*
 * Naive use of IDIO_HASH_HE_KEY (h, hv) is getting us burnt.  There
 * are a few special cases we need to handle and put them all
 * centrally.
 *
 * In particular, IDIO_HASH_FLAG_WEAK_KEYS where the key can be GC'd
 * from under our feet.
 *
 * If it has disappeared then we set the key and value to idio_S_nil
 * and return idio_S_nil, signalling the entry is free.
 */
static IDIO idio_hash_he_key (IDIO h, idio_hi_t hv, idio_hash_entry_t *he)
{
    IDIO k = IDIO_HASH_HE_KEY (he);

    if (idio_S_nil != k) {
	if (IDIO_HASH_FLAGS (h) & IDIO_HASH_FLAG_STRING_KEYS) {
	} else if (IDIO_HASH_FLAGS (h) & IDIO_HASH_FLAG_WEAK_KEYS) {
	    switch ((intptr_t) k & IDIO_TYPE_MASK) {
	    case IDIO_TYPE_FIXNUM_MARK:
	    case IDIO_TYPE_CONSTANT_MARK:
	    case IDIO_TYPE_PLACEHOLDER_MARK:
		break;
	    case IDIO_TYPE_POINTER_MARK:
		if (0 == k->type) {
		    idio_hash_entry_t *hel = IDIO_HASH_HA (h, hv);
		    if (he == hel) {
			IDIO_HASH_HA (h, hv) = IDIO_HASH_HE_NEXT (he);
		    } else {
			while (he != IDIO_HASH_HE_NEXT (hel)) {
			    hel = IDIO_HASH_HE_NEXT (hel);
			}
			IDIO_HASH_HE_NEXT (hel) = IDIO_HASH_HE_NEXT (he);
		    }
		    IDIO_GC_FREE (he);
		    IDIO_HASH_COUNT (h) -= 1;
		    k = idio_S_nil;
		} else {
		    IDIO_ASSERT (k);
		}
		break;
	    default:
		/* inconceivable! */
		idio_error_printf (IDIO_C_FUNC_LOCATION (), "type: unexpected object type %#x", k);

		/* notreached */
		return idio_S_notreached;
	    }

	} else {
	    IDIO_ASSERT (k);
	}
    }

    return k;
}

IDIO idio_copy_hash (IDIO orig, int depth)
{
    IDIO_ASSERT (orig);
    IDIO_TYPE_ASSERT (hash, orig);

    IDIO new = idio_gc_get (IDIO_TYPE_HASH);
    IDIO_GC_ALLOC (new->u.hash, sizeof (idio_hash_t));
    IDIO_HASH_GREY (new) = NULL;
    IDIO_HASH_COMP_C (new) = IDIO_HASH_COMP_C (orig);
    IDIO_HASH_HASH_C (new) = IDIO_HASH_HASH_C (orig);
    IDIO_HASH_COMP (new) = IDIO_HASH_COMP (orig);
    IDIO_HASH_HASH (new) = IDIO_HASH_HASH (orig);
    IDIO_HASH_FLAGS (new) = IDIO_HASH_FLAGS (orig);

    /*
     * Set the count to 0 as the act of idio_hash_put() in the old
     * ones will increment count.
     */
    IDIO_HASH_COUNT (new) = 0;

    idio_assign_hash_he (new, IDIO_HASH_COUNT (orig));

    /*
     * idio_copy() can invoke idio_vm_run() which can invoke the GC so
     * we need to protect {new}
     */
    idio_gc_protect (new);
    idio_hi_t i;
    for (i = 0; i < IDIO_HASH_SIZE (orig); i++) {
	idio_hash_entry_t *he = IDIO_HASH_HA (orig, i);
	for ( ; NULL != he; he = IDIO_HASH_HE_NEXT (he)) {
	    IDIO k = idio_hash_he_key (orig, i, he);
	    if (! k) {
		char em[BUFSIZ];
		sprintf (em, "copy-hash: key #%zd is NULL", i);
		idio_error_C (em, orig, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	    if (idio_S_nil != k) {
		IDIO v = IDIO_HASH_HE_VALUE (he);
		if (IDIO_COPY_DEEP == depth) {
		    v = idio_copy (v, depth);
		}
		idio_hash_put (new, k, v);
	    }
	}
    }
    idio_gc_expose (new);

    return new;
}

/*
 * SRFI 69 -- allows a destructive merge
 */
IDIO idio_merge_hash (IDIO ht1, IDIO ht2)
{
    IDIO_ASSERT (ht1);
    IDIO_ASSERT (ht2);
    IDIO_TYPE_ASSERT (hash, ht1);
    IDIO_TYPE_ASSERT (hash, ht2);

    IDIO_ASSERT_NOT_CONST (hash, ht1);

    idio_hi_t i;
    for (i = 0; i < IDIO_HASH_SIZE (ht2); i++) {
	idio_hash_entry_t *he = IDIO_HASH_HA (ht2, i);
	for ( ; NULL != he; he = IDIO_HASH_HE_NEXT (he)) {
	    IDIO k = idio_hash_he_key (ht2, i, he);
	    if (! k) {
		char em[BUFSIZ];
		sprintf (em, "merge-hash: key #%zd is NULL", i);
		idio_error_C (em, ht2, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	    if (idio_S_nil != k) {
		IDIO v = IDIO_HASH_HE_VALUE (he);
		idio_hash_put (ht1, k, v);
	    }
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
	    idio_hash_entry_t *he = IDIO_HASH_HA (h, i);
	    for ( ; NULL != he; he = IDIO_HASH_HE_NEXT (he)) {
		void *kv = idio_hash_he_key (h, i, he);
		if (idio_S_nil != kv) {
		    IDIO_GC_FREE (kv);
		}
	    }
	}
    }

    IDIO_GC_FREE (h->u.hash->ha);
    IDIO_GC_FREE (h->u.hash);
}

void idio_hash_resize (IDIO h, int larger)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (hash, h);

    IDIO_ASSERT_NOT_CONST (hash, h);

    idio_hi_t ohsize = h->u.hash->size;
    idio_hash_entry_t* *oha = h->u.hash->ha;

    idio_hi_t osize = IDIO_HASH_MASK (h) + 1;

    idio_hi_t hcount = IDIO_HASH_COUNT (h);

    idio_hi_t nsize = osize;

    /*
     * We need to figure out the "loading factor" versus the size.  We
     * can have a really small size and very long lists per bucket.
     */
    if (larger) {
	/*
	 * high is 75%
	 */
	idio_hi_t load_high = (osize / 2) + (osize / 4);
	if (hcount > load_high) {
	    while (nsize <= hcount) {
		nsize *= 2;
	    }
	    nsize *= 2;
	}
    } else {
	/*
	 * low is 6%
	 */
	idio_hi_t load_low = (osize / 16);
	if (load_low < 8) {
	    load_low = 8;
	}
	if (hcount < load_low) {
	    while (nsize > hcount) {
		nsize /= 2;
	    }
	    /*
	     * nsize is now one halving less than hcount.  We know
	     * we'll trigger a resize if hcount > size/2 so nsize
	     * needs to be bigger than that
	     */
	    nsize *= 4;

	    /*
	     * Of course, if hcount was 0, say, then nsize is now zero.
	     */
	    if (nsize < load_low) {
		nsize = load_low;
	    }
	}
    }

    if (nsize == osize) {
	return;
    }

    idio_assign_hash_he (h, nsize);

    /*
     * The re-insertion by each idio_hash_put is going to increment
     * IDIO_HASH_COUNT(h) -- we don't want to double count.
     */
    IDIO_HASH_COUNT (h) = 0;
    idio_hi_t i;
    idio_hi_t c = 0;
    for (i = 0 ; i < ohsize; i++) {
	idio_hash_entry_t *he = oha[i];
	for ( ; NULL != he; he = IDIO_HASH_HE_NEXT (he)) {
	    if (idio_S_nil != IDIO_HASH_HE_KEY (he)) {
		c++;
		idio_hash_put (h, IDIO_HASH_HE_KEY (he), IDIO_HASH_HE_VALUE (he));
	    } else {
		fprintf (stderr, "hash-resize: #n key?\n");
	    }
	}
    }

    if (hcount != c) {
	fprintf (stderr, "LOST HASH ENTRIES (c): %3zu != %3zu\n", IDIO_HASH_COUNT (h), hcount);
	idio_dump (h, 2);
	exit (2);
    }

    if (hcount != IDIO_HASH_COUNT (h)) {
	fprintf (stderr, "LOST HASH ENTRIES (count): %3zu != %3zu\n", IDIO_HASH_COUNT (h), hcount);
	idio_dump (h, 2);
	exit (3);
    }

    idio_gc_stats_free (osize * sizeof (idio_hash_entry_t));
    idio_gc_stats_free (osize * sizeof (idio_hash_entry_t));

    IDIO_GC_FREE (oha);
}

/*
 * idio_hash_default_hash_C_* are variations on a theme to caluclate a
 * hash value for a given C type of thing -- where lots of Idio types
 * map onto similar C types.
 */
idio_hi_t idio_hash_default_hash_C_uintmax_t (uintmax_t i)
{

    idio_hi_t hv = i ^ (i << 8) ^ (i << 16) ^ (i << 24);

    return hv;
}

idio_hi_t idio_hash_default_hash_C_void (void *p)
{
    IDIO_C_ASSERT (p);

    unsigned long ul = (unsigned long) p;

    /*
     * all our objects are at least 16 bytes so pointer alignment
     * means the bottom 4-5 bits are always 0
    */
    idio_hi_t hv = idio_hash_default_hash_C_uintmax_t (ul ^ (ul >> 5));
    return hv;
}

idio_hi_t idio_hash_default_hash_C_character (IDIO c)
{
    IDIO_ASSERT (c);

    return idio_hash_default_hash_C_uintmax_t (IDIO_CHARACTER_VAL (c));
}

idio_hi_t idio_hash_default_hash_C_string_C (idio_hi_t blen, const char *s_C)
{
    IDIO_C_ASSERT (s_C);

    idio_hi_t hv = idio_hash_default_hash_C_uintmax_t (blen);

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

idio_hi_t idio_hash_default_hash_C_string (IDIO s)
{
    IDIO_ASSERT (s);

    size_t size = 0;
    char *Cs = idio_string_as_C (s, &size);
    idio_hi_t r = idio_hash_default_hash_C_string_C (strlen (Cs), Cs);
    IDIO_GC_FREE (Cs);
    return r;
}

idio_hi_t idio_hash_default_hash_C_symbol (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_hash_default_hash_C_void (IDIO_SYMBOL_S (h));
}

idio_hi_t idio_hash_default_hash_C_keyword (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_hash_default_hash_C_void (IDIO_KEYWORD_S (h));
}

idio_hi_t idio_hash_default_hash_C_pair (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_hash_default_hash_C_uintmax_t ((unsigned long) IDIO_PAIR_H (h) ^ (unsigned long) IDIO_PAIR_T (h));
}

idio_hi_t idio_hash_default_hash_C_array (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_hash_default_hash_C_void (h->u.array);
}

idio_hi_t idio_hash_default_hash_C_hash (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_hash_default_hash_C_void (h->u.hash);
}

idio_hi_t idio_idio_hash_default_hash_C_closure (IDIO h)
{
    IDIO_ASSERT (h);

    idio_hi_t hv = idio_hash_default_hash_C_uintmax_t (IDIO_CLOSURE_CODE_PC (h));
    hv ^= idio_hash_default_hash_C_void (IDIO_CLOSURE_ENV (h));
    return hv;
}

idio_hi_t idio_idio_hash_default_hash_C_primitive (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_hash_default_hash_C_void (IDIO_PRIMITIVE_F (h));
}

idio_hi_t idio_idio_hash_default_hash_C_module (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_hash_default_hash_C_void (IDIO_MODULE_NAME (h));
}

idio_hi_t idio_idio_hash_default_hash_C_frame (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_hash_default_hash_C_void (h->u.frame);
}

idio_hi_t idio_idio_hash_default_hash_C_bignum (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_hash_default_hash_C_void (IDIO_BIGNUM_SIG (h));
}

idio_hi_t idio_idio_hash_default_hash_C_handle (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_hash_default_hash_C_void (IDIO_HANDLE_STREAM (h));
}

idio_hi_t idio_hash_default_hash_C_bitset (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_hash_default_hash_C_void (h->u.bitset.bits);
}

idio_hi_t idio_idio_hash_default_hash_C_struct_type (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_hash_default_hash_C_void (h->u.struct_type->fields);
}

idio_hi_t idio_idio_hash_default_hash_C_struct_instance (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_hash_default_hash_C_void (h->u.struct_instance.fields);
}

idio_hi_t idio_hash_default_hash_C_C_struct (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_hash_default_hash_C_void (IDIO_C_STRUCT_METHODS (h));
}

idio_hi_t idio_hash_default_hash_C_C_instance (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_hash_default_hash_C_void (IDIO_C_INSTANCE_P (h));
}

idio_hi_t idio_hash_default_hash_C_C_FFI (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_hash_default_hash_C_void (IDIO_C_FFI_CIFP (h));
}

/*
 * idio_hash_default_hash_C() is the default hashing function and
 * basically calls one of the above.  It will return an index into the
 * hash table ``ht`` for the key ``kv``.
 *
 * This is, the result will be modulo IDIO_HASH_MASK (ht) + 1.
 *
 * Note that a pre-computed IDIO_HASHVAL(k) is the untempered
 * idio_hi_t result of the hashing function and is suitable to be used
 * module IDIO_HASH_MASK (ht).
 */
idio_hi_t idio_hash_default_hash_C (IDIO h, void *kv)
{
    IDIO_ASSERT (h);

    IDIO k = (IDIO) kv;
    IDIO_ASSERT (k);

    idio_hi_t hv = 0;
    idio_type_e type = idio_type (k);

    switch (type) {
    case IDIO_TYPE_NONE:
    case IDIO_TYPE_PLACEHOLDER:
	idio_error_printf (IDIO_C_FUNC_LOCATION_S ("PLACEHOLDER"), "type: unexpected object type %#x", k);

	/* notreached */
	return -1;
    }

    /*
     * There's no precomputed IDIO_HASHVAL() for fixed types.
     */
    switch (type) {
    case IDIO_TYPE_FIXNUM:
    case IDIO_TYPE_CONSTANT_IDIO:
    case IDIO_TYPE_CONSTANT_TOKEN:
    case IDIO_TYPE_CONSTANT_I_CODE:
    case IDIO_TYPE_CONSTANT_CHARACTER:
    case IDIO_TYPE_CONSTANT_UNICODE:
    return (idio_hash_default_hash_C_uintmax_t ((uintptr_t) k) & IDIO_HASH_MASK (h));
    }

    /*
     * 0 is the sentinel value for a hashval.  Of course a hash value
     * could be 0 in which case for a small number of objects we
     * re-compute the hash.
     */
    if (0 != IDIO_HASHVAL (k)) {
	idio_hi_t hv = (IDIO_HASHVAL (k) & IDIO_HASH_MASK (h));
	/* fprintf (stderr, "ih_hv return with pre-comp %p %8tx %8td\n", k, IDIO_HASHVAL (k), hv); */
	return hv;
    }

    switch (type) {
    case IDIO_TYPE_STRING:
	{
	    size_t size = 0;
	    char *sk = idio_string_as_C (k, &size);
	    hv = idio_hash_default_hash_C_string_C (strlen (sk), sk);
	    IDIO_GC_FREE (sk);
	}
	break;
    case IDIO_TYPE_SUBSTRING:
	{
	    size_t size = 0;
	    char *sk = idio_string_as_C (k, &size);
	    hv = idio_hash_default_hash_C_string_C (strlen (sk), sk);
	    IDIO_GC_FREE (sk);
	}
	break;
    case IDIO_TYPE_SYMBOL:
	hv = idio_hash_default_hash_C_symbol (k);
	break;
    case IDIO_TYPE_KEYWORD:
	hv = idio_hash_default_hash_C_keyword (k);
	break;
    case IDIO_TYPE_PAIR:
	hv = idio_hash_default_hash_C_pair (k);
	break;
    case IDIO_TYPE_ARRAY:
	hv = idio_hash_default_hash_C_array (k);
	break;
    case IDIO_TYPE_HASH:
	hv = idio_hash_default_hash_C_hash (k);
	break;
    case IDIO_TYPE_CLOSURE:
	hv = idio_idio_hash_default_hash_C_closure (k);
	break;
    case IDIO_TYPE_PRIMITIVE:
	hv = idio_idio_hash_default_hash_C_primitive (k);
	break;
    case IDIO_TYPE_MODULE:
	hv = idio_idio_hash_default_hash_C_module (k);
	break;
    case IDIO_TYPE_FRAME:
	hv = idio_idio_hash_default_hash_C_frame (k);
	break;
    case IDIO_TYPE_BIGNUM:
	hv = idio_idio_hash_default_hash_C_bignum (k);
	break;
    case IDIO_TYPE_HANDLE:
	hv = idio_idio_hash_default_hash_C_handle (k);
	break;
    case IDIO_TYPE_BITSET:
	hv = idio_hash_default_hash_C_bitset (k);
	break;
    case IDIO_TYPE_STRUCT_TYPE:
	hv = idio_idio_hash_default_hash_C_struct_type (k);
	break;
    case IDIO_TYPE_STRUCT_INSTANCE:
	hv = idio_idio_hash_default_hash_C_struct_instance (k);
	break;
    case IDIO_TYPE_C_INT:
	hv = idio_hash_default_hash_C_uintmax_t ((uintmax_t) IDIO_C_TYPE_INT (k));
	break;
    case IDIO_TYPE_C_UINT:
	hv = idio_hash_default_hash_C_uintmax_t ((uintmax_t) IDIO_C_TYPE_UINT (k));
	break;
    case IDIO_TYPE_C_FLOAT:
	hv = idio_hash_default_hash_C_uintmax_t ((uintmax_t) IDIO_C_TYPE_FLOAT (k));
	break;
    case IDIO_TYPE_C_DOUBLE:
	hv = idio_hash_default_hash_C_uintmax_t ((uintmax_t) IDIO_C_TYPE_DOUBLE (k));
	break;
    case IDIO_TYPE_C_POINTER:
	hv = idio_hash_default_hash_C_void (IDIO_C_TYPE_POINTER_P (k));
	break;
    case IDIO_TYPE_C_STRUCT:
	hv = idio_hash_default_hash_C_C_struct (k);
	break;
    case IDIO_TYPE_C_INSTANCE:
	hv = idio_hash_default_hash_C_C_instance (k);
	break;
    case IDIO_TYPE_C_FFI:
	hv = idio_hash_default_hash_C_C_FFI (k);
	break;
    default:
	fprintf (stderr, "idio_hash_default_hash_C default type = %d==%s\n", type, idio_type_enum2string (type));
	idio_error_C ("idio_hash_default_hash_C: unexpected type", k, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return -1;
    }

    IDIO_HASHVAL (k) = hv;
    hv = IDIO_HASHVAL (k) & IDIO_HASH_MASK (h);
    /* fprintf (stderr, "ih_hv return with     comp %p %8tx %8td\n", k, IDIO_HASHVAL (k), hv); */
    return hv;
}

/*
 * idio_hash_value_index() will return an index into the hash table
 * ``ht`` for the key ``kv``.
 *
 * This is, the result will be modulo IDIO_HASH_MASK (ht).
 *
 * It will call the C hashing function, if set, otherwise the Idio
 * hashing function.
 *
 * ``kv`` is a generic "key value" as we can have C strings as keys.
 *
 * Note that a pre-computed IDIO_HASHVAL(k) is the untempered
 * idio_hi_t result of the hashing function and is suitable to be used
 * module IDIO_HASH_MASK (ht).
 */
idio_hi_t idio_hash_value_index (IDIO ht, void *kv)
{
    IDIO_ASSERT (ht);
    IDIO_TYPE_ASSERT (hash, ht);

    if (IDIO_HASH_HASH_C (ht) != NULL) {
	return IDIO_HASH_HASH_C (ht) (ht, kv);
    } else {
	IDIO k = (IDIO) kv;

	switch ((intptr_t) kv & IDIO_TYPE_MASK) {
	case IDIO_TYPE_POINTER_MARK:
	    if (0 != IDIO_HASHVAL (k)) {
		return (IDIO_HASHVAL (k) & IDIO_HASH_MASK (ht));
	    }
	    break;
	}

	idio_hi_t hvi = 0;

	IDIO ihvi = idio_vm_invoke_C (idio_thread_current_thread (), IDIO_LIST2 (IDIO_HASH_HASH (ht), k));

	if (idio_isa_fixnum (ihvi)) {
	    hvi = IDIO_FIXNUM_VAL (ihvi);
	} else if (idio_isa_bignum (ihvi)) {
	    /*
	     * idio_ht_t is a size_t so the fact that ptrdiff_t &
	     * size_t on non-segmented architectures are *the same
	     * size* means we can technically return a negative value
	     * here.
	     *
	     * Technically wrong is the worst kind of wrong.
	     */
	    hvi = idio_bignum_ptrdiff_value (ihvi);
	} else {
	    idio_error_param_type ("fixnum|bignum", ihvi, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return -1;
	}

	switch ((intptr_t) kv & IDIO_TYPE_MASK) {
	case IDIO_TYPE_POINTER_MARK:
	    IDIO_HASHVAL (k) = hvi;
	    break;
	}

	return (hvi & IDIO_HASH_MASK (ht));
    }

    /* notreached */
    return -1;
}

/*
 * idio_hash_equal() determines the equality of two keys.
 *
 * It will call the C equality function, if set, otherwise the Idio
 * equality function.
 *
 * Note that the C equality function is most likely to be one of
 * idio_eqp, idio_eqvp or idio_equalp depending on which macro was
 * used to create the hash (IDIO_HASH_EQP(size), ...).
 */
int idio_hash_equal (IDIO ht, void *kv1, void *kv2)
{
    IDIO_ASSERT (ht);
    IDIO_TYPE_ASSERT (hash, ht);

    if (IDIO_HASH_COMP_C (ht) != NULL) {
	return IDIO_HASH_COMP_C (ht) (kv1, kv2);
    } else {
	IDIO r = idio_vm_invoke_C (idio_thread_current_thread (), IDIO_LIST3 (IDIO_HASH_COMP (ht), (IDIO) kv1, (IDIO) kv2));
	if (idio_S_false == r) {
	    return 0;
	} else {
	    return 1;
	}
    }
}

IDIO idio_hash_put (IDIO h, void *kv, IDIO v)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (hash, h);

    IDIO_ASSERT_NOT_CONST (hash, h);

    if (idio_S_nil == kv) {
	idio_error_param_nil ("key", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    idio_hi_t hv = idio_hash_value_index (h, kv);

    idio_hash_entry_t *he = IDIO_HASH_HA (h, hv);
    int found = 0;
    for (; NULL != he; he = IDIO_HASH_HE_NEXT (he)) {
	if (idio_hash_equal (h, kv, IDIO_HASH_HE_KEY (he))) {
	    found = 1;
	    IDIO_HASH_HE_VALUE (he) = v;
	    break;
	}
    }

    if (! found) {
	idio_hash_entry_t *new;
	IDIO_GC_ALLOC (new, sizeof (idio_hash_entry_t));
	IDIO_HASH_HE_NEXT (new) = IDIO_HASH_HA (h, hv);
	IDIO_HASH_HE_KEY (new) = kv;
	IDIO_HASH_HE_VALUE (new) = v;
	IDIO_HASH_HA (h, hv) = new;

	IDIO_HASH_COUNT (h) += 1;
    }

    if (IDIO_HASH_COUNT (h) > IDIO_HASH_SIZE (h)) {
	idio_hash_resize (h, 1);
    }

    return kv;
}

idio_hash_entry_t *idio_hash_he (IDIO h, void *kv)
{
    IDIO_ASSERT (h);

    if (idio_S_nil == h) {
	return NULL;
    }

    IDIO_TYPE_ASSERT (hash, h);

    if (idio_S_nil == kv) {
	idio_error_param_nil ("key", IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return NULL;
    }

    idio_hi_t hv = idio_hash_value_index (h, kv);

    idio_hash_entry_t *he = IDIO_HASH_HA (h, hv);
    for (; NULL != he; he = IDIO_HASH_HE_NEXT (he)) {
	if (idio_hash_equal (h, kv, IDIO_HASH_HE_KEY (he))) {
	    return he;
	}
    }

    return NULL;
}

int idio_hash_exists_key (IDIO h, void *kv)
{
    IDIO_ASSERT (h);

    if (idio_S_nil == h) {
	return 0;
    }

    IDIO_TYPE_ASSERT (hash, h);

    if (idio_S_nil == kv) {
	idio_error_param_nil ("key", IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return 0;
    }

    if (NULL == idio_hash_he (h, kv)) {
	return 0;
    } else {
	return 1;
    }
}

IDIO idio_hash_exists (IDIO h, void *kv)
{
    IDIO_ASSERT (h);

    if (idio_S_nil == kv) {
	idio_error_param_nil ("key", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO_TYPE_ASSERT (hash, h);

    if (NULL == idio_hash_he (h, kv)) {
	return idio_S_nil;
    } else {
	return kv;
    }
}

IDIO idio_hash_ref (IDIO h, void *kv)
{
    IDIO_ASSERT (h);

    if (idio_S_nil == kv) {
	idio_error_param_nil ("key", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO_TYPE_ASSERT (hash, h);

    idio_hash_entry_t *he = idio_hash_he (h, kv);

    if (NULL == he) {
	return idio_S_unspec;
    }

    return IDIO_HASH_HE_VALUE (he);
}

/*
 * It is quite possible we may be asked to delete a key that doesn't
 * exist.
 */
int idio_hash_delete (IDIO h, void *kv)
{
    IDIO_ASSERT (h);

    if (idio_S_nil == h) {
	return 0;
    }

    IDIO_TYPE_ASSERT (hash, h);

    IDIO_ASSERT_NOT_CONST (hash, h);

    if (idio_S_nil == kv) {
	idio_error_param_nil ("key", IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return 0;
    }

    idio_hi_t hv = idio_hash_value_index (h, kv);

    idio_hash_entry_t *hel = IDIO_HASH_HA (h, hv);
    if (NULL == hel) {
	return 0;
    }

    idio_hash_entry_t *he = hel;

    if (idio_hash_equal (h, kv, IDIO_HASH_HE_KEY (he))) {
	IDIO_HASH_HA (h, hv) = IDIO_HASH_HE_NEXT (he);
    } else {
	while (NULL != hel &&
	       NULL != IDIO_HASH_HE_NEXT (hel)) {
	    if (idio_hash_equal (h, kv, IDIO_HASH_HE_KEY (IDIO_HASH_HE_NEXT (hel)))) {
		break;
	    }
	    hel = IDIO_HASH_HE_NEXT (hel);
	}
	he = IDIO_HASH_HE_NEXT (hel);
	IDIO_HASH_HE_NEXT (hel) = IDIO_HASH_HE_NEXT (he);
    }

    if (NULL == he) {
	return 0;
    }

    IDIO_GC_FREE (he);
    IDIO_HASH_COUNT (h) -= 1;

    idio_hi_t hsize = IDIO_HASH_SIZE (h);

    idio_hi_t load_low = (hsize / 16);
    if (IDIO_HASH_COUNT (h) < load_low) {
	idio_hash_resize (h, 0);
    }

    return 1;
}

void idio_hash_tidy_weak_references (void)
{
    IDIO hwts = IDIO_PAIR_H (idio_hash_weak_tables);

    while (idio_S_nil != hwts) {
	IDIO h = IDIO_PAIR_H (hwts);

	if (IDIO_HASH_FLAGS (h) & IDIO_HASH_FLAG_WEAK_KEYS) {
	    idio_hi_t i;
	    for (i = 0; i < IDIO_HASH_SIZE (h); i++) {
		idio_hash_entry_t *he = IDIO_HASH_HA (h, i);
		for (; NULL != he; he = IDIO_HASH_HE_NEXT (he)) {
		    IDIO k = IDIO_HASH_HE_KEY (he);
		    switch ((uintptr_t) k & IDIO_TYPE_MASK) {
		    case IDIO_TYPE_FIXNUM_MARK:
		    case IDIO_TYPE_CONSTANT_MARK:
		    case IDIO_TYPE_PLACEHOLDER_MARK:
			break;
		    case IDIO_TYPE_POINTER_MARK:
			if (IDIO_TYPE_NONE == k->type) {
			    fprintf (stderr, "ih_twr %p @%zu\n", h, i);

			    idio_hash_entry_t *hel = IDIO_HASH_HA (h, i);
			    if (he == hel) {
				IDIO_HASH_HA (h, i) = IDIO_HASH_HE_NEXT (he);
			    } else {
				while (he != IDIO_HASH_HE_NEXT (hel)) {
				    hel = IDIO_HASH_HE_NEXT (hel);
				}
				IDIO_HASH_HE_NEXT (hel) = IDIO_HASH_HE_NEXT (he);
			    }
			    IDIO_GC_FREE (he);
			    IDIO_HASH_COUNT (h) -= 1;
			}
			break;
		    default:
			/* inconceivable! */
			idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected object mark type %#x", k);

			/* notreached */
			return;
		    }
		}
	    }
	} else {
	    fprintf (stderr, "how is %p on the weak table list?\n", h);
	    idio_dump (h, 4);
	}

	hwts = IDIO_PAIR_T (hwts);
    }
}

void idio_hash_add_weak_table (IDIO h)
{
    IDIO_ASSERT (h);

    IDIO_TYPE_ASSERT (hash, h);

    IDIO_HASH_FLAGS (h) |= IDIO_HASH_FLAG_WEAK_KEYS;

    /*
     * Annoyingly, initialising the GC requires a weak-keyed table for
     * the finalizers
     */
    if (idio_S_nil == idio_hash_weak_tables) {
	idio_hash_weak_tables = idio_pair (idio_S_nil, idio_S_nil);
	idio_gc_protect (idio_hash_weak_tables);
    }

    IDIO_PAIR_H (idio_hash_weak_tables) = idio_pair (h, IDIO_PAIR_H (idio_hash_weak_tables));
}

void idio_hash_remove_weak_table (IDIO h)
{
    IDIO_ASSERT (h);

    IDIO_TYPE_ASSERT (hash, h);

    IDIO hwts = IDIO_PAIR_H (idio_hash_weak_tables);

    if (IDIO_PAIR_H (hwts) == h) {
	IDIO_PAIR_H (idio_hash_weak_tables) = IDIO_PAIR_T (hwts);
    } else {
	int removed = 0;
	IDIO p = hwts;
	hwts = IDIO_PAIR_T (hwts);
	while (idio_S_nil != hwts) {
	    if (IDIO_PAIR_H (hwts) == h) {
		removed = 1;
		IDIO_PAIR_T (p) = IDIO_PAIR_T (hwts);
		break;
	    }
	    p = hwts;
	    hwts = IDIO_PAIR_T (hwts);
	}

	if (! removed) {
	    fprintf (stderr, "ih_rwt: failed to remove weak table %p\n", h);
	}
    }
}

IDIO idio_hash_keys_to_list (IDIO h)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (hash, h);

    IDIO r = idio_S_nil;

    idio_hi_t i;
    for (i = 0; i < IDIO_HASH_SIZE (h); i++) {
	idio_hash_entry_t *he = IDIO_HASH_HA (h, i);
	for ( ; NULL != he; he = IDIO_HASH_HE_NEXT (he)) {
	    IDIO k = IDIO_HASH_HE_KEY (he);
	    if (! k) {
		char em[BUFSIZ];
		sprintf (em, "key #%zd is NULL", i);
		idio_error_C (em, h, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
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
	idio_hash_entry_t *he = IDIO_HASH_HA (h, i);
	for ( ; NULL != he; he = IDIO_HASH_HE_NEXT (he)) {
	    IDIO k = IDIO_HASH_HE_KEY (he);
	    if (! k) {
		char em[BUFSIZ];
		sprintf (em, "hash-values-to-list: key #%zd is NULL", i);
		idio_error_C (em, h, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	    if (idio_S_nil != k) {
		r = idio_pair (IDIO_HASH_HE_VALUE (he), r);
	    }
	}
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("hash?", hash_p, (IDIO o), "o", "\
test if `o` is an hash				\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is an hash, #f otherwise	\n\
")
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
 * make-hash-table [ equiv-func [ hash-func [size]]]
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
    IDIO_USER_TYPE_ASSERT (list, args);

    idio_hi_t size = 32;
    int (*equal) (void *k1, void *k2) = idio_equalp;
    idio_hi_t (*hash_C) (IDIO h, void *k) = idio_hash_default_hash_C;
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
	    hash_C = NULL;
	}

	args = IDIO_PAIR_T (args);
    }

    /*
     * SRFI-69 -- remaining args are implementation specific
     */
    if (idio_S_nil != args) {
	IDIO isize = IDIO_PAIR_H (args);

	if (idio_isa_fixnum (isize)) {
	    size = IDIO_FIXNUM_VAL (isize);
	} else {
	    idio_error_param_type ("fixnum", isize, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	args = IDIO_PAIR_T (args);
    }


    IDIO ht = idio_hash (size, equal, hash_C, comp, hash);

    return ht;
}

IDIO_DEFINE_PRIMITIVE0V_DS ("make-hash", make_hash, (IDIO args), "[ equiv-func [ hash-func [size]]]", "\
create a hash table					\n\
							\n\
:param hash-func: defaults to ``hash-table-hash``	\n\
:type hash-func: function				\n\
:param equiv-func: defaults to ``equal?``		\n\
:type equiv-func: function or symbol			\n\
:param size: default to 32				\n\
:type size: fixnum					\n\
							\n\
If either of ``hash-func`` or ``equiv-func`` is ``#n``	\n\
use the default.					\n\
							\n\
As an accelerator if ``equiv-comp`` is one of the	\n\
*symbols* ``eq?``, ``eqv?`` or ``equal?`` then use the	\n\
underlying C function.					\n\
							\n\
:return: hash table					\n\
")
{
    IDIO_ASSERT (args);

    IDIO_USER_TYPE_ASSERT (list, args);

    return idio_hash_make_hash (args);
}

IDIO idio_hash_alist_to_hash (IDIO alist, IDIO args)
{
    IDIO_ASSERT (alist);
    IDIO_ASSERT (args);
    IDIO_USER_TYPE_ASSERT (list, alist);
    IDIO_USER_TYPE_ASSERT (list, args);

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
	    idio_error_param_type ("not a pair in alist", p, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}

	alist = IDIO_PAIR_T (alist);
    }

    return ht;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("alist->hash", alist2hash, (IDIO alist, IDIO args), "al [args]", "\
convert association list ``al`` into a hash table	\n\
							\n\
:param al: association list				\n\
:type al: association list				\n\
:param args: argument for ``make-hash``			\n\
:type args: (see ``make-hash``)				\n\
							\n\
:return: hash table					\n\
")
{
    IDIO_ASSERT (alist);
    IDIO_ASSERT (args);

    IDIO_USER_TYPE_ASSERT (list, alist);
    IDIO_USER_TYPE_ASSERT (list, args);

    return idio_hash_alist_to_hash (alist, args);
}

IDIO_DEFINE_PRIMITIVE1_DS ("hash-equivalence-function", hash_equivalence_function, (IDIO ht), "h", "\
return the ``equiv-func`` of ``h``			\n\
							\n\
:param h: hash table					\n\
:type h: hash table					\n\
							\n\
:return: equivalent function				\n\
")
{
    IDIO_ASSERT (ht);

    IDIO_USER_TYPE_ASSERT (hash, ht);

    IDIO r = idio_S_unspec;

    if (IDIO_HASH_COMP_C (ht) != NULL) {
	if (IDIO_HASH_COMP_C (ht) == idio_eqp) {
	    r = idio_S_eqp;
	} else if (IDIO_HASH_COMP_C (ht) == idio_eqvp) {
	    r = idio_S_eqvp;
	} else if (IDIO_HASH_COMP_C (ht) == idio_equalp) {
	    r = idio_S_equalp;
	}
    } else {
	r = IDIO_HASH_COMP (ht);
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("hash-hash-function", hash_hash_function, (IDIO ht), "h", "\
return the ``hash-func`` of ``h``			\n\
							\n\
:param h: hash table					\n\
:type h: hash table					\n\
							\n\
:return: hash function					\n\
")
{
    IDIO_ASSERT (ht);

    IDIO_USER_TYPE_ASSERT (hash, ht);

    IDIO r = idio_S_unspec;

    if (IDIO_HASH_HASH_C (ht) != NULL) {
	if (IDIO_HASH_HASH_C (ht) == idio_hash_default_hash_C) {
	    r = idio_S_nil;
	}
    } else {
	r = IDIO_HASH_HASH (ht);
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("hash-size", hash_size, (IDIO ht), "h", "\
return the key count of ``h``				\n\
							\n\
:param h: hash table					\n\
:type h: hash table					\n\
							\n\
:return: key count					\n\
")
{
    IDIO_ASSERT (ht);

    IDIO_USER_TYPE_ASSERT (hash, ht);

    /*
     * Can we create a hash table with entries than IDIO_FIXNUM_MAX?
     * You imagine something else would have fallen over first...
     *
     * Better safe than sorry, though.  Call idio_integer() rather
     * than idio_fixnum().
     */
    return idio_integer (IDIO_HASH_COUNT (ht));
}

IDIO idio_hash_reference (IDIO ht, IDIO key, IDIO args)
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (key);
    IDIO_ASSERT (args);
    IDIO_USER_TYPE_ASSERT (hash, ht);
    IDIO_USER_TYPE_ASSERT (list, args);

    IDIO r = idio_hash_ref (ht, key);

    if (idio_S_unspec == r) {
	if (idio_S_nil != args) {
	    IDIO dv = IDIO_PAIR_H (args);
	    if (idio_isa_procedure (dv)) {
		r = idio_vm_invoke_C (idio_thread_current_thread (), dv);
	    } else {
		r = dv;
	    }
	} else {
	    idio_hash_error_key_not_found (key, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    return r;
}

IDIO_DEFINE_PRIMITIVE2V_DS ("hash-ref", hash_ref, (IDIO ht, IDIO key, IDIO args), "ht key [default]", "\
return the value indexed by ``key` in hash table ``ht``	\n\
							\n\
:param ht: hash table					\n\
:type ht: hash table					\n\
:param key: non-#n value				\n\
:type key: any non-#n					\n\
:param default: a default value if ``key`` not found	\n\
:type default: a thunk or a simple value		\n\
							\n\
:return: value (#unspec if ``key`` not found and no	\n\
	 ``default`` supplied)				\n\
")
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (key);
    IDIO_ASSERT (args);

    IDIO_USER_TYPE_ASSERT (hash, ht);
    IDIO_USER_TYPE_ASSERT (list, args);

    return idio_hash_reference (ht, key, args);
}

IDIO idio_hash_set (IDIO ht, IDIO key, IDIO v)
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (key);
    IDIO_ASSERT (v);
    IDIO_TYPE_ASSERT (hash, ht);

    IDIO_ASSERT_NOT_CONST (hash, ht);

    idio_hash_put (ht, key, v);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE3_DS ("hash-set!", hash_set, (IDIO ht, IDIO key, IDIO v), "ht key v", "\
set the index of ``key` in hash table ``ht`` to ``v``	\n\
							\n\
:param ht: hash table					\n\
:type ht: hash table					\n\
:param key: non-#n value				\n\
:type key: any non-#n					\n\
:param v: value						\n\
:type v: a value					\n\
							\n\
:return: #unspec					\n\
")
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (key);
    IDIO_ASSERT (v);

    IDIO_USER_TYPE_ASSERT (hash, ht);

    return idio_hash_set (ht, key, v);
}

/*
 * SRFI 69 -- not an error to delete a non-existent key
 */
IDIO_DEFINE_PRIMITIVE2_DS ("hash-delete!", hash_delete, (IDIO ht, IDIO key), "ht key", "\
delete the value associated with index of ``key` in	\n\
hash table ``ht``					\n\
							\n\
:param ht: hash table					\n\
:type ht: hash table					\n\
:param key: non-#n value				\n\
:type key: any non-#n					\n\
							\n\
:return: #unspec					\n\
")
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (key);

    IDIO_USER_TYPE_ASSERT (hash, ht);

    idio_hash_delete (ht, key);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE2_DS ("hash-exists?", hash_existsp, (IDIO ht, IDIO key), "ht key", "\
assert whether index of ``key` in hash table ``ht`` has	\n\
a value							\n\
							\n\
:param ht: hash table					\n\
:type ht: hash table					\n\
:param key: non-#n value				\n\
:type key: any non-#n					\n\
							\n\
:return: #t or #f					\n\
")
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (key);

    IDIO_USER_TYPE_ASSERT (hash, ht);

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
IDIO_DEFINE_PRIMITIVE3V_DS ("hash-update!", hash_update, (IDIO ht, IDIO key, IDIO func, IDIO args), "ht key func [default]", "\
update the value indexed by ``key` in hash table ``ht``		\n\
								\n\
SRFI-69:							\n\
								\n\
Semantically equivalent to, but may be implemented more		\n\
efficiently than, the following code:				\n\
								\n\
   hash-set! ht key (func (hash-ref ht key [default])))		\n\
								\n\
That is, call ``func`` on the existing value and set the	\n\
key to the returned value					\n\
								\n\
:param ht: hash table						\n\
:type ht: hash table						\n\
:param key: non-#n value					\n\
:type key: any non-#n						\n\
:param func: func to generate replacement value			\n\
:type func: 1-ary function					\n\
:param default: see ``hash-ref``				\n\
:type default: see ``hash-ref``					\n\
								\n\
:return: #unspec						\n\
")
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (key);
    IDIO_ASSERT (func);
    IDIO_ASSERT (args);

    IDIO_USER_TYPE_ASSERT (hash, ht);
    IDIO_USER_TYPE_ASSERT (list, args);

    IDIO_ASSERT_NOT_CONST (hash, ht);

    IDIO cv = idio_hash_reference (ht, key, args);

    IDIO nv = idio_vm_invoke_C (idio_thread_current_thread (), IDIO_LIST2 (func, cv));

    idio_hash_put (ht, key, nv);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE1_DS ("hash-keys", hash_keys, (IDIO ht), "ht", "\
return a list of the keys of the hash table ``ht``	\n\
							\n\
no order can be presumed				\n\
							\n\
:param ht: hash table					\n\
:type ht: hash table					\n\
							\n\
:return: #unspec					\n\
")
{
    IDIO_ASSERT (ht);

    IDIO_USER_TYPE_ASSERT (hash, ht);

    return idio_hash_keys_to_list (ht);
}

IDIO_DEFINE_PRIMITIVE1_DS ("hash-values", hash_values, (IDIO ht), "ht", "\
return a list of the values of the hash table ``ht``	\n\
							\n\
no order can be presumed				\n\
							\n\
:param ht: hash table					\n\
:type ht: hash table					\n\
							\n\
:return: #unspec					\n\
")
{
    IDIO_ASSERT (ht);

    IDIO_USER_TYPE_ASSERT (hash, ht);

    return idio_hash_values_to_list (ht);
}

IDIO_DEFINE_PRIMITIVE2_DS ("hash-walk", hash_walk, (IDIO ht, IDIO func), "ht func", "\
call ``func`` for each ``key` in hash table ``ht``		\n\
								\n\
:param ht: hash table						\n\
:type ht: hash table						\n\
:param func: func to be called with each key, value pair	\n\
:type func: 2-ary function					\n\
								\n\
:return: #unspec						\n\
")
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (func);

    IDIO_USER_TYPE_ASSERT (hash, ht);

    /*
     * Careful of the old chestnut, the invocation of the function
     * could perturb the hash so pull the keys from the hash then in a
     * separate loop, invoke the function per key.
     *
     * As we're re-enetering the VM, protect the (C-land) list of keys
     * from the GC.
     */
    IDIO keys = idio_hash_keys_to_list (ht);
    IDIO safe_keys = idio_pair (keys, idio_S_nil);
    idio_gc_protect (safe_keys);

    while (idio_S_nil != keys) {
	IDIO k = IDIO_PAIR_H (keys);
	IDIO v = idio_hash_ref (ht, k);
	idio_vm_invoke_C (idio_thread_current_thread (), IDIO_LIST3 (func, k, v));

	keys = IDIO_PAIR_T (keys);
    }

    idio_gc_expose (safe_keys);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE3_DS ("hash-fold", hash_fold, (IDIO ht, IDIO func, IDIO val), "ht func val", "\
call ``func`` for each ``key` in hash table ``ht`` with arguments:	\n\
``key``, the value indexed by ``key`` and ``val``		\n\
								\n\
``val`` is updated to the value returned by ``func``		\n\
								\n\
The final value of ``val`` is returned				\n\
								\n\
:param ht: hash table						\n\
:type ht: hash table						\n\
:param func: func to be called with each key, value, val tuple	\n\
:type func: 3-ary function					\n\
:param val: initial value for ``val``				\n\
:type func: value						\n\
								\n\
:return: final value of ``val``					\n\
")
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (func);
    IDIO_ASSERT (val);

    IDIO_USER_TYPE_ASSERT (hash, ht);

    /*
     * Careful of the old chestnut, the invocation of the function
     * could perturb the hash so pull the keys from the hash then in a
     * separate loop, invoke the function per key.
     *
     * As we're re-enetering the VM, protect the (C-land) list of keys
     * from the GC.
     */
    IDIO keys = idio_hash_keys_to_list (ht);
    IDIO safe_keys = idio_pair (keys, idio_S_nil);
    idio_gc_protect (safe_keys);

    while (idio_S_nil != keys) {
	IDIO k = IDIO_PAIR_H (keys);
	IDIO v = idio_hash_ref (ht, k);
	val = idio_vm_invoke_C (idio_thread_current_thread (), IDIO_LIST4 (func, k, v, val));

	keys = IDIO_PAIR_T (keys);
    }

    idio_gc_expose (safe_keys);

    return val;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("copy-hash", copy_hash, (IDIO ht, IDIO args), "orig [depth]", "\
copy hash table `orig`					\n\
							\n\
:param orig: initial hash table				\n\
:type orig: hash table					\n\
:param depth: (optional) 'shallow or 'deep (default)	\n\
:return: the new hash table				\n\
:rtype: hash table					\n\
")
{
    IDIO_ASSERT (ht);

    IDIO_USER_TYPE_ASSERT (hash, ht);

    int depth = IDIO_COPY_DEEP;

    if (idio_S_nil != args) {
	IDIO idepth = IDIO_PAIR_H (args);

	if (idio_isa_symbol (idepth)) {
	    if (idio_S_deep == idepth) {
		depth = IDIO_COPY_DEEP;
	    } else if (idio_S_shallow == idepth) {
		depth = IDIO_COPY_SHALLOW;
	    } else {
		idio_error_param_type ("'deep or 'shallow", idepth, IDIO_C_FUNC_LOCATION ());

		return idio_S_notreached;
	    }
	} else {
	    idio_error_param_type ("symbol", idepth, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    return idio_copy_hash (ht, depth);
}

IDIO_DEFINE_PRIMITIVE2_DS ("merge-hash!", merge_hash, (IDIO ht1, IDIO ht2), "ht1 ht2", "\
merge the key/value pairs in hash table ``ht2`` into hash	\n\
table ``ht1``							\n\
								\n\
duplicate keys in ``ht2`` will overwrite keys in `ht1``		\n\
								\n\
:param ht1: hash table						\n\
:type ht1: hash table						\n\
:param ht2: hash table						\n\
:type ht2: hash table						\n\
								\n\
:return: ``ht1``						\n\
")
{
    IDIO_ASSERT (ht1);
    IDIO_ASSERT (ht2);

    IDIO_USER_TYPE_ASSERT (hash, ht1);
    IDIO_USER_TYPE_ASSERT (hash, ht2);

    return idio_merge_hash (ht1, ht2);
}

#ifdef IDIO_DEBUG
/*
 * Referencing
 * https://attractivechaos.wordpress.com/2019/12/28/deletion-from-hash-tables-without-tombstones/
 * we can see other algorithms tending towards 0.06s per million
 * inputs and that the (randomized input) left some 6.2m entries in
 * the 50m test and 10-40 bytes per key.
 *
 * There is a subtlety here in that you really want to be using the
 * same random number generator to get comparable results.  For
 * example, using random() will get you just about 100% post-run
 * occupancy -- as you would hope for a decent random number
 * generator.
 *
 * The bits that look clever are from common.c in
 * https://github.com/attractivechaos/udb2.
 *
 * I'm seeing 0.9s per million and 12.0% of entries left across all
 * variants (suspicious!) and 50-100 bytes / key.
 *
 * We do have to rework the key into a fixnum.
 */
static inline uint32_t hash32(uint32_t key)
{
    key += ~(key << 15);
    key ^=  (key >> 10);
    key +=  (key << 3);
    key ^=  (key >> 6);
    key += ~(key << 11);
    key ^=  (key >> 16);
    return key;
}

/*****************************************************
 * This is the hash function used by almost everyone *
 *****************************************************/
static inline uint32_t hash_fn(uint32_t key)
{
	return key;
}

static double cputime(void)
{
	struct rusage r;
	getrusage(RUSAGE_SELF, &r);
	return r.ru_utime.tv_sec + r.ru_stime.tv_sec + 1e-6 * (r.ru_utime.tv_usec + r.ru_stime.tv_usec);
}

static long peakrss(void)
{
	struct rusage r;
	getrusage(RUSAGE_SELF, &r);
#ifdef __linux__
	return r.ru_maxrss * 1024;
#else
	return r.ru_maxrss;
#endif
}

static inline uint32_t get_key(const uint32_t n, const uint32_t x)
{
	return hash32(x % (n>>2));
}

uint64_t traverse_rng(uint32_t n, uint32_t x0)
{
	uint64_t sum = 0;
	uint32_t i, x;
	for (i = 0, x = x0; i < n; ++i) {
		x = hash32(x);
		sum += get_key(n, x);
	}
	return sum;
}

size_t idio_unit_test_hash_n (size_t n, size_t x0)
{
    idio_gc_collect_gen ("%%unit-test-hash");

    struct timeval t0;
    if (gettimeofday (&t0, NULL) == -1) {
	perror ("gettimeofday");
    }

    IDIO ht = IDIO_HASH_EQP (1024);

    size_t x = x0;
    size_t i;
    for (i = 0 ; i < n; i++) {
	x = hash32 (x);
	long k = get_key(n, x);
	IDIO key = idio_fixnum (k % (INTPTR_MAX >> 2));
	if (idio_hash_exists_key (ht, key)) {
	    idio_hash_delete (ht, key);
	} else {
	    idio_hash_put (ht, key, idio_S_nil);
	}
    }

    struct timeval te;
    if (gettimeofday (&te, NULL) == -1) {
	perror ("gettimeofday");
    }

    struct timeval td;
    td.tv_sec = te.tv_sec - t0.tv_sec;
    td.tv_usec = te.tv_usec - t0.tv_usec;
    if (td.tv_usec < 0) {
	td.tv_usec += 1000000;
	td.tv_sec -= 1;
    }

    double pct = IDIO_HASH_COUNT (ht) * 100 / n;
    fprintf (stderr, "%%unit-test-hash %8zu: %5ld.%06ld %8zu/%-8zu %5.1f\n", n, td.tv_sec, td.tv_usec, IDIO_HASH_COUNT (ht), IDIO_HASH_SIZE (ht), pct);

    return IDIO_HASH_COUNT (ht);
}

void idio_unit_test_hash (void)
{
    double t, t0;
    uint32_t i, m = 5, max = 50000000, n = 10000000, x0 = 1, step;
    uint64_t sum;
    long m0;

    t = cputime();
    sum = traverse_rng(n, x0);
    t0 = cputime() - t;
    fprintf(stderr, "CPU time spent on RNG: %.3f sec; total sum: %lu\n", t0, (unsigned long)sum);
    m0 = peakrss();

    step = (max - n) / m;
    for (i = 0; i <= m; ++i, n += step) {
	double t, mem;
	uint32_t size;
	t = cputime();
	size = idio_unit_test_hash_n(n, x0);
	t = cputime() - t;
	mem = (peakrss() - m0) / 1024.0 / 1024.0;
	printf("%d %8d t=%6.3f m=%7.3f t/m %6.4f m/m %7.4f\n", i, n, t, mem, t * 1e6 / n, mem * 1e6 / size);
    }

    return;
}

IDIO_DEFINE_PRIMITIVE0_DS ("%%unit-test-hash", unit_test_hash, (void), "", "\
Perform some hash unit tests				\n\
")
{
    idio_unit_test_hash ();

    return idio_S_unspec;
}
#endif

void idio_init_hash ()
{
    idio_gc_set_verboseness (3);
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
    IDIO_ADD_PRIMITIVE (copy_hash);
    IDIO_ADD_PRIMITIVE (merge_hash);
#ifdef IDIO_DEBUG
    IDIO_ADD_PRIMITIVE (unit_test_hash);
#endif
}

void idio_final_hash ()
{
    idio_gc_expose (idio_hash_weak_tables);
}

