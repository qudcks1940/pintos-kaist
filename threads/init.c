#include "threads/init.h"
#include <console.h>
#include <debug.h>
#include <limits.h>
#include <random.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "devices/kbd.h"
#include "devices/input.h"
#include "devices/serial.h"
#include "devices/timer.h"
#include "devices/vga.h"
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/loader.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "threads/thread.h"
#ifdef USERPROG
#include "userprog/process.h"
#include "userprog/exception.h"
#include "userprog/gdt.h"
#include "userprog/syscall.h"
#include "userprog/tss.h"
#endif
#include "tests/threads/tests.h"
#ifdef VM
#include "vm/vm.h"
#endif
#ifdef FILESYS
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "filesys/fsutil.h"
#endif

/* Page-map-level-4 with kernel mappings only. */
uint64_t *base_pml4;

#ifdef FILESYS
/* -f: Format the file system? */
static bool format_filesys;
#endif

/* -q: Power off after kernel tasks complete? */
bool power_off_when_done;

bool thread_tests;

static void bss_init (void);
static void paging_init (uint64_t mem_end);

static char **read_command_line (void);
static char **parse_options (char **argv);
static void run_actions (char **argv);
static void usage (void);

static void print_stats (void);


int main (void) NO_RETURN;

/* Pintos main program. */
int
main (void) {
	uint64_t mem_end;
	char **argv;

	/* Clear BSS and get machine's RAM size. */
	bss_init ();

	/* Break command line into arguments and parse options. */
	argv = read_command_line ();
	argv = parse_options (argv);

	/* Initialize ourselves as a thread so we can use locks,
	   then enable console locking. */
	thread_init ();
	// main 스레드 생성
	console_init ();

	/* Initialize memory system. */
	mem_end = palloc_init ();
	malloc_init ();
	paging_init (mem_end);

#ifdef USERPROG
	tss_init ();
	gdt_init ();
#endif

	/* Initialize interrupt handlers. */
	intr_init ();
	timer_init ();
	kbd_init ();
	input_init ();
#ifdef USERPROG
	exception_init ();
	syscall_init ();
#endif
	/* Start thread scheduler and enable interrupts. */
	thread_start ();
	serial_init_queue ();
	timer_calibrate ();

#ifdef FILESYS
	/* Initialize file system. */
	disk_init ();
	filesys_init (format_filesys);
#endif

#ifdef VM
	vm_init ();
#endif

	printf ("Boot complete.\n");

	/* Run actions specified on kernel command line. */
	run_actions (argv);
	// 여기서 argv는 -q 처럼 '-'가 붙어 있는 문자들은 다 떼고
	// 그 다음 문자의 시작 주소가 담겨 있다.
	// pintos -mlfqs -q run 'test-program'
	// 위의 예시 경우에 run 'test-program'만 argv에 담겨 있다는 말임.
	/* Finish up. */
	if (power_off_when_done)
		power_off ();
	thread_exit ();
}

/* Clear BSS */
static void
bss_init (void) {
	/* The "BSS" is a segment that should be initialized to zeros.
	   It isn't actually stored on disk or zeroed by the kernel
	   loader, so we have to zero it ourselves.

	   The start and end of the BSS segment is recorded by the
	   linker as _start_bss and _end_bss.  See kernel.lds. */
	extern char _start_bss, _end_bss;
	memset (&_start_bss, 0, &_end_bss - &_start_bss);
}

/* Populates the page table with the kernel virtual mapping,
 * and then sets up the CPU to use the new page directory.
 * Points base_pml4 to the pml4 it creates. */
