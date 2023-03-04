/*
Name: Justin Sadler
Date: 26-02-2023
Description: Source code file for linux kernel module

Sources:
	https://perma.cc/5YFF-QT9T (Basic module example)
	https://perma.cc/2FE5-44J3 (Timers in Linux Kernels)
	https://perma.cc/9MH2-3UPW (/proc filesystem) 
	https://www.oreilly.com/library/view/linux-device-drivers/0596000081/ch06s05.html (Kernel timers)
	https://olegkutkov.me/2018/03/14/simple-linux-character-device-driver/ (Character device drivers
	https://www.cs.bham.ac.uk/~exr/teaching/lectures/systems/08_09/docs/kernelAPI/x1820.html (Kernel API)
	https://linuxconfig.org/introduction-to-the-linux-kernel-log-levels (Log levels in Linux Kernel)
	Chapter 5 in Linux Device Drivers 3rd edition (LDD3) for concurrency management (semaphores and atomic variables)
	https://www.kernel.org/doc/html/v4.10/core-api/atomic_ops.html (semantics of atomic operations)
	https://stackoverflow.com/questions/10885685/jiffies-how-to-calculate-seconds-elapsed (Calculating elapsed time with jiffies)
	Chapter 11 in LDD3 for linked lists
	https://medium.com/@414apache/kernel-data-structures-linkedlist-b13e4f8de4bf (Linked list semantics)
	https://www.oreilly.com/library/view/linux-device-drivers/0596000081/ch10s05.html (Linked list semantics)
	https://stackoverflow.com/questions/8547332/efficient-way-to-find-task-struct-by-pid
	https://stackoverflow.com/questions/36581893/killing-a-userspace-program-from-a-kernel-module
*/
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h> // printk
#include <linux/slab.h> /* kmalloc() */
#include <linux/fs.h> /* everything... */
#include <linux/errno.h> /* error codes */
#include <linux/types.h> /* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h> /* O_ACCMODE */
#include <asm/system_misc.h> /* cli(), *_flags */
#include <linux/uaccess.h>
#include <asm/uaccess.h> /* copy_from/to_user */
#include <linux/sched.h> // timer, and current
#include <linux/jiffies.h> // HZ
#include <linux/rwsem.h> // Read/Write semaphores
// From fortune example
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/seq_file.h>
// From fasync_example
#include <asm/system_misc.h> /* cli(), *_flags */
// MISC
#include <asm/atomic.h> // Atomic variables
#include <linux/list.h> // Linked list
#include <linux/sched/signal.h>


#define DEBUG (0)
#define MAJOR_NO (61)
#define BUFFER_CAPACITY (256)
#define TIMER_LIMIT (2)

#if DEBUG
#	define D(x) x
#else
#	define D(x)
#endif

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Module for kernel timers");
MODULE_AUTHOR("Justin Sadler");


/****************** MODULE FUNCTIONS ********************/

//Declaration of mytimer.c functions
static int mytimer_init(void);
static void mytimer_exit(void);
static ssize_t mytimer_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
static int mytimer_open(struct inode *inode, struct file *filp);
static int mytimer_release(struct inode *inode, struct file *filp);
static int mytimer_fasync(int fd, struct file *filp, int mode);

// Declaration of helper functions
static void registerTimer(unsigned int seconds, const char * const msg);
static void changeMaxTimer(unsigned int timer_count); // Change the number of timers supported
static void removeTimers(void); // Change the number of timers supported
static void timer_handler(struct timer_list *); // Exit function for kernel timers


/* Declaration of the init and exit functions */
module_init(mytimer_init);
module_exit(mytimer_exit);

// PROC_FS Functions
static int mytimer_proc_open(struct inode*, struct file*);
static int mytimer_proc_show(struct seq_file*, void*);


/****************** Data Structures ********************/
/* Structure that declares the usual file */
/* access functions */
struct file_operations mytimer_fops = {
	write: mytimer_write,
	open: mytimer_open,
	release: mytimer_release,
	fasync: mytimer_fasync
};


