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
 * compile.c
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <assert.h>
#include <fcntl.h>
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
#include "codegen.h"
#include "compile.h"
#include "error.h"
#include "evaluate.h"
#include "expander.h"
#include "file-handle.h"
#include "fixnum.h"
#include "handle.h"
#include "hash.h"
#include "idio-string.h"
#include "module.h"
#include "pair.h"
#include "read.h"
#include "rfc6234.h"
#include "string-handle.h"
#include "struct.h"
#include "symbol.h"
#include "thread.h"
#include "unicode.h"
#include "util.h"
#include "vm-asm.h"
#include "vm.h"
#include "vtable.h"

IDIO idio_compile_module = idio_S_nil;

IDIO_DEFINE_PRIMITIVE1_DS ("compile-file-writer1", compile_file_writer, (IDIO ia), "ia", "\
Return the number of opcodes in `ia`		\n\
						\n\
:param ia: byte code				\n\
:type eenv: struct-idio-ia			\n\
:return: length					\n\
:type args: integer				\n\
")
{
    IDIO_ASSERT (ia);

    return idio_S_unspec;
}

/*
 * Compare a read in Idio string with the #define C string
 */
int idio_compile_compare_strings (IDIO is, char const *cs, size_t C_size)
{
    IDIO_ASSERT (is);
    IDIO_C_ASSERT (cs);

    IDIO_TYPE_ASSERT (string, is);

    size_t I_size = 0;
    char *C_is = idio_string_as_C (is, &I_size);

    int r = (C_size == I_size);

    if (r) {
	r = (0 == strncmp (C_is, cs, C_size));
    }

    IDIO_GC_FREE (C_is, I_size);

    return r;
}

