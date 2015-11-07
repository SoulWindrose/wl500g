/*
 *  linux/fs/proc/array.c
 *
 *  Copyright (C) 1992  by Linus Torvalds
 *  based on ideas by Darren Senn
 *
 * Fixes:
 * Michael. K. Johnson: stat,statm extensions.
 *                      <johnsonm@stolaf.edu>
 *
 * Pauline Middelink :  Made cmdline,envline only break at '\0's, to
 *                      make sure SET_PROCTITLE works. Also removed
 *                      bad '!' which forced address recalculation for
 *                      EVERY character on the current page.
 *                      <middelin@polyware.iaf.nl>
 *
 * Danny ter Haar    :	added cpuinfo
 *			<dth@cistron.nl>
 *
 * Alessandro Rubini :  profile extension.
 *                      <rubini@ipvvis.unipv.it>
 *
 * Jeff Tranter      :  added BogoMips field to cpuinfo
 *                      <Jeff_Tranter@Mitel.COM>
 *
 * Bruno Haible      :  remove 4K limit for the maps file
 *			<haible@ma2s2.mathematik.uni-karlsruhe.de>
 *
 * Yves Arrouye      :  remove removal of trailing spaces in get_array.
 *			<Yves.Arrouye@marin.fdn.fr>
 *
 * Jerome Forissier  :  added per-CPU time information to /proc/stat
 *                      and /proc/<pid>/cpu extension
 *                      <forissier@isia.cma.fr>
 *			- Incorporation and non-SMP safe operation
 *			of forissier patch in 2.1.78 by
 *			Hans Marcus <crowbar@concepts.nl>
 *
 * aeb@cwi.nl        :  /proc/partitions
 *
 *
 * Alan Cox	     :  security fixes.
 *			<Alan.Cox@linux.org>
 *
 * Al Viro           :  safe handling of mm_struct
 *
 * Gerhard Wichert   :  added BIGMEM support
 * Siemens AG           <Gerhard.Wichert@pdb.siemens.de>
 *
 * Al Viro & Jeff Garzik :  moved most of the thing into base.c and
 *			 :  proc_misc.c. The rest may eventually go into
 *			 :  base.c too.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <linux/mman.h>
#include <linux/proc_fs.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/signal.h>
#include <linux/highmem.h>
#include <linux/file.h>
#include <linux/times.h>
#include <linux/cpuset.h>
#include <linux/rcupdate.h>
#include <linux/delayacct.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/processor.h>
#include "internal.h"

/* Gcc optimizes away "strlen(x)" for constant x */
#define ADDBUF(buffer, string) \
do { memcpy(buffer, string, strlen(string)); \
     buffer += strlen(string); } while (0)

static inline char * task_name(struct task_struct *p, char * buf)
{
	int i;
	char * name;
	char tcomm[sizeof(p->comm)];

	get_task_comm(tcomm, p);

	ADDBUF(buf, "Name:\t");
	name = tcomm;
	i = sizeof(tcomm);
	do {
		unsigned char c = *name;
		name++;
		i--;
		*buf = c;
		if (!c)
			break;
		if (c == '\\') {
			buf[1] = c;
			buf += 2;
			continue;
		}
		if (c == '\n') {
			buf[0] = '\\';
			buf[1] = 'n';
			buf += 2;
			continue;
		}
		buf++;
	} while (i);
	*buf = '\n';
	return buf+1;
}

/*
 * The task state array is a strange "bitmap" of
 * reasons to sleep. Thus "running" is zero, and
 * you can test for combinations of others with
 * simple bit tests.
 */
static const char * const task_state_array[] = {
	"R (running)",		/*   0 */
	"S (sleeping)",		/*   1 */
	"D (disk sleep)",	/*   2 */
	"T (stopped)",		/*   4 */
	"t (tracing stop)",	/*   8 */
	"Z (zombie)",		/*  16 */
	"X (dead)",		/*  32 */
};

static inline const char *get_task_state(struct task_struct *tsk)
{
	unsigned int state = (tsk->state | tsk->exit_state) & TASK_REPORT;

	BUILD_BUG_ON(1 + ilog2(TASK_REPORT) != ARRAY_SIZE(task_state_array)-1);

	return task_state_array[fls(state)];
}

static inline char * task_state(struct task_struct *p, char *buffer)
{
	struct group_info *group_info;
	int g;
	struct fdtable *fdt = NULL;

	rcu_read_lock();
	buffer += sprintf(buffer,
		"State:\t%s\n"
		"SleepAVG:\t%lu%%\n"
		"Tgid:\t%d\n"
		"Pid:\t%d\n"
		"PPid:\t%d\n"
		"TracerPid:\t%d\n"
		"Uid:\t%d\t%d\t%d\t%d\n"
		"Gid:\t%d\t%d\t%d\t%d\n",
		get_task_state(p),
		(p->sleep_avg/1024)*100/(1020000000/1024),
	       	p->tgid, p->pid,
	       	pid_alive(p) ? rcu_dereference(p->real_parent)->tgid : 0,
		pid_alive(p) && p->ptrace ? rcu_dereference(p->parent)->pid : 0,
		p->uid, p->euid, p->suid, p->fsuid,
		p->gid, p->egid, p->sgid, p->fsgid);

	task_lock(p);
	if (p->files)
		fdt = files_fdtable(p->files);
	buffer += sprintf(buffer,
		"FDSize:\t%d\n"
		"Groups:\t",
		fdt ? fdt->max_fds : 0);
	rcu_read_unlock();

	group_info = p->group_info;
	get_group_info(group_info);
	task_unlock(p);

	for (g = 0; g < min(group_info->ngroups,NGROUPS_SMALL); g++)
		buffer += sprintf(buffer, "%d ", GROUP_AT(group_info,g));
	put_group_info(group_info);

	buffer += sprintf(buffer, "\n");
	return buffer;
}

