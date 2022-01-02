#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <user/syscall.h>
struct lock fs_lock;
void syscall_init(void);
int sys_wait(pid_t pid);
bool sys_create(const char * file, unsigned initial_size);
bool sys_remove(const char * file);
int sys_open(const char *file);
int sys_filesize(int fd);
int sys_write(int fd, const char *buffer, unsigned size);
void sys_exit(int status);
int sys_read (int fd, void * buffer, unsigned size);
pid_t sys_exec(const char * cmd_line);
void sys_seek(int fd, unsigned position);
unsigned sys_tell (int fd);
void sys_close(int fd);
bool sys_remove(const char * file);
bool check_if_valid_pointer(void * file);
unsigned sys_tell (int fd);

//New for Project 4
bool sys_chdir (const char *dir);
bool sys_mkdir (const char *dir);
bool sys_readdir (int fd, char *name);
bool sys_isdir (int fd);
int sys_inumber (int fd);
struct dir* parse_path(const char *dir);
bool update_path(struct dir *);
char ** parse_path_get_last(const char *dir);
#endif /* userprog/syscall.h */
