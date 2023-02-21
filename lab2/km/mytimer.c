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
#include <linux/sched.h> // timer
#include <linux/jiffies.h> // HZ


#define DEBUG 0

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Module for kernel timers");
MODULE_AUTHOR("Justin Sadler");




// Declaration of mytimer.c functions
static int mytimer_init(void);
static void mytimer_exit(void);
static ssize_t mytimer_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
static ssize_t mytimer_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
static int mytimer_open(struct inode *inode, struct file *filp);
static int mytimer_release(struct inode *inode, struct file *filp);

/* Structure that declares the usual file */
/* access functions */
struct file_operations mytimer_fops = {
	read: mytimer_read,
	write: mytimer_write,
	open: mytimer_open,
	release: mytimer_release
};

// Declaration of helper functions
static void registerTimer(char * buf); // Registers/Updates a timer
static void changeMaxTimer(const char * buf); // Change the number of timers supported
static void expire_timer(struct timer_list *); // Exit function for kernel timers
static void listTimers(void); // List the active timers


struct mytimer_t {
	char msg[128+1]; // message associated with timer
	struct timer_list * ktimer;
};

struct mytimer_t timers[5];
static unsigned int num_timers = 0;
static unsigned int timer_capacity = 1;
static const unsigned int MAX_TIMERS = 5;

/* Declaration of the init and exit functions */
module_init(mytimer_init);
module_exit(mytimer_exit);

const unsigned user_input_capacity = 256;
const unsigned user_output_capacity = 1024;
static unsigned bite = 256;

// Global variables 
static const int mytimer_major = 61;

// Buffer to store data from user
static char *user_input;

// Buffer to store data to send to user
static char *user_output;

static int mytimer_init(void) {
	/* Registering device */
	unsigned int i = 0;

	int result = register_chrdev(mytimer_major, "mytimer", &mytimer_fops);
	if (result < 0)
	{
		printk(KERN_ALERT
			"mytimer: cannot obtain major number %d\n", mytimer_major);
		return result;
	}

	/* Allocating mytimer for the buffer */
	user_input = kmalloc(user_input_capacity, GFP_KERNEL); 
	if (!user_input)
	{ 
		printk(KERN_ALERT "Insufficient kernel memory\n"); 
		result = -ENOMEM;
		goto fail; 
	} 
	memset(user_input, 0, user_input_capacity);

	/* Allocating mytimer for the buffer */
	user_output = kmalloc(user_output_capacity, GFP_KERNEL); 
	if (!user_output)
	{ 
		printk(KERN_ALERT "Insufficient kernel memory\n"); 
		result = -ENOMEM;
		goto fail; 
	} 
	memset(user_output, 0, user_output_capacity);

	for(i = 0; i < MAX_TIMERS; i++) {
		strcpy(timers[i].msg, "");
		timers[i].ktimer = NULL;
	}


	printk(KERN_ALERT "Inserting mytimer module\n"); 
	return 0;

fail: 
	mytimer_exit(); 
	return result;
}

static void mytimer_exit(void) {
	unsigned int i;

	unregister_chrdev(mytimer_major, "mytimer");
	/* Freeing buffer memory */
	if (user_input)
	{
		kfree(user_input);
	}

	if(user_output) {
		kfree(user_output);
	}
	printk(KERN_ALERT "Removing mytimer module\n");

	for(i = 0; i < MAX_TIMERS; i++) {
		if(timers[i].ktimer) {
			del_timer_sync(timers[i].ktimer);
			kfree(timers[i].ktimer);
			timers[i].ktimer = NULL;
		}
	}
}

static int mytimer_open(struct inode *inode, struct file *filp)
{
#if DEBUG
	printk(KERN_INFO "open called: process id %d, command %s\n",
		current->pid, current->comm);
#endif
	/* Success */
	return 0;
}

static int mytimer_release(struct inode *inode, struct file *filp)
{
#if DEBUG
	printk(KERN_INFO "release called: process id %d, command %s\n",
		current->pid, current->comm);
#endif
	/* Success */
	return 0;
}

static ssize_t mytimer_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{ 
	int temp;
	char tbuf[256], *tbptr = tbuf;
	unsigned int mytimer_len = strlen(user_output);

	/* end of buffer reached */
	if (*f_pos >= mytimer_len)
	{
		return 0;
	}

	/* do not go over then end */
	if (count > mytimer_len - *f_pos)
		count = mytimer_len - *f_pos;


	/* Transfering data to user space */ 
	if (copy_to_user(buf, user_output + *f_pos, count))
	{
		return -EFAULT;
	}

	tbptr += sprintf(tbptr,								   
		"read called: process id %d, command %s, count %d, offest %lld chars ",
		current->pid, current->comm, count, *f_pos);

	for (temp = *f_pos; temp < count + *f_pos; temp++)					  
		tbptr += sprintf(tbptr, "%c", user_output[temp]);

#if DEBUG
	printk(KERN_INFO "%s\n", tbuf);
#endif


	// Reset output buffer
	strcpy(user_output, "");
	return count; 
}

