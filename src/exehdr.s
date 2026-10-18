; Copyright (C) 2026 Neil Rackett
; SPDX-License-Identifier: GPL-3.0-or-later
;
; The .lnx header, in place of cc65's own, which names the cartridge
; "Cart name" by "Manufacturer". Same layout: lynx.cfg imports
; __EXEHDR__, and this module provides it before the library's can.

        .export         __EXEHDR__: absolute = 1
        .import         __BANK0BLOCKSIZE__
        .import         __BANK1BLOCKSIZE__

        .segment        "EXEHDR"
        .byte   'L','Y','N','X'         ; magic
        .word   __BANK0BLOCKSIZE__      ; bank 0 page size
        .word   __BANK1BLOCKSIZE__      ; bank 1 page size
        .word   1                       ; version
        .byte   "Pong Wars"             ; cartridge name, 32 bytes
        .res    32 - 9, 0
        .byte   "Neil Rackett"          ; manufacturer, 16 bytes
        .res    16 - 12, 0
        .byte   0                       ; rotation: none
        .res    5, 0                    ; spare