struct file_operations mytimer_proc_fops = {
    .owner = THIS_MODULE,
    .open = mytimer_proc_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};


//
struct mytimer_t {
	char msg[128+1]; // message associated with timer
	char comm[128]; // command name that registered that timer
	unsigned int pid; // PID of process registering timer
	struct timer_list ktimer; // Pointer to kernel timer
	struct list_head list_node; // Linked list node
};



///////////// Global variables  ////////`
// Module variables
static char * buffer;
static const int mytimer_major = 61;
static unsigned long start_jiffies;
// Proc variables
static struct proc_dir_entry * proc_entry;
static char * proc_buffer;

// Semaphore/mutexes
static struct rw_semaphore * buffer_semaphore;

// Timer variables
static struct list_head mytimer_list;
atomic_t num_timers;
atomic_t max_timers;

// Fasync
struct fasync_struct *async_queue; /* structure for keeping track of asynchronous readers */

static int mytimer_init(void) {
	/* Registering device */

	int result = register_chrdev(mytimer_major, "mytimer", &mytimer_fops);
	if (result < 0)
	{
		printk(KERN_ALERT
			"mytimer: cannot obtain major number %d\n", mytimer_major);
		return result;
	}

	/* Allocating timers*/
	INIT_LIST_HEAD(&mytimer_list);

	// Set max timers to 1
	atomic_set(&max_timers, 1);
	// Set number of timers to 0
	atomic_set(&num_timers, 0);
	
	// Allocating buffer
	buffer = kmalloc(BUFFER_CAPACITY, GFP_KERNEL); 
	/* Initializing semaphore*/
	buffer_semaphore = kmalloc(sizeof(struct rw_semaphore), GFP_KERNEL);

	// buffer for /proc file
	proc_buffer = (char*) vmalloc(PAGE_SIZE);

	// Create proc entry
	proc_entry = proc_create("mytimer", 0644, NULL, &mytimer_proc_fops);

	if(!(buffer && buffer_semaphore && proc_buffer && proc_entry)) {
		printk(KERN_ALERT "Insufficient kernel memory\n"); 
		result = -ENOMEM;
		goto fail; 
	}

	init_rwsem(buffer_semaphore);

	// Set proc entry to 0
	memset(proc_buffer, 0, PAGE_SIZE);


	// Set number jiffies at insertion of module
	start_jiffies = jiffies;

	printk(KERN_ALERT "Inserting mytimer module\n"); 

	return 0;

fail: 
	mytimer_exit(); 
	return result;
}

static void mytimer_exit(void) {

	struct list_head * ptr;
	struct list_head * next;
	struct mytimer_t * timer_entry;

	D(printk(KERN_DEBUG "Exitting\n"));
	unregister_chrdev(mytimer_major, "mytimer");
	/* Freeing buffer memory */
	if (buffer)
	{
		kfree(buffer);
	}

	//  Clean up semaphore
	if(buffer_semaphore) {
		kfree(buffer_semaphore);
	}

	// Deallocate timers
	list_for_each_safe(ptr, next, &mytimer_list) {
		 timer_entry = list_entry(ptr, struct mytimer_t, list_node);
		 if(timer_entry) {
			del_timer_sync(&timer_entry->ktimer);
			kfree(timer_entry);
		 }
		 list_del(ptr);
	}
	
	if(proc_buffer) {
		vfree(proc_buffer);
	}

	if(proc_entry) {
		remove_proc_entry("mytimer", NULL);
	}

	printk(KERN_ALERT "Removing mytimer module\n");
}

static int mytimer_open(struct inode *inode, struct file *filp)
{
	D(printk(KERN_DEBUG "open called: process id %d, command %s\n",
		current->pid, current->comm));
	/* Success */
	return 0;
}

static int mytimer_release(struct inode *inode, struct file *filp)
{
	D(printk(KERN_DEBUG "release called: process id %d, command %s\n",
		current->pid, current->comm));

	// remove this filp from the aynchronously notified filp's
    mytimer_fasync(-1, filp, 0);
	/* Success */
	return 0;
}


