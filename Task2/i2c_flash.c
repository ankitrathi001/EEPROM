/******************************************************************************
 *
 * File Name: i2c_flash.c
 *
 * Author: Ankit Rathi (ASU ID: 1207543476)
 * 			(Ankit.Rathi@asu.edu)
 *
 * Date: 08-OCT-2014
 *
 * Description: A driver program to read/write and erase and set pointer positions
 * 				of an EEPROM
 * 
 *****************************************************************************/
 
/**
*Include Library Headers 
*/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <asm/errno.h>
#include <linux/delay.h>

/**
 * Define constants using the macro
 */
#define DRIVER_NAME 		"i2c_flash"
#define DEVICE_NAME 		"i2c_flash"
#define WORK_QUEUE_NAME     "i2c_work_queue"
#define I2C_MINOR_NUMBER    0
#define GPIO_LED_PIN        26
#define GPIO_MUX_PIN        29
#define SLAVE_ADDRESS       0x54
#define EEPROM_PAGE_SIZE	64
#define NUMBER_OF_PAGES		512
#define FLASHGETS			1
#define FLASHGETP			2
#define FLASHSETP			3
#define FLASHERASE			4

/**
 *  Per-device data structure for each
 *  EEPROM
 */
struct i2c_EEPROM_dev
{
  struct i2c_client client;      	/* I2C client for EEPROM */
  unsigned int addr;              	/* Slave address of EEPROM */
  unsigned short current_pointer; 	/* Current Position pointer */
  struct cdev cdev;				  	/* Character Device */
  char name[20];				  	/* Character Device Name */
  unsigned int BUSY_FLAG;		  	/* Busy Flag Status */	
};

/**
 * Global Variable Declarations
 */
static dev_t dev_number;          					/* Allotted Device Number */
static struct class *eep_class;   					/* Device class */
struct i2c_EEPROM_dev *i2c_EEPROM_device_list;     	/* List of private data structures */
struct i2c_client* client_core = NULL;
static struct workqueue_struct  *i2c_eeprom_workqueue;
char *temp_read_buff = NULL;
char *temp_write_buff = NULL;
unsigned int WORK_ID_COUNTER=0;
int ready_to_write_flag=0;
int ready_to_read_flag=0;
int ERROR;
char *tempBuffer = NULL;

/**
 * Functions Declarations
 */
static ssize_t i2c_eeprom_write_into_queue(struct file *file, const char __user *buf, size_t count, loff_t *offset);
static ssize_t i2c_eeprom_read_from_queue(struct file *file, char __user *buf, size_t count, loff_t *offset);

/**
 *  Data structure for data to be passed to workers thread.
 */
typedef struct QUEUE_DATA_TAG
{
	struct file *file;
	char        *buf;
	size_t      count;
	loff_t      *offset;
}QUEUE_DATA;

/**
 *  Data structure for work to be put in workers thread.
 */
typedef struct I2C_WORK_QUEUE_TAG
{
	struct work_struct 	work;
	unsigned char 		read_or_write;
	unsigned char 		status_Flag;
	unsigned int       	work_id;
	QUEUE_DATA 			queue_Data;
} I2C_WORK_QUEUE;

/**
 *  Data structure for i2c device id of EEPROM
 */
static struct i2c_device_id eeprom_id_table[] = {
	{DEVICE_NAME,0}
};

/**
* eeprom_probe - Function to probe EEPROM
* @client: I2C Client
* @id: I2C Device ID
*
* Returns 0.
* 
* Description: This function is called when a driver is registerd and a new device
* 				is plugged in.
*/
static int eeprom_probe(struct i2c_client *client, const struct i2c_device_id * id)
{
	memcpy(&(i2c_EEPROM_device_list->client),client,sizeof(struct i2c_client));
    return 0;
}

/**
* eeprom_remove - Function to probe EEPROM
* @client: I2C Client
*
* Returns 0.
* 
* Description: This function is called when a driver is registerd and a already
* 	plugged in device is plugged out.
*/
static int eeprom_remove(struct i2c_client *client)
{
    return 0;
}

/**
* This is Board Info Structure, which gives details about the devices on
* the board for driver.
*/
static struct i2c_board_info i2c_eeprom_board_info[] = {
	{
		I2C_BOARD_INFO(DEVICE_NAME, SLAVE_ADDRESS),
	}
};

