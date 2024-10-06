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

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
/*
 * 주어진 파일 이름을 기반으로 새로운 프로세스를 생성하는 함수.
 * 
 * - file_name: 실행할 프로그램의 파일 이름.
 * 
 * 1. 파일 이름을 복사하여 경합을 방지함.
 * 2. 새로운 스레드를 생성하여 해당 파일을 실행함.
 * 3. 스레드 생성에 성공하면 스레드의 TID를 반환하고, 실패하면 오류를 반환.
 */

tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;  // 파일 이름의 복사본을 저장할 포인터
	tid_t tid;      // 생성된 스레드의 TID를 저장할 변수
	char *save_ptr; // strtok_r의 인자

	/* 파일 이름의 복사본을 생성함. 경합을 방지하기 위해 복사본을 사용. */
	fn_copy = palloc_get_page (0);  // 페이지 할당
	if (fn_copy == NULL)  // 페이지 할당 실패 시 오류 반환
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);  // 파일 이름을 복사
	
	// 추가 코드
	strtok_r(file_name," ", &save_ptr);
	printf("-----------file_name--------------\n");
	printf("%s\n",file_name);
	printf("-------------------------\n");

	printf("------------fn_copy-------------\n");
	printf("%s\n",fn_copy);
	printf("-------------------------\n");
	/* 파일 이름을 실행하는 새로운 스레드를 생성. */
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);  
	if (tid == TID_ERROR)  // 스레드 생성 실패 시 페이지 해제
		palloc_free_page (fn_copy);
	return tid;  // 스레드의 TID 반환
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
	return thread_create (name,
			PRI_DEFAULT, __do_fork, thread_current ());
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

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int process_exec (void *f_name) {
    char *file_name = f_name;
    bool success;
    // 명령어와 인자를 저장할 배열
    char *token, *save_ptr, *argv[64]; // 명령어와 인자를 저장할 배열 (최대 64개 인자)
    int argc = 0; // 인자의 개수

    /* We cannot use the intr_frame in the thread structure.
     * This is because when current thread rescheduled,
     * it stores the execution information to the member. */
		// 이 아래부분 초기화를 해주지 않으면
		// 이전 프로세스를 실행할 때의 값들이 남아있을수 있기 때문에
		// 여기서 새로 지역 변수로 설정해줘야 한다. 
    struct intr_frame _if;  
		// 인터럽트 프레임: 프로그램 상태를 저장하기 위한 구조체
    _if.ds = _if.es = _if.ss = SEL_UDSEG;  
		// 데이터 세그먼트 설정 (유저모드 세그먼트 선택자)
    _if.cs = SEL_UCSEG;  
		// 코드 세그먼트 설정 (유저모드 세그먼트 선택자)
    _if.eflags = FLAG_IF | FLAG_MBS;  
		// 인터럽트 플래그 및 필수 플래그 설정

    /* We first kill the current context */
    process_cleanup();  
		// 현재 프로세스의 자원 정리 (현재 실행 중인 프로그램 종료)

    // 명령어와 인자를 파싱하여 argv 배열에 저장
    token = strtok_r(file_name, " ", &save_ptr);  // 공백을 기준으로 첫 번째 명령어 추출
    while (token != NULL) {
        argv[argc] = token;  // 추출한 명령어 또는 인자를 argv 배열에 저장
        token = strtok_r(NULL, " ", &save_ptr);  // 다음 인자를 추출
        argc++;  // 인자 개수 증가
    }

    /* And then load the binary */
    success = load(file_name, &_if);  
		// 새로운 프로그램(사용자 프로그램)을 메모리에 로드하고 초기화

		// Argument Passing ~
    argument_stack(argv, argc, &_if); 
		// 함수 내부에서 argv와 rsp의 값을 직접 변경하기 위해 주소 전달
    
		hex_dump(_if.rsp, _if.rsp, USER_STACK - (uint64_t)_if.rsp, true); // user stack을 16진수로 프린트
    // ~ Argument Passing

    /* If load failed, quit. */
    palloc_free_page(file_name);  
		// 파일 이름에 할당된 페이지를 해제
    if (!success)
        return -1;  // 프로그램 로드 실패 시 -1 반환 (실패)

    /* Start switched process. */
    do_iret(&_if);  
		// 인터럽트 프레임을 이용하여 새로운 프로그램으로 실행 흐름 전환

    NOT_REACHED();  // 정상적으로 실행될 경우 이 코드에 도달하지 않아야 함
}

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

/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
/* 
 * 스레드 TID가 종료될 때까지 대기하고, 종료 상태를 반환하는 함수.
 * 만약 커널에 의해 종료된 경우(즉, 예외로 인해 강제 종료된 경우), -1을 반환.
 * TID가 유효하지 않거나 호출한 프로세스의 자식 프로세스가 아니거나, 
 * 또는 주어진 TID에 대해 process_wait()이 이미 성공적으로 호출된 경우, 
 * 즉시 -1을 반환하며 대기하지 않음.
 *
 * 이 함수는 2-2 문제에서 구현될 예정임. 현재는 아무것도 하지 않음.
 */

int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) 현재는 핀토스가 initd에서 process_wait()을 호출할 때
	 * 종료되지 않도록 무한 루프를 추가하는 것을 권장함. */
	while(1){
		
	}
	return -1;  // 현재는 -1을 반환
}

