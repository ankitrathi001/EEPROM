/******************************************************************************
 *
 * File Name: main_2.c
 *
 * Author: Ankit Rathi (ASU ID: 1207543476)
 *
 * Date: 08-OCT-2014
 *
 * Description: A test program to test the working scenarios of EEPROM 
 * 				Read, Write, Pointer Positions and Erase EEPROM
 * 
 *****************************************************************************/

/**
 *Include Library Headers 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

/**
 * Define constants using the macro
 */ 
#define DEVICE_PATH 		"/dev/i2c_flash"
#define EEPROM_PAGE_SIZE	64
#define NUMBER_OF_PAGES		512
#define FLASHGETS			1
#define FLASHGETP			2
#define FLASHSETP			3
#define FLASHERASE			4

void generate_randomString(char *s, const int len);

/**
 * Main Function
 */
int main(int argc, char **argv)
{
	int fd, res, option;
	/* open device */
	fd = open(DEVICE_PATH, O_RDWR);
	if (fd < 0 )
	{
		printf("Can not open device file.\n");	
		return 0;
	}
	else
	{
		printf("Device Opened Successfully.\n");
		while(1)
		{
			printf("\nInput command: \n1. Read\n2. Write\n3. FLASHGETS\n4. FLASHGETP\n5. FLASHSETP\n6. FLASHERASE\n7. Exit\n");
			scanf("%d",&option);
			switch(option)
			{
				case 1:
					read_EEPROM(fd);
					break;
				case 2:
					write_EEPROM(fd);
					break;
				case 3:
					get_Status_EEPROM(fd);
					break;
				case 4:
					get_Pointer_EEPROM(fd);
					break;
				case 5:
					set_Pointer_EEPROM(fd);
					break;
				case 6:
					erase_EEPROM(fd);
					break;
				case 7:
						exit(0);
				default: printf("Enter Valid Option\n");
					break;
			}
		}
		close(fd);
	}
}

/**
* read_EEPROM - Function to read EEPROM
* @fd: File Descriptor
*
* Returns negative errno, or else the number of bytes read.
* 
* Description: Takes input from user on number of pages to read and make a call to 
* i2c_flash driver to read pages
*/
int read_EEPROM(int fd)
{
	int retValue,count,cp;
	unsigned int i,j,k;
	char buf[EEPROM_PAGE_SIZE * NUMBER_OF_PAGES];

	printf("Enter the Number of pages to read from EEPROM\n");
	scanf("%d",&count);
	
	cp = ioctl(fd,&k, FLASHGETP);
	retValue = read(fd,&(buf[0]), count);
	if (retValue < 0)
	{
		printf("Read Failure\n");
	}
	else
	{
		printf("Read Successful\n");
		for(j=0;j<count;j++)
		{
			printf("Page %d : ",(j+cp));
			for(i = 0; i < EEPROM_PAGE_SIZE; i++)
			{
				printf("%c",buf[i + (j*EEPROM_PAGE_SIZE)]);
			}
			printf("\n");
		}
	}
	return retValue;
}

/**
* write_EEPROM - Function to write into EEPROM
* @fd: File Descriptor
*
* Returns negative errno, or else the number of bytes written.
* 
* Description: Takes input from user on number of pages to write and make a call to 
* i2c_flash driver to write corresponding number of pages
*/
int write_EEPROM(int fd)
{
	int retValue, count;
	char* writeBuffer;

	//char writeBuffer[EEPROM_PAGE_SIZE * NUMBER_OF_PAGES];
    //sprintf(writeBuffer,"Page1111Page1111Page1111Page1111Page1111Page1111Page1111Page1111Page2222Page2222Page2222Page2222Page2222Page2222Page2222Page2222");
	
	printf("Enter the Number of pages to write to EEPROM\n");
	scanf("%d",&count);
	
	writeBuffer = (char *)malloc(count*EEPROM_PAGE_SIZE);
	generate_randomString(writeBuffer,count*EEPROM_PAGE_SIZE);
	
	retValue = write(fd, writeBuffer, count);
	if (retValue < 0)
	{
		printf("Write Failure\n");
	}
	else
	{
		printf("Write Successful\n");
	}
	free(writeBuffer);
	return retValue;
}

/**
* get_Status_EEPROM - Function to get Busy Status of EEPROM
* @fd: File Descriptor
*
* Returns EEPROM Status.
* 
* Description: It return the busy status of EEPROM.
* 				1: Busy, 0: Free
*/
int get_Status_EEPROM(int fd)
{
	unsigned int retValue,i;
	retValue = ioctl(fd,&i,FLASHGETS);
	if(retValue == 0)
	{
		printf("EEPROM is Free\n");
	}
	else
	{
		printf("EEPROM is Busy\n");
	}
	return retValue;
}

/**
* get_Pointer_EEPROM - Function to get Current Pointer Position of EEPROM
* @fd: File Descriptor
*
* Returns Current Pointer Position.
* 
* Description: This functions fetches the current pointer position of EEPROM
* 				value ranges from 0-511
*/
int get_Pointer_EEPROM(int fd)
{
	int retValue;
	unsigned int i;
	
	retValue = ioctl(fd,i, FLASHGETP);
	if (retValue < 0)
	{
		printf("EEPROM Fetch Pointer Failure\n");
	}
	else
	{
		printf("EEPROM Fetch Pointer Successful\n");
		printf("Current EEPROM Pointer Position : %d\n",retValue);
	}
	return retValue;
}

/**
* set_Pointer_EEPROM - Function to set Current Pointer Position of EEPROM
* @fd: File Descriptor
*
* Returns Current Pointer Position.
* 
* Description: This functions sets the current pointer position of EEPROM
* 				value ranges from 0-511
*/
int set_Pointer_EEPROM(int fd)
{
	int retValue;
	unsigned int i;
	
	printf("Enter the Pointer Set Position (0-511)\n");
	scanf("%d",&i);
	retValue = ioctl(fd,i,FLASHSETP);
	if(retValue < 0)
	{
		printf("EEPROM Set Pointer Failure\n");
	}
	else
	{
		printf("EEPROM Set Pointer Successful\n");
		printf("Current EEPROM Pointer Position : %d\n",retValue);
	}
	return retValue;
}

/**
* erase_EEPROM - Function to erase all pages of EEPROM
* @fd: File Descriptor
*
* Returns Number of pages erased
* 
* Description: This function erases all the pages of EEPROM
*/
int erase_EEPROM(int fd)
{
	int retValue=0,i;
	unsigned char c;
	retValue = ioctl(fd,i,FLASHERASE);
	if (retValue < 0)
	{
		printf("EEPROM Erase Failure\n");
	}
	else
	{
		printf("EEPROM Erase Successful\n");
		printf("Current EEPROM Pointer Position : %d\n",retValue);
	}
	return retValue;
}

/**
* generate_randomString - Function to generate random string of given length
* @s: File Descriptor
* @len: Length of String
*
* Returns String
* 
* Description: Function to generate random string of given length
*/
void generate_randomString(char *s, const int len)
{
	int i;
    const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        
    for(i = 0; i < len; ++i)
    {
        s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
    }
    s[len] = 0;
}
