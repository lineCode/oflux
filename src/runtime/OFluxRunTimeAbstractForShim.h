#ifndef _OFLUX_RUNTIME_ABSTRACT_FOR_SHIM
#define _OFLUX_RUNTIME_ABSTRACT_FOR_SHIM

/**
 * @file OFluxRunTimeAbstract.h
 * @author Mark Pichora
 * This contains the minimum that the SHIM needs to access the run time.
 */

#include "OFlux.h"
#include "OFluxRunTimeAbstract.h"
#include "OFluxRunTimeThreadAbstract.h"
#include "OFluxProfiling.h"
#include "OFluxWrappers.h"
#include "OFluxWatermark.h"


namespace oflux {

/**
 * @class ManagerLock
 * @brief container for the mutex
 */
class ManagerLock {
public:
	ManagerLock();
	~ManagerLock();
public: // sorry C++ ppl
	oflux_mutex_t  _manager_lock;
};

class RunTimeThreadAbstractForShim;

/**
 * @class RunTimeAbstract
 * @brief base class with functionality to punt control to the run time
 */
class RunTimeAbstractForShim : public ManagerLock, public RunTimeAbstract {
public:
	RunTimeAbstractForShim();
	virtual ~RunTimeAbstractForShim();

	/**
	 * @brief wait in the "waiting to run" queue (thread conditional wait)
	 **/
	inline void wait_to_run() 
	{
		_waiting_to_run.wait();
	}

	/**
	 * @brief wait in the "waiting in pool" queue (thread conditional wait)
	 **/
	inline void wait_in_pool() 
	{
		_waiting_in_pool.wait();
	}

        virtual bool running() = 0;

	/**
	 * @brief allow another thread to come up and process events
	 *
	 **/
	virtual int wake_another_thread() = 0;

	virtual RunTimeThreadAbstractForShim * thread() = 0;
public: // sorry C++ ppl
	ConditionVariable<IntegerCounter>     _waiting_to_run;
#ifdef THREAD_COLLECTION
	ConditionVariable<WatermarkedCounter> 
#else
	ConditionVariable<IntegerCounter> 
#endif
		_waiting_in_pool;
};

enum RTT_WaitState 
	{ RTTWS_running = 0
	, RTTWS_wtr = 1
	, RTTWS_wip = 2
	, RTTWS_blockingcall = 3
	, RTTWS_wtrshim = 4 
	, RTTWS_door_service = 5 
	, RTTWS_door_wait = 6 
	};

class RunTimeThreadAbstractForShim : public RunTimeThreadAbstract {
public:
	virtual ~RunTimeThreadAbstractForShim() {}
	virtual bool is_detached() = 0;
	virtual void set_detached(bool d) = 0;
#ifdef PROFILING
	virtual TimerList & timer_list() = 0;
#endif
	virtual void wait_state(RTT_WaitState) = 0;
	virtual oflux_thread_t tid() = 0;
};

class WaitingToRunRAII {
public:
	WaitingToRunRAII(RunTimeThreadAbstractForShim * rtt)
		: _rtt(rtt)
#ifdef PROFILING
		, _tp(rtt->timer_list())
#endif
	{ _rtt->wait_state(RTTWS_blockingcall); }
	~WaitingToRunRAII()
	{ _rtt->wait_state(RTTWS_running); }
	inline void state_wtr() {  _rtt->wait_state(RTTWS_wtrshim); }
private:
	RunTimeThreadAbstractForShim * _rtt;
#ifdef PROFILING
	TimerPause       _tp;
#endif
};

class RunTimeAbort {
public:
};

/**
 * @brief an RAII class for unlocking a runtime
 *
 */
class UnlockRunTime {
public:
	UnlockRunTime(RunTimeAbstractForShim * rt);
        
	~UnlockRunTime();
private:
	AutoUnLock _aul;
	RunTimeAbstractForShim *rt_;
	bool prev_detached_;
};



};


#endif // _OFLUX_RUNTIME_ABSTRACT
