/******************************************************************************
 *
 * File Name: i2c_flash.c
 *
 * Author: Ankit Rathi (ASU ID: 1207543476)
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

/**
 * Define constants using the macro
 */
#define DRIVER_NAME 		"i2c_flash"
#define DEVICE_NAME 		"i2c_flash"
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
 * Global Variable Declarrations
 */ 
static dev_t dev_number;          					/* Allotted Device Number */
static struct class *eep_class;   					/* Device class */
struct i2c_EEPROM_dev *i2c_EEPROM_device_list;     	/* List of private data structures, one per bank */
struct i2c_client* client_core = NULL;

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
    .name       =  DEVICE_NAME,      		/* Name */
    .owner		= THIS_MODULE,				/* Owner */
  },
  .id_table     = eeprom_id_table, 			/* ID */
  .probe 		= eeprom_probe,        		/* Probe Method */
  .remove  		= eeprom_remove,       		/* Remove Method */
};

/**
* i2c_eeprom_open - Function called when EEPROM device is opened for use.
* @client: I2C Client
*
* Returns 0.
* 
* Description: This function is called when a driver is registerd and a already
* 	plugged in device is plugged out.
*/
int i2c_eeprom_open(struct inode *inode, struct file *file)
{
	int ret;
	//printk("i2c_flash.c: eep_open: Start\n");
	//Setting Current Pointer Position to 0 for first time.
	i2c_EEPROM_device_list->current_pointer = 0;
	ret = gpio_request_one(GPIO_LED_PIN, GPIOF_OUT_INIT_LOW, "Led");
	if(ret)
	{
		//printk("LED ERROR");
	}
	gpio_set_value_cansleep(GPIO_LED_PIN, 0);
	i2c_EEPROM_device_list->BUSY_FLAG = 0;
	//printk("i2c_flash.c: eep_open: End\n");
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
* @filp: File Pointer
* @buf:Buffer Space
* @count:Count to hold number of pages
* @ppos:offset
*
* Returns 0.
* 
* Description: This function is called when a driver is registerd and a already
* 	plugged in device is plugged out.
*/
ssize_t i2c_eeprom_write(struct file *filp, const char *buf, size_t count, loff_t *ppos)
{
	int retValue,i,j;
	char *receiveBuffer;
	char sendBuffer[EEPROM_PAGE_SIZE+2];
	int tempPointer;
	if(count < 1 || count > 512)
	{
		printk("Invalid Input for Page Number\n");
		return -1;
	}
	receiveBuffer = kmalloc(count * EEPROM_PAGE_SIZE, GFP_KERNEL);
	tempPointer = i2c_EEPROM_device_list->current_pointer;
	//copy contents from userspace to kernel space
	retValue = copy_from_user(receiveBuffer,buf,(count * EEPROM_PAGE_SIZE));
	//Switch ON LED before Write Operation Begins
	gpio_set_value_cansleep(GPIO_LED_PIN, 1);
	//Turn On Busy Flag
	i2c_EEPROM_device_list->BUSY_FLAG = 1;
	
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
		//Set Address High Byte
		sendBuffer[0]= ((tempPointer >> 8) & 0xFF);
		//Set Address Low Byte			
		sendBuffer[1]= (tempPointer & 0xFF);        			
		memcpy(&sendBuffer[2], &receiveBuffer[i * EEPROM_PAGE_SIZE], EEPROM_PAGE_SIZE);
		for (j=0; j<EEPROM_PAGE_SIZE; j++)
			printk("%c",receiveBuffer[(i * EEPROM_PAGE_SIZE) + j]);
		//Issue a single I2C message in master transmit mode
		retValue = i2c_master_send(&(i2c_EEPROM_device_list->client), sendBuffer,sizeof(sendBuffer));
		if(retValue<0)
		{
			printk("Error:i2c_master_send");
			gpio_set_value_cansleep(GPIO_LED_PIN, 0);
			i2c_EEPROM_device_list->BUSY_FLAG = 0;
			kfree(receiveBuffer);
			return -1;
		}
		i2c_EEPROM_device_list->current_pointer = (i2c_EEPROM_device_list->current_pointer) + EEPROM_PAGE_SIZE;
		//If current position of pointer has reached the last position then reset it back to 0
		if((i2c_EEPROM_device_list->current_pointer) >= (EEPROM_PAGE_SIZE * NUMBER_OF_PAGES))
		{
			i2c_EEPROM_device_list->current_pointer = 0;
		}
		tempPointer = i2c_EEPROM_device_list->current_pointer;
	}
	//Switch Off LED after Write Operation Ends
	gpio_set_value_cansleep(GPIO_LED_PIN, 0);
	//Turn Off Busy Flag
	i2c_EEPROM_device_list->BUSY_FLAG = 0;
	kfree(receiveBuffer);
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
ssize_t i2c_eeprom_read(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
	int retValue;
	char *SendBuffer;
	unsigned char Address[2];
	int tempPointer;
	if(count < 1 || count > 512)
	{
		printk("Invalid Input for Page Number\n");
		return -1;
	}
	SendBuffer =kzalloc((count*EEPROM_PAGE_SIZE),GFP_KERNEL);
	tempPointer = i2c_EEPROM_device_list->current_pointer;
	
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
		kfree(SendBuffer);
		return -1;
	}
	//Recieve a single I2C message in master receive mode
	retValue = i2c_master_recv(&(i2c_EEPROM_device_list->client),SendBuffer,(count*EEPROM_PAGE_SIZE));
	if(retValue < 0)
	{
		kfree(SendBuffer);
		return -1;
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

    retValue = copy_to_user(buf, SendBuffer,(count * EEPROM_PAGE_SIZE));
	if(retValue)
	{
		kfree(SendBuffer);
		return -1;
	}
	kfree(SendBuffer);
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
	short retValue =0;
	int tempPointer = 0;
	char tempBuffer[EEPROM_PAGE_SIZE];
	char sendBuffer[EEPROM_PAGE_SIZE+2];
	int i=0;
	switch(cmd)
	{
		case FLASHGETS:
			retValue = i2c_EEPROM_device_list->BUSY_FLAG;
			break;
		case FLASHGETP:
			retValue = (i2c_EEPROM_device_list->current_pointer)/(EEPROM_PAGE_SIZE);
			break;
		case FLASHSETP:
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
		case FLASHERASE:
			tempPointer = 0;
			i2c_EEPROM_device_list->current_pointer = 0;
			for(i=0;i<(EEPROM_PAGE_SIZE);i++)
				tempBuffer[i] = 0xFF;
			for(i=0;i<NUMBER_OF_PAGES;i++)
			{
				//Set Address High Byte
				sendBuffer[0]= ((tempPointer >> 8) & 0xFF);
				//Set Address Low Byte		
				sendBuffer[1]= (tempPointer & 0xFF);        			
				memcpy(&sendBuffer[2], &tempBuffer[0], EEPROM_PAGE_SIZE);
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
		default:
			break;
	}
	return retValue;
}

/**
 * Driver entry points
 */
static struct file_operations ee_fops = {
  .owner   = THIS_MODULE,
  .read    = i2c_eeprom_read,
  .unlocked_ioctl   = i2c_eeprom_ioctl,
  .open    = i2c_eeprom_open,
  .release = i2c_eeprom_release,
  .write   = i2c_eeprom_write,
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
	cdev_init(&i2c_EEPROM_device_list->cdev, &ee_fops);
	
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
	else
	{
	}
	gpio_set_value_cansleep(GPIO_MUX_PIN, 0);
	/* Inform the I2C core about driver existence. */
	err = i2c_add_driver(&eeprom_driver);
	if(err)
	{
		printk("Registering I2C driver failed, errno is %d\n", err);
		return err;
	}
	
	adap = i2c_get_adapter(I2C_MINOR_NUMBER);
	client_core = i2c_new_device(adap, i2c_eeprom_board_info);

	printk("EEPROM Driver Initialized.\n");
	return 0;
}

/**
* eeprom_exit - Function to perform IOCTL operations
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
	device_destroy(eep_class, MKDEV(MAJOR(dev_number), 0));
	cdev_del(&(i2c_EEPROM_device_list->cdev));
	class_destroy(eep_class);
	unregister_chrdev(MAJOR(dev_number), DRIVER_NAME);
	i2c_unregister_device(client_core);
	i2c_del_driver(&eeprom_driver);
	kfree(i2c_EEPROM_device_list);
}

MODULE_AUTHOR("Ankit Rathi");
MODULE_DESCRIPTION("I2C EEPROM driver");
MODULE_LICENSE("GPL");

module_init(i2c_eeprom_init);
module_exit(i2c_eeprom_exit);