static ssize_t mytimer_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos) 
{ 

	unsigned int seconds;
	char message[129];
	unsigned int timer_count;
	
	D(printk("In write method\n"));
	/*Process should not write more than Buffer capacity*/
	if(count > BUFFER_CAPACITY) {
		return -EFBIG;
	}

	/* do not go over the end */
	if (count > BUFFER_CAPACITY - *f_pos) {

		D(printk(KERN_DEBUG
			"write called: process id %d, command %s, count %d, buffer full\nResetting buffer",
			current->pid, current->comm, count));

		*f_pos = 0;
		memset(buffer, 0, BUFFER_CAPACITY);
	}

	// Assume that buffer contains a valid message from the user
	// Copy data from user

	down_write(buffer_semaphore);
	if (copy_from_user(buffer + *f_pos, buf, count))
	{
		D(printk(KERN_DEBUG
			"write called: process id %d, command %s, count %d, buffer full\nResetting buffer",
			current->pid, current->comm, count));

		return -EFAULT;
	}
	up_write(buffer_semaphore);

	// Register a timer or updating a timer
	down_read(buffer_semaphore);
	D(printk(KERN_DEBUG "Reading from buffer: %s\n", buffer));

	if(sscanf(buffer, "-s %u %128[^\n]", &seconds, message) == 2) {
		up_read(buffer_semaphore);
		registerTimer(seconds, message);
	} else if(sscanf(buffer, "-m %u", &timer_count) == 1) {
		up_read(buffer_semaphore);
		changeMaxTimer(timer_count);
	} else if(strncmp(buffer, "-r", 2) == 0) {
		up_read(buffer_semaphore);
		removeTimers();
	} else {
		up_read(buffer_semaphore);
	}

	return count;
}

static int mytimer_fasync(int fd, struct file *filp, int mode) {
	D(printk(KERN_DEBUG "In fasync method\n"));
	// add or remove entries from the list of interested processes when the FASYNC flag changes for an open file
    return fasync_helper(fd, filp, mode, &async_queue); 
}

////////////////// PROC FS functions
//
static int mytimer_proc_open(struct inode *inode, struct file *file) {
	D(printk(KERN_DEBUG "Opening /proc/mytimer/\n"));
    return single_open(file, mytimer_proc_show, NULL);
}

static int mytimer_proc_show(struct seq_file *m, void *v) {

	char * bufferPtr;
	struct mytimer_t * timer_entry;

	D(printk(KERN_DEBUG "In proc show\n"));

	bufferPtr = proc_buffer;
	bufferPtr += sprintf(bufferPtr, "[TIME SINCE MODULE WAS LOADED]: %u ms\n", jiffies_to_msecs(jiffies - start_jiffies));


	// Print specifications for each timer
	list_for_each_entry(timer_entry, &mytimer_list, list_node) {
		if(timer_entry) {
			bufferPtr += sprintf(bufferPtr, "Timer:\n\t[PID]: %u\n\t[COMMAND NAME]: %s\n\t[TIMER]: %s<%lu s>\n", 
					timer_entry->pid, timer_entry->comm, timer_entry->msg, (timer_entry->ktimer.expires - jiffies) / HZ);
		}

	}

	// Print
	seq_printf(m, "[MODULE NAME]: mytimer\n%s", proc_buffer);
	return 0;
}

