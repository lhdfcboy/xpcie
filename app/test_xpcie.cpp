#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h> /* open */
#include <iostream>
#include <fstream>
#include <assert.h>
#include <malloc.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <byteswap.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <termios.h>

#include <sys/types.h>
#include <sys/mman.h>

#include "Thread.h"
#include "LogUtils.h"

using std::cout;
using std::endl;

#define DRV_NAME "xpcie"

#if (USE_LOG4CPP ==1 )
#include <iostream>
#include <log4cpp/Category.hh>
#include <log4cpp/FileAppender.hh>
#include <log4cpp/SimpleLayout.hh>
#include <log4cpp/OstreamAppender.hh>
#include <log4cpp/PropertyConfigurator.hh>
#include <log4cpp/PatternLayout.hh>
#include <log4cpp/RollingFileAppender.hh>
#include <log4cpp/NDC.hh>
log4cpp::Category& rootCategory = log4cpp::Category::getRoot();
void initLogger()
{

	try
	{
		log4cpp::PropertyConfigurator::configure("./log4cpp.accel.conf");
	}	
	catch (log4cpp::ConfigureFailure& f)	
	{
		//std::cout << "Configure Problem " << f.what() << std::endl;
		/*
		PatternLayout supports following set of format characters:
		%% - a single percent sign
		%c - the category
		%d - the date\n Date format: The date format character may be followed by a date format specifier enclosed between braces. For example, %d{%H:%M:%S,%l} or %d{%d %m %Y %H:%M:%S,%l}. If no date format specifier is given then the following format is used: "Wed Jan 02 02:03:55 1980". The date format specifier admits the same syntax as the ANSI C function strftime, with 1 addition. The addition is the specifier %l for milliseconds, padded with zeros to make 3 digits.
		%m - the message
		%n - the platform specific line separator
		%p - the priority
		%r - milliseconds since this layout was created.
		%R - seconds since Jan 1, 1970
		%u - clock ticks since process start
		%x - the NDC
		%t - thread name
		By default, ConversionPattern for PatternLayout is set to "%m%n".
		*/
		log4cpp::Appender *appender1 = new log4cpp::OstreamAppender("console",&std::cout);

		log4cpp::PatternLayout *pl2 = new log4cpp::PatternLayout();
		pl2->setConversionPattern("[%x] %m%n");
		appender1->setLayout(pl2);

		rootCategory.setPriority(log4cpp::Priority::DEBUG);
		rootCategory.addAppender(appender1);

		rootCategory.debug("log4cpp ok");
	}
}
#else
	NewLine globalNewLineObject;

#endif



	/* ltoh: little to host */
	/* htol: little to host */
#if __BYTE_ORDER == __LITTLE_ENDIAN
#  define ltohl(x)       (x)
#  define ltohs(x)       (x)
#  define htoll(x)       (x)
#  define htols(x)       (x)
#elif __BYTE_ORDER == __BIG_ENDIAN
#  define ltohl(x)     __bswap_32(x)
#  define ltohs(x)     __bswap_16(x)
#  define htoll(x)     __bswap_32(x)
#  define htols(x)     __bswap_16(x)
#endif

#define FATAL do { fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", __LINE__, __FILE__, errno, strerror(errno)); exit(1); } while(0)

