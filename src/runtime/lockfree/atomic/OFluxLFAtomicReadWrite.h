#ifndef OFLUX_LOCKFREE_ATOMIC_RW
#define OFLUX_LOCKFREE_ATOMIC_RW
/*
 *    OFlux: a domain specific language with event-based runtime for C++ programs
 *    Copyright (C) 2008-2012  Mark Pichora <mark@oanda.com> OANDA Corp.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Affero General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "lockfree/atomic/OFluxLFAtomic.h"
#include "lockfree/OFluxMachineSpecific.h"
#include "OFluxRollingLog.h"

/**
 * @file OFluxLFAtomicReadWrite.h
 * @author Mark Pichora
 * Lock-free version of the Read-write atomic
 */

namespace oflux {
namespace lockfree {
namespace atomic {

namespace readwrite {
 struct EventBaseHolder;
} // namespace readwrite


class RWWaiterPtr {
public:
        RWWaiterPtr()
        { u._u64 = 0LL; }
        RWWaiterPtr(
		  int rc
		, bool md
		, bool mk
		, uint32_t e = 0)
        {
		U uu;
                uu.s1.epoch = e;
                uu.s1.rcount_mode_mkd = 
			(rc<<2) 
			| (md ? 0x0002: 0x0000)
			| (mk ? 0x0001: 0x0000);
		u._u64 = uu._u64;
        }
        RWWaiterPtr(readwrite::EventBaseHolder * n, uint32_t e = 0)
	{
		U uu;
		uu.s2.epoch = e;
		uu.s2.next = n;
		u._u64 = uu._u64;
	}
        RWWaiterPtr(const RWWaiterPtr & o)
        {
                u._u64 = o.u._u64;
        }
        RWWaiterPtr(const RWWaiterPtr & o, int incr)
        {
                u._u64 = o.u._u64;
		u.s2.epoch += incr;
        }
        RWWaiterPtr & operator=(const RWWaiterPtr & o)
        {
                u._u64 = o.u._u64;
                return *this;
        }
	inline bool set(int rc, bool md, bool mk, uint32_t e)
	{
		U uu;
                uu.s1.epoch = e;
                uu.s1.rcount_mode_mkd = 
			(rc<<2) 
			| (md ? 0x0002: 0x0000)
			| (mk ? 0x0001: 0x0000);
		u._u64 = uu._u64;
		store_load_barrier();
		return true;
	}
	inline bool set(readwrite::EventBaseHolder *n, uint32_t e)
	{
		U uu;
		uu.s2.epoch = e;
		uu.s2.next = n;
		u._u64 = uu._u64;
		return true;
	}
	inline uint32_t epoch() const
	{ return u.s1.epoch; }
        inline bool mkd() const
        { return u.s1.rcount_mode_mkd & 0x0001; }
        inline bool mode() const
		// true iff read
        { return (u.s1.rcount_mode_mkd & 0x0002) >> 1; }
        inline int rcount() const
        { return u.s1.rcount_mode_mkd >> 2; }
        inline readwrite::EventBaseHolder * ptr() const
	{ return u.s2.next; }
        inline bool compareAndSwap(
                  const RWWaiterPtr & old_o
                , const RWWaiterPtr & new_o)
        {
                return __sync_bool_compare_and_swap(
                          &(u._u64)
                        , old_o.u._u64
                        , new_o.u._u64);
        }
        inline bool compareAndSwap(
                  const RWWaiterPtr & old_o
                , const int rc
                , bool md
		, bool mk
		, uint32_t e)
        {
                RWWaiterPtr rwh(rc,md,mk,e);
		store_load_barrier();
                return compareAndSwap(old_o,rwh);
        }
        inline bool compareAndSwap(
                  const RWWaiterPtr & old_o
                , readwrite::EventBaseHolder * n
		, uint32_t e)
        {
                RWWaiterPtr rwh(n,e);
                return compareAndSwap(old_o,rwh);
        }
	inline uint64_t u64() const
	{
		return u._u64;
	}
public:
        union U {
                struct S1 {
                        uint32_t epoch;
                        int rcount_mode_mkd;
                } s1;
                struct S2 {
                        uint32_t epoch;
                        readwrite::EventBaseHolder * next;
                } s2;
                uint64_t _u64;
        } u;
};

namespace readwrite {
struct EventBaseHolder {
	EventBaseHolder(EventBasePtr & e, bool md)
		: next(0,0,0)
		, val(get_EventBasePtr(e))
		, mode(md)
	{}

	RWWaiterPtr next;
	EventBase * val;
	int mode;
};
} // namespace readwrite

class ReadWriteWaiterList {
public:
	static readwrite::EventBaseHolder sentinel;
//#define LF_RW_WAITER_INSTRUMENTATION
#ifdef LF_RW_WAITER_INSTRUMENTATION
	struct Observation {
		enum    { Action_none
			, Action_A_o_w
			, Action_Rel 
			, Action_a_o_w
			, Action_rel 
			} action;
		const char * trans;
		int res;
		const readwrite::EventBaseHolder * e;
		const readwrite::EventBaseHolder * h;
		const readwrite::EventBaseHolder * t;
		const readwrite::EventBaseHolder * e_t;
		int mode;
		int r_mode;
		int tailup;
		RWWaiterPtr rwptr;
		RWWaiterPtr n_rwptr;
		unsigned retries;
		pthread_t tid;
		EventBase * by_ev;
		EventBase * ev;
		long long term_index;
	};

