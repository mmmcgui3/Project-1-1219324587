//Madison McGuire

//includes
#include <linux/init.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/tty.h>
#include <linux/uidgid.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/string.h>
#include <linux/semaphore.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/param.h>
#include <linux/timer.h>
#include <linux/ktime.h>
#include <linux/time_namespace.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>

//defines
#define MAX_BUFFER_SIZE 1000
#define MAX_NO_OF_PRODUCERS 1
#define MAX_NO_OF_CONSUMERS 100
#define PCINFO(s, ...) pr_info("###[%s]###" s, __FUNCTION__, ##__VA_ARGS__)

unsigned long long total_time_elapsed = 0;

struct process_info
{
	unsigned long pid;
	unsigned long long start_time;
	unsigned long long boot_time;
} process_default_info = {0, 0};

int total_no_of_process_produced = 0;
int total_no_of_process_consumed = 0;

int end_flag = 0;

char producers[MAX_NO_OF_PRODUCERS][12] = {"kProducer-X"};
char consumers[MAX_NO_OF_CONSUMERS][12] = {"kConsumer-X"};

static struct task_struct *ctx_producer_thread[MAX_NO_OF_PRODUCERS];
static struct task_struct *ctx_consumer_thread[MAX_NO_OF_CONSUMERS];

// use fill and use to keep track of the buffer
struct process_info buffer[MAX_BUFFER_SIZE];
int fill = 0;
int use = 0;

// TODO Define your input parameters (buffSize, prod, cons, uuid) here
// Then use module_param to pass them from insmod command line. (--Assignment 2)

// Define integer variables to hold the values of the input parameters.
int buffSize = 0;
int prod = 0;
int cons = 0;
int uuid = 0;


//variable
//type
//permissions
module_param(buffSize, int, 0);
module_param(prod, int, 0);
module_param(cons, int, 0);
module_param(uuid, int, 0);

// TODO Define your semaphores here (empty, full, mutex) -- Assignment 3

// Define three semaphore variables: empty, full, and mutex.
struct semaphore empty;
struct semaphore full;
struct semaphore mutex;

// Define the producer_thread_function that will be run in a thread.
int producer_thread_function(void *pv)
{
	allow_signal(SIGKILL);
	struct task_struct *task;
	
	// Iterate through all the processes (tasks).
	for_each_process(task)
	{
		if (task->cred->uid.val == uuid)
		{
			// TODO Implement your producer kernel thread here
			// use kthread_should_stop() to check if the kernel thread should stop
			// use down() and up() for semaphores
			// Hint: Please refer to sample code to see how to use process_info struct
			// Hint: kthread_should_stop() should be checked after down() and before up()

      // Check if the buffer is not full (fill is less than MAX_BUFFER_SIZE).
      if (fill < MAX_BUFFER_SIZE) {
        // Acquire the empty semaphore to indicate that an empty slot is being used.
	down(&empty);
	// Acquire the mutex semaphore to ensure exclusive access to the buffer.
        down(&mutex);

	// Check if the kernel thread should stop
        if (kthread_should_stop()) {
          up(&mutex);
          up(&empty);
          break;
        }
	
	// Check again if the buffer became full during the time when the mutex was acquired
        if (fill >= MAX_BUFFER_SIZE) {
          up(&mutex);
          up(&empty);
          break;
        }

	// Add task information to the buffer and increment the fill counter
        buffer[fill].pid = task->pid;
        buffer[fill].start_time = task->start_time;
        fill++;

	// Check if the kernel thread should stop
        if (kthread_should_stop()) {
          up(&mutex);
          up(&empty);
          break;
        }

	// Release the mutex semaphore and indicate that the buffer is full
        up(&mutex);
        up(&full);
      }


			total_no_of_process_produced++;
			PCINFO("[%s] Produce-Item#:%d at buffer index: %d for PID:%d \n", current->comm,
				   total_no_of_process_produced, (fill + buffSize - 1) % buffSize, task->pid);
		}
	}

	PCINFO("[%s] Producer Thread stopped.\n", current->comm);
	ctx_producer_thread[0] = NULL;
	return 0;
}

int consumer_thread_function(void *pv)
{
	allow_signal(SIGKILL);
	int no_of_process_consumed = 0;

	while (!kthread_should_stop())
	{
		// TODO Implement your consumer kernel thread here
		// use end_flag (see kernel module exit function) to check if the kernel thread should stop
		// if end_flag, then break
		// use down() and up() for semaphores
		// Hint: Please refer to sample code to see how to use process_info struct
		// Hint: end_flag should be checked after down() and before up()
    unsigned long long start_time_ns = buffer[use].start_time;
    unsigned long nowpid = buffer[use].pid;
 
    // Clear the buffer and increment use  
    buffer[use].pid = 0;
    buffer[use].start_time = 0;
    buffer[use].boot_time = 0;
    use++;
	
    // Get the full and mutex semaphores
    down(&full);
    down(&mutex);

    // Check if the end_flag is set
    if (end_flag) {
      up(&mutex);
      up(&full);
      break;
    }

    // Check the end_flag again
    if (end_flag) {
      up(&mutex);
      up(&full);
      break;
    }

    // Release mutex and empty semaphores
    up(&mutex);
    up(&empty);

		unsigned long long ktime = ktime_get_ns();
		unsigned long long process_time_elapsed = (ktime - start_time_ns) / 1000000000;
		total_time_elapsed += ktime - start_time_ns;


		unsigned long long process_time_hr = process_time_elapsed / 3600;
		unsigned long long process_time_min = (process_time_elapsed - 3600 * process_time_hr) / 60;
		unsigned long long process_time_sec = (process_time_elapsed - 3600 * process_time_hr) - (process_time_min * 60);
    unsigned long long process_pid = current->pid;

		no_of_process_consumed++;
		total_no_of_process_consumed++;
		PCINFO("[%s] Consumed Item#-%d on buffer index:%d::PID:%lu \t Elapsed Time %llu:%llu:%llu \n", current->comm,
			   no_of_process_consumed, (use + buffSize - 1) % buffSize, nowpid, process_time_hr, process_time_min, process_time_sec);
	}

	PCINFO("[%s] Consumer Thread stopped.\n", current->comm);
	return 0;
}

