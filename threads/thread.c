#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. 
	 현재 실행 중인 코드를 스레드로 변환하여 스레딩 시스템을 초기화합니다. 
	 이 작업은 일반적인 경우에는 불가능하지만, 
	 여기서는 loader.S가 스택의 하단을 페이지 경계에 맞춰 설정했기 때문에 가능합니다.
	실행 대기열(run queue)과 tid 락도 초기화됩니다.
	이 함수를 호출한 후에는 **page allocator(페이지 할당기)**를 초기화한 후에만
	**thread_create()**로 스레드를 생성해야 합니다.
	이 함수가 완료될 때까지는 **thread_current()**를 호출하는 것이 안전하지 않습니다.*/
void
thread_init (void) {
    ASSERT (intr_get_level () == INTR_OFF);  // 인터럽트가 비활성화 상태인지 확인

    /* 커널을 위한 임시 GDT를 재로드합니다.
     * 이 GDT에는 사용자 컨텍스트가 포함되지 않습니다.
     * 커널은 나중에 gdt_init()에서 사용자 컨텍스트를 포함한 GDT를 다시 설정합니다. */
    struct desc_ptr gdt_ds = {
        .size = sizeof (gdt) - 1,
        .address = (uint64_t) gdt
    };
    lgdt (&gdt_ds);  // GDT 레지스터를 설정

    /* 글로벌 스레드 컨텍스트를 초기화합니다. */
    // 여기서 lock을 걸어주지 않으면, 같은 번호를 가진 스레드가 여러 개 생길 수 있습니다.
    // 따라서 스레드 식별자(tid)를 할당할 때 충돌을 방지하기 위해 락을 걸어줍니다.
    lock_init (&tid_lock);  // 스레드 식별자 할당을 보호하는 락 초기화
    list_init (&ready_list);  // 준비 상태의 스레드 리스트 초기화
    list_init (&destruction_req);  // 삭제 대기 스레드 리스트 초기화

    /* 현재 실행 중인 스레드를 위한 스레드 구조체를 설정합니다. */
    initial_thread = running_thread ();  // 현재 실행 중인 스레드를 가져옴
    init_thread (initial_thread, "main", PRI_DEFAULT);  // 스레드를 초기화
    initial_thread->status = THREAD_RUNNING;  // 스레드 상태를 실행 중으로 설정
    initial_thread->tid = allocate_tid ();  // 스레드 식별자(tid)를 할당
}


/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

	The code provided sets the new thread's `priority' member to
	PRIORITY, but no actual priority scheduling is implemented.
	Priority scheduling is the goal of Problem 1-3. 
	이 코드는 새로운 커널 스레드를 생성하는 thread_create 함수입니다. 
	이름, 우선순위, 실행할 함수와 그 함수의 인자를 받아 새로운 스레드를 생성하고 
	ready list에 추가하는 역할을 합니다.

	스레드를 생성한 후에는 그 스레드가 바로 실행될 수도 있고,
	생성한 스레드가 바로 종료될 수도 있습니다. 
	또한, 우선순위 스케줄링이 구현되어 있지 않기 때문에 
	현재는 우선순위 값만 저장되고 실제 스케줄링에는 영향을 미치지 않습니다. 
	우선순위 스케줄링은 나중에 구현해야 하는 문제입니다.*/

