    .section .text
    .globl  _start
_start:
    andi    a5, a3, 0xc0
    li      a2, 0xc0
    beq     a3, a2, .Ltarget
    .space  0x34
.Ltarget:
    ret
