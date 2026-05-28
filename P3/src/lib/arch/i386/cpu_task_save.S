	.text
	.global cpu_fresh_stack
cpu_fresh_stack:
	/* Move params to registers. */
	mov	+4(%esp), %ecx		// Function to call
	mov	+8(%esp), %edx		// New stack

	/* Switch to fresh stack. */
	mov	%edx,	%esp		// New stack
	mov	$0,	%ebp		// Null stack frame

	/* Save function to new stack. */
	push	%ecx

1:	/* Call function. */
	mov	+0(%esp), %ecx		// Get function from new frame
	call	*%ecx			// Call function

	/* If function returns, simply call it again. */
	jmp	1b

	.text
	.global	cpu_task_save
	.global	cpu_task_restore
	.type	cpu_task_save, @function
	.type	cpu_task_restore, @function

cpu_task_save:
	mov	+4(%esp), %ecx		// arg1: struct cpu_task_save ptr

	/* TODO: Save state into cpu_task_save struct. */
	/*Saving the relevant registers to the struct*/
	
 	mov %ebx, +0(%ecx) 
	
    mov %edi, +4(%ecx) 
    mov %esi, +8(%ecx)

    mov %esp, +12(%ecx)
    mov %ebp, +16(%ecx) 

	mov	+0(%esp), %edx
	mov	%edx, +20(%ecx)
	
	/* Return to caller with return value 0. */
	mov	$0, %eax		// 0 return code to indicate save
	ret

cpu_task_restore:
	mov	+4(%esp), %ecx		// arg1: struct cpu_task_save ptr
	mov	+8(%esp), %eax		// arg2: return code
	
	mov +0(%ecx), %ebx
	mov +4(%ecx), %edi
	mov +8(%ecx), %esi

	mov +12(%ecx), %esp
	mov +16(%ecx), %ebp


	/* TODO: Restore state from cpu_task_save struct.
	 * Should basically be the reverse of cpu_task_save. */

	jmp	*+20(%ecx)

	/* Return to caller of cpu_task_save with given return value. */