static char * render_sigset_t(const char *header, sigset_t *set, char *buffer)
{
	int i, len;

	len = strlen(header);
	memcpy(buffer, header, len);
	buffer += len;

	i = _NSIG;
	do {
		int x = 0;

		i -= 4;
		if (sigismember(set, i+1)) x |= 1;
		if (sigismember(set, i+2)) x |= 2;
		if (sigismember(set, i+3)) x |= 4;
		if (sigismember(set, i+4)) x |= 8;
		*buffer++ = (x < 10 ? '0' : 'a' - 10) + x;
	} while (i >= 4);

	*buffer++ = '\n';
	*buffer = 0;
	return buffer;
}

static void collect_sigign_sigcatch(struct task_struct *p, sigset_t *ign,
				    sigset_t *catch)
{
	struct k_sigaction *k;
	int i;

	k = p->sighand->action;
	for (i = 1; i <= _NSIG; ++i, ++k) {
		if (k->sa.sa_handler == SIG_IGN)
			sigaddset(ign, i);
		else if (k->sa.sa_handler != SIG_DFL)
			sigaddset(catch, i);
	}
}

static inline char * task_sig(struct task_struct *p, char *buffer)
{
	unsigned long flags;
	sigset_t pending, shpending, blocked, ignored, caught;
	int num_threads = 0;
	unsigned long qsize = 0;
	unsigned long qlim = 0;

	sigemptyset(&pending);
	sigemptyset(&shpending);
	sigemptyset(&blocked);
	sigemptyset(&ignored);
	sigemptyset(&caught);

	rcu_read_lock();
	if (lock_task_sighand(p, &flags)) {
		pending = p->pending.signal;
		shpending = p->signal->shared_pending.signal;
		blocked = p->blocked;
		collect_sigign_sigcatch(p, &ignored, &caught);
		num_threads = atomic_read(&p->signal->count);
		qsize = atomic_read(&p->user->sigpending);
		qlim = p->signal->rlim[RLIMIT_SIGPENDING].rlim_cur;
		unlock_task_sighand(p, &flags);
	}
	rcu_read_unlock();

	buffer += sprintf(buffer, "Threads:\t%d\n", num_threads);
	buffer += sprintf(buffer, "SigQ:\t%lu/%lu\n", qsize, qlim);

	/* render them all */
	buffer = render_sigset_t("SigPnd:\t", &pending, buffer);
	buffer = render_sigset_t("ShdPnd:\t", &shpending, buffer);
	buffer = render_sigset_t("SigBlk:\t", &blocked, buffer);
	buffer = render_sigset_t("SigIgn:\t", &ignored, buffer);
	buffer = render_sigset_t("SigCgt:\t", &caught, buffer);

	return buffer;
}

static inline char *task_cap(struct task_struct *p, char *buffer)
{
    return buffer + sprintf(buffer, "CapInh:\t%016x\n"
			    "CapPrm:\t%016x\n"
			    "CapEff:\t%016x\n",
			    cap_t(p->cap_inheritable),
			    cap_t(p->cap_permitted),
			    cap_t(p->cap_effective));
}

int proc_pid_status(struct task_struct *task, char * buffer)
{
	char * orig = buffer;
	struct mm_struct *mm = get_task_mm(task);

	buffer = task_name(task, buffer);
	buffer = task_state(task, buffer);
 
	if (mm) {
		buffer = task_mem(mm, buffer);
		mmput(mm);
	}
	buffer = task_sig(task, buffer);
	buffer = task_cap(task, buffer);
	buffer = cpuset_task_status_allowed(task, buffer);
#if defined(CONFIG_S390)
	buffer = task_show_regs(task, buffer);
#endif
	return buffer - orig;
}

