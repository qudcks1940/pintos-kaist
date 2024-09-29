/* Checks that when the alarm clock wakes up threads, the
   higher-priority threads run first. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static thread_func alarm_priority_thread;
static int64_t wake_time;
static struct semaphore wait_sema;

void
test_alarm_priority (void) 
{
  int i;
  
  /* 이 테스트는 MLFQS(Multi-Level Feedback Queue Scheduler) 모드에서는 동작하지 않음. */
  ASSERT (!thread_mlfqs);  // MLFQS가 활성화되지 않았는지 확인

  /* 5초 후 깨어날 시점을 설정 */
  wake_time = timer_ticks () + 5 * TIMER_FREQ;  // 현재 시점 + 5초 후 시간을 wake_time에 저장
  sema_init (&wait_sema, 0);  
  // wait_sema를 0으로 초기화하여 스레드들이 대기하도록 설정

  /* 우선순위가 다른 10개의 스레드 생성 */
  for (i = 0; i < 10; i++) 
    {
      /* 우선순위 계산: 기본 우선순위에서 (i+5)%10을 빼고 1을 더 뺀 값 */
      int priority = PRI_DEFAULT - (i + 5) % 10 - 1;
      char name[16];
      
      /* 우선순위를 이름에 포함한 스레드 이름 생성 */
      snprintf (name, sizeof name, "priority %d", priority);
      
      /* 우선순위와 이름을 가지고 alarm_priority_thread 함수가 실행되는 스레드 생성 */
      thread_create (name, priority, alarm_priority_thread, NULL);
    }

  /* 현재 실행 중인 스레드의 우선순위를 최저로 설정하여 다른 스레드들이 우선 실행되도록 함 */
  thread_set_priority (PRI_MIN);  

  /* 10개의 스레드가 모두 깨어날 때까지 기다림 (sema_down을 통해 대기) */
  for (i = 0; i < 10; i++)
    sema_down (&wait_sema);  // 모든 스레드가 sema_up을 호출할 때까지 대기
}

static void
alarm_priority_thread (void *aux UNUSED) 
{
  /* 타이머가 틱(tick)하는 시점까지 바쁘게 대기 */
  int64_t start_time = timer_ticks ();  // 스레드가 시작되는 시점 저장
  while (timer_elapsed (start_time) == 0)  // 최소 1틱이 지나도록 대기
    continue;  // 바쁘게 기다림 (busy-wait)

  /* 현재 시간이 바뀌었으니, race condition을 걱정하지 않고 timer_sleep 호출 가능 */
  timer_sleep (wake_time - timer_ticks ());  // 설정된 시간이 될 때까지 대기 (sleep)

  /* 스레드가 깨어난 후 메시지 출력 */
  msg ("Thread %s woke up.", thread_name ());  // 스레드 이름과 함께 깨어났음을 알림

  /* wait_sema 세마포어를 증가시켜 대기 중인 메인 스레드가 깨어나게 함 */
  sema_up (&wait_sema);  // 메인 스레드에서 sema_down으로 대기 중인 스레드를 깨움
}