/**
* This is I2C Driver Structure.
*/
static struct i2c_driver eeprom_driver =
{
  .driver = {
    .name       = DEVICE_NAME,      		/* Name */
    .owner		= THIS_MODULE,				/* Owner */
  },
  .id_table     = eeprom_id_table, 			/* ID */
  .probe 		= eeprom_probe,        		/* Probe Method */
  .remove  		= eeprom_remove,       		/* Remove Method */
};

/**
* eeprom_open - Function called when EEPROM device is opened for use.
* @client: I2C Client
*
* Returns 0.
* 
* Description: This function is called when a driver is registerd and a already
* 	plugged in device is plugged out.
*/
static int i2c_eeprom_open(struct inode *inode, struct file *file)
{
	int ret;
	//Setting Current Pointer Position to 0 for first time.
	i2c_EEPROM_device_list->current_pointer = 0;
	ret = gpio_request_one(GPIO_LED_PIN, GPIOF_OUT_INIT_LOW, "Led");
	if(ret)
	{
		//printk("LED ERROR");
	}
	gpio_set_value_cansleep(GPIO_LED_PIN, 0);
	i2c_EEPROM_device_list->BUSY_FLAG = 0;
	ready_to_write_flag  = 0;
	ready_to_read_flag   = 0;
	return 0;
}

/**
* i2c_eeprom_release - Function called during release of EEPROM.
* @inode: Inodes
* @filp: Fle Pointer
*
* Returns 0.
* 
*/
int i2c_eeprom_release(struct inode *inode, struct file *filp)
{
	return 0;
}

/**
* i2c_eeprom_write - Function called to write message into EEPROM.
* @filp: I2C Client
* @buf:Buffer Space
* @count:Count to hold number of pages
* @ppos:offset
*
* Returns 0.
* 
* Description: This function is called when a driver is registerd and a already
* 	plugged in device is plugged out.
*/
static ssize_t i2c_eeprom_write(struct file *filp, const char *buf, size_t count, loff_t *ppos)
{
	int retValue,i;
	char *receiveBuffer;
	char sendBuffer[EEPROM_PAGE_SIZE+2];
	int tempPointer;
	//printk("i2c_flash.c: i2c_eeprom_write: Start\n");
	receiveBuffer = kmalloc(count * EEPROM_PAGE_SIZE, GFP_KERNEL);
	tempPointer = i2c_EEPROM_device_list->current_pointer;
	//copy contents from userspace to kernel space
	retValue = copy_from_user(receiveBuffer,buf,(count * EEPROM_PAGE_SIZE));

	if(retValue)
	{
		printk("Error: copy from user");
		gpio_set_value_cansleep(GPIO_LED_PIN, 0);
		i2c_EEPROM_device_list->BUSY_FLAG = 0;
		kfree(receiveBuffer);
		return -1;
	}

	for(i=0;i<count;i++)
	{
		//Switch ON LED before Write Operation Begins
		gpio_set_value_cansleep(GPIO_LED_PIN, 1);
		//Turn On Busy Flag
		i2c_EEPROM_device_list->BUSY_FLAG = 1;
	
		//Set Address High Byte
		sendBuffer[0]= ((tempPointer >> 8) & 0xFF);
		//Set Address Low Byte
		sendBuffer[1]= (tempPointer & 0xFF);
		memcpy(&sendBuffer[2], &receiveBuffer[i * EEPROM_PAGE_SIZE], EEPROM_PAGE_SIZE);

		//Issue a single I2C message in master transmit mode
		retValue = i2c_master_send(&(i2c_EEPROM_device_list->client), sendBuffer,sizeof(sendBuffer));
		if(retValue<0)
		{
			printk("Error:i2c_master_send");
			ready_to_write_flag = -1;
			gpio_set_value_cansleep(GPIO_LED_PIN, 0);
			i2c_EEPROM_device_list->BUSY_FLAG = 0;
			kfree(receiveBuffer);
			return -1;
		}
		else
		{
			ready_to_write_flag = 1;
		}
		i2c_EEPROM_device_list->current_pointer = (i2c_EEPROM_device_list->current_pointer) + EEPROM_PAGE_SIZE;
		//If current position of pointer has reached the last position then reset it back to 0
		if((i2c_EEPROM_device_list->current_pointer) >= (EEPROM_PAGE_SIZE * NUMBER_OF_PAGES))
		{
			i2c_EEPROM_device_list->current_pointer = 0;
		}
		tempPointer = i2c_EEPROM_device_list->current_pointer;
		
		//Switch Off LED after Write Operation Ends
		gpio_set_value_cansleep(GPIO_LED_PIN, 0);
		//Turn Off Busy Flag
		i2c_EEPROM_device_list->BUSY_FLAG = 0;
	}
	kfree(receiveBuffer);
	msleep_interruptible(3);
	//printk("i2c_flash.c: i2c_eeprom_write: End\n");
	return 0;
}

