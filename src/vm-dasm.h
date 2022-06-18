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
 * vm-dasm.h
 *
 */

#ifndef VM_DASM_H
#define VM_DASM_H

#define IDIO_IA_GET_NEXT(bc,pcp)	IDIO_IA_AE (bc, *(pcp)); (*pcp)++;
#define IDIO_THREAD_FETCH_NEXT(bc)	IDIO_IA_GET_NEXT (bc, &(IDIO_THREAD_PC(thr)))

char const *idio_vm_bytecode2string (int code);
void idio_vm_dasm (FILE *fp, IDIO thr, IDIO_IA_T bc, idio_pc_t pc0, idio_pc_t pce);
void idio_vm_dump_dasm ();

void idio_init_vm_dasm ();

#endif

/* Local Variables: */
/* mode: C */
/* coding: utf-8-unix */
/* End: */