static void
paging_init (uint64_t mem_end) {
	uint64_t *pml4, *pte;
	int perm;
	pml4 = base_pml4 = palloc_get_page (PAL_ASSERT | PAL_ZERO);

	extern char start, _end_kernel_text;
	// Maps physical address [0 ~ mem_end] to
	//   [LOADER_KERN_BASE ~ LOADER_KERN_BASE + mem_end].
	for (uint64_t pa = 0; pa < mem_end; pa += PGSIZE) {
		uint64_t va = (uint64_t) ptov(pa);

		perm = PTE_P | PTE_W;
		if ((uint64_t) &start <= va && va < (uint64_t) &_end_kernel_text)
			perm &= ~PTE_W;

		if ((pte = pml4e_walk (pml4, va, 1)) != NULL)
			*pte = pa | perm;
	}

	// reload cr3
	pml4_activate(0);
}

/* Breaks the kernel command line into words and returns them as
   an argv-like array. */
static char **
read_command_line (void) {
	static char *argv[LOADER_ARGS_LEN / 2 + 1];
	char *p, *end;
	int argc;
	int i;

	argc = *(uint32_t *) ptov (LOADER_ARG_CNT);
	p = ptov (LOADER_ARGS);
	end = p + LOADER_ARGS_LEN;
	for (i = 0; i < argc; i++) {
		if (p >= end)
			PANIC ("command line arguments overflow");

		argv[i] = p;
		p += strnlen (p, end - p) + 1;
	}
	argv[argc] = NULL;

	/* Print kernel command line. */
	printf ("Kernel command line:");
	for (i = 0; i < argc; i++)
		if (strchr (argv[i], ' ') == NULL)
			printf (" %s", argv[i]);
		else
			printf (" '%s'", argv[i]);
	printf ("\n");

	return argv;
}

/* Parses options in ARGV[]
   and returns the first non-option argument. */
/* Parses options in ARGV[]
   and returns the first non-option argument.
   ARGV[] 배열에서 명령줄 옵션을 파싱하고, 옵션이 아닌 첫 번째 인자를 반환하는 함수.
   예를 들어, 커맨드 라인 인자가 -mlfqs, -q와 같이 '-'로 시작하는 경우, 이는 옵션으로 간주하고
   해당 옵션에 맞는 동작을 설정한다. 옵션이 아닌 첫 번째 인자를 반환한다.
*/
static char **
parse_options (char **argv) {
	// *argv가 NULL이 아니고, **argv가 '-'로 시작하는 경우 옵션으로 간주하여 반복적으로 처리.
	for (; *argv != NULL && **argv == '-'; argv++) {
		char *save_ptr;  // strtok_r 함수의 상태 정보를 저장할 포인터.
		
		// 현재 argv의 값을 '=' 문자를 기준으로 나누어 옵션 이름과 값을 분리.
		char *name = strtok_r (*argv, "=", &save_ptr);  // '=' 이전의 옵션 이름 추출.
		char *value = strtok_r (NULL, "", &save_ptr);   // '=' 이후의 옵션 값 추출.

		// '-h' 옵션: 도움말을 출력하고 시스템 종료.
		if (!strcmp (name, "-h"))
			usage ();

		// '-q' 옵션: 커널 작업이 끝나면 시스템 종료.
		else if (!strcmp (name, "-q"))
			power_off_when_done = true;  // power_off_when_done 플래그를 true로 설정하여 커널 종료 후 시스템 종료.

#ifdef FILESYS
		// '-f' 옵션: 파일 시스템을 포맷할지 여부 설정.
		else if (!strcmp (name, "-f"))
			format_filesys = true;  // 파일 시스템 포맷 플래그 설정.
#endif

		// '-rs' 옵션: 난수 생성 시드값 설정.
		else if (!strcmp (name, "-rs"))
			random_init (atoi (value));  // 난수 생성기의 시드값을 atoi()를 통해 정수로 변환하여 설정.

		// '-mlfqs' 옵션: 멀티 레벨 피드백 큐 스케줄러 활성화.
		else if (!strcmp (name, "-mlfqs"))
			thread_mlfqs = true;  // 멀티 레벨 피드백 큐 스케줄러를 사용하기 위해 해당 플래그를 true로 설정.

#ifdef USERPROG
		// '-ul' 옵션: 사용자 메모리 페이지 제한 설정.
		else if (!strcmp (name, "-ul"))
			user_page_limit = atoi (value);  // 사용자 페이지 제한 값을 정수로 변환하여 설정.

		// '-threads-tests' 옵션: 스레드 테스트 실행 여부 설정.
		else if (!strcmp (name, "-threads-tests"))
			thread_tests = true;  // 스레드 테스트 모드를 활성화.
#endif

		// 알 수 없는 옵션이 있을 경우, 시스템을 패닉 상태로 만들고 종료.
		else
			PANIC ("unknown option `%s' (use -h for help)", name);
	}

	// 옵션이 아닌 첫 번째 인자를 반환. 예를 들어, 실행할 프로그램의 이름일 수 있음.
	return argv;
}


