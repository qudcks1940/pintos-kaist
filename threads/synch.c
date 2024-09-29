/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
/* 이 파일은 Nachos 교육용 운영체제의 소스 코드에서 파생되었습니다.
   Nachos의 저작권 고지는 아래에 완전히 복제되어 있습니다. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   이 소프트웨어 및 그 문서를 사용, 복사, 수정, 배포할 수 있는 권한을 
   명시된 요건을 충족하는 한, 요금 없이 부여합니다. */

/* 세마포어 초기화 함수: 주어진 값을 사용하여 SEMA를 새로운 세마포어로 초기화합니다. */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);  // 세마포어가 NULL이 아닌지 확인

	sema->value = value;     // 세마포어의 값을 설정
	list_init (&sema->waiters);  // 대기 스레드 목록 초기화
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
/* 세마포어의 "다운(P)" 연산: SEMA의 값이 0이 될 때까지 기다린 후, 원자적으로 값을 감소시킵니다.
   이 함수는 대기할 수 있으므로, 인터럽트 핸들러에서 호출해서는 안 됩니다. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);  // 세마포어가 NULL이 아닌지 확인
	ASSERT (!intr_context ());  // 인터럽트 컨텍스트에서 호출되지 않았는지 확인

	old_level = intr_disable ();  // 인터럽트 비활성화
	while (sema->value == 0) {  // 세마포어 값이 0이면
		list_insert_ordered (&sema->waiters, &thread_current()->elem, priority_greater_func, NULL);
      // list_push_back (&sema->waiters, &thread_current ()->elem);  
      // 현재 스레드를 대기 리스트에 추가
		thread_block ();  // 스레드를 블록
	}
	sema->value--;  // 세마포어 값 감소
	intr_set_level (old_level);  // 이전 인터럽트 상태 복원
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */

/* 세마포어의 "다운(P)" 연산: 세마포어가 이미 0이 아닌 경우에만 값을 감소시키고,
   0이면 기다리지 않고 false를 반환합니다. */	 
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);	// 세마포어가 NULL이 아닌지 확인

	old_level = intr_disable ();	// 인터럽트 비활성화
	if (sema->value > 0)			// 세마포어 값이 0보다 크면
	{
		sema->value--;		// 값을 감소시키고
		success = true;		// 성공 반환
	}
	else
		success = false;	// 값이 0이면 실패 반환
	intr_set_level (old_level);		// 이전 인터럽트 상태 복원

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
/* 세마포어의 "업(V)" 연산: SEMA의 값을 증가시키고, 대기 중인 스레드가 있으면 그 중 하나를 깨웁니다. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);		// 세마포어가 NULL이 아닌지 확인

	old_level = intr_disable ();		// 인터럽트 비활성화
	if (!list_empty (&sema->waiters)){     // 대기 중인 스레드가 있으면
   
   }		

	sema->value++;				// 세마포어 값 증가
   
   intr_set_level (old_level);		// 이전 인터럽트 상태 복원
   
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
/* 세마포어를 사용하는 자기 테스트 함수 */
void
sema_self_test (void) {
	struct semaphore sema[2];			// 두 개의 세마포어 생성
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);			// 첫 번째 세마포어를 0으로 초기화
	sema_init (&sema[1], 0);			// 두 번째 세마포어를 0으로 초기화
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);// 테스트 스레드 생성
	for (i = 0; i < 10; i++)  // 반복
	{
		sema_up (&sema[0]);  // 첫 번째 세마포어 업
		sema_down (&sema[1]);  // 두 번째 세마포어 다운
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
/* 세마포어 테스트에 사용되는 스레드 함수 */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)  // 10번 반복
	{
		sema_down (&sema[0]);  // 첫 번째 세마포어 다운
		sema_up (&sema[1]);  // 두 번째 세마포어 업
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
/* 락 초기화 함수: LOCK을 새로운 락으로 초기화합니다. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);  // 락이 NULL이 아닌지 확인

	lock->holder = NULL;  // 락 소유자를 초기화
	sema_init (&lock->semaphore, 1);  // 세마포어로 락 초기화 (값 1)
}


/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
/* 락을 획득하는 함수: LOCK을 현재 스레드가 소유하게 만듭니다. 필요하다면 대기합니다. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);  // 락이 NULL이 아닌지 확인
	ASSERT (!intr_context ());  // 인터럽트 컨텍스트가 아닌지 확인
	ASSERT (!lock_held_by_current_thread (lock));  // 현재 스레드가 이미 락을 소유하고 있지 않은지 확인

	sema_down (&lock->semaphore);  // 세마포어 다운
	lock->holder = thread_current ();  // 락 소유자를 현재 스레드로 설정
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
/* 락을 시도해서 획득하는 함수: 성공하면 true, 실패하면 false를 반환합니다. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);  // 락이 NULL이 아닌지 확인
	ASSERT (!lock_held_by_current_thread (lock));  // 현재 스레드가 락을 소유하고 있지 않은지 확인

	success = sema_try_down (&lock->semaphore);  // 세마포어 시도해서 다운
	if (success)
		lock->holder = thread_current ();  // 성공하면 락 소유자를 현재 스레드로 설정
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
/* 락 해제 함수: 현재 스레드가 소유한 락을 해제합니다. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);  // 락이 NULL이 아닌지 확인
	ASSERT (lock_held_by_current_thread (lock));  // 현재 스레드가 락을 소유하고 있는지 확인

	lock->holder = NULL;  // 락 소유자 해제
	sema_up (&lock->semaphore);  // 세마포어 업
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
/* 현재 스레드가 LOCK을 소유하고 있는지 확인하는 함수 */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);  // 락이 NULL이 아닌지 확인

	return lock->holder == thread_current ();  // 락의 소유자가 현재 스레드인지 반환
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
/* 조건 변수를 초기화하는 함수 */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);  // 조건 변수가 NULL이 아닌지 확인

	list_init (&cond->waiters);  // 조건 변수 대기 스레드 리스트 초기화
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
/* 조건 변수 대기 함수: LOCK을 해제하고 COND가 신호를 보낼 때까지 대기한 후 
LOCK을 다시 획득합니다. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);  // 조건 변수가 NULL이 아닌지 확인
	ASSERT (lock != NULL);  // 락이 NULL이 아닌지 확인
	ASSERT (!intr_context ());  // 인터럽트 컨텍스트가 아닌지 확인
	ASSERT (lock_held_by_current_thread (lock));  // 현재 스레드가 락을 소유하고 있는지 확인

	sema_init (&waiter.semaphore, 0);  // 세마포어를 0으로 초기화
	list_push_back (&cond->waiters, &waiter.elem);  // 대기 리스트에 추가
	lock_release (lock);  // 락 해제
	sema_down (&waiter.semaphore);  // 세마포어 다운 (대기)
	lock_acquire (lock);  // 락 다시 획득
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
/* 조건 변수가 신호를 보내는 함수: 대기 중인 스레드가 있으면 하나를 깨웁니다. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);  // 조건 변수가 NULL이 아닌지 확인
	ASSERT (lock != NULL);  // 락이 NULL이 아닌지 확인
	ASSERT (!intr_context ());  // 인터럽트 컨텍스트가 아닌지 확인
	ASSERT (lock_held_by_current_thread (lock));  // 현재 스레드가 락을 소유하고 있는지 확인

	if (!list_empty (&cond->waiters))  // 대기 스레드가 있으면
		sema_up (&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);  // 첫 번째 대기 스레드 깨움
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
/* 조건 변수에 있는 모든 대기 스레드를 깨우는 함수 */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);  // 조건 변수가 NULL이 아닌지 확인
	ASSERT (lock != NULL);  // 락이 NULL이 아닌지 확인

	while (!list_empty (&cond->waiters))  // 대기 스레드가 있는 동안
		cond_signal (cond, lock);  // 하나씩 깨움
}