tid_t
thread_create (const char *name, int priority, 
               thread_func *function, void *aux) {
    struct thread *t;  // 새로 생성할 스레드의 구조체 포인터
    tid_t tid;         // 스레드 식별자 (thread ID)

    ASSERT (function != NULL);  // 함수 포인터가 NULL이면 안 되므로 검증

    /* 스레드 구조체 할당 */
    t = palloc_get_page (PAL_ZERO);  // 새로운 페이지 할당 및 초기화
    if (t == NULL)  // 메모리 할당 실패 시
        return TID_ERROR;  // 에러 반환

    /* 스레드 초기화 */
    init_thread (t, name, priority);  // 이름과 우선순위를 지정하여 스레드 초기화
    tid = t->tid = allocate_tid ();  // 새로운 스레드 ID 할당

    /* 스레드를 실행할 커널 함수 설정 */
    // rdi 레지스터에 실행할 함수 주소 (function)를, rsi 레지스터에 인자 (aux)를 전달
    t->tf.rip = (uintptr_t) kernel_thread;  // 스레드가 실행될 함수 지정
    t->tf.R.rdi = (uint64_t) function;      // 스레드에 전달할 첫 번째 인자
    t->tf.R.rsi = (uint64_t) aux;           // 스레드에 전달할 두 번째 인자
    // 데이터 세그먼트, 코드 세그먼트 등 설정
    t->tf.ds = SEL_KDSEG;  // 데이터 세그먼트 셀렉터 설정
    t->tf.es = SEL_KDSEG;  // 데이터 세그먼트 셀렉터 설정
    t->tf.ss = SEL_KDSEG;  // 스택 세그먼트 셀렉터 설정
    t->tf.cs = SEL_KCSEG;  // 코드 세그먼트 셀렉터 설정
    t->tf.eflags = FLAG_IF;  // 인터럽트 플래그 활성화

    /* 스레드를 ready list에 추가 */
    thread_unblock (t);  // 스레드를 ready 상태로 만들어 실행 대기열에 추가
		// 스레드 만들때 레디리스트 들어갈 일이 여러번 된다면 그땐?
    return tid;  // 생성된 스레드의 ID 반환
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. 
	 이 코드는 thread_unblock() 함수로, 
	 blocked 상태에 있는 스레드를 ready-to-run 상태로 전환하는 역할을 합니다. 
	 주로 스레드가 자원 대기 등으로 인해 blocked 상태에 있다가 
	 다시 실행 가능한 상태로 전환될 때 호출됩니다.

	이 함수는 현재 실행 중인 스레드를 선점하지 않습니다. 
	즉, **thread_unblock()**을 호출한 즉시 스레드가 실행되는 것이 아니라, 
	해당 스레드는 ready list에 추가되어 실행 대기 상태로 바뀝니다. 
	인터럽트가 비활성화된 상태에서 실행되며, 
	이후 다시 인터럽트 상태를 원래대로 복구합니다.
	 
	 */
// void
// thread_unblock (struct thread *t) {
//     enum intr_level old_level;  // 이전 인터럽트 상태를 저장할 변수
// 		struct thread *curr = thread_current();

//     ASSERT (is_thread (t));  // 스레드 t가 유효한 스레드인지 확인
//     ASSERT (t->status == THREAD_BLOCKED);  // 스레드 t가 BLOCKED 상태인지 확인

//     /* 인터럽트 비활성화 */
//     old_level = intr_disable ();  // 인터럽트를 비활성화하여 원자성을 보장
//     /* ready list에 스레드를 추가 */
// 		list_insert_ordered(&ready_list, &t->elem, priority_greater_func, NULL);
// 		// list_push_back (&ready_list, &t->elem);  // 스레드를 ready list에 추가
//     t->status = THREAD_READY;  // 스레드 상태를 READY로 변경
//     /* 인터럽트 원래 상태로 복구 */
//     intr_set_level (old_level);  // 이전 인터럽트 상태로 복구
		
// }

void
thread_unblock (struct thread *t) {

	enum intr_level old_level; 	// 인터럽트 상태를 저장하는 변수. 
	// 함수 내부에서 인터럽트를 비활성화한 후 완료되면 원래 상태로 복원합니다.

	ASSERT (is_thread (t));

	ASSERT (t->status == THREAD_BLOCKED);
	
	old_level = intr_disable ();
	list_insert_ordered (&ready_list, &t->elem, priority_greater_func, NULL);
	t->status = THREAD_READY;

	intr_set_level (old_level);

	/* ready_list가 비어 있지 않고, 첫 번째 스레드의 우선순위가 현재 스레드보다 높으면 양보 */
	if (!list_empty(&ready_list)) {
			struct thread *first_t = list_entry(list_begin(&ready_list), struct thread, elem);
			
			/* 첫 번째 스레드의 우선순위가 현재 스레드보다 높으면 양보 */
			if (first_t->priority > thread_get_priority()) {
					/* 현재 스레드가 idle 스레드가 아닌 경우에만 양보 */
					if (thread_current() != idle_thread) {
							thread_yield();
					}
			}
	}
}

// alarm-priority 해결을 위해 추가한 코드 
bool priority_greater_func (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
    struct thread *thread_a = list_entry(a, struct thread, elem);  // a에서 스레드 구조체 가져오기
    struct thread *thread_b = list_entry(b, struct thread, elem);  // b에서 스레드 구조체 가져오기

    // 우선순위가 더 높은 스레드가 먼저 오도록 비교
    return thread_a->priority > thread_b->priority;
}



/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread * thread_current (void) {
    struct thread *t = running_thread(); // 현재 실행 중인 스레드의 주소를 받음

		/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
    ASSERT (is_thread (t));              // t가 올바른 스레드인지 확인
    ASSERT (t->status == THREAD_RUNNING);// 스레드 상태가 RUNNING인지 확인

    return t;                            // 현재 실행 중인 스레드의 주소를 반환
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		// list_push_back (&ready_list, &curr->elem);
		/* ready list에 스레드를 추가 */
		list_insert_ordered(&ready_list, &curr->elem, priority_greater_func, NULL);
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	// struct thread *first_t = list_entry(list_front(&ready_list), struct thread, elem);
	thread_current ()->priority = new_priority;
	if (list_entry(list_begin(&ready_list), struct thread, elem)->priority > thread_get_priority()) {
		thread_yield();
	}
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			// 이 부분에서 
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}
