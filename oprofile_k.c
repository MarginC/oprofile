#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/delay.h> 
#include <linux/sched.h> 
#include <linux/unistd.h>
#include <linux/mman.h> 
#include <linux/file.h> 

#include <asm/io.h>
 
#include "oprofile.h"
 
/* stuff we have to do ourselves */

#define APIC_DEFAULT_PHYS_BASE 0xfee00000
 
/* FIXME: not up to date */
static void set_pte_phys(unsigned long vaddr, unsigned long phys)
{
	pgprot_t prot;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	pgd = pgd_offset_k(vaddr);
	pmd = pmd_offset(pgd, vaddr);
	pte = pte_offset(pmd, vaddr);
	prot = PAGE_KERNEL;
	if (boot_cpu_data.x86_capability & X86_FEATURE_PGE)
		pgprot_val(prot) |= _PAGE_GLOBAL;
	set_pte(pte, mk_pte_phys(phys, prot));
	__flush_tlb_one(vaddr);
}

void my_set_fixmap(void)
{
	unsigned long address = __fix_to_virt(FIX_APIC_BASE);

	set_pte_phys (address,APIC_DEFAULT_PHYS_BASE);
}

/* poor man's do_nmi() */ 
 
static void mem_parity_error(unsigned char reason, struct pt_regs * regs)
{
	printk("oprofile: Uhhuh. NMI received. Dazed and confused, but trying to continue\n");
	printk("You probably have a hardware problem with your RAM chips\n");

	/* Clear and disable the memory parity error line. */
	reason = (reason & 0xf) | 4;
	outb(reason, 0x61);
}

static void io_check_error(unsigned char reason, struct pt_regs * regs)
{
	unsigned long i;

	printk("oprofile: NMI: IOCK error (debug interrupt?)\n");
	/* Can't show registers */
 
	/* Re-enable the IOCK line, wait for a few seconds */
	reason = (reason & 0xf) | 8;
	outb(reason, 0x61);
	i = 2000;
	while (--i) udelay(1000);
	reason &= ~8;
	outb(reason, 0x61);
}

static void unknown_nmi_error(unsigned char reason, struct pt_regs * regs)
{
	/* No MicroChannelA check */
 
	printk("oprofile: Uhhuh. NMI received for unknown reason %02x.\n", reason);
	printk("Maybe you need to boot with nmi_watchdog=0\n"); 
	printk("Dazed and confused, but trying to continue\n");
	printk("Do you have a strange power saving mode enabled?\n");
}

asmlinkage void my_do_nmi(struct pt_regs * regs, long error_code)
{
	unsigned char reason = inb(0x61);

	/* can't count this NMI */
 
	if (!(reason & 0xc0)) {
		/* FIXME: couldn't we do something with irq_stat ?
		   we'd have to risk deadlock on console_lock ...
		   how does do_nmi() panic the machine with do_exit() -
		   neither !pid or in_interrupt() is necessarily true */ 
		unknown_nmi_error(reason, regs);
		return;
	}
	if (reason & 0x80)
		mem_parity_error(reason, regs);
	if (reason & 0x40)
		io_check_error(reason, regs);
	/*
	 * Reassert NMI in case it became active meanwhile
	 * as it's edge-triggered.
	 */
	outb(0x8f, 0x70);
	inb(0x71);		/* dummy */
	outb(0x0f, 0x70);
	inb(0x71);		/* dummy */
}

/* map reading stuff */

/* we're ok to fill up one entry (8 bytes) in the buffer.
 * Any more (i.e. all mapping info) goes in the map buffer;
 * the daemon knows how many entries to read depending on
 * the value in the main buffer entry. FIXME: all entries
 * go into CPU#0 buffer. This is a big problem in SMP !
 *
 * We also have plenty of races involving out-of-date samples
 * loitering in the hash table. There seems to be no nice
 * way round it though, so we forget about it. These can happen
 * on fork()/exec() pair, or when executable sections are
 * remapped
 */
    
/* each entry is 16-byte aligned */
static struct op_map map_buf[OP_MAX_MAP_BUF];
static unsigned long nextmapbuf; 
static unsigned int map_open; 
 
