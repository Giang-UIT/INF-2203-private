
.global dispatch

dispatch: 
	
	/*Jump if SP is not zero */
    /*Check for any conflict between the segments of each different processes. 
    This assumes that each segment is not written back to bootimage after finished */

	mov %edi, %eax //move the start_addr to ax register
    //push the parameters to the stack. 
    push %edi //give the process a small stack
    push %esi
    push %edx

    jmp *%eax //jump to the start_addr to execute
    
    pop %edx
    pop %esi
    pop %edi
	
	ret