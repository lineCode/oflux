#include "lockfree/atomic/OFluxLFAtomicPooled.h"
#include "lockfree/allocator/OFluxSMR.h"
#include <cstdio>

namespace oflux {
namespace lockfree {
namespace atomic {



/////////////////////////////////////////////////////////////////
// PoolEventList state encoding:
//
// 1. (resourcesN) mkd(head) && unmk(head) = { mkd(next) && unmk(next) != NULL, id > 0 }
// 2. (empty) !mkd(head) && head = { next == NULL, id == 0 } 
// 3. (waitingM) !mkd(head) && head = { unmk(next) != NULL, id > 0 }
// Note: in (1) all non-NULL next links are mkd()

PoolEventList::~PoolEventList()
{
	EventBaseHolder * hp = _head.get();
	EventBaseHolder * hp_n = NULL;
	while(unmk(hp)) {
		hp_n = unmk(hp)->next;
		AtomicCommon::allocator.put(unmk(hp));
		hp = hp_n;
	}
}


// push(Event * e) _________________________________________________________
//   Either:
//    return true and *(e->resource_loc) is written with a free resource
//   Or
//    return false and park e in the internal waiters data structure
bool
PoolEventList::push(EventBaseHolder * e)
{
	bool res = false;
#ifdef POOL_WAITERLIST_INSTRUMENTATION
	Observation & obs = log.submit();
	obs.trans = "";
	obs.action = Observation::Action_A_o_w;
	obs.res = 0;
	obs.e = e;
	obs.r = 0;
	obs.h64 = 0;
	obs.t = 0;
	obs.tb = -1;
	obs.retries = 0;
	obs.tid = oflux_self();
	obs.ev = e->ev;
	obs.term_index = 0;
#endif  // POOL_WAITERLIST_INSTRUMENTATION
	//*(e->resource_loc) = NULL;
	//e->next = NULL;
	EventBaseHolder * t = NULL;

	StampedPtr<EventBaseHolder> h;
	EventBaseHolder * hp = NULL;
	EventBaseHolder * hn = NULL;
	while(1) {
		h = _head;
		hp = h.get();
		// hazard ptr on head
		::oflux::lockfree::smr::set(hp,0);
		if(hp != _head.get()) continue;
                //
		hn = unmk(hp)->next;
		//t = _tail;
		HAZARD_PTR_ASSIGN(t,_tail,1);

		while(t->next) {
			t = t->next;
		}
#ifdef POOL_WAITERLIST_INSTRUMENTATION
		obs.t = t;
		obs.h64 = h.u64();
#endif  // POOL_WAITERLIST_INSTRUMENTATION
		if(	// condition:
			mkd(hp)
			&& unmk(hn) != NULL) {
			// action:
			*(e->resource_loc) = unmk(hp);
			if(_head.cas(h, hn)) {
				// 1->(1,2)
#ifdef POOL_WAITERLIST_INSTRUMENTATION
				obs.trans = "1->(1,2)";
#endif  // POOL_WAITERLIST_INSTRUMENTATION
				res = true;
				break;
			}
			*(e->resource_loc) = NULL;
		} else if( //condition
			hp
			&& (hp == t)
			&& !mkd(hp)
			&& !hn) {
			// action:
			e->next = hp;
			if(_head.cas(h,e)) {
				// 2->3
#ifdef POOL_WAITERLIST_INSTRUMENTATION
				obs.trans="2->3";
#endif  // POOL_WAITERLIST_INSTRUMENTATION
				res = false;
				break;
			}
		} else if( //condition:
			hp 
			&& (hp != t)
			&& !mkd(hp)
			&& !mkd(hn)
			&& hn) {
			// action:

			EventBase * ev;
			EventBaseHolder ** resource_loc = e->resource_loc;
			e->next = NULL;
			ev_swap(ev,e->ev);
			resource_loc_asgn(e,NULL);
			e->resource_loc = NULL;

			if(__sync_bool_compare_and_swap(
					  &(t->next)
					, NULL
					, e)) {
				// 3->3
#ifdef POOL_WAITERLIST_INSTRUMENTATION
				obs.trans="3->3";
				obs.tb =
#endif  // POOL_WAITERLIST_INSTRUMENTATION
				__sync_bool_compare_and_swap(&_tail,t,e);
				//_tail = e;
				resource_loc_asgn(t,resource_loc);
				t->resource_loc = resource_loc;
				ev_swap(t->ev,ev);
				res = false;
				break;
			}
			ev_swap(e->ev,ev);
			resource_loc_asgn(e,resource_loc);
			e->resource_loc = resource_loc;
		}
#ifdef POOL_WAITERLIST_INSTRUMENTATION
		++obs.retries;
#endif  // POOL_WAITERLIST_INSTRUMENTATION
	}
	HAZARD_PTR_RELEASE(0);
	HAZARD_PTR_RELEASE(1);
#ifdef POOL_WAITERLIST_INSTRUMENTATION
	obs.res = res;
	obs.action = Observation::Action_a_o_w;
	obs.term_index = log.at();
#endif  // POOL_WAITERLIST_INSTRUMENTATION
	return res;
}

EventBaseHolder *
PoolEventList::complete_pop()
{
	EventBaseHolder * he = _head.get();
	while(mkd(he) && mkd(unmk(he)->next)) {
		he = unmk(he)->next;
	}
	EventBaseHolder * hen = unmk(he)->next;
	if(hen && !mkd(hen) 
		&& hen->ev != NULL 
		&& hen->resource_loc != NULL
		&& hen->resource == NULL) {
		// proceed
	} else {
		return NULL;
	}
	StampedPtr<EventBaseHolder> h = _head;
	EventBaseHolder * hp = h.get();
	EventBaseHolder * hn = unmk(hp)->next;
	if(mkd(hp) && unmk(hn) && _head.cas(h,hn)) {
		if(__sync_bool_compare_and_swap(
				  &(unmk(he)->next)
				, hen
				, hen->next)) {
			*(hen->resource_loc) = unmk(hp);
			oflux_log_trace2("  ::complete_pop() success\n");
			hen->busyWaitOnEv();
			return hen;
		} else {
			oflux_log_trace2("  ::complete_pop() fail\n");
			do { 
				h = _head;
			} while(!_head.cas(h,mk(hp)));
		}
	}
	return NULL;
}


// pop(Resource * r) _____________________________________________________
//   Either
//    return re != NULL released event with *(re->release_loc) = r
//   Or
//    return NULL and park resource r in the pool of free resources
EventBaseHolder *
PoolEventList::pop(EventBaseHolder * by_ev)
{
	EventBaseHolder * res = NULL;
#ifdef POOL_WAITERLIST_INSTRUMENTATION
	Observation & obs = log.submit();
	obs.trans = "";
	obs.action = Observation::Action_Rel;
	obs.res = 0;
	obs.e = by_ev;
	obs.r = 0;
	obs.h64 = 0;
	obs.t = 0;
	obs.tb = -1;
	obs.retries = 0;
	obs.tid = oflux_self();
	obs.ev = obs.e->ev;
	obs.term_index = 0;
#endif  // POOL_WAITERLIST_INSTRUMENTATION
	EventBaseHolder * r = *(by_ev->resource_loc);
	*(by_ev->resource_loc) = NULL;
	StampedPtr<EventBaseHolder> h = NULL;
	EventBaseHolder * hp = NULL;
	EventBaseHolder * hn = NULL;
	EventBaseHolder * t = NULL;
	while(1) {
		h = _head;
		hp = h.get();
		// hazard on head
		::oflux::lockfree::smr::set(hp,0);
		if(hp != _head.get()) continue;
		//
		hn = unmk(hp)->next;
		//t = _tail;
		HAZARD_PTR_ASSIGN(t,_tail,1);
		while(t->next) {
			t = t->next;
		}
#ifdef POOL_WAITERLIST_INSTRUMENTATION
		obs.h64 = h.u64();
		obs.t = t;
#endif  // POOL_WAITERLIST_INSTRUMENTATION
		if(	// condition:
			mkd(hp)
			&& unmk(hn) != NULL
			&& (unmk(hp)->resource != NULL)) {
			// action:
			r->next = mk(hp);
			if(_head.cas(h,mk(r))) {
				// 1->1
#ifdef POOL_WAITERLIST_INSTRUMENTATION
				obs.trans = "1->1";
#endif  // POOL_WAITERLIST_INSTRUMENTATION
				res = complete_pop();
				break;
			}
		} else if(
			// condition:
			!mkd(hp)
			&& (hp == t)
			&& hp->ev == 0
			&& !hn) {
			// action:
			r->next = hp;
			if(_head.cas(h,mk(r))) {
				// 2->1
#ifdef POOL_WAITERLIST_INSTRUMENTATION
				obs.trans="2->1";
#endif  // POOL_WAITERLIST_INSTRUMENTATION
				res = complete_pop();
				break;
			}
		} else if(
			// condition:
			!mkd(hp)
			&& (hp != t)
			&& !mkd(hn)
			&& hp->ev != 0
			&& unmk(hn) != NULL) {
			// action:
			if(_head.cas(h,unmk(hn))) {
				// 3->(2,3)
#ifdef POOL_WAITERLIST_INSTRUMENTATION
				obs.trans = "3->(2,3)";
#endif  // POOL_WAITERLIST_INSTRUMENTATION
				*(hp->resource_loc) = r;
				hp->busyWaitOnEv();
				res = hp;
				break;
			}
		}
#ifdef POOL_WAITERLIST_INSTRUMENTATION
		++obs.retries;
#endif  // POOL_WAITERLIST_INSTRUMENTATION
	}
	HAZARD_PTR_RELEASE(0);
	HAZARD_PTR_RELEASE(1);
#ifdef POOL_WAITERLIST_INSTRUMENTATION
	obs.res = (res != NULL);
	obs.r = res;
	obs.action = Observation::Action_rel;
	obs.term_index = log.at();
#endif  // POOL_WAITERLIST_INSTRUMENTATION
	return res;
}


void
PoolEventList::dump()
{
	EventBaseHolder * hp = _head.get();
	const char * state =
		( hp == 0 ?
			"undefined  [0]"
		: (mkd(hp) ?
			"resourcesN [1]"
		: (hp->next == NULL ?
			"empty      [2]" :       
			"waitingM   [3]" )));

	oflux_log_trace("[" PTHREAD_PRINTF_FORMAT "]  atomic[%p] in state %s\n"
		, oflux_self()
		, this
		, state);
	char buff[5000];
	size_t at = 0;
	at += snprintf(buff+at,5000-at,"  %s head = "
		, mkd(_head.get()) ? "m" : " ");
	EventBaseHolder * e = _head.get();
	while(unmk(e) != NULL) {
		if(mkd(e)) {
			at += snprintf(buff+at,5000-at,"r%c%p->"
				, mkd(e) ? 'm' : ' ',unmk(e)->resource);
		} else {
			at += snprintf(buff+at,5000-at,"e%c%p->"
				, mkd(e) ? 'm' : ' ',unmk(e)->ev);
		}
		if(at >= 5000) return; \
		e  = unmk(e)->next;
	}
	if(e == NULL) {
		at += snprintf(buff+at,5000-at,"(0x0)\n");
	} else if(is_one(e)) {
		at += snprintf(buff+at,5000-at,"(0x1)\n");
	} else {
		at += snprintf(buff+at,5000-at,"\n");
	}
	oflux_log_trace("[" PTHREAD_PRINTF_FORMAT "] %s",oflux_self(),buff);
	oflux_log_trace("[" PTHREAD_PRINTF_FORMAT "]   tail = e%p/r%p\n"
		, oflux_self()
		, _tail->ev
		, _tail->resource);
}

// clarify when _resource_ebh is what:
//------------------------------------
//
// constructed:  emptied ebh (has resource set to NULL)
// acquired:   populated ebh (resource set to not NULL)
// released:
//


AtomicPooled::AtomicPooled(AtomicPool * pool,void * data)
	: _next(NULL)
	, _pool(pool)
	, _resource_ebh(AtomicCommon::allocator.get(data))
	, _by_ebh(AtomicCommon::allocator.get('\0',&_resource_ebh))
{
	oflux_log_trace2("AC::alloc.g APD*this:%p _resource_ebh:%p %d\n"
		, this
		, _resource_ebh
		, __LINE__);
	oflux_log_trace2("AC::alloc.g APD*this:%p _by_ebh:%p %d\n"
		, this
		, _by_ebh
		, __LINE__);
	init();
	oflux_log_trace2("APD::APD APD*this:%p\n",this);
	//check();
}

void
AtomicPooled::relinquish()
{
	oflux_log_trace2("[" PTHREAD_PRINTF_FORMAT "] APD::relinquish APD*this:%p\n"
		, oflux_self()
		, this);
	_pool->release(this);
	//check();
}

const char *
AtomicPooled::atomic_class_str = "lockfree::Pooled";

bool
AtomicPooled::acquire_or_wait(EventBasePtr & ev,int t)
{
#ifdef OFLUX_DEEP_LOGGING
	AtomicPool::dump(_pool);
#endif // OFLUX_DEEP_LOGGING
	assert(_by_ebh);
	oflux_log_trace("[" PTHREAD_PRINTF_FORMAT "] AP::a_o_w ev %s ev*:%p  atom APD*this:%p "
		" _by_ebh:%p _by_ebh->rsrc_loc: %p\n"
		, oflux_self()
		, ev->flow_node()->getName()
		, ev.get()
		, this
		, _by_ebh
		, _by_ebh->resource_loc);
	EventBase * evb = ev.get();
	_by_ebh->ev = evb;
	_by_ebh->type = t;
	if(_resource_ebh) {
		assert(_resource_ebh->resource == NULL);
		oflux_log_trace2("AC::alloc.p _resource_ebh:%p %d\n"
			, _resource_ebh
			, __LINE__);
		AtomicCommon::allocator.put(_resource_ebh);
		_resource_ebh = NULL;
	}
	//assert(_by_ebh->resource_loc == &_resource_ebh);
	EventBaseHolder * local_by_ebh = _by_ebh;
	_by_ebh = AtomicCommon::allocator.get('\0',&_resource_ebh);
	oflux_log_trace2("AC::alloc.g %p %d\n",_by_ebh,__LINE__);
	init();
	bool acqed = _pool->waiters.push(local_by_ebh); // try to acquire the resource
	assert(!acqed || _resource_ebh->resource);
	if(acqed) {
		AtomicCommon::allocator.put(local_by_ebh);
	} else {
		assert(ev.recover());
	}
	//assert(_by_ebh->resource_loc == &_resource_ebh);
	oflux_log_trace("[" PTHREAD_PRINTF_FORMAT "] AP::a_o_w ev %s ev*:%p APD*this:%p res %d\n"
		, oflux_self()
		, evb->flow_node()->getName()
		, evb
		, this
		, acqed);
#ifdef OFLUX_DEEP_LOGGING
	AtomicPool::dump(_pool);
#endif // OFLUX_DEEP_LOGGING
	//assert(_by_ebh->resource_loc == &_resource_ebh);
	return acqed;
}

void
AtomicPooled::release(
	  std::vector<EventBasePtr> & rel_ev_vec
	, EventBasePtr &ev)
{
#ifdef OFLUX_DEEP_LOGGING
	AtomicPool::dump(_pool);
#endif // OFLUX_DEEP_LOGGING
	assert(_resource_ebh);
	assert(_resource_ebh->resource != NULL);
	oflux_log_trace2("[" PTHREAD_PRINTF_FORMAT "] AP::rel   ev %s ev*:%p  APD*this:%p _by_ebh:%p _res_ebh->res:%p\n"
		, oflux_self()
		, ev ? ev->flow_node()->getName() : "<NULL>"
		, ev
		, this
		, _by_ebh
		, _resource_ebh->resource);
	//assert(_by_ebh->resource_loc == &_resource_ebh);
	EventBaseHolder * rel_ebh = _pool->waiters.pop(_by_ebh);
	//assert(_by_ebh->resource_loc == &_resource_ebh);
	if(rel_ebh) { 
		assert(rel_ebh != _by_ebh);
		assert(rel_ebh->ev);
		assert((*(rel_ebh->resource_loc))->resource);
		oflux_log_trace("[" PTHREAD_PRINTF_FORMAT "] AP::rel   ev %s ev*:%p APD*this:%p passed to %s rel_ebh->ev:%p rel_ebh->res_loc:%p rsrc:%p\n"
			, oflux_self()
			, ev.get() ? ev->flow_node()->getName() : "<NULL>"
			, ev.get()
			, this
			, rel_ebh->ev->flow_node()->getName()
			, rel_ebh->ev
			, rel_ebh->resource_loc
			, (*(rel_ebh->resource_loc))->resource);
		//_by_ebh.ev = rel_ebh->ev;
		//_by_ebh.type = rel_ebh->type;
		rel_ev_vec.push_back(EventBasePtr(rel_ebh->ev));
		oflux_log_trace2("AC::alloc.p rel_ebh:%p %d\n"
			, rel_ebh
			, __LINE__);
		AtomicCommon::allocator.put(rel_ebh);
	} else {
		oflux_log_trace("[" PTHREAD_PRINTF_FORMAT "] AP::rel   ev %s ev*:%p  APD*this:%p rsrc nothing out\n"
			, oflux_self()
			, ev.get() ? ev->flow_node()->getName() : "<NULL>"
			, ev.get()
			, this);
	}
	static int * _null = NULL;
	_resource_ebh = AtomicCommon::allocator.get(_null);
	store_load_barrier();
	oflux_log_trace2("AC::alloc.g _res_ebh:%p %d\n",_resource_ebh,__LINE__);
#ifdef OFLUX_DEEP_LOGGING
	AtomicPool::dump(_pool);
#endif // OFLUX_DEEP_LOGGING
	if(!rel_ebh) {
		_pool->release(this);
	}
}

Allocator<AtomicPooled,DeferFree> AtomicPool::allocator; // (new allocator::MemoryPool<sizeof(AtomicPooled)>());

AtomicPool::AtomicPool()
	: head_free(NULL)
{
}

AtomicPool::~AtomicPool()
{
	oflux_log_trace2("~AtomicPool APD*this:%p\n",this);
	// reclaim items from the free list
	AtomicPooled * aph = head_free;
	AtomicPooled * aph_n = NULL;
	while(aph) {
		aph_n = aph->_next;
		AtomicPool::allocator.put(aph);
		aph = aph_n;
	}
}

void
AtomicPool::release(AtomicPooled *ap)
{
	oflux_log_trace2("[" PTHREAD_PRINTF_FORMAT "] AP::release APD*:%p\n",oflux_self(),ap);
	ap->init();
	// put it on the free list
	AtomicPooled * h;
	do {
		h = head_free;
		ap->_next = h;
	} while(!__sync_bool_compare_and_swap(&head_free,h,ap));
}

const void *
AtomicPool::get(oflux::atomic::Atomic * & a_out,const void *)
{
	char _here;
	static const char * p_here = &_here;
	AtomicPooled * ap_out = NULL;
	while((ap_out = head_free) 
			&& !__sync_bool_compare_and_swap(
				  &head_free
				, ap_out
				, ap_out->_next));
	if(!ap_out) {
		void * p = NULL;
		ap_out = AtomicPool::allocator.get(this,p);
		oflux_log_trace2("[" PTHREAD_PRINTF_FORMAT "] AP::get APD*:%p new\n",oflux_self(),ap_out);
	} else {
		oflux_log_trace2("[" PTHREAD_PRINTF_FORMAT "] AP::get APD*:%p old\n",oflux_self(),ap_out);
	}
	a_out = ap_out;
	//ap_out->check();
	return &p_here;
}

void
AtomicPooled::check()
{
	assert(_by_ebh->resource_loc == &_resource_ebh);
}

class AtomicPoolWalker : public oflux::atomic::AtomicMapWalker {
public:
	AtomicPoolWalker(AtomicPool &pool)
		: _done(false)
		, _atomic(&pool,NULL)
	{}
	virtual ~AtomicPoolWalker();
	virtual bool next(const void * & key,oflux::atomic::Atomic * &atom);
	// so we pretend there is only one atomic
	// this is cool for most applications of this walker -- I think
private:
	bool _done;
	AtomicPooled _atomic;
};

AtomicPoolWalker::~AtomicPoolWalker()
{
}

bool 
AtomicPoolWalker::next(const void * &, oflux::atomic::Atomic * &atom)
{
	bool res = !_done;
	atom = NULL;
	if(res) {
		_done = true;
		atom = &_atomic;
	}
	return res;
}

oflux::atomic::AtomicMapWalker *
AtomicPool::walker()
{
	return new AtomicPoolWalker(*this);
}

} // namespace atomic
} // namespace lockfree
} // namespace oflux