	RollingLog<Observation> log;

#endif // LF_RW_WAITER_INSTRUMENTATION
	static Allocator<readwrite::EventBaseHolder,DeferFree> allocator;

	ReadWriteWaiterList()
		: _head(allocator.get(EventBase::no_event,0))
		, _tail(_head)
	{}
	~ReadWriteWaiterList();
	bool push(readwrite::EventBaseHolder * e);
	void pop( readwrite::EventBaseHolder * & el
		, const EventBasePtr & by_ev
		, int mode);
	void dump();
	size_t count_waiters() const;
	bool held() const;
	bool has_waiters() const;
	size_t rcount() const;

public:
	readwrite::EventBaseHolder * _head;
	readwrite::EventBaseHolder * _tail;
};

class AtomicReadWrite : public AtomicCommon {
public:
	AtomicReadWrite(void * data)
		: AtomicCommon(data)
		, _wtype(EventBaseHolder::None)
	{}
	virtual ~AtomicReadWrite() {}
	virtual int held() const 
	{ 
		int rcount = _waiters.rcount();
		return rcount < 0 ? 1 : rcount;
	}
	virtual size_t waiter_count() 
	{ return _waiters.count_waiters(); }
	virtual bool has_no_waiters()
	{ 
		return !_waiters.has_waiters();
	}
	virtual int wtype() const { return _wtype; }
	virtual const char * atomic_class() const
	{ return "lockfree::AtomicReadWrite"; }
	virtual bool acquire_or_wait(EventBasePtr & ev, int wtype)
	{
		store_load_barrier();
		readwrite::EventBaseHolder * ebh = 
			ReadWriteWaiterList::allocator.get(ev,wtype);
		// Upgradable could be stuck into the lower-level
		// lf queue, but the added complexity is probably not worth it
		//  we tolerate: that sometimes upgradable will grab Write
		//  access to non-NULL data (which the app will treat as readonly.
		// we guarantee that NULL data is not grabbed for Read.
		int local_wtype =
			( wtype == EventBaseHolder::Upgradeable
			? ( *data()
			    ? EventBaseHolder::Read 
			    : EventBaseHolder::Write)
			: wtype);
		ebh->mode = local_wtype;
		bool acqed = _waiters.push(ebh);
		if(acqed) {
			oflux_log_trace2("RW::a_o_w %s %p %p acqed %d %d\n"
				, ev->flow_node()->getName()
				, ev
				, this
				, wtype
				, _waiters.rcount());
			_wtype = local_wtype;
			store_load_barrier();
			ReadWriteWaiterList::allocator.put(ebh); 
		} else {
			checked_recover_EventBasePtr(ev);
			oflux_log_trace2("RW::a_o_w %s %p %p waited %d %d\n"
				, ev->flow_node()->getName()
				, ev
				, this
				, wtype
				, _waiters.rcount());
		}
		return acqed;
	}
	virtual void release(
		  std::vector<EventBasePtr > & rel_ev
                , EventBasePtr & by_e)
	{
		readwrite::EventBaseHolder * el = NULL;
		oflux_log_trace2("RW::rel   %s %p %p releasing %d %d\n"
			, by_e->flow_node()->getName()
			, get_EventBasePtr(by_e)
			, this
			, _wtype
			, _waiters.rcount());
		store_load_barrier();
		_waiters.pop(el,by_e,_wtype);
		readwrite::EventBaseHolder * e = el;
		readwrite::EventBaseHolder * n_e = NULL;
		while(e && e != &ReadWriteWaiterList::sentinel) {
			assert(!e->next.mkd()
				&& "rw atom should not have released a marked ebh");
			n_e = e->next.ptr();
			_wtype = e->mode;
			store_load_barrier();
			oflux_log_trace2("RW::rel   %s %p %p came out %d %d\n"
				, e->val ? e->val->flow_node()->getName() : "<null>"
				, e->val
				, this
				, e->type
				, _waiters.rcount());
			if(e->val) { 
				rel_ev.push_back(EventBasePtr(e->val)); 
			} else {
				oflux_log_error("RW::rel   put out a NULL event\n");
			}
			ReadWriteWaiterList::allocator.put(e);
			e = n_e;
		}
	}
	virtual void log_snapshot_waiters() const {} // TODO
	inline void _dump() { _waiters.dump(); }
private:
	ReadWriteWaiterList _waiters;
	int _wtype;
};


} // namespace atomic
} // namespace lockfree
} // namespace oflux

#endif // OFLUX_LOCKFREE_ATOMIC_RW