/* Exit the process. This function is called by thread_exit (). */
/* 프로세스를 종료합니다. 이 함수는 thread_exit()에 의해 호출됩니다. */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: 여기에서 여러분의 코드를 작성하세요.
	 * TODO: 프로세스 종료 메시지를 구현하세요 (project2/process_termination.html을 참고하세요).
	 * TODO: 우리는 여기에서 프로세스 자원 정리를 구현할 것을 권장합니다. */
	
	process_cleanup ();
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
static bool load(const char *file_name, struct intr_frame *if_) {
    struct thread *t = thread_current();  // 현재 스레드 가져오기
    struct ELF ehdr;  // ELF 헤더 구조체
    struct file *file = NULL;  // 파일 포인터 초기화
    off_t file_ofs;  // 파일 오프셋 (프로그램 헤더를 읽을 때 사용)
    bool success = false;  // 로드 성공 여부 플래그
    int i;

    /* 페이지 디렉터리 할당 및 활성화 */
    t->pml4 = pml4_create();  
		// 새로운 페이지 디렉터리 생성
    if (t->pml4 == NULL)  
		// 페이지 디렉터리 생성 실패 시
        goto done;
    process_activate(thread_current());  
		// 현재 스레드의 페이지 디렉터리 활성화

    /* 실행 파일 열기 */
    file = filesys_open(file_name);  // 실행 파일 열기
    if (file == NULL) {  // 파일을 열지 못한 경우
        printf("load: %s: open failed\n", file_name);  
				// 파일 열기 실패 출력
        goto done;
    }

    /* 실행 파일의 ELF 헤더 읽기 및 검증 */
    if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr  
		// ELF 헤더 크기만큼 읽기 실패
        || memcmp(ehdr.e_ident, "\177ELF\2\1\1", 7)  
				// ELF 마법 숫자 확인 실패
        || ehdr.e_type != 2  
				// 실행 파일 타입 확인 (ET_EXEC)
        || ehdr.e_machine != 0x3E  
				// AMD64 확인
        || ehdr.e_version != 1  
				// 버전 확인
        || ehdr.e_phentsize != sizeof(struct Phdr)  
				// 프로그램 헤더 크기 확인
        || ehdr.e_phnum > 1024) {  // 프로그램 헤더 개수 제한
        printf("load: %s: error loading executable\n", file_name);  
				// 실행 파일 로드 실패
        goto done;
    }

    /* 프로그램 헤더 읽기 */
    file_ofs = ehdr.e_phoff;  // 프로그램 헤더 오프셋 설정
    for (i = 0; i < ehdr.e_phnum; i++) {  // 프로그램 헤더 수만큼 반복
        struct Phdr phdr;

        if (file_ofs < 0 || file_ofs > file_length(file))  
				// 잘못된 파일 오프셋
            goto done;
        file_seek(file, file_ofs);  
				// 파일에서 프로그램 헤더 위치로 이동

        if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)  
				// 프로그램 헤더 읽기 실패
            goto done;
        file_ofs += sizeof phdr;  
				// 파일 오프셋을 다음 프로그램 헤더로 이동

        switch (phdr.p_type) {  
					// 프로그램 헤더 타입에 따라 처리
            case PT_NULL:  
						// 무시할 세그먼트
            case PT_NOTE:
            case PT_PHDR:
            case PT_STACK:
            default:
                break;
            case PT_DYNAMIC:  
						// 지원되지 않는 세그먼트
            case PT_INTERP:
            case PT_SHLIB:
                goto done;
            case PT_LOAD:  
						// 로드할 세그먼트인 경우
                if (validate_segment(&phdr, file)) {  
									// 세그먼트 유효성 검사
                    bool writable = (phdr.p_flags & PF_W) != 0;  
										// 세그먼트가 쓰기 가능한지 확인
                    uint64_t file_page = phdr.p_offset & ~PGMASK;  
										// 파일 페이지 시작 주소
                    uint64_t mem_page = phdr.p_vaddr & ~PGMASK;  
										// 메모리 페이지 시작 주소
                    uint64_t page_offset = phdr.p_vaddr & PGMASK;  
										// 페이지 내 오프셋
                    uint32_t read_bytes, zero_bytes;

                    if (phdr.p_filesz > 0) {  
											// 파일 크기가 0보다 큰 경우
                        /* 파일에서 읽을 부분과 나머지 메모리 부분을 0으로 채움 */
                        read_bytes = page_offset + phdr.p_filesz;  
												// 파일에서 읽을 바이트 수
                        zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);  // 0으로 채울 바이트 수
                    } else {  // 파일 크기가 0인 경우
                        /* 전체 메모리 부분을 0으로 채움 */
                        read_bytes = 0;
                        zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
                    }

                    if (!load_segment(file, file_page, (void *) mem_page, read_bytes, zero_bytes, writable))  // 세그먼트 로드 실패
                        goto done;
                } else
                    goto done;
                break;
        }
    }

    /* 스택 설정 */
    if (!setup_stack(if_))  // 스택 설정 실패
        goto done;

    /* 프로그램의 시작 주소 설정 (엔트리 포인트) */
    if_->rip = ehdr.e_entry;  // 프로그램의 엔트리 포인트를 설정

    success = true;  // 로드 성공

done:
    /* 파일을 닫고 성공/실패 여부 반환 */
    file_close(file);
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
