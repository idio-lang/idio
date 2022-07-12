/*
 * Copyright (c) 2022 Ian Fitchet <idf(at)idio-lang.org>
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
 * rfc6234.c
 *
 * Present an Idio interface to some of the RFC 6234 functionality
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "idio-system.h"
#include "idio-config.h"

#include "gc.h"
#include "idio.h"

#include "array.h"
#include "c-type.h"
#include "error.h"
#include "evaluate.h"
#include "expander.h"
#include "file-handle.h"
#include "fixnum.h"
#include "hash.h"
#include "idio-string.h"
#include "module.h"
#include "pair.h"
#include "read.h"
#include "rfc6234.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "unicode.h"
#include "util.h"
#include "vm-asm.h"
#include "vm.h"
#include "vtable.h"

IDIO idio_rfc6234_SHA1_sym = idio_S_nil;
IDIO idio_rfc6234_SHA224_sym = idio_S_nil;
IDIO idio_rfc6234_SHA256_sym = idio_S_nil;
IDIO idio_rfc6234_SHA384_sym = idio_S_nil;
IDIO idio_rfc6234_SHA512_sym = idio_S_nil;

void idio_rfc6234_verify_alg (char const *func, IDIO alg, enum SHAversion *whichSha, int *hashsize)
{
    IDIO_ASSERT (alg);

    IDIO_TYPE_ASSERT (symbol, alg);

    *whichSha = SHA1;
    *hashsize = 0;

    if (idio_rfc6234_SHA1_sym == alg) {
	/* redundant? */
	*whichSha = SHA1;
	*hashsize = SHA1HashSize;
    } else if (idio_rfc6234_SHA224_sym == alg) {
	*whichSha = SHA224;
	*hashsize = SHA224HashSize;
    } else if (idio_rfc6234_SHA256_sym == alg) {
	*whichSha = SHA256;
	*hashsize = SHA256HashSize;
    } else if (idio_rfc6234_SHA384_sym == alg) {
	*whichSha = SHA384;
	*hashsize = SHA384HashSize;
    } else if (idio_rfc6234_SHA512_sym == alg) {
	*whichSha = SHA512;
	*hashsize = SHA512HashSize;
    } else {
	/*
	 * Test Case(s):
	 *
	 * rfc6234-errors/shasum-{string,fd,file}-bad-alg-value.idio
	 */
	idio_error_param_value_msg (func, "alg", alg, "should be one of 'SHA1, 'SHA224, 'SHA256, 'SHA384, 'SHA512", IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return;
    }
}

