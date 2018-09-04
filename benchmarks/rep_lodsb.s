# Million reps
#
# See how the rep prefix affects retired instructions and branches for lodsb

# For x86_64

	.globl _start
_start:
	xor	%rcx,%rcx		    # rcx = 0
	mov	$1000000,%rcx		# load counter using rcx
    mov	$buffer,%rsi        # source string
	rep	lodsb               # executed 1 million times

	#================================
	# Exit
	#================================
exit:
	xor %rdi,%rdi		# return 0
	mov	$60,%rax		# SYSCALL_EXIT
	nop
	syscall	            # exit

.bss
.lcomm 	buffer,1000000
