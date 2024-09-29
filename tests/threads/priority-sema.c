/* Tests that the highest-priority thread waiting on a semaphore
   is the first to wake up. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static thread_func priority_sema_thread;
static struct semaphore sema;

/* 우선순위 기반 세마포어 테스트를 수행하는 함수 */
void
test_priority_sema (void) 
{
  int i;
  
  /* MLFQS(멀티레벨 피드백 큐 스케줄러)에서는 동작하지 않음 */
  ASSERT (!thread_mlfqs);

  /* 세마포어 초기화, 초기값은 0 (스레드들이 대기 상태에 머물게 함) */
  sema_init (&sema, 0);
  
  /* 메인 스레드의 우선순위를 최소값(PRI_MIN)으로 설정 */
  thread_set_priority (PRI_MIN);
  
  /* 10개의 스레드 생성, 각 스레드에게 우선순위를 부여하고, 우선순위 기반 세마포어 테스트 함수 실행 */
  for (i = 0; i < 10; i++) 
    {
      int priority = PRI_DEFAULT - (i + 3) % 10 - 1;  // 각 스레드의 우선순위 계산
      char name[16];
      snprintf (name, sizeof name, "priority %d", priority);  // 스레드 이름 생성
      thread_create (name, priority, priority_sema_thread, NULL);  // 새로운 스레드 생성 및 실행
    }

  /* 세마포어를 순차적으로 올려서 스레드들이 차례로 깨어나도록 함 */
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema);  // 대기 중인 스레드 하나를 깨움
      msg ("Back in main thread.");  // 메인 스레드로 돌아왔음을 알리는 메시지 출력
    }
}

/* 각 스레드가 실행하는 함수 */
static void
priority_sema_thread (void *aux UNUSED) 
{
  /* 세마포어 대기, 메인 스레드에서 sema_up이 호출될 때까지 대기 상태 */
  sema_down (&sema);

  /* 스레드가 깨어나면 메시지 출력 */
  msg ("Thread %s woke up.", thread_name ());
}