/**
* i2c_eeprom_read - Function to read EEPROM
* @filp: File Pointer
* @buf:Buffer
* @count:Number of Pages
* @ppos:Position Pointer
*
* Returns 0.
* 
* Description: This function is called to read data from EEPROM
*/
static ssize_t i2c_eeprom_read(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
	int retValue;
	unsigned char Address[2];
	int tempPointer;

	//printk("i2c_flash.c: i2c_eeprom_read: Start\n");
	tempPointer = i2c_EEPROM_device_list->current_pointer;
	
	tempBuffer =kzalloc((count*EEPROM_PAGE_SIZE),GFP_KERNEL);
	if(tempBuffer == NULL)
	{
		printk("tempBuffer : Failure in malloc during read\n");
		return -ENOMEM;
	}
	
	//Set Address High Byte
	Address[0] = (unsigned )((tempPointer >> 8) & (0x00FF));
	//Set Address Low Byte
	Address[1] = (unsigned )(tempPointer & (0x00FF));
	//Switch ON LED before Read Operation Begins
	gpio_set_value_cansleep(GPIO_LED_PIN, 1);
	i2c_EEPROM_device_list->BUSY_FLAG = 1;
	//Issue a single I2C message in master transmit mode
	retValue = i2c_master_send(&(i2c_EEPROM_device_list->client), &Address[0],sizeof(Address));
	if(retValue < 0)
	{
		return -1;
	}
	else
	{
		msleep_interruptible(3);
	}
	//Receive a single I2C message in master receive mode
	retValue = i2c_master_recv(&(i2c_EEPROM_device_list->client),tempBuffer,(count*EEPROM_PAGE_SIZE));
	if(retValue < 0)
	{
		i2c_EEPROM_device_list->BUSY_FLAG = 0;
		ready_to_read_flag = 0;
		return -1;
	}
	else
	{
		ready_to_read_flag = 1;
	}
	//Switch OFF LED after Read Operation Ends
	gpio_set_value_cansleep(GPIO_LED_PIN, 0);
	i2c_EEPROM_device_list->BUSY_FLAG = 0;
	i2c_EEPROM_device_list->current_pointer = i2c_EEPROM_device_list->current_pointer + (count*EEPROM_PAGE_SIZE);
	// If pointer has reached last position then set it to the start position.
	if((i2c_EEPROM_device_list->current_pointer) >= ((EEPROM_PAGE_SIZE * NUMBER_OF_PAGES) - EEPROM_PAGE_SIZE))
	{
		i2c_EEPROM_device_list->current_pointer = 0;
	}
	tempPointer = i2c_EEPROM_device_list->current_pointer;

	//printk("i2c_flash.c: i2c_eeprom_read: End\n");
    return 0;
}

