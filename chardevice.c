#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <asm/uaccess.h>

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
static bool is_dev_opened = false;

static char msg[BUF_LEN];
static char *buff_Ptr;
static bool deleted = true;
static bool is_fwd_dir = true;
static bool output_once = false;

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

void direction_backward(void)
{
	printk(KERN_ALERT "Back direction\n");
	is_fwd_dir = false;
}

void direction_forward(void)
{
	printk(KERN_ALERT "Forward direction\n");
	is_fwd_dir = true;
}

static struct file_operations fops = {
        .read = device_read,
        .write = device_write,
        .open = device_open,
        .release = device_release };

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
	return SUCCESS;
}

void cleanup_module(void)
{
	unregister_chrdev(Major, DEVICE_NAME);
	printk(KERN_ALERT "Closing.\n\n");
}

static int device_open(struct inode *inode, struct file *file)
{
	if (is_dev_opened)
	{
		return -EBUSY;
	}
//	++Device_Open;
	is_dev_opened = true;
	buff_Ptr = msg;
	try_module_get(THIS_MODULE);
	return SUCCESS;
}

static  int device_release(struct inode *inode, struct file *file)
{
//	--Device_Open;
	is_dev_opened = false;
	module_put(THIS_MODULE);
	return 0;
}

static ssize_t device_read(struct file *filp,char __user * buff, size_t len, loff_t * offset)
{
	if (*buff_Ptr == 0 || deleted || output_once)
	{
		output_once = false;
		return 0;
	}
	printk(KERN_INFO "Get\n");
	int bytes = 0;
	char c;
	int size = 0;
	if(!is_fwd_dir) // if bakward
	{
		while (*buff_Ptr != '\0') // well, it is size
		{
			++buff_Ptr;
			++size;
		}
		while (size != 0)
		{
			--size;
			--buff_Ptr;

			put_user(*buff_Ptr, ++buff);

			--len;
			++bytes;
		}
		output_once = true;
	}
	else
	{
		while (*buff_Ptr)
		{
			put_user(*(buff_Ptr++), buff++);
			--len;
			++bytes;
		}
	}
	printk(KERN_INFO "Bytes read: %d\n", bytes - 1);
	return bytes;
}

static ssize_t device_write(struct file *filp, const char __user * buff, size_t len, loff_t * offset)
{
	int i;
	char new_msg[BUF_LEN];

	printk(KERN_INFO "Saving contents: [lenght=%d]", (int)(len - 1) );

	for (i = 0; i < len && i < BUF_LEN; i++)
	{
		get_user(new_msg[i], buff + i);
	}
	if ( str_cmp(new_msg,"delete"))
	{
		delete_content();
	}
	else if (str_cmp(new_msg,"dir back"))
	{
		direction_backward();
	}
	else if (str_cmp(new_msg,"dir forward"))
	{
		direction_forward();
	}
	else if (str_cmp(new_msg, "reverse"))
	{
		if (is_fwd_dir)
		{
			direction_backward();
		}
		else
		{
			direction_forward();
		}
	}
	else
	{
		deleted = false;

		for (i = 0; i < BUF_LEN; i++)
		{
			get_user(msg[i], " ");
		}

		for (i = 0; i < len && i < BUF_LEN; i++)
		{
			get_user(msg[i], buff + i);
		}

		buff_Ptr = msg;
	}
	return i;
}
