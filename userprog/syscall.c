#include <stdio.h>
#include <syscall-nr.h>
#include <filesys/file.h>

#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/syscall.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include <devices/shutdown.h>
#include <devices/input.h>
#include <userprog/process.h>
#include <filesys/filesys.h>
// File Sys
#include "filesys/directory.h"
#include "lib/string.h"
#include "threads/malloc.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "vm/spt.h"

static void syscall_handler(struct intr_frame *);

//todo add struct lock fslock;
// struct lock fs_lock;

void
syscall_init(void)
{
    intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
    lock_init(&fs_lock);
}

bool check_if_valid_pointer(void * file){
    // printf("WTF: 0x%x\n",file);
    struct thread * cur = thread_current();
    if(file == NULL || file > PHYS_BASE || pagedir_get_page(cur->pagedir, file) == NULL){
        return false;
    }
    return true;
}


static void
syscall_handler(struct intr_frame *f UNUSED)
{
    /* Remove these when implementing syscalls */
    int syscallno;

    // Make sure the args are occupying valid memory locations
    if (!check_if_valid_pointer(f->esp) || f->esp+1 > PHYS_BASE-4){ 
        sys_exit(-1);
    }
    
    syscallno  = (int) *((int*)(f->esp));

    //When we call a syscall we want to save the stack pointer to thread control block
    //For stack growth
    struct thread * cur = thread_current();
    cur->esp_saved = f->esp;


    
    switch (syscallno)
    {
        case SYS_HALT:
            shutdown_power_off();
            break; 

        case SYS_EXIT: 
            sys_exit(*((int *)f->esp + 1));
            break;

        case SYS_EXEC: 
            f->eax = sys_exec(*((char **)f->esp + 1));
            break;

        case SYS_WAIT: 
            f->eax = sys_wait(*((pid_t *)f->esp + 1));
            break;

        case SYS_CREATE:
            f->eax = sys_create( *((char**)f->esp + 1), *((unsigned *)f->esp + 2));
            break;

        case SYS_REMOVE:
            f->eax = sys_remove(*((char**)f->esp + 1));
            break;

        case SYS_OPEN:
            f->eax = sys_open(*((char**)f->esp + 1)); //return the fd to f->eax
            break;

        case SYS_FILESIZE:
            f->eax = sys_filesize(*((int*)f->esp + 1));
            break;

        case SYS_READ:
            f->eax = sys_read( *((int*)f->esp + 1), *((char**)f->esp + 2), *((unsigned *)f->esp + 3));
            break;

        case SYS_WRITE:
            // f->esp + 1 points to a file descriptor, f->esp + 2 is a pointer to buffer, f->esp + 3 is a pointer ot an unsigned size value
            f->eax = sys_write( *((int*)f->esp + 1), *((char**)f->esp + 2), *((unsigned *)f->esp + 3));
            break;

        case SYS_SEEK:
            lock_acquire(&fs_lock);
            sys_seek( *((int*)f->esp + 1), *((unsigned*)f->esp + 2));
            lock_release(&fs_lock);
            break;

        case SYS_TELL:
            lock_acquire(&fs_lock);
            f->eax = sys_tell(*((int*)f->esp+ 1));
            lock_release(&fs_lock);
            break;

        case SYS_CLOSE:
            lock_acquire(&fs_lock);
            sys_close(*((int*)f->esp + 1));
            lock_release(&fs_lock);
            break;

        // new for project 4    
        case SYS_CHDIR:
            f->eax = sys_chdir(*((char **)f->esp + 1));
            break;
        case SYS_MKDIR:
            f->eax = sys_mkdir(*((char **)f->esp + 1));
            break;            
        case SYS_READDIR:
            f->eax = sys_readdir(*((int*)f->esp + 1), *((char **)f->esp + 2));
            break;
        case SYS_ISDIR:
            f->eax = sys_isdir(*((int*)f->esp + 1));
            break;
        case SYS_INUMBER:
            f->eax = sys_inumber(*((int*)f->esp + 1));
            break;            
        default:
            break;
    }

}

