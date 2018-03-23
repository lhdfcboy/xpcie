
# ///////////////////////////////////////////////////////////////////////////////////////
# 定义编译连接指令变量（可以是cc、gcc、g++等）
CC = g++
# 编译目标
SERVER = test_xpcie

#google test 目录
GTEST_INC=

# 公共目标文件
CPP_OBJS = 	app/test_xpcie.o \
			app/Lock.o \
			app/LogUtils.o \
			app/Thread.o 
		

# 所有目标文件
OBJS = $(CPP_OBJS) $(C_OBJS)

# 公共包含文件
COMMON_INC = -I./app


# 定义所有库文件路径变量
ALL_INCS=$(COMMON_INC) 


# ///////////////////////////////////////////////////////////////////////////////////////
#5. 定义编译选项变量 
CFLAGS = -O0 -g 


# ///////////////////////////////////////////////////////////////////////////////////////
#6. 定义连接选项变量 
LFLAGS = ./app/liblog4cpp.a  -lpthread -lrt


# ///////////////////////////////////////////////////////////////////////////////////////
#7. 主目标依赖命令(使用了字符“@”使得make执行命令不显示在屏幕上)
all: xpcie.ko  $(SERVER)

xpcie.ko:
	make -C driver


$(SERVER) : $(OBJS) $(LIBACCEL)
	$(CC)   -o $(SERVER) $(OBJS) $(LFLAGS)


#格式：目标集合: 目标模式 : 目标依赖模式
#“$<”表示所有的依赖目标集
#“$@”表示目标模式集
#@echo "正在编译各个源文件"
$(CPP_OBJS): %.o: %.cpp
	$(CC) -c $(CFLAGS) $(ALL_INCS) $< -o $@

$(C_OBJS): %.o: %.c
	$(CC) -c -Wno-write-strings $(CFLAGS) $(ALL_INCS) $< -o $@



# ///////////////////////////////////////////////////////////////////////////////////////
#8. “.PHONY”来显示地指明一个目标是“伪目标”
# 
.PHONY : clean
clean :
	make -C ../api clean
	make -C ../driver clean
	rm -f $(SERVER) $(OBJS) 
