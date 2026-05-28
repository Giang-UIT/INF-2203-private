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
	mov	$0, %eax		// 0 return code to indicate save

	/* Save state.
	 * Note: These offsets must match the cpu_task_save struct. */

	/* Save floating point registers. */
	fsave	+24(%ecx)
	fwait

	/* Save nonvolatile registers. */
	mov	%ebp,	+20(%ecx)
	mov	%edi,	+16(%ecx)
	mov	%esi,	+12(%ecx)
	mov	%ebx,	+ 8(%ecx)

	/* Save stack and return address. */
	mov	+0(%esp), %edx		// Get return address.
	mov	%edx,	+ 4(%ecx)	// Save return address.
	mov	%esp,	+ 0(%ecx)	// Save stack.

	/* Return to caller with return value 0. */
	ret

cpu_task_restore:
	mov	+4(%esp), %ecx		// arg1: struct cpu_task_save ptr
	mov	+8(%esp), %eax		// arg2: return code

	/* Restore state.
	 * Note: These offsets must match the cpu_task_save struct. */

	/* Restore stack and return address. */
	mov	+ 0(%ecx), %esp		// Switch stacks!
	mov	+ 4(%ecx), %edx		// Get return address.
	mov	%edx, +0(%esp)		// Restore return address.

	/* Restore nonvolatile registers. */
	mov	+ 8(%ecx), %ebx
	mov	+12(%ecx), %esi
	mov	+16(%ecx), %edi
	mov	+20(%ecx), %ebp

	/* Restore floating point registers. */
	frstor	+24(%ecx)

	/* Return to caller of cpu_task_save with given return value. */
	ret