int idio_compile_file_reader (IDIO eenv, IDIO I_file, char *file, size_t file_len)
{
    IDIO_ASSERT (eenv);
    IDIO_ASSERT (I_file);
    IDIO_C_ASSERT (file);

    IDIO_TYPE_ASSERT (struct_instance, eenv);
    IDIO_TYPE_ASSERT (string, I_file);

    /*
     * /path/to/__idio__/{mod}.{ASM_COMMIT} where we shouldn't be here
     * unless {ASM_COMMIT} in the file name matches
     * IDIO_BUILD_ASM_COMMIT.  Although some intrepid user might call
     * us direct.
     *
     * The corresponding idio file will be /path/to/{mod}.idio
     */
    char *file_dot = memrchr (file, '.', file_len);
    if (NULL == file_dot) {
#ifdef IDIO_DEBUG
	fprintf (stderr, "no dot in %s\n", file);
#endif
	return 0;
    }

    size_t ibac_len = sizeof (IDIO_BUILD_ASM_COMMIT) - 1;
    if ((size_t) (file_dot - file) < ibac_len) {
#ifdef IDIO_DEBUG
	fprintf (stderr, "not %zu after dot (%zu) in %s\n", ibac_len, file_dot - file, file);
#endif
	return 0;
    }

    char *file_slash = memrchr (file, '/', file_dot - file);
    if (NULL == file_slash) {
#ifdef IDIO_DEBUG
	fprintf (stderr, "no slash before dot in %s\n", file);
#endif
	return 0;
    }

    char *file_slash2 = memrchr (file, '/', file_slash - file);
    if (NULL == file_slash2) {
#ifdef IDIO_DEBUG
	fprintf (stderr, "no slash#2 before slash in %s\n", file);
#endif
	return 0;
    }

    size_t icd_len = sizeof (IDIO_CACHE_DIR) - 1;
    if (strncmp (file_slash2 + 1, IDIO_CACHE_DIR, icd_len)) {
#ifdef IDIO_DEBUG
	fprintf (stderr, "not %s between last two slashes in %s\n", IDIO_CACHE_DIR, file);
#endif
	return 0;
    }

    size_t iie_len = sizeof (IDIO_IDIO_EXT) - 1;
    size_t if_len = (file_slash2 - file) + (file_dot - file_slash) + iie_len;

    char ifn[if_len + 1];
    memcpy (ifn, file, file_slash2 - file);
    char *end = ifn + (file_slash2 - file);
    memcpy (end, file_slash, file_dot - file_slash);
    end += (file_dot - file_slash);
    memcpy (end, IDIO_IDIO_EXT, iie_len);
    end += iie_len;
    end[0] = '\0';

    /*
     * The largest checksum buffer will be "SHA512:" + SHA512HashSize
     * -- albeit we are using SH256
     */
    char chksum[7 + SHA512HashSize + 1];
    int chksum_len = 0;		/* cf. #f */

    if (access (ifn, R_OK) == 0) {

	int if_fd = open (ifn, O_RDONLY);

	if (-1 == if_fd) {
	    idio_error_system_errno ("open", idio_string_C (ifn), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return 0;
	}

	int r_buf_len = 0;
	char *r_buf = idio_rfc6234_shasum_fd ("compile-file-reader", if_fd, idio_rfc6234_SHA256_sym, &r_buf_len);

	if (-1 == close (if_fd)) {
	    idio_error_system_errno ("close", idio_string_C (ifn), IDIO_C_FUNC_LOCATION ());

	    /* notreached */
	    return 0;
	}

	memcpy (chksum, "SHA256:", 7);
	memcpy (chksum + 7, r_buf, r_buf_len);
	chksum_len = 7 + r_buf_len;
	chksum[chksum_len] = '\0';

	IDIO_GC_FREE (r_buf, r_buf_len);
    }

    /*
     * Finally we can open the cache file and start reading the
     * contents.
     */
    if (idio_S_nil == I_file) {
	I_file = idio_string_C_len (file, file_len);
    }
    IDIO fh = idio_open_file_handle_C ("open-input-file", I_file, file, file_len, 0, IDIO_MODE_RE, sizeof (IDIO_MODE_RE) - 1, 0, 0);

    /*
     * idio-build-compiler-commit
     *
     * The format isn't likely to change much but is versioned by the
     * commit of this file.
     */
    IDIO ibcc = idio_read (fh);

    if (! (idio_isa_string (ibcc) &&
	   idio_compile_compare_strings (ibcc, IDIO_STATIC_STR_LEN (IDIO_BUILD_COMPILER_COMMIT)))) {
#ifdef IDIO_DEBUG
	idio_debug ("compiler-commit %s != ", ibcc);
	fprintf (stderr, "%s\n", IDIO_BUILD_COMPILER_COMMIT);
#endif
	return 0;
    }
#ifdef IDIO_COMPILE_FILE_READ
    idio_debug ("compiler-commit %s ", ibcc);
#endif

    /*
     * when
     *
     * not really of much use -- reporting, maybe
     */
    IDIO when = idio_read (fh);

    if (! idio_isa_string (when)) {
#ifdef IDIO_DEBUG
	idio_debug ("when %s is not a string\n", when);
#endif
	return 0;
    }
#ifdef IDIO_COMPILE_FILE_READ
    idio_debug ("when %s ", when);
#endif

    /*
     * idio-build-asm-commit
     *
     * Was the byte code in the cache file generated using the same
     * version of the byte code we are using?
     */
    IDIO ibac = idio_read (fh);

    if (! (idio_isa_string (ibac) &&
	   idio_compile_compare_strings (ibac, IDIO_STATIC_STR_LEN (IDIO_BUILD_ASM_COMMIT)))) {
#ifdef IDIO_DEBUG
	idio_debug ("asm-commit %s != ", ibac);
	fprintf (stderr, "%s\n", IDIO_BUILD_ASM_COMMIT);
#endif
	return 0;
    }
#ifdef IDIO_COMPILE_FILE_READ
    idio_debug ("asm-commit %s\n", ibac);
#endif

    /*
     * pre-compiler chksum
     *
     * Are we using byte code compiled for the same file?  But only if
     * we could checksum the file ourselves, above.
     *
     * This leaves the possibility of byte code-only distributions.
     */
    IDIO pc_chksum = idio_read (fh);

    if (chksum_len) {
	if (! (idio_isa_string (pc_chksum) &&
	       idio_compile_compare_strings (pc_chksum, chksum, chksum_len))) {
#ifdef IDIO_DEBUG
	    idio_debug ("chksum %s != ", pc_chksum);
	    fprintf (stderr, "%s for %s\n", chksum, file);
#endif
	    return 0;
	}
    }
#ifdef IDIO_COMPILE_FILE_READ
    idio_debug ("pc-chksum %s\n", pc_chksum);
#endif

    /*
     * array lengths
     *
     * The size of the symbol and value tables.
     *
     * Could we really exceed a fixnum?
     */
    IDIO alen = idio_read (fh);

    if (! idio_isa_fixnum (alen)) {
#ifdef IDIO_DEBUG
	idio_debug ("alen %s is not a fixnum\n", alen);
#endif
	return 0;
    }
#ifdef IDIO_COMPILE_FILE_READ
    idio_debug ("alen %s ", alen);
#endif

    idio_as_t C_alen = IDIO_FIXNUM_VAL (alen);

    /*
     * symbol table entries
     */
    IDIO ste = idio_read (fh);

    if (! (idio_isa_list (ste))) {
#ifdef IDIO_DEBUG
	idio_debug ("symbol table entries %s is not a list\n", ste);
#endif
	return 0;
    }
#ifdef IDIO_COMPILE_FILE_READ
    fprintf (stderr, "ste #%zu ", idio_list_length (ste));
#endif

    idio_as_t max_ci = 0;
    IDIO st = idio_array (C_alen);
    /* enable all array elements */
    IDIO_ARRAY_USIZE (st) = C_alen;

    while (idio_S_nil != ste) {
	IDIO si_ci = IDIO_PAIR_H (ste);

	IDIO si = IDIO_PAIR_H (si_ci);
	if (! idio_isa_fixnum (si)) {
	    return 0;
	}
	idio_as_t C_si = IDIO_FIXNUM_VAL (si);

	if (C_si >= C_alen) {
#ifdef IDIO_DEBUG
	    idio_debug ("symbol table entry %s is too large: ", si_ci);
	    fprintf (stderr, "%zu >= %zu\n", C_si, C_alen);
#endif
	    return 0;
	}

	IDIO ci = IDIO_PAIR_T (si_ci);
	if (idio_S_false != ci) {
	    if (! idio_isa_fixnum (ci)) {
#ifdef IDIO_DEBUG
		idio_debug ("si-ci %s: ci is not a fixnum\n", si_ci);
#endif
		return 0;
	    }
	    idio_as_t C_ci = IDIO_FIXNUM_VAL (ci);

	    idio_array_insert_index (st, ci, C_si);

	    if (C_ci > max_ci) {
		max_ci = C_si;
	    }
	}

	ste = IDIO_PAIR_T (ste);
    }

    /*
     * constants
     */
    IDIO cs = idio_read (fh);

    if (! (idio_isa_array (cs))) {
#ifdef IDIO_DEBUG
	fprintf (stderr, "constants is not an array\n");
#endif
	return 0;
    }
#ifdef IDIO_COMPILE_FILE_READ
    fprintf (stderr, "cs #%zu ", idio_array_size (cs));
#endif


    if (max_ci >= idio_array_size (cs)) {
#ifdef IDIO_DEBUG
	fprintf (stderr, "constants is smaller (%zu) than the largest symbol index (%zu)\n", idio_array_size (cs), max_ci);
#endif
	return 0;
    }

    /*
     * program counter
     *
     * Could we really exceed a fixnum?
     */
    IDIO pc = idio_read (fh);

    if (! idio_isa_fixnum (pc)) {
#ifdef IDIO_DEBUG
	idio_debug ("pc %s is not a fixnum\n", pc);
#endif
	return 0;
    }
#ifdef IDIO_COMPILE_FILE_READ
    idio_debug ("pc %s ", pc);
#endif

    idio_pc_t C_pc = IDIO_FIXNUM_VAL (pc);

    /*
     * byte code string
     */
    IDIO bs = idio_read (fh);

    if (! idio_isa_octet_string (bs)) {
#ifdef IDIO_DEBUG
	idio_debug ("bs %s is not an octet string\n", bs);
#endif
	return 0;
    }
#ifdef IDIO_COMPILE_FILE_READ
    fprintf (stderr, "bs #%zu ", idio_string_len (bs));
#endif

    if (C_pc >= (ssize_t) idio_string_len (bs)) {
#ifdef IDIO_DEBUG
	fprintf (stderr, "pc is greater (%zu) than the length of the byte code (%zu)\n", C_pc, idio_string_len (bs));
#endif
	return 0;
    }

    /*
     * source code expressions
     */
    IDIO ses = idio_read (fh);

    if (! (idio_isa_array (ses))) {
#ifdef IDIO_DEBUG
	idio_debug ("source code expressions %s is not an array\n", ses);
#endif
	return 0;
    }
#ifdef IDIO_COMPILE_FILE_READ
    fprintf (stderr, "ses #%zu ", idio_array_size (ses));
#endif

    /*
     * source code properties
     */
    IDIO sps = idio_read (fh);

    if (! (idio_isa_array (sps))) {
#ifdef IDIO_DEBUG
	idio_debug ("source code properties %s is not an array\n", sps);
#endif
	return 0;
    }
#ifdef IDIO_COMPILE_FILE_READ
    fprintf (stderr, "sps #%zu\n", idio_array_size (sps));
#endif

    if (idio_array_size (ses) != idio_array_size (sps)) {
#ifdef IDIO_COMPILE_FILE_READ
	fprintf (stderr, "number of source code expressions (%zu) does not match the number of source code properties (%zu)\n", idio_array_size (ses), idio_array_size (sps));
#endif
	/* return 0; */
    }

    /*
     * Phew!
     */

    IDIO vs = idio_array_dv (C_alen, idio_fixnum (0));
    /* enable all array elements */
    IDIO_ARRAY_USIZE (vs) = C_alen;

    idio_ai_t al = idio_array_size (cs);
    /*
     * IDIO_HASH_EQP doesn't like a zero length hash so set it to be 1
     */
    IDIO ch = IDIO_HASH_EQP (al ? al : 1);

    idio_ai_t ai;

    for (ai = 0; ai < al; ai++) {
	IDIO v = idio_array_ref_index (cs, ai);
	if (idio_S_nil != v) {
	    idio_hash_set (ch, v, idio_fixnum (ai));
	}
    }

    idio_xi_t xi = idio_vm_add_xenv (idio_string_C (ifn), st, cs, ch, vs, ses, sps, bs);
#ifdef IDIO_COMPILE_FILE_READ
    fprintf (stderr, "xi [%zu] for %s\n", xi, ifn);
#endif

    idio_struct_instance_set_direct (eenv, IDIO_EENV_ST_ST,  IDIO_XENV_ST (idio_xenvs[xi]));
    idio_struct_instance_set_direct (eenv, IDIO_EENV_ST_VT,  IDIO_XENV_VT (idio_xenvs[xi]));
    idio_struct_instance_set_direct (eenv, IDIO_EENV_ST_CS,  IDIO_XENV_CS (idio_xenvs[xi]));
    idio_struct_instance_set_direct (eenv, IDIO_EENV_ST_CH,  IDIO_XENV_CH (idio_xenvs[xi]));
    idio_struct_instance_set_direct (eenv, IDIO_EENV_ST_SES, IDIO_XENV_SES (idio_xenvs[xi]));
    idio_struct_instance_set_direct (eenv, IDIO_EENV_ST_SPS, IDIO_XENV_SPS (idio_xenvs[xi]));

    IDIO CPT_byte_code = idio_C_pointer_type (idio_CSI_idio_ia_s,  IDIO_XENV_BYTE_CODE (idio_xenvs[xi]));
    IDIO_C_TYPE_POINTER_FREEP (CPT_byte_code) = 0;

    idio_struct_instance_set_direct (eenv, IDIO_EENV_ST_BYTE_CODE, CPT_byte_code);

    idio_struct_instance_set_direct (eenv, IDIO_EENV_ST_XI, idio_fixnum (xi));

    idio_vm_run_xenv (xi, C_pc);

    return 1;
}

IDIO_DEFINE_PRIMITIVE1_DS ("compile-file-reader", compile_file_reader, (IDIO file), "file", "\
Read and run the execution environment in `file`	\n\
						\n\
:param file: pre-compiled source code		\n\
:type file: string				\n\
:return: ``#t`` if the file could be run, ``#f`` otherwise	\n\
:rtype: boolean					\n\
")
{
    IDIO_ASSERT (file);

    IDIO_USER_TYPE_ASSERT (string, file);

    IDIO r = idio_S_false;

    size_t file_len = 0;
    char *C_file = idio_string_as_C (file, &file_len);

    IDIO thr = idio_thread_current_thread ();
    IDIO cm = IDIO_THREAD_MODULE (thr);

    IDIO dsh = idio_open_output_string_handle_C ();
    idio_display (IDIO_MODULE_NAME (cm), dsh);
    idio_display_C ("> compile-file-reader ", dsh);
    idio_display (file, dsh);
    IDIO desc = idio_get_output_string (dsh);

    IDIO eenv = idio_evaluate_eenv (thr, desc, idio_S_true, cm);

    if (idio_compile_file_reader (eenv, file, C_file, file_len)) {
	r = idio_S_true;
    }

    return r;
}

void idio_compile_add_primitives ()
{
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_compile_module, compile_file_writer);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_compile_module, compile_file_reader);
}

void idio_init_compile ()
{
    idio_module_table_register (idio_compile_add_primitives, NULL, NULL);

    idio_compile_module = idio_module (IDIO_SYMBOL ("compile"));

    idio_module_set_symbol_value (IDIO_SYMBOL ("*idio-cache-dir*"), IDIO_STRING (IDIO_CACHE_DIR), idio_compile_module);
}