void oprof_out8(void *buf);
 
static int (*old_sys_fork)(struct pt_regs);
static int (*old_sys_vfork)(struct pt_regs);
static int (*old_sys_clone)(struct pt_regs);
static int (*old_sys_execve)(struct pt_regs);
static long (*old_sys_mmap2)(unsigned long, unsigned long, unsigned long,
	unsigned long, unsigned long, unsigned long);
static long (*old_sys_init_module)(const char *, struct module *); 
static long (*old_sys_exit)(int);
 
inline static void oprof_report_fork(u16 oldpid, pid_t newpid)
{
	struct op_sample samp; 

	samp.count = OP_FORK;
	samp.pid = oldpid;
	samp.eip = newpid;
	oprof_out8(&samp);
}
 
static int my_sys_fork(struct pt_regs regs)
{
	u16 pid = (u16)current->pid;
	int ret;
 
	ret = old_sys_fork(regs);
	if (ret)
		oprof_report_fork(pid,ret);
	return ret;
}

static int my_sys_vfork(struct pt_regs regs)
{
	u16 pid = (u16)current->pid;
	int ret;
 
	ret = old_sys_vfork(regs);
	if (ret)
		oprof_report_fork(pid,ret); 
	return ret;
}

static int my_sys_clone(struct pt_regs regs)
{
	u16 pid = (u16)current->pid;
	int ret;
 
	ret = old_sys_clone(regs);
	if (ret)
		oprof_report_fork(pid,ret); 
	return ret;
}
 
int oprof_map_open(void)
{
	if (map_open)
		return -EBUSY;

	map_open=1;
	return 0; 
}

int oprof_map_release(void)
{
	if (!map_open)
		return -EFAULT;

	map_open=0;
	return 0;
}

int oprof_map_read(char *buf, size_t count, loff_t *ppos)
{
	ssize_t max;
	char *data = (char *)map_buf; 

	max = sizeof(struct op_map)*OP_MAX_MAP_BUF;
 
	if (!count)
		return 0;
 
	if (*ppos + count > max)
		return -EINVAL;

	data += *ppos;
 
	if (copy_to_user(buf,data,count))
		return -EFAULT;
	
	*ppos += count;

	/* wrap around */ 
	if (*ppos==max)
		*ppos = 0; 

	return count;
} 
 
#define map_round(v) (((v)+(sizeof(struct op_map)/2))/sizeof(struct op_map)) 
 
/* buf must be PAGE_SIZE bytes large */
static int oprof_output_map(unsigned long addr, unsigned long len,
	unsigned long pgoff, struct file *file, char *buf)
{
	char *str = ((char *)&map_buf[nextmapbuf])+12; 
	char *start = (char *)map_buf; 
	char *line;
	unsigned long sizemap;
 
	if (!file)
		return 0;
 
	map_buf[nextmapbuf].addr = addr;
	map_buf[nextmapbuf].len = len;
	map_buf[nextmapbuf].offset = pgoff<<PAGE_SHIFT;
	memset(str,0,4);
 
	line = d_path(file->f_dentry, file->f_vfsmnt, buf, PAGE_SIZE);
	printk("line %s\n",line);
	printk("size %u\n",strlen(line)); 

	if (str+strlen(line)+1 >= (OP_MAX_MAP_BUF*sizeof(struct op_map))+start) {
		printk(KERN_ERR "oprofile: exceeded map buffer !!!\n");
		return 0;
	}

	strncpy(str,line,strlen(line));
	*(str+strlen(line)) = '\0';

	sizemap = (unsigned long)(map_round(strlen(line)+1+12));

	if (nextmapbuf+sizemap >= OP_MAX_MAP_BUF) {
		printk(KERN_ERR "oprofile: nextmapbuf,sizemap %lu,%lu !!!\n",nextmapbuf,sizemap); 
		return 0;
	}

	nextmapbuf += sizemap;
	return sizemap;
}

