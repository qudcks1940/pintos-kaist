#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);
struct thread* get_child_process(int pid);
void argument_stack(char **argv, int argc, struct intr_frame *if_);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* 사용자 프로그램 "initd"를 시작하는 함수입니다. 
 * FILE_NAME에서 프로그램을 로드한 후, 새로운 스레드를 생성하여 실행합니다.
 * 이 함수는 스레드가 생성되기 전에 스케줄링될 수 있으며,
 * 심지어는 함수가 반환되기 전에 스레드가 종료될 수 있습니다.
 * 생성된 스레드의 ID를 반환하며, 스레드 생성에 실패하면 TID_ERROR를 반환합니다.
 * 주의: 이 함수는 한 번만 호출되어야 합니다. */
tid_t
process_create_initd (const char *file_name) {
    char *fn_copy, *save_ptr;
    tid_t tid;

    /* FILE_NAME의 복사본을 만듭니다.
     * 그렇지 않으면 호출자와 load() 함수 사이에 경쟁 상태가 발생할 수 있습니다.
     * (원본 문자열이 다른 곳에서 변경될 수 있기 때문에 복사본을 만들어야 합니다.) */
    fn_copy = palloc_get_page (0);
    if (fn_copy == NULL)
        return TID_ERROR; // 페이지 할당에 실패하면 오류 반환
    strlcpy (fn_copy, file_name, PGSIZE); // FILE_NAME을 복사하고 페이지 크기(PGSIZE)만큼 복사함

    /* FILE_NAME을 실행할 새로운 스레드를 생성합니다.
     * 새로운 스레드의 이름은 FILE_NAME이며,
     * 스레드의 우선순위는 기본값(PRI_DEFAULT)입니다.
     * 스레드가 실행할 함수는 initd이고, fn_copy를 인자로 전달합니다. */
	strtok_r(file_name, " ", &save_ptr);
	printf("-------------------\n");
	printf("-------%s-----\n",file_name);
	printf("-------------------\n");
    tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);
    if (tid == TID_ERROR) // 스레드 생성 실패 시
        palloc_free_page (fn_copy); // 할당된 페이지를 해제하여 메모리 누수 방지
    return tid; // 생성된 스레드의 ID 반환 (실패 시 TID_ERROR 반환)
}


/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/
	struct thread *curr = thread_current();
	// 현재 스레드의 정보를 복사해둠. 현재 인터럽트 프레임에 레지스터값, 
	// 플래그, 스택포인터 등이 포함되어 있어 현재 프로세스의 상태를 나타냄.
	// 이 if_값을 현재 스레드의 parent_if필드에 복사해둠. 나중에 자식 프로세스에 복제하기 위한 정보.
	memcpy(&curr->parent_if, if_, sizeof(struct intr_frame));

	// 새로운 스레드 생성. do fork는 생성되고 실행될 함수. 
	// 부모프로세스의 상태를 복사하고, 자식 프로세스를 부모와 동일하게 만들어줌.
	// create가 성공하면 새 스레드의 pid를 반환함. 실패시 에러 반환.
	tid_t pid = thread_create (name, PRI_DEFAULT, __do_fork, thread_current ());
	if (pid == TID_ERROR)
		return TID_ERROR;
	
	// 새로 생성된 자식 스레드가 부모 스레드의 자식 리스트에 추가 됐는지 확인.
	// 성공적으로 추가 됐으면, 자식 스레드의 정보를 반환
	struct thread *child = get_child_process(pid);
	// 부모 프로세스가 자식 프로세스가 메모리 로드 완료할 떄까지 기다리도록 만드는 역할.
	// 자식 스레드가 메모리에 로드되면 sema_up을 호출해서 부모 스레드가 계속 실행되도록 신호 보냄.
	sema_down(&child->load_sema); 

	// 자식스레드가 정상적으로 실행되지 않고 오류 발생시키며 종료됐다면,
	//  exit_status가 오류값을 반환하면서 포크 과정이 실패했음을 나타냄.
	if(child->exit_status == TID_ERROR)
		return TID_ERROR;
	
	// 이 과정들이 잘 완료 됐다면 생성된 자식 스레드의 프로세스id를 반환.
	return pid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	process_init ();

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
	thread_exit ();
}

/*process_exec() 함수는 새로운 프로세스를 실행하기 위해 현재 프로세스의 리소스를 정리하고, 
 새로운 프로세스를 메모리에 로드한 다음, 해당 프로세스로 전환합니다. 
 이 과정에서 프로세스의 실행 상태를 저장하는 구조체를 사용하여 프로세스가 정상적으로 실행될 수 있도록 합니다. 
 현재 실행 중인 컨텍스트를 f_name으로 전환.
 * 실패할 경우 -1을 반환합니다. */
