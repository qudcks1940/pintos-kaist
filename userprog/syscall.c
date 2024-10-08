#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

// 여기서부터 내가 추가.
#include "include/lib/user/syscall.h" 
#include "include/filesys/filesys.h"
#include "include/filesys/file.h"
#include "include/userprog/process.h"
#include "include/threads/palloc.h"


// process 관련
void halt (void);
void exit (int status);
pid_t fork (const char *thread_name);
int exec (const char *file);
int wait (pid_t);

// fd 관련
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
int process_add_file(struct file *file);
void process_remove_file(int fd); 
struct file* process_get_file(int fd);
void check_address(const void *addr);



/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */
#define MAX_FD 128  // 각 프로세스에서 최대 파일 디스크립터 개수
// 프로세스가 대부분의 상황에서 충분히 많은 파일을 동시에 열 수 있으면서도, 효율적인 자원 관리를 가능하게 하는 적당한 값

// 파일 디스크립터를 관리하기 위한 구조체 정의
struct file *fd_table[MAX_FD];  // 각 프로세스마다 파일 디스크립터 테이블을 가짐
static struct intr_frame *frame; // 포크함수에 인자를 추가 할 수 없어서 따로 선언
struct lock filesys_lock;

// 주소값이 유효한지 검사.
void check_address(const void *addr)
{
	struct thread *t = thread_current();
	if (addr == NULL || addr == "\0" || !is_user_vaddr(addr) || pml4_get_page(t->pml4, addr) == NULL)
		exit(-1);
}