/**
* i2c_eeprom_ioctl - Function to perform IOCTL operations
* @file: File Pointer
* @arg: Arguments to the functions
* @cmd: Command to perform specific functions
*
* Returns pointer position, no of pages erased.
* 
* Description: This function is called to perform ioctl functions like 
* set pointer positions, get pointer positions, erase eeprom and to check the 
* status of the EEPROM.
*/
static long i2c_eeprom_ioctl(struct file *file, unsigned int arg, unsigned long cmd)
{
	long retValue =0;
	int tempPointer = 0;
	char tempIOCTLBuffer[EEPROM_PAGE_SIZE];
	char sendBuffer[EEPROM_PAGE_SIZE+2];
	int i=0;
	//printk(KERN_INFO "i2c_flash.c: eep_ioctl: Start\n");
	switch(cmd)
	{
		case FLASHGETS:
			{
				retValue = i2c_EEPROM_device_list->BUSY_FLAG;
			    break;
			}
		case FLASHGETP:
			{
				retValue = (i2c_EEPROM_device_list->current_pointer)/(EEPROM_PAGE_SIZE);
			    break;
			}
		case FLASHSETP:
			{
				if(arg > 511 || arg < 0)
				{
					retValue = -1;
				}
				else
				{
					i2c_EEPROM_device_list->current_pointer = arg * EEPROM_PAGE_SIZE;
					retValue = (i2c_EEPROM_device_list->current_pointer)/(EEPROM_PAGE_SIZE);
				}
				break;
			}
		case FLASHERASE:
			{
				if(i2c_EEPROM_device_list->BUSY_FLAG == 0)
				{
					tempPointer = 0;
					i2c_EEPROM_device_list->current_pointer = 0;
					for(i=0;i<(EEPROM_PAGE_SIZE);i++)
					{
						tempIOCTLBuffer[i] = 0xFF;
					}
					for(i=0;i<NUMBER_OF_PAGES;i++)
					{
						//Set Address High Byte
						sendBuffer[0]= ((tempPointer >> 8) & 0xFF);
						//Set Address Low Byte
						sendBuffer[1]= (tempPointer & 0xFF);
						memcpy(&sendBuffer[2], &tempIOCTLBuffer[0], EEPROM_PAGE_SIZE);
						gpio_set_value_cansleep(GPIO_LED_PIN, 1);
						retValue = i2c_master_send(&(i2c_EEPROM_device_list->client), sendBuffer,sizeof(sendBuffer));
						gpio_set_value_cansleep(GPIO_LED_PIN, 0);
						if(retValue<0)
						{
							printk("Error:i2c_master_send");
							gpio_set_value_cansleep(GPIO_LED_PIN, 0);
							return -1;
						}
						i2c_EEPROM_device_list->current_pointer = (i2c_EEPROM_device_list->current_pointer) + EEPROM_PAGE_SIZE;
						tempPointer = i2c_EEPROM_device_list->current_pointer;
						printk("Page %i Erase Successfull\n",i);
					}
					i2c_EEPROM_device_list->current_pointer = 0;
					retValue = i2c_EEPROM_device_list->current_pointer;
					break;
				}
				else
				{
					printk("Resource (EEPROM) is Busy\n");
					retValue = -1;
				}
			}
		default:
			break;
	}
	//printk("i2c_flash.c: eep_ioctl: End\n");
	return retValue;
}
/**
 * Driver entry points 
 */
static struct file_operations i2c_eeprom_fops = {
  .owner   = THIS_MODULE,
  .read    = i2c_eeprom_read_from_queue,
  .write   = i2c_eeprom_write_into_queue,
  .open    = i2c_eeprom_open,
  .release = i2c_eeprom_release,
  .unlocked_ioctl   = i2c_eeprom_ioctl,
};

/**
 * Device Initialization
 */
static int __init i2c_eeprom_init(void)
{
	int err;
	struct i2c_adapter *adap;

	/* Allocate the per-device data structure, i2c_EEPROM_dev */
	i2c_EEPROM_device_list = kmalloc(sizeof(struct i2c_EEPROM_dev), GFP_KERNEL);

	memset(i2c_EEPROM_device_list, 0, sizeof(struct i2c_EEPROM_dev));
  
	/* Register and create the /dev interfaces to access the EEPROM banks.  */
	if(alloc_chrdev_region(&dev_number, I2C_MINOR_NUMBER, 1, DRIVER_NAME) < 0)
	{
		printk("Can't register device\n");
		return -1;
	}

	eep_class = class_create(THIS_MODULE, DEVICE_NAME);
	
	/* Setting i2c driver name */
	sprintf(i2c_EEPROM_device_list->name, DEVICE_NAME);
	
	/* Connect the file operations with cdev */
	cdev_init(&i2c_EEPROM_device_list->cdev, &i2c_eeprom_fops);
	
	/* Connect the major/minor number to the cdev */
	if (cdev_add(&i2c_EEPROM_device_list->cdev, MKDEV(MAJOR(dev_number), 0), 1))
	{
		printk("Bad kmalloc\n");
		return 1;
	}

	device_create(eep_class, NULL, MKDEV(MAJOR(dev_number), 0), NULL, DEVICE_NAME);
	err = gpio_request_one(GPIO_MUX_PIN, GPIOF_OUT_INIT_LOW, "Mux");
	if(err)
	{
		//printk("MUX ERROR");
	}
	gpio_set_value_cansleep(GPIO_MUX_PIN, 0);
	/* Inform the I2C core about driver existence. */
	err = i2c_add_driver(&eeprom_driver);
	if(err)
	{
		printk("Registering I2C driver failed, errno is %d\n", err);
		return err;
	}
	
	i2c_eeprom_workqueue = create_singlethread_workqueue(WORK_QUEUE_NAME);
	
	adap = i2c_get_adapter(I2C_MINOR_NUMBER);
	client_core = i2c_new_device(adap, i2c_eeprom_board_info);

	printk("EEPROM Driver Initialized.\n");
	return 0;
}

