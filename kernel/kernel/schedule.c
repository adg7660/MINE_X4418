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

#include "schedule.h"
#include "task.h"
#include "lib.h"
#include "printk.h"
#include "timer.h"
#include "smp.h"

struct schedule task_schedule[NR_CPUS];

struct task_struct *get_next_task()
{
	struct task_struct * tsk = NULL;

	if(list_empty(&task_schedule[SMP_cpu_id()].task_queue.list))
	{
		return init_task[SMP_cpu_id()];
	}

	tsk = list_next_entry(&task_schedule[SMP_cpu_id()].task_queue, list);
	list_del(&tsk->list);

	task_schedule[SMP_cpu_id()].running_task_count -= 1;

	return tsk;
}

void insert_task_queue(struct task_struct *tsk)
{
	struct task_struct *tmp = NULL;

	if(tsk == init_task[SMP_cpu_id()])
		return ;

	tmp = list_next_entry(&task_schedule[SMP_cpu_id()].task_queue, list);

	if(list_empty(&task_schedule[SMP_cpu_id()].task_queue.list)){
	}else{
		while(tmp->vrun_time < tsk->vrun_time)
			tmp = list_next_entry(tmp, list);
	}

	list_add_tail(&tsk->list, &tmp->list);
	task_schedule[SMP_cpu_id()].running_task_count += 1;
}

void schedule()
{
	struct task_struct *tsk = NULL;
	long cpu_id = SMP_cpu_id();

	cli();
	current->flags &= ~NEED_SCHEDULE;
	tsk = get_next_task();

	//color_printk(RED,BLACK,"#schedule:current(pid=%d vrun_time=%d pointer=%X),tsk(pid=%d vrun_time=%d pointer=%X)\n", current->pid, current->vrun_time, current, tsk->pid, tsk->vrun_time, tsk);

	if(!task_schedule[cpu_id].CPU_exec_task_jiffies)
		switch(tsk->priority)
		{
			case 0:
			case 1:
				task_schedule[cpu_id].CPU_exec_task_jiffies = 4/task_schedule[cpu_id].running_task_count;
				break;
			case 2:
			default:
				task_schedule[cpu_id].CPU_exec_task_jiffies = 4/task_schedule[cpu_id].running_task_count*3;
				break;
		}
	if (current->vrun_time >= tsk->vrun_time || current->state != TASK_RUNNING) {
		if(current->state == TASK_RUNNING)
			insert_task_queue(current);
		//printf("[");
		//color_printk(GREEN,BLACK,"PC=%X, SP=%X, current PGD=%X, tsk PGD=%X", tsk->cpu_context.pc, tsk->cpu_context.sp, current->mm->pgd, tsk->mm->pgd);
		switch_mm(current, tsk);
		switch_to(current,tsk);
		//printf("]\n");
	} else {
		insert_task_queue(tsk);
	}
	sti();
}

void schedule_init()
{
	int i = 0;
	memset(&task_schedule,0,sizeof(struct schedule) * NR_CPUS);

	for(i = 0; i < NR_CPUS; i++)
	{
		init_list_head(&task_schedule[i].task_queue.list);
		task_schedule[i].task_queue.vrun_time = 0x7fffffff;

		task_schedule[i].running_task_count = 1;
		task_schedule[i].CPU_exec_task_jiffies = 4;
	}
	//TODO：initcall和schedule_init的执行顺序确定
	init_mm.pgd = (pgd_t *)mmu_get_tlb_base_addr();
	current = init_task[0];
	current->mm = &init_mm;
	wait_queue_init(&init_task_union.task.wait_childexit, NULL);
	init_list_head(&init_task_union.task.list);
	init_task_union.task.preempt_count = 0;
	init_task_union.task.state = TASK_RUNNING;
}
