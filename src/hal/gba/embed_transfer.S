@ SPDX-License-Identifier: BSD-2-Clause OR MIT
@ Embeds what a game needs to boot another GBA (src/link/transfer.h): the bootstrap and the
@ runtime as multiboot images, and maybe the pack to send. BM_BOOTSTRAP_PATH and BM_RUNTIME_PATH
@ are defined by CMake.

    .section .rodata.bm_embedded_transfer, "a"
    .balign 4
    .global bm_embedded_bootstrap
bm_embedded_bootstrap:
    .incbin BM_BOOTSTRAP_PATH
    .global bm_embedded_bootstrap_end
bm_embedded_bootstrap_end:
    .balign 4
    .global bm_embedded_runtime
bm_embedded_runtime:
    .incbin BM_RUNTIME_PATH
    .global bm_embedded_runtime_end
bm_embedded_runtime_end:
    .balign 4
#ifdef BM_SEND_PACK_PATH
@ The pack sent to the other GBA, when it is not the one the game plays (BM_SEND_PACK_PATH).
    .global bm_embedded_send_pack
bm_embedded_send_pack:
    .incbin BM_SEND_PACK_PATH
    .global bm_embedded_send_pack_end
bm_embedded_send_pack_end:
    .balign 4
#endif