/**
* i2c_eeprom_exit - Function to perform IOCTL operations
* @file: File Pointer
* @arg: Arguments to the functions
* @cmd: Command to perform specific functions
*
* Returns pointer position, no of pages erased.
* 
* Description: This function is called to perform ioctl functions like 
* set pointer positions, get pointer positions, erase eeprom and to check the 
* status of the EEPROM.
*/
static void __exit i2c_eeprom_exit(void)
{
	//printk("i2c_flash.c: i2c_dev_exit: Start\n");
	device_destroy(eep_class, MKDEV(MAJOR(dev_number), 0));
	cdev_del(&(i2c_EEPROM_device_list->cdev));
	class_destroy(eep_class);
	unregister_chrdev(MAJOR(dev_number), DRIVER_NAME);
	i2c_unregister_device(client_core);
	i2c_del_driver(&eeprom_driver);
	if(i2c_eeprom_workqueue)
	{
		destroy_workqueue(i2c_eeprom_workqueue);
	}
	kfree(i2c_EEPROM_device_list);
	//printk("i2c_flash.c: i2c_dev_exit: End\n");
}

/**
* i2c_eeprom_work_queue_fn - Function to perform IOCTL operations
* @work: Work Data Structure
*
* Returns void
* 
* Description: This function is called from workers thread. After receiving the
* call from workers thread, it calls respective read or write functions based
* on the type of work to be performed.
*/
static void i2c_eeprom_work_queue_fn( struct work_struct *work)
{
	int retValue = 0;
	I2C_WORK_QUEUE *rcvd_work = (I2C_WORK_QUEUE *)work;
	//printk("i2c_flash.c : i2c_eeprom_work_queue_fn : Start\n");
	if(rcvd_work->read_or_write == 'W')
	{
		retValue = i2c_eeprom_write(rcvd_work->queue_Data.file, rcvd_work->queue_Data.buf, rcvd_work->queue_Data.count, rcvd_work->queue_Data.offset);
	}
	else if(rcvd_work->read_or_write == 'R')
	{
		retValue = i2c_eeprom_read(rcvd_work->queue_Data.file, rcvd_work->queue_Data.buf, rcvd_work->queue_Data.count, rcvd_work->queue_Data.offset);
	}
	else
	{
		printk("Invalid work type\n");
	}
	//printk("i2c_flash.c : i2c_eeprom_work_queue_fn : End\n");
	return;
}

/**
* i2c_eeprom_write_into_queue - Function to add job into the worker thread for write operation
* 
* @file: Work Data Structure
* @buf: Data Buffer
* @count: Count of number of pages.
* @offset: offset position pointer
*
* Returns ssize_t
* 
* Description: This function is the .write function entry point from the user space. After receiving
* the request from the user, the job is assigned to workers thread and is immediately returned back
* to user space. So this makes the function to appear as Non Blocking call.
*/
static ssize_t i2c_eeprom_write_into_queue(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
	int retValue = 0;
	int resValue = 0;
	I2C_WORK_QUEUE *send_work_queue;
	//printk("i2c_flash.c : i2c_eeprom_write_into_queue : Start\n");
	if(count < 1 || count > 512)
	{
		printk("Invalid Input for Page Number\n");
		return 0;
	}
	if(i2c_eeprom_workqueue)
	{
		if(i2c_EEPROM_device_list->BUSY_FLAG == 0)
		{
			retValue = 0;
			temp_write_buff = kmalloc((count*EEPROM_PAGE_SIZE), GFP_KERNEL);
			if (temp_write_buff == NULL)
			{
				printk("Failure in malloc during write\n");
				return -ENOMEM;
			}
			resValue = copy_from_user((void *)temp_write_buff, (void __user *)buf, (count*EEPROM_PAGE_SIZE));
			send_work_queue = (I2C_WORK_QUEUE *)kmalloc(sizeof(I2C_WORK_QUEUE), GFP_KERNEL);
			if (send_work_queue == NULL)
			{
				printk("Failure in malloc during write_transfer\n");
				return -ENOMEM;
			}
			/* Initialize the work into work queue function */
			INIT_WORK( (struct work_struct *)send_work_queue, (void *)i2c_eeprom_work_queue_fn);
			
			send_work_queue->work_id = ++WORK_ID_COUNTER;
			send_work_queue->read_or_write     = 'W';
			send_work_queue->status_Flag	   = 'Q';
			send_work_queue->queue_Data.file   = file;
			send_work_queue->queue_Data.buf    = (char*)temp_write_buff;
			send_work_queue->queue_Data.count  = count;
			send_work_queue->queue_Data.offset = offset;
			
			retValue = queue_work(i2c_eeprom_workqueue, (struct work_struct *)send_work_queue );
			if(retValue > 0)
			{
				printk("Write Work Successfully added in Work Queue\n");
			}
			retValue = 0;
		}
		else
		{
			retValue = -EBUSY;
		}
	}
	else
	{
		printk("Work Queue is NULL\n");
	}
	//printk("i2c_flash.c : i2c_eeprom_write_into_queue : End\n");
	return retValue;
}