//sets the cwd to the input directory
bool update_path(struct dir* directory){
    //Maybe we need to set cwd instead of its contents
    struct thread * cur = thread_current();
    cur->cwd = directory;
    cur->cwd->pos = 0;
    return true;
}


//does not update cwd according to the given input path.
struct dir* parse_path(const char *dir){
    struct dir * base_dir;
    struct thread * cur = thread_current();

    // Empty String means we dont update path, return current directory
    if(strcmp(dir,"") == 0){
        return cur->cwd;
    }
    if(strcmp(dir,"/") == 0){ //might not even need this
        struct dir * temp = dir_open_root(); //Remove this
        return temp;
    }

    // logic ? True: False;
    base_dir = dir[0] == '/' ? dir_open_root() : cur->cwd;


    // Copy the dir
    char * rest = malloc(strlen(dir) + 1);
    strlcpy(rest, dir, strlen(dir) + 1);

    char * token;

    // Edge Case: Empty String
   
    // Main Logic    
    struct inode *inode = NULL; 

    struct dir * ret_dir = malloc(sizeof(struct dir));
    ret_dir->inode = base_dir->inode;

    while ((token = strtok_r(rest, "/", &rest))){
        // Case: .  -> current directory
        // Case: .. -> parent directory
        // Case: root's parent directory is itself?
        bool found = dir_lookup(ret_dir, token, &inode);
        ret_dir->inode = inode;

        if(!found){
            base_dir = NULL;
            return base_dir;
        }
        // printf("%s\n", token);
    }
    // ret_dir->inode = inode;
    // //Maybe we need to set cwd instead of its contents
    // cur->cwd->inode = inode;
    // cur->cwd->pos = 0;
    // printf("%d \n",is_absolute);    // Navigate and 

    return ret_dir; // TODO: FIX THIS SHIT OUTPUT
}
// Changes the current working directory of the process to dir, 
// which may be relative or absolute. Returns true if successful, false on failure.
bool sys_chdir (const char *dir){
    struct dir* new_dir = parse_path(dir);
    struct thread * cur = thread_current();
    // If valid directory, update path
    if(new_dir && !new_dir->inode->removed){
        update_path(new_dir);
        cur->cwd = new_dir;
        return true;
    }
    else{
        return false;
    }
}


//separates last entry from the rest of path. Returns them as a array

//char** out;
//out[0] "path"
//out[0] "dir/file"
char ** parse_path_get_last(const char *dir){
    // Handle dir is empty situation
    if(strcmp(dir,"") == 0){
        // Return structure
        char ** two_points = malloc(2 * sizeof(char*));
        two_points[0] = "";
        two_points[1] = "";
        return two_points;
    }
    if(strcmp(dir,"/") == 0){
        // Return structure
        char ** two_points = malloc(2 * sizeof(char*));
        two_points[0] = "/";
        two_points[1] = "";
        return two_points;
    }

    // Copy the dir
    char * copy = malloc(strlen(dir) + 1);
    strlcpy(copy, dir, strlen(dir) + 1);

    // Reverse the String
    char temp = '~';
    for(unsigned int i = 0; i< strlen(copy)/2; i++){
        temp = copy[i];
        copy[i] = copy[strlen(copy)-1-i];
        copy[strlen(copy)-1-i] = temp;
    }

    // Tokenize once
    char * token;
    token = strtok_r(copy, "/", &copy);

    // Reverse the string
    for(unsigned int i = 0; i< strlen(copy)/2; i++){
        temp = copy[i];
        copy[i] = copy[strlen(copy)-1-i];
        copy[strlen(copy)-1-i] = temp;
    }

    // edge 
    if(strcmp(copy, "") == 0 && dir[0] == '/'){
        copy = "/" ;
    }
    // Reverse Token
    for(unsigned int i = 0; i< strlen(token)/2; i++){
        temp = token[i];
        token[i] = token[strlen(token)-1-i];
        token[strlen(token)-1-i] = temp;
    }

    // Return structure
    char ** two_points = malloc(2 * sizeof(char*));
    two_points[0] = copy;
    two_points[1] = token;

    return two_points;
}