// Assume msg shouuld contain [SEC] [MESSAGE]. SEC and message should be seperated by a space
static void registerTimer(unsigned int seconds, const char * const msg) {

	struct mytimer_t  * timer_entry;

	D(printk(KERN_DEBUG "In register timer\n"));

	list_for_each_entry(timer_entry, &mytimer_list, list_node) {
		if(timer_entry && strcmp(msg, timer_entry->msg) == 0) {
			mod_timer(&(timer_entry->ktimer), jiffies + seconds * HZ);
			D(printk(KERN_DEBUG "Updating timer %s to %u seconds", timer_entry->msg, seconds));
			return;
		}
	}

	// Message doesn't exist in timers

	// Check if we are at max capacity of timers 
	if(atomic_read(&num_timers) >= atomic_read(&max_timers)) {
		// No timer will be created
		D(printk(KERN_DEBUG "Too many timers! Capacity : %u \n", atomic_read(&max_timers)));
		return;
	}

	D(printk(KERN_DEBUG "Creating timer %s with %u secs. Sent by %u\n", msg, seconds, current->pid));

	// Create a new timer
	timer_entry = kmalloc(sizeof(struct mytimer_t), GFP_KERNEL);
	if(!timer_entry) {
		printk(KERN_ALERT "Insufficient kernel memory\nCannot add another timer!"); 
		return;
	}


	timer_entry->pid = current->pid;
	strcpy(timer_entry->msg, msg);
	strcpy(timer_entry->comm, current->comm);

	timer_setup(&(timer_entry->ktimer), timer_handler, 0); 
	mod_timer(&(timer_entry->ktimer), jiffies + seconds * HZ);
	list_add_tail(&(timer_entry->list_node), &mytimer_list);

	
	atomic_inc(&num_timers);


	D(printk(KERN_DEBUG "Created timer %s w/ %lu secs. Sent by %u\n",  timer_entry->msg, (timer_entry->ktimer.expires - jiffies) / HZ,  timer_entry->pid));
	return;
}

static void timer_handler(struct timer_list * ktimer) {

	struct list_head * list_ptr;
	struct mytimer_t  * timer_entry = NULL;
	int found = 0;

	D(printk(KERN_DEBUG "In timer handler\n"));
	// Send SIGIO to user-space program
	if(async_queue) {
		kill_fasync(&async_queue, SIGIO, POLL_IN);
	}

	// Break when we found the timer_entry corresponding to the ktimer
	list_for_each(list_ptr, &mytimer_list) {
		timer_entry = list_entry(list_ptr, struct mytimer_t, list_node);
		if(timer_entry &&  ktimer == &(timer_entry->ktimer)) {
			found = 1;
			break;
		}
	}

	if(!found) {
		printk(KERN_ALERT "ERROR: Didn't find expired timer\n");
		return;
	}


	D(printk(KERN_DEBUG "Free'd ktimer %s\n", timer_entry->msg));

	/*This casuses problems: del_timer_sync(ktimer);*/
	del_timer(ktimer);
	kfree(timer_entry);
	list_del(list_ptr);

	// Remove timer
	atomic_dec(&num_timers);
}

static void changeMaxTimer(unsigned int timer_count) {
	// Don't change the max timer(s) available if
	// the current number of timers is greater.
	if(timer_count < atomic_read(&num_timers)) {
		return;
	}
	D(printk(KERN_DEBUG "Changing max timers to %u\n", timer_count));
	atomic_set(&max_timers, timer_count);
}


static void removeTimers() {

	struct mytimer_t * timer_entry;
	struct list_head * ptr;
	struct list_head * next;
	struct task_struct * task;
	struct siginfo info;
	int ret;

	memset(&info, 0, sizeof(struct siginfo));
	info.si_signo = SIGKILL;

	D(printk(KERN_DEBUG "Removing timers\n"));
	// Delete Linked list of timers
	list_for_each_safe(ptr, next, &mytimer_list) {
		timer_entry = list_entry(ptr, struct mytimer_t, list_node);
		// Get information on process waiting on timer
		//task = find_task_by_vpid(timer_entry->pid); Didn't work
		task = pid_task(find_vpid(timer_entry->pid), PIDTYPE_PID);

		if(task) get_task_struct(task);
		// Send SIGKILL to process waiting on timer
		ret = send_sig_info(SIGKILL, &info, task);
		if (ret < 0) {
			D(printk(KERN_DEBUG "error sending signal\n"));
		}
		// Delete timer information
		del_timer(&(timer_entry->ktimer));
		kfree(timer_entry);
		list_del(ptr);
	}

	atomic_set(&num_timers, 0);
}
