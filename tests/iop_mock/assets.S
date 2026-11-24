.section .rodata
.macro asset name, value
.global lp_\name,lp_\name\()_end
lp_\name:
.byte \value
lp_\name\()_end:
.endm
asset sio2man,0
asset padman,1
asset mcman,2
asset mcserv,3
asset libsd,4
asset audsrv,5
.section .note.GNU-stack,"",@progbits