// Creates the directory named dir, which may be relative or absolute. 
// Returns true if successful, false on failure. Fails if dir already exists or if any directory name 
// in dir, besides the last, does not already exist. That is, mkdir("/a/b/c") succeeds only if
//  "/a/b" already exists and "/a/b/c" does not.

bool sys_mkdir (const char *dir){

    struct thread * cur = thread_current();
    char ** path_parsed = parse_path_get_last(dir);
    
    // Return values of function call
    char * prefix_path = path_parsed[0];
    char * postfix_dir = path_parsed[1];


    // Checks if the directory for path already exists 
    struct dir * path_dir = parse_path(prefix_path);
    if(path_dir == NULL){ 
        return false;
    }

    // Creates the directory
    block_sector_t inode_sector = 0;

    // Creates a new directory (TODO, move to sub_dir_create)
    bool success = (path_dir != NULL
                    && free_map_allocate(1, &inode_sector)
                    && inode_create(inode_sector, 16 * sizeof(struct dir_entry),INODE_DIR)
                    && dir_add(path_dir, postfix_dir, inode_sector));

    // Add parent (..) and current (.) entires to postfix_dir

    // First add .
    struct dir * dot = parse_path(dir);
    dir_add(dot, ".", inode_sector);

    // Then add ..
    dir_add(dot,"..", path_dir->inode->sector);

    // Free shit

    return success;
}

// Reads a directory entry from file descriptor fd, which must represent a directory. 
// If successful, stores the null-terminated file name in name, which must have room for READDIR_MAX_LEN + 1 
// bytes, and returns true. If no entries are left in the directory, returns false. "." and ".." should 
// not be returned by readdir.If the directory changes while it is open, then it is acceptable for some 
// entries not to be read at all or to be read multiple times. 
// Otherwise, each directory entry should be read once, in any order.
// READDIR_MAX_LEN is defined in "lib/user/syscall.h". If your file system supports 
// longer file names than the basic file system, you should increase this value from the default of 14.

bool sys_readdir(int fd, char *name){
    //todo
    struct thread * cur = thread_current();
    // struct inode * inode = cur->fdtab[fd]->inode;
    // /* Check if inode is directory*/
    // struct dir_entry ee[26]; // Could we find a set amount?
    // bool success = false;
    // if(inode->data.type == INODE_DIR){
    //     /* Check if all files are not in use*/
    //     block_read(fs_device,inode->data.DPters[0], ee);
    //     for(unsigned int entry = 2; entry < 16; entry++){
    //         if(ee[entry].in_use == true){
    //             success = true;
    //         }
    //     }
    // }
    // // Write the name of the file
    // if(success){
    // }

    bool success = dir_readdir(dir_open(cur->fdtab[fd]->inode),name);

    return success;
}

// Returns true if fd represents a directory, false if it represents an ordinary file.

bool sys_isdir (int fd){
    //todo
    struct thread * cur = thread_current();
    if(cur->fdtab[fd]->inode->data.type == 1){
        return true;
    }
    else{
        return true;
    }

}

// Returns the inode number of the inode associated with fd, which may represent an ordinary file or a directory.
// An inode number persistently identifies a file or directory. It is unique during the file's existence. In Pintos, the sector number of the inode is suitable for use as an inode number.

int sys_inumber (int fd){ 
    struct thread * cur = thread_current();
    return inode_get_inumber(cur->fdtab[fd]->inode);
}