IDIO idio_rfc6234_shasum_string (IDIO s, IDIO alg)
{
    IDIO_ASSERT (s);
    IDIO_ASSERT (alg);

    IDIO_TYPE_ASSERT (string, s);
    IDIO_TYPE_ASSERT (symbol, alg);

    enum SHAversion whichSha = SHA1;
    int hashsize = 0;

    /* Do we use these? */
    int eb = 0;			/* extrabits */
    int neb = 0;		/* number of extrabits */

    idio_rfc6234_verify_alg ("shasum-string", alg, &whichSha, &hashsize);

    USHAContext sha;
    int err;

    memset (&sha, '\343', sizeof (sha)); /* force bad data into struct */
    err = USHAReset(&sha, whichSha);
    if (shaSuccess != err) {
	char em[81];
	snprintf (em, 80, "shasum-string shaReset Error %d", err);
	idio_error_C (em, IDIO_LIST2 (s, alg), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    size_t size = 0;
    char *C_s = idio_string_as_C (s, &size);

    err = USHAInput (&sha, (const uint8_t *) C_s, size);
    if (shaSuccess != err) {
	IDIO_GC_FREE (C_s, size);

	char em[81];
	snprintf (em, 80, "shasum-string shaInput Error %d", err);
	idio_error_C (em, IDIO_LIST2 (s, alg), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    IDIO_GC_FREE (C_s, size);

    if (neb > 0) {
	err = USHAFinalBits (&sha, (uint8_t) eb, neb);
	if (shaSuccess != err) {
	    char em[81];
	    snprintf (em, 80, "shasum-string shaFinalBits Error %d", err);
	    idio_error_C (em, IDIO_LIST2 (s, alg), IDIO_C_FUNC_LOCATION ());

	    return idio_S_notreached;
	}
    }

    uint8_t Message_Digest_Buf[USHAMaxHashSize];
    uint8_t *Message_Digest = Message_Digest_Buf;

    err = USHAResult (&sha, Message_Digest);
    if (shaSuccess != err) {
	char em[81];
	snprintf (em, 80, "shasum-string shaResult Error %d, could not compute message digest", err);
	idio_error_C (em, IDIO_LIST2 (s, alg), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }

    /* hexify */
    char r_buf[hashsize * 2];
    for (int i = 0; i < hashsize; i++) {
	uint8_t c = Message_Digest[i];
	r_buf[i*2]   = idio_hex_digits[(c & 0xf0) >> 4];
	r_buf[i*2+1] = idio_hex_digits[(c & 0x0f)];
    }

    return idio_string_C_len (r_buf, hashsize * 2);
}

IDIO_DEFINE_PRIMITIVE1V_DS ("shasum-string", rfc6234_shasum_string, (IDIO s, IDIO args), "s [alg]", "\
shasum the string `s`			\n\
					\n\
:param s: string			\n\
:type s: string				\n\
:param alg: SHA algorithm, defaults to ``'SHA256``	\n\
:type alg: symbol, optional		\n\
:return: digest				\n\
:rtype: string				\n\
")
{
    IDIO_ASSERT (s);

    /*
     * Test Case: rfc6234-errors/shasum-string-bad-string-type.idio
     *
     * shasum-string #t
     */
    IDIO_USER_TYPE_ASSERT (string, s);

    IDIO alg = idio_rfc6234_SHA256_sym;

    if (idio_isa_pair (args)) {
	alg = IDIO_PAIR_H (args);
	/*
	 * Test Case: rfc6234-errors/shasum-string-bad-alg-type.idio
	 *
	 * shasum-string "" #t
	 */
	IDIO_USER_TYPE_ASSERT (symbol, alg);
    }

    return idio_rfc6234_shasum_string (s, alg);
}

char *idio_rfc6234_shasum_fd (char const *func, int fd, IDIO alg, int *rblp)
{
    IDIO_ASSERT (alg);

    IDIO_TYPE_ASSERT (symbol, alg);

    enum SHAversion whichSha = SHA1;
    int hashsize = 0;

    /* Do we use these? */
    int eb = 0;			/* extrabits */
    int neb = 0;		/* number of extrabits */

    idio_rfc6234_verify_alg (func, alg, &whichSha, &hashsize);

    USHAContext sha;
    int err;

    memset (&sha, '\343', sizeof (sha)); /* force bad data into struct */
    err = USHAReset(&sha, whichSha);
    if (shaSuccess != err) {
	char em[81];
	snprintf (em, 80, "%s shaReset Error %d", func, err);
	idio_error_C (em, alg, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return NULL;
    }

    char buf[BUFSIZ];
    for (;;) {
	ssize_t read_r = read (fd, buf, BUFSIZ);

	if (-1 == read_r) {
	    idio_error_system_errno (func, alg, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return NULL;
	}

	if (0 == read_r) {
	    *buf = '\0';
	    break;
	}

	err = USHAInput (&sha, (const uint8_t *) buf, read_r);
	if (shaSuccess != err) {
	    char em[81];
	    snprintf (em, 80, "%s shaInput Error %d", func, err);
	    idio_error_C (em, alg, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return NULL;
	}
    }

    if (neb > 0) {
	err = USHAFinalBits (&sha, (uint8_t) eb, neb);
	if (shaSuccess != err) {
	    char em[81];
	    snprintf (em, 80, "%s shaFinalBits Error %d", func, err);
	    idio_error_C (em, alg, IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return NULL;
	}
    }

    uint8_t Message_Digest_Buf[USHAMaxHashSize];
    uint8_t *Message_Digest = Message_Digest_Buf;

    err = USHAResult (&sha, Message_Digest);
    if (shaSuccess != err) {
	char em[81];
	snprintf (em, 80, "%s shaResult Error %d, could not compute message digest", func, err);
	idio_error_C (em, alg, IDIO_C_FUNC_LOCATION ());

	/* notreached */
	return NULL;
    }

    /* hexify */
    *rblp = hashsize * 2;
    char *r_buf = idio_alloc (*rblp);
    for (int i = 0; i < hashsize; i++) {
	uint8_t c = Message_Digest[i];
	r_buf[i*2]   = idio_hex_digits[(c & 0xf0) >> 4];
	r_buf[i*2+1] = idio_hex_digits[(c & 0x0f)];
    }

    return r_buf;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("shasum-fd", rfc6234_shasum_fd, (IDIO fd, IDIO args), "fd [alg]", "\
shasum the contents from `fd`		\n\
					\n\
:param fd: fd				\n\
:type fd: file descriptor		\n\
:param alg: SHA algorithm, defaults to ``'SHA256``	\n\
:type alg: symbol, optional		\n\
:return: digest				\n\
:rtype: string				\n\
")
{
    IDIO_ASSERT (fd);

    /*
     * Test Case: rfc6234-errors/shasum-fd-bad-fd-type.idio
     *
     * shasum-fd #t
     */
    IDIO_USER_C_TYPE_ASSERT (int, fd);

    IDIO alg = idio_rfc6234_SHA256_sym;

    if (idio_isa_pair (args)) {
	alg = IDIO_PAIR_H (args);
	/*
	 * Test Case: rfc6234-errors/shasum-fd-bad-alg-type.idio
	 *
	 * shasum-fd C/0i #t
	 */
	IDIO_USER_TYPE_ASSERT (symbol, alg);
    }

    int r_buf_len = 0;
    char *r_buf = idio_rfc6234_shasum_fd ("shasum-fd", IDIO_C_TYPE_int (fd), alg, &r_buf_len);

    IDIO r = idio_string_C_len (r_buf, r_buf_len);

    IDIO_GC_FREE (r_buf, r_buf_len);

    return r;
}

IDIO_DEFINE_PRIMITIVE1V_DS ("shasum-file", rfc6234_shasum_file, (IDIO file, IDIO args), "file [alg]", "\
shasum the contents of `file`		\n\
					\n\
:param file: file			\n\
:type file: pathname or string		\n\
:param alg: SHA algorithm, defaults to ``'SHA256``	\n\
:type alg: symbol, optional		\n\
:return: digest				\n\
:rtype: string				\n\
")
{
    IDIO_ASSERT (file);

    /*
     * Test Case: rfc6234-errors/shasum-file-bad-file-type.idio
     *
     * shasum-file #t
     */
    IDIO_USER_TYPE_ASSERT (string, file);

    IDIO alg = idio_rfc6234_SHA256_sym;

    if (idio_isa_pair (args)) {
	alg = IDIO_PAIR_H (args);
	/*
	 * Test Case: rfc6234-errors/shasum-file-bad-alg-type.idio
	 *
	 * shasum-file "/dev/null" #t
	 */
	IDIO_USER_TYPE_ASSERT (symbol, alg);
    }

    IDIO fh = idio_file_handle_open_file ("shasum-file", file, idio_S_nil, IDIO_MODE_RE, sizeof (IDIO_MODE_RE) - 1);

    int r_buf_len = 0;
    char *r_buf = idio_rfc6234_shasum_fd ("shasum-file", IDIO_FILE_HANDLE_FD (fh), alg, &r_buf_len);

    idio_close_file_handle (fh);

    IDIO r = idio_string_C_len (r_buf, r_buf_len);

    IDIO_GC_FREE (r_buf, r_buf_len);

    return r;
}

void idio_rfc6234_add_primitives ()
{
    IDIO_ADD_PRIMITIVE (rfc6234_shasum_string);
    IDIO_ADD_PRIMITIVE (rfc6234_shasum_fd);
    IDIO_ADD_PRIMITIVE (rfc6234_shasum_file);
}

void idio_init_rfc6234 ()
{
    idio_module_table_register (idio_rfc6234_add_primitives, NULL, NULL);

    idio_rfc6234_SHA1_sym   = IDIO_SYMBOL ("SHA1");
    idio_rfc6234_SHA224_sym = IDIO_SYMBOL ("SHA224");
    idio_rfc6234_SHA256_sym = IDIO_SYMBOL ("SHA256");
    idio_rfc6234_SHA384_sym = IDIO_SYMBOL ("SHA384");
    idio_rfc6234_SHA512_sym = IDIO_SYMBOL ("SHA512");
}
