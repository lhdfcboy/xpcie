/********************************************************************
	created:	2014/04

	
	purpose:	各种锁和信号量的封装
*********************************************************************/


#ifndef LOCK_H_
#define LOCK_H_

#include <pthread.h>

//sem
#include <semaphore.h>

//锁接口类
class ILock
{
public:
	virtual ~ILock() {}

	virtual void Lock() const = 0;
	virtual void Unlock() const = 0;
};

//读写锁，进程范围内
class RWLock {
private:
	pthread_rwlockattr_t m_rwlockattr;
	pthread_rwlock_t m_lock;
public:
	RWLock();
	virtual ~RWLock();
	 int ReadLock() ;
	 int WriteLock() ;
	 int UnLock();
};

//互斥锁，进程范围内
class CMutex : public ILock
{
public:
	CMutex();
	~CMutex();

	virtual void Lock() const;
	virtual void Unlock() const;

private:
	mutable pthread_mutex_t m_mutex;
};

//原子操作模板
template <class Type>
class LockedValue
{
private:
	RWLock _lock;
	Type _value;
public:
	LockedValue();
	void set(Type value);
	Type get ();
	Type increase();
	Type decrease();
};

template <class Type>
LockedValue<Type>::LockedValue()
{
	_value=0;
}


template <class Type>
void LockedValue<Type>::set( Type value )
{
	_lock.WriteLock();
	_value=value;
	_lock.UnLock();
}

template <class Type>
Type LockedValue<Type>::get()
{
	Type t;
	_lock.ReadLock();
	t=_value;
	_lock.UnLock();
	return t;
}

template <class Type>
Type LockedValue<Type>::increase()
{
	Type t;
	_lock.WriteLock();
	t=++_value;
	_lock.UnLock();
	return t;
}

template <class Type>
Type LockedValue<Type>::decrease()
{
	Type t;
	_lock.WriteLock();
	t=--_value;
	_lock.UnLock();
	return t;
}




//信号量
class PosixSemaphore
{
private:
	sem_t* pSem;
public:
	PosixSemaphore(unsigned int iValue, const char* pName);
	~PosixSemaphore();
		
	int wait();
	int post();
	int getValue();
};

#endif