/*
    Writes size bytes from buffer to the open file fd. 
    Returns the number of bytes actually written, which may be less 
    than size if some bytes could not be written.

    Writing past end-of-file would normally extend the file, but file growth is not implemented by the basic file system. 
    The expected behavior is to write as many bytes as possible up to end-of-file and return the actual number written, or 0 if no bytes could be written at all.
    Fd 1 writes to the console. 
    Your code to write to the console should write all of buffer in one call to putbuf(), at least as long as size is not bigger than a few hundred bytes. 
    (It is reasonable to break up larger buffers.) Otherwise, lines of text output by different processes may end up interleaved on the console, confusing both human readers and our grading scripts.
*/
int sys_write (int fd, const char *buffer, unsigned size){

    struct thread * cur = thread_current();

    // Check if Buffer is Valid -> not null and in heap and stack
    if(!check_if_valid_pointer((void*)buffer)){
        sys_exit(-1);
    }

    // if(buffer == 0x8048000){
    //     int num = 5;
    // }

    // struct spte_t *result = spt_get(pg_no(buffer), cur);

    // if(result != NULL && !result->value.writable){
    //     sys_exit(-1);
    // }

    // Writing to stdout, printf should work
    if(fd == 1){
        putbuf(buffer, size);
        return size;
    }
    // Invalid File Descriptors
    if (fd <= 0 || fd >= cur->nextfd){
        return 0; 
    }
    // Cannot write to directories
    if(cur->fdtab[fd]->inode->data.type == INODE_DIR){
        return -1;
    }
    
    //All file sys calls need to be wrapped by a lock and unlock
    lock_acquire(&fs_lock);
    int val = file_write(cur->fdtab[fd], buffer, size);
    lock_release(&fs_lock);
    return val;
}


/**
 * Terminates the current user program, returning status to the kernel. 
 * If the process's parent `wait`s for it (see below), this is the status that will be returned. 
 * Conventionally, a status of 0 indicates success and nonzero values indicate errors.
 */
void sys_exit (int status){
    struct thread *cur = thread_current();
    printf("%s: exit(%d)\n", cur->name, status);
    cur->exitStatus = status; //stored status in child thread structure
    // lock_release(&fs_lock);
    // Close File on Exit
    lock_acquire(&fs_lock);
    file_close(cur->filePointer);
    lock_release(&fs_lock);
    
    // Dying and Dead
    sema_up(&(cur->dying)); // two way handshake
    sema_down(&(cur->dead)); // Wait for parent to reap status before dies

    // todo Exiting or terminating a process implicitly closes all its 
    // open file descriptors, as if by calling sys_close for each one.
    
    thread_exit();
}



/* Closes file descriptor fd. Exiting or terminating a process implicitly closes 
 * all its open file descriptors, as if by calling this function for each one.
*/ 
void sys_close(int fd){
    struct thread *cur = thread_current();
    if((fd > 0 && fd < cur->nextfd) && cur->fdtab[fd] != NULL){
        file_close(cur->fdtab[fd]);
        cur->fdtab[fd] = NULL;
    }
}


/**
 *  Reads size bytes from the file open as fd into buffer.
 *  Returns the number of bytes actually read (0 at end of file), or -1 
 *  if the file could not be read (due to a condition other than end of file). 
 *  Fd 0 reads from the keyboard using `input_getc()`. 
 */
int sys_read (int fd, void * buffer, unsigned size){
    struct thread *cur = thread_current();
    // STDIN
    if(fd == 0){
        return input_getc();
    }
    // Invalid File Descriptors
    if(fd == 1 || fd >= cur->nextfd || fd < 0){
        return 0; 
    }

    // Buffer is Invalid
    // if(!check_if_valid_pointer((void*) buffer)){
    //     // if(pagedir_get_page(cur->pagedir, buffer)){
    //         sys_exit(-1);
    //     // }
    // }
    if(buffer == NULL || buffer > PHYS_BASE ){
        sys_exit(-1);
    }

    lock_acquire(&fs_lock);
    int val = file_read(cur->fdtab[fd], buffer, size);
    lock_release(&fs_lock);
    return val;
}