#define MAP_SIZE (64*1024UL)
#define MAP_MASK (MAP_SIZE - 1)

	int testReg(int argc, char **argv) {
		int fd;
		void *map_base, *virt_addr;
		uint32_t read_result, writeval;
		off_t target;
		/* access width */
		int access_width = 'w';
		char *device;


		/* not enough arguments given? */
		if (argc < 3) {
			fprintf(stderr, "\nUsage:\t%s <device> <address> [[type] data]\n"
				"\tdevice  : character device to access\n"
				"\taddress : memory address to access\n"
				"\ttype    : access operation type : [b]yte, [h]alfword, [w]ord\n"
				"\tdata    : data to be written for a write\n\n",
				argv[0]);
			exit(1);
		}

		printf("argc = %d\n", argc);

		device = strdup(argv[1]);
		printf("device: %s\n", device);
		target = strtoul(argv[2], 0, 0);
		printf("address: 0x%08x\n", (unsigned int)target);

		printf("access type: %s\n", argc >= 4 ? "write" : "read");

		/* data given? */
		if (argc >= 4)
		{
			printf("access width given.\n");
			access_width = tolower(argv[3][0]);
		}
		printf("access width: ");
		if (access_width == 'b')
			printf("byte (8-bits)\n");
		else if (access_width == 'h')
			printf("half word (16-bits)\n");
		else if (access_width == 'w')
			printf("word (32-bits)\n");
		else
		{
			printf("word (32-bits)\n");
			access_width = 'w';
		}

		if ((fd = open(argv[1], O_RDWR | O_SYNC)) == -1) FATAL;
		printf("character device %s opened.\n", argv[1]);
		fflush(stdout);

		/* map one page */
		map_base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (map_base == (void *)-1) FATAL;
		printf("Memory mapped at address %p.\n", map_base);
		fflush(stdout);

		/* calculate the virtual address to be accessed */
		virt_addr = map_base + target;
		/* read only */
		if (argc <= 4) {
			//printf("Read from address %p.\n", virt_addr); 
			switch (access_width) {
			case 'b':
				read_result = *((uint8_t *)virt_addr);
				printf("Read 8-bits value at address 0x%08x (%p): 0x%02x\n", (unsigned int)target, virt_addr, (unsigned int)read_result);
				break;
			case 'h':
				read_result = *((uint16_t *)virt_addr);
				/* swap 16-bit endianess if host is not little-endian */
				read_result = ltohs(read_result);
				printf("Read 16-bit value at address 0x%08x (%p): 0x%04x\n", (unsigned int)target, virt_addr, (unsigned int)read_result);
				break;
			case 'w':
				read_result = *((uint32_t *)virt_addr);
				/* swap 32-bit endianess if host is not little-endian */
				read_result = ltohl(read_result);
				printf("Read 32-bit value at address 0x%08x (%p): 0x%08x\n", (unsigned int)target, virt_addr, (unsigned int)read_result);
				return (int)read_result;
				break;
			default:
				fprintf(stderr, "Illegal data type '%c'.\n", access_width);
				exit(2);
			}
			fflush(stdout);
		}
		/* data value given, i.e. writing? */
		if (argc >= 5)
		{
			writeval = strtoul(argv[4], 0, 0);
			switch (access_width)
			{
			case 'b':
				printf("Write 8-bits value 0x%02x to 0x%08x (0x%p)\n", (unsigned int)writeval, (unsigned int)target, virt_addr);
				*((uint8_t *)virt_addr) = writeval;
#if 0
				if (argc > 4) {
					read_result = *((uint8_t *)virt_addr);
					printf("Written 0x%02x; readback 0x%02x\n", writeval, read_result);
				}
#endif
				break;
			case 'h':
				printf("Write 16-bits value 0x%04x to 0x%08x (0x%p)\n", (unsigned int)writeval, (unsigned int)target, virt_addr);
				/* swap 16-bit endianess if host is not little-endian */
				writeval = htols(writeval);
				*((uint16_t *)virt_addr) = writeval;
#if 0
				if (argc > 4) {
					read_result = *((uint16_t *)virt_addr);
					printf("Written 0x%04x; readback 0x%04x\n", writeval, read_result);
				}
#endif
				break;
			case 'w':
				printf("Write 32-bits value 0x%08x to 0x%08x (0x%p)\n", (unsigned int)writeval, (unsigned int)target, virt_addr);
				/* swap 32-bit endianess if host is not little-endian */
				writeval = htoll(writeval);
				*((uint32_t *)virt_addr) = writeval;
#if 0
				if (argc > 4) {
					read_result = *((uint32_t *)virt_addr);
					printf("Written 0x%08x; readback 0x%08x\n", writeval, read_result);
				}
#endif
				break;
			}
			fflush(stdout);
		}
		if (munmap(map_base, MAP_SIZE) == -1) FATAL;
		close(fd);
		return 0;
	}
int testXpcieUser()
{
	int fd0 = 0;
	int buffer;
	ssize_t readSize = 0;
	fd_set readFdset;
	timeval timeout;

	timeout.tv_sec = 10;
	timeout.tv_usec = 0;

	fd0 = open("/dev/" DRV_NAME "_user", O_RDWR);
	if (fd0 < 0)
	{
		logError << "open /dev/" DRV_NAME "_user failed";
		return -1;
	}

	close(fd0);
}

int main (int argc, char** argv)
{
	int ret;
#if( USE_LOG4CPP == 1)
	initLogger();
	log4cpp::NDC::push(("main    "));
#endif

	testReg(argc, argv);

	return ret;
}