char *replace_char(char *str, char find, char replace)
{
	char *current_pos = strchr(str, find);
	while (current_pos)
	{
		*current_pos = replace;
		current_pos = strchr(current_pos, find);
	}
	return str;
}

void name_threads(void)
{
	int index;
	for (index = 0; index < prod; index++)
	{
		char id = (index + 1) + '0';
		strcpy(producers[index], "kProducer-X");
		strcpy(producers[index], replace_char(producers[index], 'X', id));
	}

	for (index = 0; index < cons; index++)
	{
		char id = (index + 1) + '0';
		strcpy(consumers[index], "kConsumer-X");
		strcpy(consumers[index], replace_char(consumers[index], 'X', id));
	}
}

static int __init thread_init_module(void)
{
	PCINFO("CSE330 Project-1 Kernel Module Inserted\n");
	PCINFO("Kernel module received the following inputs: UID:%d, Buffer-Size:%d, No of Producer:%d, No of Consumer:%d", uuid, buffSize, prod, cons);

	if (buffSize > 0 && (prod >= 0 && prod < 2))
	{
		// TODO initialize the semaphores here
   		sema_init(&mutex, 1);
		sema_init(&empty, buffSize);
    		sema_init(&full, 0);
		name_threads();

		int index;
		for (index = 0; index < buffSize; index++)
			buffer[index] = process_default_info;

		// TODO use kthread_run to create producer kernel threads here
		// Hint: Please refer to sample code to see how to use kthread_run, kthread_should_stop, kthread_stop, etc.
		// Hint: use ctx_producer_thread[index] to store the return value of kthread_run
  		for (index = 0; index < prod; index++) {
      			ctx_producer_thread[index] = kthread_run(producer_thread_function, NULL, producers[index]);
   		 }

		// TODO use kthread_run to create consumer kernel threads here
		// Hint: Please refer to sample code to see how to use kthread_run, kthread_should_stop, kthread_stop, etc.
		// Hint: use ctx_consumer_thread[index] to store the return value of kthread_run
    		for (index = 0; index < cons; index++) {
      			ctx_consumer_thread[index] = kthread_run(consumer_thread_function, NULL, consumers[index]);
   		 }
		
	}
	else
	{
		PCINFO("Incorrect Input Parameter Configuration Received. No kernel threads started. Please check input parameters.");
		PCINFO("The kernel module expects buffer size (a positive number) and # of producers(0 or 1) and # of consumers > 0");
	}

	return 0;
}

static void __exit thread_exit_module(void)
{
	if (buffSize > 0)
	{
		while (1)
		{
			if (total_no_of_process_consumed == total_no_of_process_produced || !cons || !prod)
			{
				if (!cons)
				{
					up(&empty);
				}
				int index;
				for (index = 0; index < prod; index++)
				{
					if (ctx_producer_thread[index])
					{
						kthread_stop(ctx_producer_thread[index]);
					}
				}

				end_flag = 1;
				for (index = 0; index < cons; index++)
				{
					up(&full);
					up(&mutex);
				}
				for (index = 0; index < cons; index++)
				{
					if (ctx_consumer_thread[index]){
						kthread_stop(ctx_consumer_thread[index]);
					}
				}
				break;
			}
			else
				continue;
		}

		total_time_elapsed = total_time_elapsed / 1000000000;

		unsigned long long total_time_hr = total_time_elapsed / 3600;
		unsigned long long total_time_min = (total_time_elapsed - 3600 * total_time_hr) / 60;
		unsigned long long total_time_sec = (total_time_elapsed - 3600 * total_time_hr) - (total_time_min * 60);

		PCINFO("Total number of items produced: %d", total_no_of_process_produced);
		PCINFO("Total number of items consumed: %d", total_no_of_process_consumed);
		PCINFO("The total elapsed time of all processes for UID %d is \t%llu:%llu:%llu  \n", uuid, total_time_hr, total_time_min, total_time_sec);
	}

	PCINFO("CSE330 Project 1 Kernel Module Removed\n");
}

module_init(thread_init_module);
module_exit(thread_exit_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Madison McGuire");
MODULE_DESCRIPTION("CSE330 2023 Fall Project 1 Process Management\n");
MODULE_VERSION("0.1");
