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
 * (idio_hash_default_hash_C ()) we need to walk the chain of
 * same-hash-value keys until we find the one ``equal`` to us or walk
 * off the end of the chain (idio_hash_hv_follow_chain ()).
 *
 * To add fun to the mix there are several symbol tables (symbols,
 * tags, C_typedefs etc.) where we start with a C string.  For these
 * we need a _C variant as uniqueness is determined by strncmp ().
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
    IDIO_HASH_START (h) = size - 1;

    idio_hi_t i;
    for (i = 0; i < size; i++) {
	IDIO_HASH_HE_KEY (h, i) = idio_S_nil;
	IDIO_HASH_HE_VALUE (h, i) = idio_S_nil;
	IDIO_HASH_HE_NEXT (h, i) = size + 1;
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
    /* IDIO_C_ASSERT (comp_C); */
    /* IDIO_C_ASSERT (hash_C); */
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
static IDIO idio_hash_he_key (IDIO h, idio_hi_t hv)
{
    IDIO ck = IDIO_HASH_HE_KEY (h, hv);

    if (idio_S_nil != ck) {
	if (IDIO_HASH_FLAGS (h) & IDIO_HASH_FLAG_STRING_KEYS) {
	} else if (IDIO_HASH_FLAGS (h) & IDIO_HASH_FLAG_WEAK_KEYS) {
	    switch ((intptr_t) ck & IDIO_TYPE_MASK) {
	    case IDIO_TYPE_FIXNUM_MARK:
	    case IDIO_TYPE_CONSTANT_MARK:
	    case IDIO_TYPE_PLACEHOLDER_MARK:
		break;
	    case IDIO_TYPE_POINTER_MARK:
		if (0 == ck->type) {
		    /*
		     * null out the key (and value) as a form of lazy
		     * deletion (without any actual deletion as we
		     * leave the next index in situ)
		     */
		    IDIO_HASH_HE_KEY (h, hv) = idio_S_nil;
		    IDIO_HASH_HE_VALUE (h, hv) = idio_S_nil;
		    /* IDIO_HASH_HE_NEXT (h, hv) = IDIO_HASH_SIZE (h) + 1; */
		    IDIO_HASH_COUNT (h) -= 1;
		    ck = idio_S_nil;
		} else {
		    IDIO_ASSERT (ck);
		}
		break;
	    default:
		/* inconceivable! */
		idio_error_printf (IDIO_C_FUNC_LOCATION (), "type: unexpected object type %#x", ck);

		/* notreached */
		return idio_S_notreached;
	    }

	} else {
	    IDIO_ASSERT (ck);
	}
    }

    return ck;
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

    idio_hi_t i;
    for (i = 0; i < IDIO_HASH_SIZE (orig); i++) {
	IDIO k = idio_hash_he_key (orig, i);
	if (! k) {
	    char em[BUFSIZ];
	    sprintf (em, "copy-hash: key #%zd is NULL", i);
	    idio_error_C (em, orig, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	if (idio_S_nil != k) {
	    IDIO v = IDIO_HASH_HE_VALUE (orig, i);
	    if (IDIO_COPY_DEEP == depth) {
		v = idio_copy (v, depth);
	    }
	    idio_hash_put (new, k, v);
	}
    }

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
	IDIO k = idio_hash_he_key (ht2, i);
	if (! k) {
	    char em[BUFSIZ];
	    sprintf (em, "merge-hash: key #%zd is NULL", i);
	    idio_error_C (em, ht2, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
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
	    void *kv = idio_hash_he_key (h, i);
	    if (idio_S_nil != kv) {
		free (kv);
	    }
	}
    }

    free (h->u.hash->he);
    free (h->u.hash);
}

void idio_hash_resize (IDIO h, int larger)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (hash, h);

    IDIO_ASSERT_NOT_CONST (hash, h);

    idio_hi_t ohsize = h->u.hash->size;
    idio_hash_entry_t *ohe = h->u.hash->he;

    /*
     * if we said osize = ohash->size it would be including the
     * existing +16%, as we are growing this then we only want to
     * double not 2*116%
     */
    idio_hi_t osize = IDIO_HASH_MASK (h) + 1;

    idio_hi_t hcount = IDIO_HASH_COUNT (h);

    idio_hi_t nsize = osize;

    if (larger) {
	idio_hi_t load_high = (osize / 2) + (osize / 4);
	if (hcount > load_high) {
	    while (nsize <= hcount) {
		nsize *= 2;
	    }
	    nsize *= 2;
	}
    } else {
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

    /* fprintf (stderr, "idio_hash_resize: %10p %s = %zd/%zd/%zd -> %zd\n", h, larger ? "L" : "S", hcount, osize, IDIO_HASH_SIZE (h), nsize); */
    IDIO_FPRINTF (stderr, "idio_hash_resize: %10p = %zd/%zd/%zd -> %zd\n", h, hcount, osize, IDIO_HASH_SIZE (h), nsize);
    idio_assign_hash_he (h, nsize);

    /*
     * The re-insertion by each idio_hash_put is going to increment
     * IDIO_HASH_COUNT(h) -- we don't want to double count.
     */
    IDIO_HASH_COUNT (h) = 0;
    idio_hi_t i;
    idio_hi_t c = 0;
    for (i = 0 ; i < ohsize; i++) {
	if (idio_S_nil != ohe[i].k) {
	    c++;
	    idio_hash_put (h, ohe[i].k, ohe[i].v);
	}
    }

    idio_gc_stats_free (osize * sizeof (idio_hash_entry_t));
    idio_gc_stats_free (osize * sizeof (idio_hash_entry_t));

    free (ohe);

    idio_hash_verify_all_keys (h);
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

    char *Cs = idio_string_as_C (s);
    idio_hi_t r = idio_hash_default_hash_C_string_C (strlen (Cs), Cs);
    free (Cs);
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

    return idio_hash_default_hash_C_void (IDIO_STRUCT_TYPE_FIELDS (h));
}

idio_hi_t idio_idio_hash_default_hash_C_struct_instance (IDIO h)
{
    IDIO_ASSERT (h);

    return idio_hash_default_hash_C_void (IDIO_STRUCT_INSTANCE_FIELDS (h));
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
 * This is, the result will be modulo IDIO_HASH_MASK (ht).
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

    idio_hi_t hv = IDIO_HASH_SIZE (h) + 1;
    idio_type_e type = idio_type (k);

    switch (type) {
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
	    char *sk = idio_string_as_C (k);
	    hv = idio_hash_default_hash_C_string_C (strlen (sk), sk);
	    free (sk);
	}
	break;
    case IDIO_TYPE_SUBSTRING:
	{
	    char *sk = idio_string_as_C (k);
	    hv = idio_hash_default_hash_C_string_C (strlen (sk), sk);
	    free (sk);
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

	idio_hi_t hvi = IDIO_HASH_SIZE (ht) + 1;

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

void idio_hash_verify_chain (IDIO h, void *kv, int reqd)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (hash, h);

    if (idio_gc_verboseness (2)) {
	/* fprintf (stderr, "idio_hash_verify_chain: h=%10p size=%zu\n", h, IDIO_HASH_SIZE (h)); */
	idio_hi_t ohvi = idio_hash_value_index (h, kv);
	idio_hi_t nhvi = ohvi;
	/*
	  fprintf (stderr, "idio_hash_verify_chain: kv=%10p ohvi=%zu k=", kv, ohvi);
	  if (IDIO_HASH_FLAGS (h) & IDIO_HASH_FLAG_STRING_KEYS) {
	  fprintf (stderr, "%s\n", (char *) kv);
	  } else {
	  idio_debug ("%s\n", (IDIO) kv);
	  }
	*/

	size_t i = 0;
	int seen = 0;
	while (nhvi < IDIO_HASH_SIZE (h)) {
	    void *nkv = idio_hash_he_key (h, nhvi);
	    if (idio_S_nil != nkv) {
		idio_hi_t hvi = idio_hash_value_index (h, nkv);
		if (hvi != ohvi) {
		    /* fprintf (stderr, "idio_hash_verify_chain: kv=%10p nkv=%10p nhvi=%zu hvi %zu != ohvi %zu\n", kv, nkv, nhvi, hvi, ohvi); */
		    fprintf (stderr, "risky recurse for %10p!\n", nkv);
		    idio_hash_verify_chain (h, nkv, 1);
		    fprintf (stderr, "risky recurse for %10p done!\n", nkv);
		    idio_error_printf (IDIO_C_FUNC_LOCATION (), "in-chain hvi mismatch");

		    /* notreached */
		    abort ();
		}
	    }
	    if (reqd) {
		if (IDIO_HASH_FLAGS (h) & IDIO_HASH_FLAG_STRING_KEYS) {
		    if (0 == strcmp (nkv, kv)) {
			seen = 1;
		    }
		} else {
		    if (idio_hash_equal (h, kv, nkv)) {
			seen = 1;
		    }
		}
	    }
	    /* fprintf (stderr, "idio_hash_verify_chain: kv=%10p nkv=%10p nhvi=%zu -> nnhvi=%zu\n", kv, nkv, nhvi, IDIO_HASH_HE_NEXT (h, nhvi));  */
	    nhvi = IDIO_HASH_HE_NEXT (h, nhvi);
	    i++;
	}
	/* fprintf (stderr, "idio_hash_verify_chain: kv=%10p %sseen in %zu\n", kv, seen ? "" : "NOT ", i); */
	if (reqd &&
	    ! seen) {
	    fprintf (stderr, "ih_vc: ERROR %p kv=%p ohvi=%td ", h, kv, ohvi);
	    if (IDIO_HASH_FLAGS (h) & IDIO_HASH_FLAG_STRING_KEYS) {
		fprintf (stderr, "k=%s\n", (char *) kv);
	    } else {
		idio_debug ("k=%s\n", (IDIO) kv);
	    }
	    idio_dump (h, 16);
	    idio_error_printf (IDIO_C_FUNC_LOCATION (), "kv=%p not in chain!", kv);

	    /* notreached */
	    abort ();
	}
    }
}

void idio_hash_verify_all_keys (IDIO h)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (hash, h);

    IDIO_FPRINTF (stderr, "idio_hash_verify_all_keys:\n");
    /* idio_dump (h, 1); */
    idio_hi_t hv = 0;
    for (hv = 0; hv < IDIO_HASH_SIZE (h); hv++) {
	void *kv = idio_hash_he_key (h, hv);
	if (idio_S_nil != kv) {
	    IDIO_FPRINTF (stderr, "idio_hash_verify_all_keys: hv=%zd %p ", hv, kv);
	    if (IDIO_HASH_FLAGS (h) & IDIO_HASH_FLAG_STRING_KEYS) {
		IDIO_FPRINTF (stderr, "\"%s\"", (char *) kv);
	    }
	    IDIO_FPRINTF (stderr, "\n");

	    idio_hash_verify_chain (h, kv, 1);
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
     * To avoid determining if a safer signed type, say, intptr_t has
     * sizeof(intptr_t) == sizeof(idio_hi_t/size_t) on any given
     * machine we can set the loop check to be "var > 0" and repeat
     * the test for the case "var == 0" after the loop(*).
     *
     * Think of this as a special case...
     *
     * Of course, if we did reach i == 0 then our load factor is
     * effectively 1.00.  Which isn't good for other reasons.
     *
     * (*) We also need to handle the similar corner conditions for
     * when IDIO_HASH_START(h) becomes 0.
     */

    idio_hi_t s;
    for (s = IDIO_HASH_START (h); s > 0 ; s--) {
	if (idio_S_nil == idio_hash_he_key (h, s)) {
	    /*
	     * We've found a free slot @s so we need to patch up
	     * IDIO_HASH_START(h) for next time.  Nominally that will be @s-1
	     */
	    if (1 == s) {
		/*
		 * IDIO_HASH_START(h) has been decrementing
		 * irrespective of any GC.  So restart again --
		 * potentially a complete timewaster next time round
		 * although you'd like to think that we would have
		 * resized somewhere along the line.
		 *
		 * Note that this loop causes a problem if s==1
		 * because we'd make IDIO_HASH_START(h)==0 here which
		 * would prevent us entering this loop at all (s > 0)
		 * next time.
		 */
		IDIO_HASH_START (h) = IDIO_HASH_SIZE (h) - 1;
	    } else {
		IDIO_HASH_START (h) = s - 1;
	    }

	    return s;
	}
    }

    /* s == 0 */
    if (idio_S_nil == idio_hash_he_key (h, s)) {
	/*
	 * Similar to s==1 before, here @s-1 will be a large number
	 * (as idio_hi_t is unsigned).
	 */
	IDIO_HASH_START (h) = IDIO_HASH_SIZE (h) - 1;
	return s;
    }

    /*
     * No free slots?  Actually we could get here if, say,
     * IDIO_HASH_START(h) was small and the bottom slots are all
     * filled with non-#n values.  We'll return size+1 to our caller
     * who will (probably) try to resize() and put() again.
     *
     * If the hash was mostly empty, resize will have done nothing but
     * we have just set IDIO_HASH_START(h) back to the top (of the
     * cellar).  So we should find something free.
     */
    IDIO_HASH_START (h) = IDIO_HASH_SIZE (h) - 1;
    return (IDIO_HASH_SIZE (h) + 1);
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

    idio_hi_t hi = idio_hash_hv_follow_chain (h, kv);

    if (hi > IDIO_HASH_SIZE (h)) {
	/* XXX this was just done in idio_hash_hv_follow_chain !!! */
	hi = idio_hash_value_index (h, kv);
    }

    idio_gc_set_verboseness (3);
    IDIO_FPRINTF (stderr, "idio_hash_put: kv=%10p hi=%" PRIuPTR " nhi=%" PRIuPTR "\n", kv, hi, IDIO_HASH_HE_NEXT (h, hi));

    /* current object @hi */
    IDIO ck = idio_hash_he_key (h, hi);

    if (idio_S_nil == ck) {
	IDIO_HASH_HE_KEY (h, hi) = kv;
	IDIO_HASH_HE_VALUE (h, hi) = v;
	IDIO_HASH_HE_NEXT (h, hi) = IDIO_HASH_SIZE (h) + 1;
	idio_hash_verify_chain (h, kv, 1);
	IDIO_HASH_COUNT (h) += 1;
	return kv;
    }

    if (idio_hash_equal (h, ck, kv)) {
	IDIO_HASH_HE_VALUE (h, hi) = v;
	idio_hash_verify_chain (h, kv, 1);
	return kv;
    }

    /*
     * if we said osize = ohash->size it would be including the
     * existing +16%, as we are growing this then we only want to
     * double not 2*116%
     */
    idio_hi_t hsize = IDIO_HASH_MASK (h) + 1;

    idio_hi_t load_high = (hsize / 2) + (hsize / 4);
    if (IDIO_HASH_COUNT (h) > load_high) {
	idio_hash_resize (h, 1);
	idio_hash_put (h, kv, v);
	return kv;
    }

    idio_hi_t fhi = idio_hash_find_free_slot (h);
    if (fhi > IDIO_HASH_SIZE (h)) {
	IDIO_FPRINTF (stderr, "idio_hash_put: no free slot -> resize\n");
	idio_hash_resize (h, 1);
	idio_hash_put (h, kv, v);
	return kv;
    }
    IDIO_FPRINTF (stderr, "idio_hash_put: fhi %" PRIuPTR "\n", fhi);

    idio_hash_verify_chain (h, ck, 1);
    idio_hi_t ckhi = idio_hash_value_index (h, ck);

    /* either me or him is going to go to the end of the chain */
    if (ckhi != hi) {
	/* he's in the wrong place */
	IDIO_FPRINTF (stderr, "idio_hash_put: kv=%10p insert hv(k@%" PRIuPTR " ck=%10p)=%" PRIuPTR "; mv -> %" PRIuPTR "\n", kv, hi, ck, ckhi, fhi);

	/* who points to him? */
	idio_hi_t phi = ckhi;
	idio_hi_t nhi = IDIO_HASH_HE_NEXT (h, phi);
	IDIO_FPRINTF (stderr, "idio_hash_put: kv=%10p start phi=%" PRIuPTR " -> nhi=%" PRIuPTR "\n", kv, phi, nhi);
	while (nhi != hi) {
	    phi = nhi;
	    nhi = IDIO_HASH_HE_NEXT (h, phi);
	    IDIO_FPRINTF (stderr, "idio_hash_put: kv=%10p step phi=%" PRIuPTR " -> nhi=%" PRIuPTR "\n", kv, phi, nhi);

	    if (nhi > IDIO_HASH_SIZE (h)) {
		fprintf (stderr, "idio_hash_put: kv=%10p  ck=%10p\n", kv, ck);
		fprintf (stderr, "idio_hash_put: hi=%" PRIuPTR " ckhi=%" PRIuPTR " phi=%" PRIuPTR "\n", hi, ckhi, phi);
		fprintf (stderr, "idio_hash_put: nhi=%" PRIuPTR " > size %" PRIuPTR "\n", nhi, IDIO_HASH_SIZE (h));
		idio_gc_set_verboseness (3);
		idio_hash_verify_chain (h, kv, 1);
		idio_hash_verify_chain (h, ck, 1);
		idio_error_printf (IDIO_C_FUNC_LOCATION (), "oh dear");

		return idio_S_notreached;
	    }
	}

	IDIO_FPRINTF (stderr, "idio_hash_put: kv=%10p insert phi=%" PRIuPTR " -> fhi=%" PRIuPTR "\n", kv, phi, fhi);

	/* point them at fhi */
	IDIO_HASH_HE_NEXT (h, phi) = fhi;

	IDIO_FPRINTF (stderr, "idio_hash_put: kv=%10p insert fhi=%" PRIuPTR " = HE (hi=%" PRIuPTR ")\n", kv, fhi, hi);

	/* shift ck */
	IDIO_HASH_HE_KEY (h, fhi) = idio_hash_he_key (h, hi);
	IDIO_HASH_HE_VALUE (h, fhi) = IDIO_HASH_HE_VALUE (h, hi);
	IDIO_HASH_HE_NEXT (h, fhi) = IDIO_HASH_HE_NEXT (h, hi);

	IDIO_FPRINTF (stderr, "idio_hash_put: kv=%10p insert hi=%" PRIuPTR " = kv + -> nhv=%" PRIuPTR "\n", kv, hi, IDIO_HASH_SIZE (h) + 1);

	/* insert k */
	IDIO_HASH_HE_KEY (h, hi) = kv;
	IDIO_HASH_HE_VALUE (h, hi) = v;
	IDIO_HASH_HE_NEXT (h, hi) = IDIO_HASH_SIZE (h) + 1;

	idio_hash_verify_chain (h, ck, 1);
	idio_hash_verify_chain (h, kv, 1);
    } else {
	/* I go to the end of the chain */

	IDIO_FPRINTF (stderr, "idio_hash_put: kv=%10p append\n", kv);

	/* find the end of the chain */
	idio_hi_t phi = hi;
	idio_hi_t nhi = IDIO_HASH_HE_NEXT (h, phi);
	IDIO_FPRINTF (stderr, "idio_hash_put: kv=%10p start phi=%" PRIuPTR " -> nhi=%" PRIuPTR "\n", kv, phi, nhi);
	while (nhi < IDIO_HASH_SIZE (h)) {
	    phi = nhi;
	    nhi = IDIO_HASH_HE_NEXT (h, phi);
	    IDIO_FPRINTF (stderr, "idio_hash_put: kv=%10p step phi=%" PRIuPTR " -> nhi=%" PRIuPTR "\n", kv, phi, nhi);
	}

	IDIO_FPRINTF (stderr, "idio_hash_put: kv=%10p append phi=%" PRIuPTR " -> fhi=%" PRIuPTR "\n", kv, phi, fhi);

	/* point them at fhi */
	IDIO_HASH_HE_NEXT (h, phi) = fhi;

	IDIO_FPRINTF (stderr, "idio_hash_put: kv=%10p append fhi=%" PRIuPTR " = kv=%10p -> nhv=%" PRIuPTR "\n", kv, fhi, kv, IDIO_HASH_SIZE (h) + 1);

	IDIO_HASH_HE_KEY (h, fhi) = kv;
	IDIO_HASH_HE_VALUE (h, fhi) = v;
	IDIO_HASH_HE_NEXT (h, fhi) = IDIO_HASH_SIZE (h) + 1;

	idio_hash_verify_chain (h, kv, 1);
    }

    IDIO_HASH_COUNT (h) += 1;
    return kv;
}

idio_hi_t idio_hash_hv_follow_chain (IDIO h, void *kv)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (hash, h);

    if (idio_S_nil == kv) {
	idio_error_param_nil ("key", IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return IDIO_HASH_SIZE (h) + 1;
    }

    idio_hi_t hi = idio_hash_value_index (h, kv);

    if (hi > IDIO_HASH_SIZE (h)) {
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "hi %" PRIuPTR " > size %" PRIuPTR " kv=%10p", hi, IDIO_HASH_SIZE (h), kv);

	/* notreached */
	return IDIO_HASH_SIZE (h) + 1;
    }

    IDIO_FPRINTF (stderr, "idio_hash_hv_follow_chain: kv=%10p hi=%" PRIuPTR "\n", kv, hi);
    if (IDIO_HASH_FLAGS (h) & IDIO_HASH_FLAG_STRING_KEYS) {
	IDIO_FPRINTF (stderr, "idio_hash_hv_follow_chain: SK kv=%s\n", (char *) kv);
    }

    idio_hi_t chi = hi;
    IDIO ck = idio_hash_he_key (h, chi);

    IDIO_FPRINTF (stderr, "idio_hash_hv_follow_chain: kv=%10p chi=%" PRIuPTR " ck=%10p nhi=%" PRIuPTR "\n", kv, chi, ck, IDIO_HASH_HE_NEXT (h, chi));

    while (! idio_hash_equal (h, ck, kv) &&
	   IDIO_HASH_HE_NEXT (h, chi) < IDIO_HASH_SIZE (h)) {
	chi = IDIO_HASH_HE_NEXT (h, chi);
	ck = idio_hash_he_key (h, chi);
	IDIO_FPRINTF (stderr, "idio_hash_hv_follow_chain: kv=%10p chi=%" PRIuPTR " ck=%10p nhi=%" PRIuPTR "\n", kv, chi, ck, IDIO_HASH_HE_NEXT (h, chi));
    }

    if (! idio_hash_equal (h, ck, kv)) {
	IDIO_FPRINTF (stderr, "idio_hash_hv_follow_chain: kv=%10p not found\n", kv);
	return (IDIO_HASH_SIZE (h) + 1);
    }

    IDIO_FPRINTF (stderr, "idio_hash_hv_follow_chain: kv=%10p found @ chi=%" PRIuPTR "\n", kv, chi);

    return chi;
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
	return IDIO_HASH_SIZE (h) + 1;
    }

    idio_hi_t hi = idio_hash_hv_follow_chain (h, kv);

    if (hi > IDIO_HASH_SIZE (h)) {
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

    idio_hi_t hi = idio_hash_hv_follow_chain (h, kv);

    if (hi > IDIO_HASH_SIZE (h)) {
	return idio_S_nil;
    }

    return kv;

    /*
    IDIO_C_ASSERT (k == idio_hash_he_key (h, hi));

    return idio_hash_he_key (h, hi);
    */
}

IDIO idio_hash_get (IDIO h, void *kv)
{
    IDIO_ASSERT (h);

    if (idio_S_nil == kv) {
	idio_error_param_nil ("key", IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO_TYPE_ASSERT (hash, h);

    IDIO_FPRINTF (stderr, "idio_hash_get: %10p\n", kv);
    idio_hi_t hi = idio_hash_hv_follow_chain (h, kv);

    if (hi > IDIO_HASH_SIZE (h)) {
	return idio_S_unspec;
    }

    return IDIO_HASH_HE_VALUE (h, hi);
}

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

    idio_hi_t hi = idio_hash_value_index (h, kv);

    if (hi > IDIO_HASH_SIZE (h)) {
	idio_error_printf (IDIO_C_FUNC_LOCATION (), "hi %" PRIuPTR " > size %" PRIuPTR, hi, IDIO_HASH_SIZE (h));

	/* notreached */
	return 0;
    }

    IDIO_FPRINTF (stderr, "idio_hash_delete: %10p starting @%" PRIuPTR "\n", kv, hi);

    idio_hi_t phi = IDIO_HASH_SIZE (h) + 1;
    idio_hi_t chi = hi;
    IDIO ck = idio_hash_he_key (h, chi);
    while (! idio_hash_equal (h, ck, kv) &&
	   IDIO_HASH_HE_NEXT (h, chi) < IDIO_HASH_SIZE (h)) {
	phi = chi;
	chi = IDIO_HASH_HE_NEXT (h, chi);
	ck = idio_hash_he_key (h, chi);
	IDIO_FPRINTF (stderr, "idio_hash_delete: %10p stepped to %" PRIuPTR " -> @%" PRIuPTR " -> %" PRIuPTR "\n", ck, phi, chi, IDIO_HASH_HE_NEXT (h, chi));
    }

    if (idio_S_nil == ck) {
	IDIO_FPRINTF (stderr, "idio_hash_delete: %10p nil found @%" PRIuPTR "?\n", kv, chi);
	return 0;
    }

    if (! idio_hash_equal (h, ck, kv)) {
	IDIO_FPRINTF (stderr, "idio_hash_delete: %10p not found\n", kv);
	return 0;
    }

    idio_hi_t nhi = IDIO_HASH_HE_NEXT (h, chi);
    IDIO_FPRINTF (stderr, "idio_hash_delete: %10p deleting from phi=%" PRIuPTR " -> chi=%" PRIuPTR " -> nhi=%" PRIuPTR "\n", ck, phi, chi, nhi);

    if ((IDIO_HASH_SIZE (h) + 1) == phi) {
	/* head of chain */
	if (nhi < IDIO_HASH_SIZE (h)) {
	    IDIO_FPRINTF (stderr, "idio_hash_delete: %10p head of chain: chi=%" PRIuPTR " <- nhi=%" PRIuPTR "\n", ck, chi, nhi);
	    IDIO_HASH_HE_KEY (h, chi) = idio_hash_he_key (h, nhi);
	    IDIO_HASH_HE_VALUE (h, chi) = IDIO_HASH_HE_VALUE (h, nhi);
	    IDIO_HASH_HE_NEXT (h, chi) = IDIO_HASH_HE_NEXT (h, nhi);

	    IDIO_FPRINTF (stderr, "idio_hash_delete: %10p head of chain: nullify nhi=%" PRIuPTR "\n", ck, nhi);
	    IDIO_HASH_HE_KEY (h, nhi) = idio_S_nil;
	    IDIO_HASH_HE_VALUE (h, nhi) = idio_S_nil;
	    IDIO_HASH_HE_NEXT (h, nhi) = IDIO_HASH_SIZE (h) + 1;
	} else {
	    IDIO_FPRINTF (stderr, "idio_hash_delete: %10p head of chain: nullify chi=%" PRIuPTR "\n", ck, chi);
	    IDIO_HASH_HE_KEY (h, chi) = idio_S_nil;
	    IDIO_HASH_HE_VALUE (h, chi) = idio_S_nil;
	    IDIO_HASH_HE_NEXT (h, chi) = IDIO_HASH_SIZE (h) + 1;
	}
    } else {
	IDIO_FPRINTF (stderr, "idio_hash_delete: %10p link phi=%" PRIuPTR " -> nhi=%" PRIuPTR "\n", ck, phi, nhi);
	IDIO_HASH_HE_NEXT (h, phi) = nhi;

	IDIO_FPRINTF (stderr, "idio_hash_delete: %10p nullify chi=%" PRIuPTR "\n", ck, chi);
	IDIO_HASH_HE_KEY (h, chi) = idio_S_nil;
	IDIO_HASH_HE_VALUE (h, chi) = idio_S_nil;
	IDIO_HASH_HE_NEXT (h, chi) = IDIO_HASH_SIZE (h) + 1;
    }

    idio_hash_verify_chain (h, kv, 0);
    IDIO_HASH_COUNT (h) -= 1;

    /*
     * if we said osize = ohash->size it would be including the
     * existing +16%, as we are growing this then we only want to
     * double not 2*116%
     */
    idio_hi_t hsize = IDIO_HASH_MASK (h) + 1;

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
		int k_freed = 0;
		IDIO k = IDIO_HASH_HE_KEY (h, i);
		switch ((uintptr_t) k & IDIO_TYPE_MASK) {
		case IDIO_TYPE_FIXNUM_MARK:
		case IDIO_TYPE_CONSTANT_MARK:
		case IDIO_TYPE_PLACEHOLDER_MARK:
		    break;
		case IDIO_TYPE_POINTER_MARK:
		    if (IDIO_TYPE_NONE == k->type) {
			k_freed = 1;
			fprintf (stderr, "ih_twr %p @%zu\n", h, i);
			IDIO_HASH_HE_KEY (h, i) = idio_S_nil;
			IDIO_HASH_HE_VALUE (h, i) = idio_S_nil;
			/*
			 * XXX left IDIO_HASH_HE_NEXT() alone
			 *
			 * Hopefully, anyone will will shift this #n
			 * out of the chain next time round
			 */
		    }
		    break;
		default:
		    /* inconceivable! */
		    idio_error_printf (IDIO_C_FUNC_LOCATION (), "unexpected object mark type %#x", k);

		    /* notreached */
		    return;
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
	IDIO k = idio_hash_he_key (h, i);
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

    return r;
}

IDIO idio_hash_values_to_list (IDIO h)
{
    IDIO_ASSERT (h);
    IDIO_TYPE_ASSERT (hash, h);

    IDIO r = idio_S_nil;

    idio_hi_t i;
    for (i = 0; i < IDIO_HASH_SIZE (h); i++) {
	IDIO k = idio_hash_he_key (h, i);
	if (! k) {
	    char em[BUFSIZ];
	    sprintf (em, "hash-values-to-list: key #%zd is NULL", i);
	    idio_error_C (em, h, IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
	if (idio_S_nil != k) {
	    r = idio_pair (IDIO_HASH_HE_VALUE (h, i), r);
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
    IDIO_VERIFY_PARAM_TYPE (list, args);

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
    IDIO_VERIFY_PARAM_TYPE (list, alist);
    IDIO_VERIFY_PARAM_TYPE (list, args);

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
    IDIO_VERIFY_PARAM_TYPE (hash, ht);

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
    IDIO_VERIFY_PARAM_TYPE (hash, ht);

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
    IDIO_VERIFY_PARAM_TYPE (hash, ht);

    /*
     * Can we create a hash table with entries than IDIO_FIXNUM_MAX?
     * You imagine something else would have fallen over first...
     *
     * Better safe than sorry, though.  Call idio_integer() rather
     * than idio_fixnum().
     */
    return idio_integer (IDIO_HASH_COUNT (ht));
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
    IDIO_VERIFY_PARAM_TYPE (hash, ht);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    return idio_hash_ref (ht, key, args);
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

IDIO_DEFINE_PRIMITIVE3 ("hash-set!", hash_set, (IDIO ht, IDIO key, IDIO v))
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (key);
    IDIO_ASSERT (v);
    IDIO_VERIFY_PARAM_TYPE (hash, ht);

    return idio_hash_set (ht, key, v);
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
    IDIO_VERIFY_PARAM_TYPE (hash, ht);
    IDIO_VERIFY_PARAM_TYPE (list, args);

    IDIO_ASSERT_NOT_CONST (hash, ht);

    IDIO cv = idio_hash_ref (ht, key, args);

    IDIO nv = idio_vm_invoke_C (idio_thread_current_thread (), IDIO_LIST2 (func, cv));

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
    IDIO_VERIFY_PARAM_TYPE (hash, ht);

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
	IDIO v = idio_hash_get (ht, k);
	idio_vm_invoke_C (idio_thread_current_thread (), IDIO_LIST3 (func, k, v));

	keys = IDIO_PAIR_T (keys);
    }

    idio_gc_expose (safe_keys);

    return idio_S_unspec;
}

IDIO_DEFINE_PRIMITIVE3 ("hash-fold", hash_fold, (IDIO ht, IDIO func, IDIO val))
{
    IDIO_ASSERT (ht);
    IDIO_ASSERT (func);
    IDIO_ASSERT (val);
    IDIO_VERIFY_PARAM_TYPE (hash, ht);

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
	IDIO v = idio_hash_get (ht, k);
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
    IDIO_VERIFY_PARAM_TYPE (hash, ht);

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

IDIO_DEFINE_PRIMITIVE2 ("merge-hash!", merge_hash, (IDIO ht1, IDIO ht2))
{
    IDIO_ASSERT (ht1);
    IDIO_ASSERT (ht2);
    IDIO_VERIFY_PARAM_TYPE (hash, ht1);
    IDIO_VERIFY_PARAM_TYPE (hash, ht2);

    return idio_merge_hash (ht1, ht2);
}

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
}

void idio_final_hash ()
{
    idio_gc_expose (idio_hash_weak_tables);
}