static int oprof_output_maps(struct task_struct *task)
{
	int sizemap=0;
	char *buffer;
	struct mm_struct *mm;
	struct vm_area_struct *map;

	buffer = (char *) __get_free_page(GFP_KERNEL);
	if (!buffer)
		return 0;
 
	task_lock(task);
	if ((mm=task->mm))
		atomic_inc(&task->mm->mm_users);
	task_unlock(task);
 
	if (!mm)
		goto out; 

	down(&mm->mmap_sem);
 
	for (map=mm->mmap; map; map=map->vm_next) {
		if (!(map->vm_flags&VM_EXEC))
			continue;

		sizemap += oprof_output_map(map->vm_start, map->vm_end-map->vm_start,
				map->vm_pgoff, map->vm_file, buffer);
	}

	up(&mm->mmap_sem);
 
	mmput(mm);
out:
	free_page((unsigned long)buffer);
	return sizemap;
}
 
static int my_sys_execve(struct pt_regs regs)
{
	struct task_struct *task = current;
	int ret;
 
	ret = old_sys_execve(regs);
	if (ret >= 0) {
		struct op_sample samp;

		samp.count = OP_DROP;
		samp.pid = task->pid;
		/* how many bytes to read from buffer */
		samp.eip = oprof_output_maps(task);
		oprof_out8(&samp);
	}
	return ret;
}

static int my_sys_mmap2(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags,
	unsigned long fd, unsigned long pgoff)
{
	int ret;

 
	ret = old_sys_mmap2(addr,len,prot,flags,fd,pgoff);

	if ((prot&PROT_EXEC) && ret >= 0) {
		struct op_sample samp;
		struct file *file;
		char *buffer;

		buffer = (char *) __get_free_page(GFP_KERNEL);
		if (!buffer)
			return ret;

		file = fget(fd);
		if (!file) {
			free_page((unsigned long)buffer);
			return ret;
		}

		samp.count = OP_MAP;
		samp.pid = current->pid;
		/* how many bytes to read from buffer */
		samp.eip = oprof_output_map(ret,len,pgoff,file,buffer);
		fput(file);
		free_page((unsigned long)buffer);
		oprof_out8(&samp);
	}
	return ret;
}

static long my_sys_init_module(const char *name_user, struct module *mod_user)
{
	long ret; 
	ret = old_sys_init_module(name_user, mod_user);
	
	if (ret >= 0) {
		struct op_sample samp;

		samp.count = OP_DROP_MODULES;
		samp.pid = 0;
		samp.eip = 0;
		oprof_out8(&samp);
	}
	return ret;
}

static long my_sys_exit(int error_code)
{
	struct op_sample samp;

	samp.count = OP_EXIT;
	samp.pid = current->pid;
	samp.eip = 0;
	oprof_out8(&samp);
 
	return old_sys_exit(error_code);
}
 
extern void *sys_call_table[];
 
void __init op_intercept_syscalls(void)
{
	old_sys_fork = sys_call_table[__NR_fork]; 
	old_sys_vfork = sys_call_table[__NR_vfork];
	old_sys_clone = sys_call_table[__NR_clone];
	old_sys_execve = sys_call_table[__NR_execve];
	old_sys_mmap2 = sys_call_table[__NR_mmap2];
	old_sys_init_module = sys_call_table[__NR_init_module];
	old_sys_exit = sys_call_table[__NR_exit]; 

	sys_call_table[__NR_fork] = my_sys_fork;
	sys_call_table[__NR_vfork] = my_sys_vfork;
	sys_call_table[__NR_clone] = my_sys_clone;
	sys_call_table[__NR_execve] = my_sys_execve;
	sys_call_table[__NR_mmap2] = my_sys_mmap2; 
	sys_call_table[__NR_init_module] = my_sys_init_module;
	sys_call_table[__NR_exit] = my_sys_exit;
}

void __exit op_replace_syscalls(void)
{
	sys_call_table[__NR_fork] = old_sys_fork;
	sys_call_table[__NR_vfork] = old_sys_vfork;
	sys_call_table[__NR_clone] = old_sys_clone;
	sys_call_table[__NR_execve] = old_sys_execve;
	sys_call_table[__NR_mmap2] = old_sys_mmap2; 
	sys_call_table[__NR_init_module] = old_sys_init_module;
	sys_call_table[__NR_exit] = old_sys_exit;
}