/**
 * Runs the executable whose name is given in cmd_line, passing any given arguments, and returns 
 * the new process's program id (pid). Must return pid -1, which otherwise should not be a valid pid, 
 * if the program cannot load or run for any reason. Thus, the parent process cannot return from the 
 * exec until it knows whether the child process successfully loaded its executable. You must use 
 * appropriate synchronization to ensure this. 
 * 
 * @param cmd_line 
 * @return pid_t 
 */
pid_t sys_exec(const char * cmd_line){
    if(!check_if_valid_pointer((void *)cmd_line)){
        sys_exit(-1);
    }

    lock_acquire(&fs_lock);
    pid_t cPid = process_execute(cmd_line);
    lock_release(&fs_lock);
    return cPid > 0 ? cPid: -1;
}

/**
 * Creates a new file called file initially initial_size bytes in size. Returns true if successful, 
 * false otherwise. Creating a new file does not open it: opening the new file is a separate operation 
 * which would require a open system call.
 * 
 * @param file 
 * @param initial_size 
 * @return true 
 * @return false 
 */
bool sys_create(const char * file, unsigned initial_size){

    struct thread *cur = thread_current();

    char ** path_parsed = parse_path_get_last(file);
    
    // Return values of function call
    char * prefix_path = path_parsed[0];
    char * postfix_dir = path_parsed[1];
    
    

    // Checks if the directory for path already exists 
    struct dir * path_dir = parse_path(prefix_path);

    if(path_dir == NULL || path_dir->inode->removed){
        return false;
    }

    // Check for null file pointer
    if(!check_if_valid_pointer((void*)file)){
        sys_exit(-1);
    }

    // Jank solution, file created cannot be ""
    if(strcmp(file,"") == 0){
        sys_exit(-1);
    }

    lock_acquire(&fs_lock);
    bool ret = filesys_create(postfix_dir,initial_size, dir_reopen(path_dir));
    lock_release(&fs_lock);
    return ret; 
}

/**

Opens the file called file. Returns a nonnegative integer handle called a "file descriptor" (fd), 
or -1 if the file could not be opened. File descriptors numbered 0 and 1 are reserved for the 
console: fd 0 (STDIN_FILENO) is standard input, fd 1 (STDOUT_FILENO) is standard output. 
The open system call will never return either of these file descriptors, which are valid as 
system call arguments only as explicitly described below.

Each process has an independent set of file descriptors. File descriptors are not inherited 
by child processes.

When a single file is opened more than once, whether by a single process or different processes, 
each open returns a new file descriptor. Different file descriptors for a single file are closed 
independently in separate calls to close and they do not share a file position.
*/

int sys_open(const char *file){
    // Check for null file pointer
    if(file == NULL){
        return -1;
    }
    // Or if Buffer is invalid
    if(!check_if_valid_pointer((void *) file)){
        sys_exit(-1);
    }
    if(strcmp(file,"") == 0){
        return -1;
    }

    char ** path_parsed = parse_path_get_last(file);
    
    // Return values of function call
    char * prefix_path = path_parsed[0];
    char * postfix_file = path_parsed[1];

    // Checks if the directory for path already exists 
    struct dir * path_dir = parse_path(prefix_path);

    // Return -1 if the file/path doesn't exist
    if(path_dir == NULL || path_dir->inode->removed){
        return -1;
    }



    struct thread *cur = thread_current();

    lock_acquire(&fs_lock);
    // struct file * filePointer = filesys_open(file, dir_reopen(cur->cwd));
    struct file * filePointer = filesys_open(postfix_file, dir_reopen(path_dir));
    lock_release(&fs_lock);

    // Handle -1 Case
    if(filePointer == NULL){
        return -1;
    }

    // Update File Descriptor table
    cur->fdtab[cur->nextfd] = filePointer;
    return cur->nextfd++;
}

//Returns the size, in bytes, of the file open as fd.
int sys_filesize(int fd){
    struct thread *cur = thread_current();
    lock_acquire(&fs_lock);
    int val = file_length(cur->fdtab[fd]);
    lock_release(&fs_lock);
    return val;
}