int
process_exec (void *f_name) {
	// argc와 argv를 각각 rdi와 rsi 레지스터에 설정
	char *file_name = f_name;  // `f_name`을 복사하여 `file_name`에 저장합니다. 
    // 시스템 콜의 인자로 전달된 실행 파일 이름을 사용합니다.
	char *token, *save_ptr, *argv[64]; // 명령어와 인자를 저장할 배열
	bool success;
	int argc = 0;   // 인자의 개수 (argument count)

	/* 
     * intr_frame 구조체 초기화: 
     * 프로세스의 실행 상태를 관리하는 인터럽트 프레임(_if) 구조체를 선언하고, 
     * 새로운 프로세스 실행을 위한 세그먼트 레지스터와 플래그 레지스터를 설정합니다.
       이 구조체는 프로세스의 실행 상태를 저장하는데 사용됩니다 */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG; //사용자 데이터 세그먼트로 설정
	_if.cs = SEL_UCSEG; //사용자 코드 세그먼트로 설정
	// 플래그 레지스터를 설정, FLAG_IF는 인터럽트를 활성화하고, FLAG_MBS는 상태를 설정
	_if.eflags = FLAG_IF | FLAG_MBS; 

	 /* 
     * 현재 컨텍스트 종료: 
     * 이전 프로세스의 리소스를 정리하고 해제하는 역할을 하는 process_cleanup()을 호출합니다.
     * 이를 통해 현재 프로세스의 종료 상태를 준비합니다.
     */
	process_cleanup ();

	/* 
     * file_name을 공백(' ')으로 구분하여 인자들을 추출하고 argv 배열에 저장합니다.
     * strtok_r를 사용하여 첫 번째 호출에서는 `f_name`을 전달하고, 이후 호출에서는 NULL을 전달하여 
     * 이전 호출의 위치에서 계속 토큰화가 이루어집니다.
     */

	token = strtok_r(file_name, " ", &save_ptr); // 첫 번째 호출 이후 NULL로 설정하여 다음 토큰을 처리
	while (token != NULL) {
		argv[argc++] = token; // argv 배열에 토큰 저장
		token = strtok_r(NULL, " ", &save_ptr); 
	}

	/* 
     * 바이너리 로드: 
     * load() 함수는 file_name으로 지정된 프로그램(바이너리)을 메모리에 로드하고, 
     * 성공하면 인터럽트 프레임 _if에 새로운 프로세스의 실행 정보를 설정합니다.
     */
    success = load(file_name, &_if);

    /* 
     * 인자를 스택에 푸시: 
     * argument_stack() 함수를 호출하여 argv에 저장된 인자들을 
     * 프로세스의 스택에 올려놓습니다. 
     * argc는 인자의 개수입니다.
     */
    argument_stack(argv, argc, &_if);

	/* 
     * 스택 덤프: 
     * hex_dump()는 스택 메모리의 내용을 확인할 수 있도록 덤프를 출력하는 함수입니다.
     * _if.rsp는 현재 스택 포인터를 가리키며, 스택의 시작과 끝 범위를 덤프 출력합니다.
     */
    hex_dump(_if.rsp, _if.rsp, USER_STACK - (uint64_t)_if.rsp, true);

    /* 
     * 메모리 해제: 
     * palloc_free_page() 함수는 file_name으로 할당된 페이지 메모리를 해제합니다.
     */
	palloc_free_page (file_name);
	
	/* 
     * 바이너리 로드 실패 시: 
     * load() 함수가 실패하면 -1을 반환하여 오류를 나타냅니다.
     */
    if (!success)
        return -1;

    /* 
     * do_iret() 함수 호출: 
     * 새로운 프로세스를 시작하기 위해 do_iret()을 호출합니다.
     * 이 함수는 _if 구조체에 저장된 프로세스의 실행 정보를 사용하여 
     * CPU 레지스터를 설정하고, 실행을 시작합니다.
     */
    do_iret(&_if);

    /* 
     * NOT_REACHED() 매크로: 
     * 이 매크로는 코드가 이 지점에 도달해서는 안 된다는 의미입니다. 
     * do_iret()이 성공적으로 실행되면, 프로세스는 새로운 프로그램으로 전환되기 때문에
     * 이 아래 코드는 실행되지 않아야 합니다. 만약 도달한다면, 오류가 발생한 것입니다.
     */
	NOT_REACHED ();
}
/* 프로그램을메모리에적재하고응용프로그램실행*/