/* Runs the task specified in ARGV[1].
   ARGV[1]에 있는 작업(프로그램)을 실행하는 함수.
   이 함수는 명령어로 전달된 사용자 프로그램을 실행하는 역할을 한다.
   USERPROG가 정의되어 있는 경우, 사용자 프로그램을 실행하고 대기하며, 그렇지 않으면 테스트 프로그램을 실행한다. */

static void
run_task (char **argv) {
	const char *task = argv[1];  // 실행할 작업(프로그램)의 이름을 argv[1]에서 가져옴

	printf ("Executing '%s':\n", task);  // 실행 중인 작업의 이름 출력

#ifdef USERPROG
	/* USERPROG가 정의된 경우, 사용자 프로그램 실행 모드임 */
	if (thread_tests){  // 스레드 테스트가 활성화되어 있으면 테스트 실행
		run_test (task);  // 주어진 테스트 이름을 실행
	} else {
		/* 사용자 프로그램을 실행하고, 종료될 때까지 대기 */
		process_wait (process_create_initd (task));  
		// process_create_initd는 새로운 프로세스를 생성하고, process_wait는 해당 프로세스가 종료될 때까지 대기
	}
#else
	/* USERPROG가 정의되지 않은 경우, 스레드 테스트 프로그램을 실행 */
	run_test (task);  // 주어진 테스트 이름을 실행
#endif

	printf ("Execution of '%s' complete.\n", task);  // 작업 완료 메시지 출력
}


/* Executes all of the actions specified in ARGV[]
   up to the null pointer sentinel.
   ARGV[] 배열에 있는 명령어들을 실행하고, NULL 포인터를 만날 때까지 계속 실행함.
   ARGV[] 배열에는 사용자가 명령줄에서 입력한 명령어와 그에 따른 인자들이 포함되어 있음. */

