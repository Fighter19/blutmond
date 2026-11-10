@ SPDX-License-Identifier: BSD-2-Clause OR MIT
@ Embeds a pack in a GBA image: BM_PACK_PATH is the cooked pack (defined by CMake).

    .section .rodata.bm_embedded_pack, "a"
    .balign 4
    .global bm_embedded_pack
bm_embedded_pack:
    .incbin BM_PACK_PATH
    .global bm_embedded_pack_end
bm_embedded_pack_end:
    .balign 4
