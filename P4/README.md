
Issue with the first task: 
1) processes cannot run more than once without breaking the OS. 
- First execution is fine. Second execution trigger a page fault. Third execution causes the process to be stucked. 

--- 
I have discovered that the same thread is being executed over and over again. On the second attempt, *process pointer* in <ins>process_load_path</ins> is empty. Therefore it caused page fault while running <ins>addrspc_init</ins>

![alt text](image.png)
![alt text](image-1.png)