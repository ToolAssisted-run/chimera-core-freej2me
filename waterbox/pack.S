/* pack.S - the pack of the runtime's files (make-pack.py), built into the
 * core's image: read-only, so it is in no savestate. memfs.c serves it.
 * PACK_PATH names the pack file at build time.
 * SPDX-License-Identifier: MIT */
	.section .rodata.chimera_pack, "a"
	.balign 4096
	.globl chimera_pack
chimera_pack:
	.incbin PACK_PATH
	.globl chimera_pack_end
chimera_pack_end:
	.section .note.GNU-stack, "", @progbits