/**
* i2c_eeprom_read_from_queue - Function to add job into the worker thread for read operation 
* 
* @file: Work Data Structure
* @buf: Data Buffer
* @count: Count of number of pages.
* @offset: offset position pointer
*
* Returns ssize_t
* 
* Description: This function is the .read function entry point from the user space. After receiving
* the request from the user, the job is assigned to workers thread and is immediately returned back
* to user space. So this makes the function to appear as Non Blocking call.
*/
static ssize_t i2c_eeprom_read_from_queue(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	int retValue = 0;
	I2C_WORK_QUEUE *send_work_queue;
	//printk("i2c_flash.c : i2c_eeprom_read_from_queue : Start\n");
	if(count < 1 || count > 512  )
	{
		printk("Invalid Input for Page Number\n");
		return 0;
	}
	if(i2c_eeprom_workqueue)
	{
		temp_read_buff = kzalloc(count*EEPROM_PAGE_SIZE, GFP_KERNEL);
		if(temp_read_buff == NULL)
		{
			printk("Failure in malloc during read\n");
			return -ENOMEM;
		}

		/* Allocate memory */
		send_work_queue = (I2C_WORK_QUEUE *)kmalloc(sizeof(I2C_WORK_QUEUE), GFP_KERNEL);
		if(send_work_queue == NULL)
		{
			printk("Failure in malloc during read_transfer\n");
			return -ENOMEM;
		}
		/* Initialize the work into work queue function */
		INIT_WORK((struct work_struct *)send_work_queue, (void *)i2c_eeprom_work_queue_fn);
		
		send_work_queue->work_id 		   = ++WORK_ID_COUNTER;
		send_work_queue->status_Flag	   = 'Q';
		send_work_queue->read_or_write     = 'R';
		send_work_queue->queue_Data.file   = file;
		send_work_queue->queue_Data.buf    = (char *)temp_read_buff;
		send_work_queue->queue_Data.count  = count;
		send_work_queue->queue_Data.offset = offset;
		
		if(ready_to_read_flag == 0)
		{
			if(i2c_EEPROM_device_list->BUSY_FLAG == 0)
			{
				retValue = queue_work(i2c_eeprom_workqueue, (struct work_struct *)send_work_queue);
				if(retValue > 0)
				{
					printk("Successfully queued readqueue\n");
				}
				ERROR = -EAGAIN;
				retValue = -1;
			}
			else
			{
				ERROR = -EBUSY;
				retValue = -1;
			}
			return retValue;
		}
		else
		{
			retValue = copy_to_user((void *)buf, tempBuffer, (count*EEPROM_PAGE_SIZE));
			ready_to_read_flag = 0;
			if(temp_read_buff != NULL)
			{
				kfree(temp_read_buff);
				temp_read_buff = NULL;
			}
			if(tempBuffer != NULL)
			{
				kfree(tempBuffer);
				tempBuffer = NULL;
			}
			retValue = 0;
			return retValue;
		}
	}
	else
	{
		printk("Work Queue is NULL\n");
	}
	//printk("i2c_flash.c : i2c_eeprom_read_from_queue : End\n");
	return retValue;
}

MODULE_AUTHOR("Ankit Rathi");
MODULE_DESCRIPTION("I2C EEPROM driver");
MODULE_LICENSE("GPL");

module_init(i2c_eeprom_init);
module_exit(i2c_eeprom_exit);
