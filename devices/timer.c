#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;
static struct list sleep_list;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
void
timer_init (void) {
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);
	list_init(&sleep_list); // sleeplist 초기화.

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
	   still less than one timer tick. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();  // 1. 인터럽트를 비활성화하고 현재 인터럽트 상태를 저장
	int64_t t = ticks;                            // 2. 전역 변수 `ticks`의 값을 읽음 (현재까지 경과된 틱)
	intr_set_level (old_level);                   // 3. 이전 인터럽트 상태로 복원
	barrier ();                                   // 4. 컴파일러 최적화 방지
	return t;                                     // 5. 읽어온 틱 값을 반환
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}


/* Suspends execution for approximately TICKS timer ticks. */
// timer_sleep이 해야하는 행동
// 스레드를 실제로 잠재우고, 
// 그동안 CPU는 다른 작업을 처리하거나 유휴 상태로 대기하도록 구현

// 대기 큐를 사용하여 스레드 잠자게 하고,
// 타이머 인터럽트가 발생하면 스레드를 다시 실행 대기 상태로 만들기
void timer_sleep (int64_t ticks) {
		// sleep하는 도중에 intr 올까봐.
		struct thread *this_thread = thread_current();

		int64_t start = timer_ticks ();			// 1. 현재 시간을 가져옴
		// timer_sleep 함수를 호출한 스레드를 차단 상태로 만들어 잠들게 한다.
		// 차단된 스레드는 타이머가 해당 스레드를 깨울 때까지 대기 리스트에서 유지
    ASSERT (intr_get_level () == INTR_ON);
		enum intr_level old_level = intr_disable ();
		// 인터럽트가 활성화되어 있는지 확인
		this_thread->wake_ticks = start + ticks;
		list_push_back(&sleep_list, &this_thread->elem); // 원본을 변경하기 위해서 실제 데이터가 있는 메모리 주소로 들어가서 변경한다.
		thread_block();
		intr_set_level (old_level);  // 인터럽트 복원.

}

/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
    ticks++;  // 시스템 틱을 증가시킴

    struct list_elem *sleep_ptr = list_begin(&sleep_list);  // sleep_list의 첫 번째 요소를 가져옴

    while (sleep_ptr != list_end(&sleep_list)) {  // 리스트의 끝까지 순회
        struct thread *t = list_entry(sleep_ptr, struct thread, elem);  // list_elem을 thread 구조체로 변환

        // 현재 ticks가 스레드의 wake_ticks 이상이면 스레드를 깨움
        if (ticks >= t->wake_ticks) {
            sleep_ptr = list_remove(sleep_ptr);  // 리스트에서 제거하고 다음 요소로 이동
            thread_unblock(t);  // 스레드를 준비 상태로 전환
        } else {
            sleep_ptr = list_next(sleep_ptr);  // 다음 요소로 이동
        }
    }

    thread_tick();  // 현재 스레드의 tick을 업데이트
}

/* Suspends execution for approximately MS milliseconds. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}




/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) {
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait (loops);

	/* If the tick count changed, we iterated too long. */
	barrier ();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* We're waiting for at least one full timer tick.  Use
		   timer_sleep() because it will yield the CPU to other
		   processes. */
		timer_sleep (ticks);
	} else {
		/* Otherwise, use a busy-wait loop for more accurate
		   sub-tick timing.  We scale the numerator and denominator
		   down by 1000 to avoid the possibility of overflow. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}