static void
run_actions (char **argv) {
	// pintos -mlfqs -q run 'test-program'
	// 위의 예시 경우에 run 'test-program'만 argv에 담겨 있다는 말임.
	/* An action structure, representing an executable command.
	   각각의 액션(명령어)을 정의한 구조체.
	   - name: 액션 이름 (예: "run", "ls" 등).
	   - argc: 인자의 개수 (명령어 자체도 포함하여 계산).
	   - function: 해당 액션을 실행할 함수 포인터.
	*/
	struct action {
		char *name;                       /* Action name. (액션 이름) */
		int argc;                         /* # of args, including action name. (액션 이름을 포함한 인자 개수) */
		void (*function) (char **argv);   /* Function to execute action. (액션을 실행할 함수 포인터) */
	};

	/* Table of supported actions.
	   Pintos에서 지원하는 액션들의 테이블.
	   사용자가 입력한 명령어와 인자를 기반으로, 이 테이블에 있는 액션을 찾아 실행.
	   */
	static const struct action actions[] = {
		{"run", 2, run_task},    // "run" 명령어, 인자 2개 필요, 실행할 함수는 run_task
#ifdef FILESYS
		{"ls", 1, fsutil_ls},    // "ls" 명령어, 인자 1개 필요, 실행할 함수는 fsutil_ls
		{"cat", 2, fsutil_cat},  // "cat" 명령어, 인자 2개 필요, 실행할 함수는 fsutil_cat
		{"rm", 2, fsutil_rm},    // "rm" 명령어, 인자 2개 필요, 실행할 함수는 fsutil_rm
		{"put", 2, fsutil_put},  // "put" 명령어, 인자 2개 필요, 실행할 함수는 fsutil_put
		{"get", 2, fsutil_get},  // "get" 명령어, 인자 2개 필요, 실행할 함수는 fsutil_get
#endif
		{NULL, 0, NULL},         // 끝을 알리는 NULL 값 (액션이 더 이상 없음을 의미)
	};

	// argv 배열에 NULL이 나올 때까지 각 액션을 처리함
	while (*argv != NULL) {
		const struct action *a;  // 실행할 액션을 가리키는 포인터
		int i;

		/* Find action name.
		   현재 argv에 있는 명령어와 일치하는 액션을 테이블에서 찾음.
		   일치하는 액션을 찾지 못하면 PANIC 메시지를 출력하고 종료. */
		for (a = actions; ; a++)
			if (a->name == NULL)  // 액션 이름이 NULL이면 액션을 찾을 수 없다는 의미
				PANIC ("unknown action `%s' (use -h for help)", *argv);
			else if (!strcmp (*argv, a->name))  // argv와 일치하는 액션 이름이 있으면 탈출
				break;

		/* Check for required arguments.
		   액션이 요구하는 인자의 개수만큼 argv에 인자가 존재하는지 확인.
		   부족하면 PANIC 메시지를 출력하고 종료. */
		for (i = 1; i < a->argc; i++)
			if (argv[i] == NULL)  // 인자가 부족하면 오류 발생
				PANIC ("action `%s' requires %d argument(s)", *argv, a->argc - 1);

		/* Invoke action and advance.
		   해당 액션을 실행하고, argv를 다음 액션 위치로 이동.
		   함수 포인터인 a->function을 호출하여 액션을 수행. */
		a->function (argv);  // 액션 실행 (argv 배열을 인자로 전달)
		argv += a->argc;     // argv 포인터를 다음 명령어 위치로 이동
	}
}

/* Prints a kernel command line help message and powers off the
   machine. */
static void
usage (void) {
	printf ("\nCommand line syntax: [OPTION...] [ACTION...]\n"
			"Options must precede actions.\n"
			"Actions are executed in the order specified.\n"
			"\nAvailable actions:\n"
#ifdef USERPROG
			"  run 'PROG [ARG...]' Run PROG and wait for it to complete.\n"
#else
			"  run TEST           Run TEST.\n"
#endif
#ifdef FILESYS
			"  ls                 List files in the root directory.\n"
			"  cat FILE           Print FILE to the console.\n"
			"  rm FILE            Delete FILE.\n"
			"Use these actions indirectly via `pintos' -g and -p options:\n"
			"  put FILE           Put FILE into file system from scratch disk.\n"
			"  get FILE           Get FILE from file system into scratch disk.\n"
#endif
			"\nOptions:\n"
			"  -h                 Print this help message and power off.\n"
			"  -q                 Power off VM after actions or on panic.\n"
			"  -f                 Format file system disk during startup.\n"
			"  -rs=SEED           Set random number seed to SEED.\n"
			"  -mlfqs             Use multi-level feedback queue scheduler.\n"
#ifdef USERPROG
			"  -ul=COUNT          Limit user memory to COUNT pages.\n"
#endif
			);
	power_off ();
}


/* Powers down the machine we're running on,
   as long as we're running on Bochs or QEMU. */
void
power_off (void) {
#ifdef FILESYS
	filesys_done ();
#endif

	print_stats ();

	printf ("Powering off...\n");
	outw (0x604, 0x2000);               /* Poweroff command for qemu */
	for (;;);
}

/* Print statistics about Pintos execution. */
static void
print_stats (void) {
	timer_print_stats ();
	thread_print_stats ();
#ifdef FILESYS
	disk_print_stats ();
#endif
	console_print_stats ();
	kbd_print_stats ();
#ifdef USERPROG
	exception_print_stats ();
#endif
}