static ssize_t mytimer_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos) 
{ 
	int temp;
	char tbuf[256], *tbptr = tbuf;
	char registerTimerBuffer[256];

	/* end of buffer reached */
	if (*f_pos >= user_input_capacity)
	{
#if DEBUG
		printk(KERN_INFO
			"write called: process id %d, command %s, count %d, buffer full\n",
			current->pid, current->comm, count);
#endif
		return -ENOSPC;
	}

	/* do not eat more than a bite */
	if (count > bite) count = bite;

	/* do not go over the end */
	if (count > user_input_capacity - *f_pos)
		count = user_input_capacity - *f_pos;

	// Assume that buffer contains a valid message from the user
	// Copy data from user
	memset(user_input, 0, user_input_capacity);
	if (copy_from_user(user_input + *f_pos, buf, count))
	{
		return -EFAULT;
	}

	tbptr += sprintf(tbptr,								   
		"write called: process id %d, command %s, count %d,  offset %lld, chars ",
		current->pid, current->comm, count, *f_pos);

	for (temp = *f_pos; temp < count + *f_pos; temp++)					  
		tbptr += sprintf(tbptr, "%c", user_input[temp]);

#if DEBUG
	printk(KERN_INFO "tbuf: %s\n", tbuf);
	printk(KERN_INFO "User input: %s\n", user_input);
#endif

	// Register a timer or updating a timer
	if(strncmp(user_input, "-s", 2) == 0) {
		memset(registerTimerBuffer, 0, 256); // Reset buffer
		strcpy(registerTimerBuffer, user_input + 3);
		registerTimer(registerTimerBuffer);

	} else if(strncmp(user_input, "-m", 2) == 0) {
		changeMaxTimer(user_input + 3);
	} else if(strncmp(user_input, "-l", 2) == 0) {
		listTimers();
	}


	return count;
}


// Assume msg shouuld contain [SEC] [MESSAGE]. SEC and message should be seperated by a space
static void registerTimer(char * registerTimerBuffer) {

	// Get the number of seconds and message from buffer
	const char * numStr;
	const char * msg;
	unsigned long int seconds;
	unsigned int i;

#if DEBUG
	printk(KERN_INFO "In register timer. Buffer: %s\n", registerTimerBuffer);
#endif
	numStr = strsep(&registerTimerBuffer, " \t\n\r\f\v");
	msg = strsep(&registerTimerBuffer, "\n\r\f"); 
	seconds = simple_strtoul(numStr, NULL, 10);
	


	for(i = 0; i < num_timers; i++) {
#if DEBUG
		printk(KERN_INFO "Comparing %s and %s\n", msg, timers[i].msg);
#endif

		if(strcmp(msg, timers[i].msg) == 0) {
			// modify timer
			mod_timer(timers[i].ktimer, jiffies + seconds * HZ);
			// send message to user that a timer was updated
#if DEBUG
			printk(KERN_INFO "Updating timer %s to %ld seconds", msg, seconds);
#endif
			sprintf(user_output, "The timer %s was updated!\n", msg);
			return;
		}
	}

	// Message doesn't exist in timers
	

	// Check if we are at max capacity of timers 
	if(num_timers == timer_capacity) {
		// No timer will be created
		// Userspace program will print error message
		sprintf(user_output, "%d timer(s) already exist(s)!\n", num_timers);
#if DEBUG
		printk(KERN_INFO "Too many timers! Capacity : %d \n", timer_capacity);
#endif
		return;
	}


	// Register new timer and add it to the mytimer_t array
	
	i = num_timers;
	timers[i].ktimer = kmalloc(sizeof(struct timer_list), GFP_KERNEL);

	strcpy(timers[i].msg, msg); // Copy message into timer
	timer_setup(timers[i].ktimer, expire_timer, 0); 
	mod_timer(timers[i].ktimer, jiffies + seconds * HZ);

	// Add Kernel timer to Data structure
	num_timers++;

	// Send nothing to the user
	strcpy(user_output, "");

#if DEBUG
	printk(KERN_INFO "Created timer %s w/ %ld secs\n",  timers[i].msg, seconds);
#endif
	return;
}

static void expire_timer(struct timer_list * ktimer) {

	// Get the corresponding message for the timer
	unsigned int i = 0;

#if DEBUG
	printk(KERN_INFO "Searching for message\n");
#endif

	for(; i < num_timers; i++) {
		if(timers[i].ktimer == ktimer) {
#if DEBUG
			printk(KERN_INFO "Found message at index %d\n", i);
#endif
			break;
		}
	}

	// Assume message was found
	
	// remove kernel timer from module
	/*This casuses problems: del_timer_sync(ktimer);*/
	kfree(ktimer);
	timers[i].ktimer = NULL;

#if DEBUG
	printk(KERN_INFO "Free'd ktimer\n");
#endif
	// Send alert
	printk(KERN_ALERT "%s\n", timers[i].msg);

	// Remove timer from data structure
	for(; i < num_timers - 1; i++) {
		// Move valid timers to the left left of the array
		strcpy(timers[i].msg, timers[i + 1].msg);
		timers[i].ktimer = timers[i + 1].ktimer;
	}
	timers[num_timers - 1].ktimer = NULL;

	num_timers--;
}

static void changeMaxTimer(const char * buf) {
	// Assume buf contains a null-terminated string of a decimal integer 
	// Assume no leading or trailing whitespace
	unsigned long count = simple_strtoul(buf, NULL, 10);
	unsigned int i;

	// Assume that count >= currnet number of timers
	for(i = timer_capacity - 1; i >= count; i--) {
		if(timers[i].ktimer) {
			del_timer_sync(timers[i].ktimer);
			kfree(timers[i].ktimer);
			timers[i].ktimer = NULL;
		}
	}

	// Change the number of timers supported
	timer_capacity = count;
	// Send nothing to the user
	strcpy(user_output, "");
}

static void listTimers(void) {

	unsigned int i = 0; 
	char * temp = user_output;
	unsigned long timeout;

	for(i = 0; i < num_timers; i++) {
		// Getting the timeout in jiffies
		timeout = timers[i].ktimer->expires - jiffies; 
		temp += sprintf(temp, "%s %ld\n", timers[i].msg, timeout / HZ);
	}
}
