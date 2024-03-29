#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>
#include <exception>
#include <semaphore.h>  // 信号量

// 线程同步封装类

// 互斥锁类
class locker {
public:
	// 构造函数
	locker() {
		if (pthread_mutex_init(&m_mutex, NULL) != 0)
			throw std::exception();
	}

	~locker() {
		pthread_mutex_destroy(&m_mutex);
	}

	bool lock() {
		return pthread_mutex_lock(&m_mutex) == 0;	
	}

	bool unlock() {
		return pthread_mutex_unlock(&m_mutex) == 0;
	}	

	// 获取互斥锁的指针
	pthread_mutex_t* get() {
		return &m_mutex;
	}

private:
	// 互斥锁
	pthread_mutex_t m_mutex;

};

// 条件变量类 通过条件变量看线程啥时候唤醒什么的
class cond {
	public:
		cond() {
			if (pthread_cond_init(&m_cond, NULL) != 0) {
				throw std::exception();
			}
		}
		bool wait(pthread_mutex_t * mutex) {
			int ret = 0;
			ret = pthread_cond_wait(&m_cond, mutex) == 0;
			return ret == 0;
		}
		~cond() {
			pthread_cond_destroy(&m_cond);
		}
		bool timedwait(pthread_mutex_t * mutex, struct timespec t) {
			int ret = 0;
			//pthread_mutex_lock(&m_mutex);
			ret = pthread_cond_timedwait(&m_cond, mutex, &t);
			//pthread_mutex_unlock(&m_mutex);
			return ret == 0;
		}
		// signal增加条件变量
		bool signal() {
			return pthread_cond_signal(&m_cond) == 0;
		}

		bool broadcast() {
			return pthread_cond_broadcast(&m_cond) == 0;
		}
	private:
		pthread_cond_t m_cond;
};

class sem {
public:
	sem() {
		if (sem_init(&m_sem, 0, 0) != 0) {
			throw std::exception();
		}
	}
	
	sem(int num) {
		if (sem_init(&m_sem, 0, num) != 0) {
			throw std::exception();
		}
	}

	~sem() {
		sem_destroy(&m_sem);
	}

	// 等待信号量
	bool wait() {
			return sem_wait(&m_sem) == 0;
	}
	//增加信号量
	bool post() {
		return sem_post(&m_sem) == 0;
	}

private:
	sem_t m_sem;

};


#endif