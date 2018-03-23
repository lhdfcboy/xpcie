#include "Lock.h"
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>//O_CREAT


#include "LogUtils.h"
#include <unistd.h> /* exit  __NR_gettid */
#include <sys/syscall.h> /* __NR_gettid */

#define DEBUG_GLOBAL_SEM 1
RWLock::RWLock() {
	// TODO Auto-generated constructor stub

	pthread_rwlockattr_init(&m_rwlockattr);
	int ret= pthread_rwlock_init(&m_lock, &m_rwlockattr);
}

int  RWLock::ReadLock()
{
	pthread_rwlock_rdlock(&m_lock);
	return 0;
}


int  RWLock::WriteLock()
{
	pthread_rwlock_wrlock(&m_lock);
	return 0;
}

int  RWLock::UnLock()
{
	pthread_rwlock_unlock(&m_lock);
	return 0;
}

RWLock::~RWLock() {
	// TODO Auto-generated destructor stub
	pthread_rwlockattr_destroy(&m_rwlockattr);
	pthread_rwlock_destroy(&m_lock);
}

//动态方式初始化互斥锁
CMutex::CMutex()
{
	pthread_mutex_init(&m_mutex, NULL);
}

//注销互斥锁
CMutex::~CMutex()
{
	pthread_mutex_destroy(&m_mutex);
}

//确保拥有互斥锁的线程对被保护资源的独自访问
void inline CMutex::Lock() const
{

	pthread_mutex_lock(&m_mutex);
}

//释放当前线程拥有的锁，以使其它线程可以拥有互斥锁，对被保护资源进行访问
void inline CMutex::Unlock() const
{

	pthread_mutex_unlock(&m_mutex);
}



PosixSemaphore::PosixSemaphore( unsigned int iValue, const char* pName )
{
	const int pshared=1;
	mode_t mode=O_RDWR;

	//printf("CSemaphore constructor,iValue=%d, pName=%s\n", iValue, pName);

	
#if( DEBUG_GLOBAL_SEM ==1)
	//任意进程共享方式
	pSem=sem_open (pName, O_CREAT, mode, iValue);
	if (!pSem)
	{
		perror("create sem failed, check access permission?");
	}
	
	assert(pSem);
#else
	
	//父子进程共享方式，用于调试
	pSem=new sem_t;
	sem_init(pSem,pshared,iValue);
	
#endif


}

PosixSemaphore::~PosixSemaphore()
{

#if( DEBUG_GLOBAL_SEM ==1)
	sem_close(pSem);
#else
	sem_destroy(pSem);
	delete pSem;
#endif
	
}

int PosixSemaphore::wait()
{
	//成功返回0，失败返回-1
// 	int tid;
// 	tid=syscall(__NR_gettid); 
// 	cout << SHOWGREEN("wait") VNAME(tid) << endl;
	return sem_wait(pSem);
}

int PosixSemaphore::post()
{
	//int tid;
	//tid=syscall(__NR_gettid); 
	//cout << SHOWGREEN("post") VNAME(tid) << endl;
	return sem_post(pSem);
}

int PosixSemaphore::getValue()
{
	int ret,iValue;
	
	ret=sem_getvalue(pSem,&iValue);
	//printf("ret=%d, pSem=%p iValue=%d\n", ret, pSem,iValue );
	return iValue;
}