// void get_argument(void *esp, int *arg, int count)
// {
// 	/* 유저스택에저장된인자값들을커널로저장*/
// 	/* 인자가저장된위치가유저영역인지확인*/
// } 

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {

	frame = f;
	uint64_t syscall_num = f->R.rax;
	switch(syscall_num)
	{
		// 반환값 있으면 f->R.rax 에 넣기.
		// 인자 갯수만큼 순서대로 rdi, rsi, rdx 순
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:	
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = fork(f->R.rdi);
			break;
		case SYS_EXEC:
			f->R.rax = exec(f->R.rdi);
			break;
		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_OPEN:
			f->R.rax = open(f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;	
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;	
		case SYS_TELL:
			f->R.rax = tell(f->R.rdi);
			break;	
		case SYS_CLOSE:
			close(f->R.rdi);
			break;	

		default:
			break;
	}
}

void // 완료
halt (void) 
{
	power_off();
}

void // 완료
exit (int status)
{
	struct thread *curr = thread_current();
	curr->exit_status = status;
	printf("%s: exit(%d)\n", curr->name, status); // 요구사항
	thread_exit(); // 얘가 process_exit()불러옴.
}

pid_t // 완료
fork (const char *thread_name)
{
	return process_fork(thread_name, frame); // process.c
}

int  // 완료
exec (const char *file) // 주어진 파일 이름을 기반으로 새로운 프로그램 실행.
{
	check_address(file);

	// 파일 이름 복사용 페이지 할당. 새로운 프로그램의 파일이름을 저장하는데 사용됨.
	char *fn_copy = palloc_get_page(0);
	if (fn_copy == NULL) // 메모리가 부족해서 할당 실패 시시
		exit(-1);
	
	// 실제 복사하는 과정. file에 있는 문자열을 fn_copy로 복사함.
	strlcpy(fn_copy, file, PGSIZE);

	// 실제로 새 프로그램을 실행하는 함수. 실패하면 -1을 반환.
	if(process_exec(fn_copy) == -1)
		exit(-1);
}

int // 완료
wait (int pid)
{
	return process_wait(pid);
}

bool // 완료
create (const char *file, unsigned initial_size)
{
	check_address(file); // 파일 이름이 유효한지 검사. 유효하지 않으면 exit
	lock_acquire(&filesys_lock);
	bool rtn = filesys_create(file, initial_size); // 파일 생성하고 생성됐으면 true 내보냄.
	lock_release(&filesys_lock);
	
	return rtn; // 리턴값 정해주기.
}

bool // 완료
remove (const char *file)
{
	check_address(file); // 파일 이름이 유효한지 검사. 유효하지 않으면 exit
	return filesys_remove(file); // create함수에서 변수 rtn으로 따로 bool값 받아서 한거를 한 줄로 줄일수 있음.
}

int // 완료
open (const char *file)
{

	check_address(file); // 파일 이름이 유효한지 검사. 유효하지 않으면 exit
	lock_acquire(&filesys_lock);
	struct file *opened_file = filesys_open(file); // 파일을 열기.
	if (!opened_file) // 실패하면 반환 -1
		return -1;
	
	int fd = process_add_file(opened_file);
	if (fd == -1); // fd 테이블이 가득차서 실패하면 파일 닫고 
		file_close(opened_file);
	lock_acquire(&filesys_lock);
	return fd;
}

int // 완료
filesize (int fd)
{ 
	struct file *get_file = process_get_file(fd);
	if (get_file == NULL)
		return -1;

	return file_length(get_file);
	
}

int // 완료
read (int fd, void *buffer, unsigned length) // 이 함수는 fd로 열린 파일에서 size만큼의 데이터를 읽어와 buffer에 저장합니다.
{
	int byte = 0;
	int i = 0;
	char ch;

	// 버퍼 초기화
	check_address(buffer);
    memset(buffer, 0, length);

	if (fd == 0) { // fd가 0번이면, 키보드에서 입력을 받아 읽어야 합니다.

		 // 문자를 입력받아 버퍼에 저장
		while (i < length - 1) { // 마지막 문자는 null 문자로 예약
			ch = input_getc(); // 한 문자를 입력받음

			// Enter 키를 누르면 입력 종료
			if (ch == '\n') {
				break;
			}

			((char *)buffer)[i++] = ch; // 입력받은 문자를 버퍼에 저장 // 버퍼가 void포인터라서 수정하려면 다시 캐스팅 해줘야 됨.
			byte++;	
		}

		((char *)buffer)[i] = '\0'; // 문자열 종료를 위해 null 문자 추가
	
	} else { // 파일에서 읽기.

		struct file *get_file = process_get_file(fd);
		if (get_file == NULL)
			return -1;
		byte = file_read(get_file, buffer, length);

	}
	
	return byte; // 읽은 바이트 수 반환

}

int // 완료
write (int fd, const void *buffer, unsigned length) // 이 함수는 fd로 열린 파일에 buffer로부터 size만큼의 데이터를 씁니다.
{
	int byte = 0;
	check_address(buffer);
	// write 함수에서는 buffer 내용을 그대로 사용 해야 하므로 초기화 하면 실제로 출력할 데이터가 손실됨..

	if (fd == 1){
		putbuf(buffer, length); // fd가 1이면 콘솔로 데이터 출력.
		byte = length;
	} else {
		struct file *get_file = process_get_file(fd);
		if (get_file == NULL)
			return -1;
		byte = file_write(get_file, buffer, length);
	}

	return byte;
}

void // 완료
seek (int fd, unsigned position)
{
	struct file *file = process_get_file(fd);
	if (file == NULL)
		return;
	file_seek(file, position);
}

unsigned // 완료
tell (int fd)
{
	struct file *file = process_get_file(fd);
	if (file == NULL)
		return;
	return file_tell(file);
}
void // 완료
close (int fd)
{
	struct file *file = process_get_file(fd);
	if (file == NULL)
		return;
	file_close(file);
	process_remove_file(fd);
}

// 파일 디스크립터 테이블에서 빈 슬롯을 찾아 새로운 파일 디스크립터 할당
int 
process_add_file(struct file *file) 
{
    struct thread *curr = thread_current(); // 현재 스레드 정보 가져오기
    for (int fd = 2; fd < MAX_FD; fd++) {  // 0, 1은 stdin, stdout 예약됨
        if (curr->fd_table[fd] == NULL) {
            curr->fd_table[fd] = file;  // 파일 디스크립터 테이블에 파일 추가
            return fd;            // 파일 디스크립터 반환
        }
    }
    return -1;  // 파일 디스크립터 테이블이 가득 차면 실패
}

// 파일 디스크립터 테이블에서 해당 파일 디스크립터를 제거
void 
process_remove_file(int fd) 
{
    struct thread *curr = thread_current(); // 현재 스레드 정보 가져오기
    if (fd >= 2 && fd < MAX_FD)
    	curr->fd_table[fd] = NULL; // 해당 디스크립터 제거
        // 해당 파일 디스크립터를 NULL로 설정하여 제거
}

struct file* 
process_get_file(int fd) 
{
    struct thread *curr = thread_current();  // 현재 스레드(프로세스) 정보 가져오기
    if (fd < 2 || fd >= MAX_FD)  // 0과 1은 예약된 fd (STDIN, STDOUT)
        return NULL;
    
    return curr->fd_table[fd];  // fd 테이블에서 파일 객체 반환
}