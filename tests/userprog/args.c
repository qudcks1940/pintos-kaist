/* Prints the command-line arguments.
   This program is used for all of the args-* tests.  Grading is
   done differently for each of the args-* tests based on the
   output. */
/* 명령줄 인자(command-line arguments)를 출력함.
   이 프로그램은 모든 args-* 테스트에서 사용됨.
   각 args-* 테스트는 출력에 따라 다르게 채점됨. */

#include "tests/lib.h"

int
main (int argc, char *argv[]) 
{
  int i;

  test_name = "args";  // 테스트 이름을 "args"로 설정, 테스트 결과를 구분하기 위함.

  // 스택 및 argv 포인터가 64비트 시스템에서 8바이트 정렬을 준수하는지 확인
  if (((unsigned long long) argv & 7) != 0)
    msg ("argv and stack must be word-aligned, actually %p", argv);  // 정렬되지 않았을 경우 경고 메시지 출력

  msg ("begin");  // 테스트 시작 메시지 출력
  msg ("argc = %d", argc);  // 명령줄 인자 개수(argc)를 출력

  // argv 배열에 담긴 명령줄 인자들을 출력
  for (i = 0; i <= argc; i++)
    if (argv[i] != NULL)  // argv[i]가 NULL이 아니면 해당 인자 값을 출력
      msg ("argv[%d] = '%s'", i, argv[i]);
    else  // argv[i]가 NULL이면 null로 출력
      msg ("argv[%d] = null", i);

  msg ("end");  // 테스트 종료 메시지 출력

  return 0;  // 프로그램 정상 종료
}