/*
Waits for a child process pid and retrieves the child's exit status.
If pid is still alive, waits until it terminates. Then, returns the status that pid passed to exit. 
If pid did not call exit(), but was terminated by the kernel (e.g. killed due to an exception), 
wait(pid) must return -1. It is perfectly legal for a parent process to wait for child processes 
that have already terminated by the time the parent calls wait, but the kernel must still allow 
the parent to retrieve its child's exit status, or learn that the child was terminated by the kernel.

Wait must fail and return -1 immediately if any of the following conditions is true:

pid does not refer to a direct child of the calling process. pid is a direct child of the calling 
process if and only if the calling process received pid as a return value from a successful call 
to exec. Note that children are not inherited: if A spawns child B and B spawns child process C, 
then A cannot wait for C, even if B is dead. A call to wait(C) by process A must fail. 
Similarly, orphaned processes are not assigned to a new parent if their parent process 
exits before they do.

The process that calls wait has already called wait on pid. That is, a process may wait for 
any given child at most once. Processes may spawn any number of children, wait for them in any order, 
and may even exit without having waited for some or all of their children. Your design should 
consider all the ways in which waits can occur. All of a process's resources, including its 
struct thread, must be freed whether its parent ever waits for it or not, and regardless of whether 
the child exits before or after its parent. You must ensure that Pintos does not terminate until 
the initial process exits. The supplied Pintos code tries to do this by calling 
process_wait() (in "userprog/process.c") from main() (in "threads/init.c"). We suggest that you 
implement process_wait() according to the comment at the top of the function and then 
implement the wait system call in terms of process_wait(). Implementing this system call requires 
considerably more work than any of the rest.

*/
int sys_wait(pid_t pid){
    return process_wait(pid);
}



/* Changes the next byte to be read or written in open file fd to position, 
 * expressed in bytes from the beginning of the file. (Thus, a position of 0 is the file's start.)
 * A seek past the current end of a file is not an error. A later read obtains 0 bytes, indicating 
 * end of file. A later write extends the file, filling any unwritten gap with zeros. 
 * (However, in Pintos files have a fixed length until project 4 is complete, 
 * so writes past end of file will return an error.) These semantics are implemented in the 
 * file system and do not require any special effort in system call implementation.
*/
void sys_seek(int fd, unsigned position){
    struct thread *cur = thread_current(); 

    // Invalid File Descriptors
    if(fd == 0 || fd >= cur->nextfd || fd < 0){
        return;
    }

    cur->fdtab[fd]->pos = position;

}


/* Returns the position of the next byte to be read or written in open file fd, 
 * expressed in bytes from the beginning of the file.
*/
unsigned sys_tell (int fd){
    struct thread *cur = thread_current(); 
    return cur->fdtab[fd]->pos; 
}


/**
 * Deletes the file called file. Returns true if successful, false otherwise. 
 * A file may be removed regardless of whether it is open or closed, and removing an open file does not close it. 
 * See Removing an Open File, for details. 
 */
bool sys_remove(const char * file){
    // Edge Case: Dont remove the root
    if(strcmp(file,"/") == 0){
        return false;
    }

    struct thread *cur = thread_current(); 
    char ** path_parsed = parse_path_get_last(file);
    
    // Return values of function call
    char * prefix_path = path_parsed[0];
    char * postfix_file = path_parsed[1];

    /**
     * Update the remove system call so that it can delete empty directories (other than the root) in addition to 
     * regular files. Directories may only be deleted if they do not contain any files or subdirectories (other than 
     * "." and ".."). You may decide whether to allow deletion of a directory that is open by a process or in use as
     * a process's current working directory. If it is allowed, then attempts to open files (including "." and "..") 
     * or create new files in a deleted directory must be disallowed.
     */
    
    // Checks if the directory for path already exists 
    struct dir * path_dir = parse_path(prefix_path);
    if(path_dir == NULL){
        return false;
    }

    lock_acquire(&fs_lock);
    bool val = filesys_remove(postfix_file, dir_reopen(path_dir));
    lock_release(&fs_lock);

    return val;
}
    