/*
유저스택에프로그램이름과인자들을저장하는함수
parse: 프로그램이름과인자가저장되어있는메모리공간, 
count: 인자의개수, 
esp: 스택포인터를가리키는주소*/
void argument_stack(char **argv, int argc, struct intr_frame *if_)
{
	char *arg_address[128];

	// 프로그램 이름, 인자 문자열 push
	for(int i = argc - 1; i >= 0; i--)
	{
		int arg_i_len = strlen(argv[i]) +1;	//sential(\0) 포함
		if_->rsp -= arg_i_len;			//인자 크기만큼 스택을 늘려줌
		memcpy(if_->rsp, argv[i], arg_i_len);	//늘려준 공간에 해당 인자를 복사
		arg_address[i] = (char *)if_->rsp;	//arg_address에 위 인자를 복사해준 주소값을 저장
	}

	// word-align(8의 배수)로 맞춰주기
	if(if_->rsp % 8 != 0)
	{	
		int padding = if_->rsp % 8;
		if_->rsp -= padding;
		memset(if_->rsp, 0, padding);
	}

	// 인자 문자열 종료를 나타내는 0 push
	if_->rsp -= 8; 	
	memset(if_->rsp, 0, 8);

	// 각 인자 문자열의 주소 push
	for(int i = argc-1; i >= 0; i--)
	{
		if_->rsp -= 8;
		memcpy(if_->rsp, &arg_address[i], 8);
	}

	// fake return address
	if_->rsp -= 8;
	memset(if_->rsp, 0, 8);

	//rdi 에는 인자의 개수, rsi에는 argv 첫 인자의 시작 주소 저장
	if_->R.rdi = argc;
	if_->R.rsi = if_->rsp + 8;	//fake return address + 8
}

/* TID 스레드가 종료될 때까지 기다린 후 그 종료 상태를 반환합니다.
 * 만약 커널에 의해 종료되었다면 (즉, 예외로 인해 종료된 경우),
 * -1을 반환합니다. TID가 유효하지 않거나 호출한 프로세스의
 * 자식 프로세스가 아닌 경우, 또는 주어진 TID에 대해 process_wait()이 
 * 이미 성공적으로 호출된 경우, 즉시 -1을 반환하며 기다리지 않습니다.*/
int
process_wait (tid_t child_tid UNUSED) {
	
	struct thread *child = get_child_process(child_tid);
	if (child == NULL)
		return -1;  // 직속 자식이 아니거나 없는 자식이면 -1 반환

	// 이미 wait가 호출된 적이 있는지 확인 (중복 방지)
    if (child->wait_sema.value == 0) {
        return -1;  // 이미 wait() 호출됨
    }

	// 자식이 종료될 때까지 대기한다. (process_exit에서 자식이 종료될 때 sema_up 해줄 것이다.)
	// 세마포어를 내리면서 해당 프로세스가 기다리도록 한다. 
	// 세마포어가 신호를 받을때까지(즉, 자식 프로세스가 종료 될 때까지) 부모프로세스가 대기 상태로 있음
	sema_down(&child->wait_sema);

	 // 자식이 커널에 의해 강제 종료되었는지 확인하고, 이 경우 -1 반환
    if (child->exit_status == -1) {
        return -1;
    }
	// 자식이 종료됨을 알리는 `wait_sema` signal을 받으면 현재 스레드(부모)의 자식 리스트에서 제거한다.
	// 부모가 자식을 더 이상 기다리지 않게됨. 부모-자식 관계를 명시적으로 끊는 작업...ㅜㅜ
	list_remove(&child->child_elem);

	// 자식이 시스템에서 완전히 종료될 수 있도록 세마신호를 주고, 종료 상태 값을 받아옴.
	sema_down(&child->exit_sema);

	return child->exit_status;
}

struct thread*
get_child_process(int pid)
{
	struct list *child_list = &thread_current()->child_list;
	struct list_elem *e;

	for (e = list_begin(child_list); e != list_end(child_list); e = list_next(e)) {
		struct thread *t = list_entry(e, struct thread, child_elem);

		if (t->tid == pid)
			return t;
	}
	return NULL;

}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	
	struct list *fd_table = &curr->fd_table; // 프로세스가 열어둔 파일들의 정보를 가지고 있음.
	struct list_elem *e;
	struct file_descriptor *fd;

	while(!list_empty(fd_table)) { // fd_table의 열려있는 모든 파일 닫고 메모리 반환.
		e = list_pop_front(fd_table);
		fd = list_entry(e, struct file_descriptor, fd_elem); //리스트 요소 e에서 fd 가져옴.
		close(fd); //fd가 가리키고 있는 파일을 닫음.
	}

	file_close(curr->running); // 현재 프로세스가 실행하고 있는 파일까지 끄면서 마무리.

	process_cleanup (); // 프로세스가 종료될 때 메모리 정리와 자원 반환.

	sema_up(&curr->wait_sema); // 자식 프로세스가 종료되었음을 부모에게 알리는 역할.
	sema_down(&curr->exit_sema); 
	// 자식 프로세스는 완전히 시스템에서 종료되기 전에 
	// 다시 한번 세마포어를 사용해 대기 상태로 들어감. 부모가 자식 프로세스의 종료를 완전히 확인한 후에만
	// 자식 프로세스가 시스템에서 제거될 수 있도록 보장하는 역할을 함.
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	file_close (file);
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */
