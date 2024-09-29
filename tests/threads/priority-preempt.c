/* Ensures that a high-priority thread really preempts.

   Based on a test originally submitted for Stanford's CS 140 in
   winter 1999 by by Matt Franklin
   <startled@leland.stanford.edu>, Greg Hutchins
   <gmh@leland.stanford.edu>, Yu Ping Hu <yph@cs.stanford.edu>.
   Modified by arens. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

static thread_func simple_thread_func;

/* 우선순위 선점을 테스트하는 함수 */
void
test_priority_preempt (void) 
{
  /* MLFQS(멀티레벨 피드백 큐 스케줄러)에서는 동작하지 않음 */
  ASSERT (!thread_mlfqs);

  /* 현재 스레드의 우선순위가 기본 우선순위(PRI_DEFAULT)인지 확인 */
  ASSERT (thread_get_priority () == PRI_DEFAULT);

  /* 기본 우선순위보다 1 높은 우선순위를 가진 스레드를 생성 */
  thread_create ("high-priority", PRI_DEFAULT + 1, simple_thread_func, NULL);
  
  /* 메시지 출력: high-priority 스레드는 더 높은 우선순위이기 때문에, 
     이 시점에서 이미 실행이 완료되었어야 한다는 메시지를 출력 */
  msg ("The high-priority thread should have already completed.");
}

static void 
simple_thread_func (void *aux UNUSED) 
{
  int i;
  
  /* 스레드가 5번 반복하여 메시지를 출력하고, 매번 CPU 점유권을 양보(thread_yield) */
  for (i = 0; i < 5; i++) 
    {
      /* 현재 스레드의 이름과 반복 횟수를 출력 */
      msg ("Thread %s iteration %d", thread_name (), i);
      
      /* 다른 스레드에게 CPU 점유권을 양보 */
      thread_yield ();
    }

  /* 모든 반복 작업이 끝난 후, 해당 스레드의 완료 메시지 출력 */
  msg ("Thread %s done!", thread_name ());
}

