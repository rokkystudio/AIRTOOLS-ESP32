    .section .text
    .globl  _start
_start:
    andi    a5, a2, 0xc0
    li      a3, 0xc0
    beq     a2, a3, .Ltarget
    .space  0x34
.Ltarget:
    ret