static int do_task_stat(struct task_struct *task, char * buffer, int whole)
{
	unsigned long vsize, eip, esp, wchan = ~0UL;
	long priority, nice;
	int tty_pgrp = -1, tty_nr = 0;
	sigset_t sigign, sigcatch;
	char state;
	int res;
 	pid_t ppid = 0, pgid = -1, sid = -1;
	int num_threads = 0;
	struct mm_struct *mm;
	unsigned long long start_time;
	unsigned long cmin_flt = 0, cmaj_flt = 0;
	unsigned long  min_flt = 0,  maj_flt = 0;
	cputime_t cutime, cstime, utime, stime;
	unsigned long rsslim = 0;
	char tcomm[sizeof(task->comm)];
	unsigned long flags;

	state = *get_task_state(task);
	vsize = eip = esp = 0;
	mm = get_task_mm(task);
	if (mm) {
		vsize = task_vsize(mm);
		eip = KSTK_EIP(task);
		esp = KSTK_ESP(task);
	}

	get_task_comm(tcomm, task);

	sigemptyset(&sigign);
	sigemptyset(&sigcatch);
	cutime = cstime = utime = stime = cputime_zero;

	rcu_read_lock();
	if (lock_task_sighand(task, &flags)) {
		struct signal_struct *sig = task->signal;

		if (sig->tty) {
			tty_pgrp = pid_nr(sig->tty->pgrp);
			tty_nr = new_encode_dev(tty_devnum(sig->tty));
		}

		num_threads = atomic_read(&sig->count);
		collect_sigign_sigcatch(task, &sigign, &sigcatch);

		cmin_flt = sig->cmin_flt;
		cmaj_flt = sig->cmaj_flt;
		cutime = sig->cutime;
		cstime = sig->cstime;
		rsslim = sig->rlim[RLIMIT_RSS].rlim_cur;

		/* add up live thread stats at the group level */
		if (whole) {
			struct task_struct *t = task;
			do {
				min_flt += t->min_flt;
				maj_flt += t->maj_flt;
				utime = cputime_add(utime, t->utime);
				stime = cputime_add(stime, t->stime);
				t = next_thread(t);
			} while (t != task);

			min_flt += sig->min_flt;
			maj_flt += sig->maj_flt;
			utime = cputime_add(utime, sig->utime);
			stime = cputime_add(stime, sig->stime);
		}

		sid = signal_session(sig);
		pgid = process_group(task);
		ppid = rcu_dereference(task->real_parent)->tgid;

		unlock_task_sighand(task, &flags);
	}
	rcu_read_unlock();

	if (!whole || num_threads<2)
		wchan = get_wchan(task);
	if (!whole) {
		min_flt = task->min_flt;
		maj_flt = task->maj_flt;
		utime = task->utime;
		stime = task->stime;
	}

	/* scale priority and nice values from timeslices to -20..20 */
	/* to make it look like a "normal" Unix priority/nice value  */
	priority = task_prio(task);
	nice = task_nice(task);

	/* Temporary variable needed for gcc-2.96 */
	/* convert timespec -> nsec*/
	start_time = (unsigned long long)task->start_time.tv_sec * NSEC_PER_SEC
				+ task->start_time.tv_nsec;
	/* convert nsec -> ticks */
	start_time = nsec_to_clock_t(start_time);

	res = sprintf(buffer,"%d (%s) %c %d %d %d %d %d %u %lu \
%lu %lu %lu %lu %lu %ld %ld %ld %ld %d 0 %llu %lu %ld %lu %lu %lu %lu %lu \
%lu %lu %lu %lu %lu %lu %lu %lu %d %d %u %u %llu\n",
		task->pid,
		tcomm,
		state,
		ppid,
		pgid,
		sid,
		tty_nr,
		tty_pgrp,
		task->flags,
		min_flt,
		cmin_flt,
		maj_flt,
		cmaj_flt,
		cputime_to_clock_t(utime),
		cputime_to_clock_t(stime),
		cputime_to_clock_t(cutime),
		cputime_to_clock_t(cstime),
		priority,
		nice,
		num_threads,
		start_time,
		vsize,
		mm ? get_mm_rss(mm) : 0,
	        rsslim,
		mm ? mm->start_code : 0,
		mm ? mm->end_code : 0,
		mm ? mm->start_stack : 0,
		esp,
		eip,
		/* The signal information here is obsolete.
		 * It must be decimal for Linux 2.0 compatibility.
		 * Use /proc/#/status for real-time signals.
		 */
		task->pending.signal.sig[0] & 0x7fffffffUL,
		task->blocked.sig[0] & 0x7fffffffUL,
		sigign      .sig[0] & 0x7fffffffUL,
		sigcatch    .sig[0] & 0x7fffffffUL,
		wchan,
		0UL,
		0UL,
		task->exit_signal,
		task_cpu(task),
		task->rt_priority,
		task->policy,
		(unsigned long long)delayacct_blkio_ticks(task));
	if(mm)
		mmput(mm);
	return res;
}

int proc_tid_stat(struct task_struct *task, char * buffer)
{
	return do_task_stat(task, buffer, 0);
}

int proc_tgid_stat(struct task_struct *task, char * buffer)
{
	return do_task_stat(task, buffer, 1);
}

int proc_pid_statm(struct task_struct *task, char *buffer)
{
	int size = 0, resident = 0, shared = 0, text = 0, lib = 0, data = 0;
	struct mm_struct *mm = get_task_mm(task);
	
	if (mm) {
		size = task_statm(mm, &shared, &text, &data, &resident);
		mmput(mm);
	}

	return sprintf(buffer,"%d %d %d %d %d %d %d\n",
		       size, resident, shared, text, lib, data, 0);
}
