/***************************************************
*		版权声明
*
*	本操作系统名为：MINE
*	该操作系统未经授权不得以盈利或非盈利为目的进行开发，
*	只允许个人学习以及公开交流使用
*
*	代码最终所有权及解释权归田宇所有；
*
*	本模块作者：	田宇
*	EMail:		345538255@qq.com
*
*
***************************************************/

#include <schedule.h>
#include "waitqueue.h"
#include "task.h"
#include "lib.h"
#include "assert.h"

void wait_queue_init(wait_queue_T * wait_queue, struct task_struct *tsk) {
	init_list_head(&wait_queue->wait_list);
	wait_queue->tsk = tsk;
}

void sleep_on(wait_queue_T * wait_queue_head) {
	wait_queue_T wait;
	wait_queue_init(&wait, current);
	current->state = TASK_UNINTERRUPTIBLE;
	list_add_tail(&wait.wait_list, &wait_queue_head->wait_list);

	schedule();
}

void interruptible_sleep_on(wait_queue_T *wait_queue_head) {
	wait_queue_T wait;
	wait_queue_init(&wait, current);
	current->state = TASK_INTERRUPTIBLE;
	list_add_tail(&wait.wait_list, &wait_queue_head->wait_list);

	schedule();
}

void wakeup(wait_queue_T * wait_queue_head, long state) {
	wait_queue_T * wait = NULL;

	if (list_empty(&wait_queue_head->wait_list))
		return;

	wait = list_next_entry(wait_queue_head, wait_list);

	if (wait->tsk->state & state) {
		list_del(&wait->wait_list);
		wakeup_process(wait->tsk);
	}
}


