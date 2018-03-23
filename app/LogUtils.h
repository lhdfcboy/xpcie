#pragma once

#include <typeinfo>
#include <sys/time.h>
#include <iostream>
#include <sstream>
using std::cout; 
using std::endl;



#define COLOR_NONE                 "\e[0m"
#define COLOR_BLACK                "\e[0;30m"
#define COLOR_L_BLACK              "\e[1;30m"
#define COLOR_RED                  "\e[0;31m"
#define COLOR_L_RED                "\e[1;31m"
#define COLOR_GREEN                "\e[0;32m"
#define COLOR_L_GREEN              "\e[1;32m"
#define COLOR_BROWN                "\e[0;33m"
#define COLOR_YELLOW               "\e[1;33m"
#define COLOR_BLUE                 "\e[0;34m"
#define COLOR_L_BLUE               "\e[1;34m"
#define COLOR_PURPLE               "\e[0;35m"
#define COLOR_L_PURPLE             "\e[1;35m"
#define COLOR_CYAN                 "\e[0;36m"
#define COLOR_L_CYAN               "\e[1;36m"
#define COLOR_GRAY                 "\e[0;37m"
#define COLOR_WHITE                "\e[1;37m"

#define CONSOLE_BOLD                 "\e[1m"
#define  CONSOLE_UNDERLINE            "\e[4m"
#define  CONSOLE_BLINK                "\e[5m"
#define  CONSOLE_REVERSE              "\e[7m"
#define  CONSOLE_HIDE                 "\e[8m"
#define CONSOLE_CLEAR                "\e[2J"
#define  CONSOLE_CLRLINE              "\r\e[K"
#define  CONSOLE_NORMAL              "\e[25m"

#define SHOWRED(STR)  COLOR_L_RED STR COLOR_NONE
#define SHOWGREEN(STR)  COLOR_L_GREEN STR COLOR_NONE
#define SHOWYELLOW(STR)  COLOR_YELLOW STR COLOR_NONE
#define SHOWBLINK(STR)  CONSOLE_BLINK STR CONSOLE_NORMAL

#define VNAME(VARNAME)  <<( #VARNAME "=" )<<(VARNAME)<<" "

#define TIMER_BEGIN timeval tv1,tv2; \
								gettimeofday(&tv1, NULL);

#define TIMER_END(COMMENT) gettimeofday(&tv2, NULL); \
								long diff= (tv2.tv_sec*1000000L+tv2.tv_usec)-(tv1.tv_sec*1000000L+tv1.tv_usec ); 
							


//十六进制查看缓冲区
void hex_dump(unsigned char *buf, long size);

//使用NewLineStream 和 NewLine 输出日志， 不需要加std::endl
class NewLineStream
{
private:
	std::ostringstream *_buffer;
public:
	//构造函数中，分配stringstream，并附加字符串
	template<typename T> 
	NewLineStream( const T& t)
	{
		_buffer = new std::ostringstream;
		(*_buffer) << (t);
	}

	//通过析构函数输出所有字符串
	~NewLineStream()
	{
		if (_buffer) {
			std::cout << (*_buffer).str() << std::endl;
			delete _buffer;
			_buffer = NULL;
		}
	}

	//不论传输参数是什么类型，都通过stringstream的<<重载附加成字符串
	template<typename T> 
	NewLineStream& operator<<(const T& t) {
		(*_buffer) << t;
		return *this;
	}

};

class NewLine
{
public:
	//构造一个新的stream对象，以便在结束时通过析构函数打印std::endl
	template<typename T> 
	NewLineStream operator<<(const T& t) {
		return NewLineStream(  t);
	}
};


#define USE_LOG4CPP 1
#if (USE_LOG4CPP == 1 )
	#include <log4cpp/Category.hh>
	#include <log4cpp/Priority.hh>
	extern log4cpp::Category& rootCategory;
	#define logDebug rootCategory<<log4cpp::Priority::DEBUG
	#define logInfo rootCategory<<log4cpp::Priority::INFO
	#define logError rootCategory<<log4cpp::Priority::ERROR <<  SHOWRED("ERROR ")
#else
	extern NewLine globalNewLineObject;
	#define logDebug globalNewLineObject  
	#define logInfo globalNewLineObject << SHOWYELLOW("INFO\t ")
	#define logError globalNewLineObject << SHOWRED("ERROR\t ")
#endif

