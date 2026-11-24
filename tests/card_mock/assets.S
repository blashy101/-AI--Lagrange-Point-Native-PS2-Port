.section .rodata
.balign 64
.global lp_save_icon,lp_save_icon_end
lp_save_icon:
.incbin "assets/save.icn"
lp_save_icon_end:
.section .note.GNU-stack,"",@progbits
