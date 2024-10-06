#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "include/lib/user/syscall.h"

/* Projects 2 and later. */
void halt (void) NO_RETURN;
// 0
void exit (int status) NO_RETURN;
// 1
pid_t fork (const char *thread_name);
// 2
int exec (const char *file);
// 3
int wait (pid_t);
// 4
bool create (const char *file, unsigned initial_size);
// 5
bool remove (const char *file);
// 6
int open (const char *file);
// 7
int filesize (int fd);
// 8
int read (int fd, void *buffer, unsigned length);
// 9
int write (int fd, const void *buffer, unsigned length);
// 10
void seek (int fd, unsigned position);
// 11
unsigned tell (int fd);
// 12
void close (int fd);
// 13


#endif /* userprog/syscall.h */
