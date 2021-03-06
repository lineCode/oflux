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
#include "atomic/OFluxAtomic.h"
#include "atomic/OFluxAtomicHolder.h"
#include "flow/OFluxFlowNode.h"
#include "event/OFluxEventBase.h"
#include "OFluxLogging.h"

namespace oflux {
namespace atomic {

EventBasePtr Atomic::_null_static; // null

static const char *
convert_wtype_to_string(int wtype)
{
	static const char * conv[] =
		{ "None "
		, "Read "
		, "Write"
		, "Excl."
		, "Upgr."
		};
	static const char * fallthrough = "?    ";
	return (wtype >=0 && (size_t)wtype < (sizeof(conv)/sizeof(fallthrough))
		? conv[wtype]
		: fallthrough);
}

void
AtomicMapAbstract::log_snapshot(const char * guardname)
{
	AtomicMapWalker * w = walker();
	const void * k = NULL;
	Atomic * a = NULL;
	oflux_log_info(" Guard: %s\n",guardname);
	size_t key_count = 0;
	while(w->next(k,a)) {
		assert(a);
		oflux_log_info("  [%-5u] %s %d %s %c%c\n"
			, key_count
			, a->atomic_class()
			, a->waiter_count()
			, convert_wtype_to_string(a->wtype())
			, (*(a->data()) ? '-' : 'N')
			, (a->held() ? 'H' : '-'));
		if(a->waiter_count() > 0 && (key_count == 0 || a->atomic_class() != AtomicPooled::atomic_class_str)) {
			a->log_snapshot_waiters();
		}
		++key_count;
	}
	oflux_log_info("/Guard: %s\n",guardname);
	delete w;
}

void
AtomicCommon::log_snapshot_waiters() const
{
	std::deque<AtomicCommon::AtomicQueueEntry>::const_iterator itr = _waiters.begin();
	size_t repeat_count = 0;
	const char * this_wtype = NULL;
	const char * this_name = NULL;
	const char * last_wtype = "";
	const char * last_name = "";
	while(itr != _waiters.end()) {
		this_wtype = convert_wtype_to_string((*itr).wtype);
		this_name = (*itr).event->flow_node()->getName();
		if(this_wtype == last_wtype && this_name == last_name) {
			// no line emitted
		} else {
			if(repeat_count > 1) {
				oflux_log_debug("       ...(repeated %u times)\n"
					, repeat_count);
			}
			oflux_log_debug("   %s %s\n"
				, this_wtype
				, this_name);
			repeat_count = 0;
		}
		++repeat_count;
		last_wtype = this_wtype;
		last_name = this_name;
		++itr;
	}
	if(repeat_count > 1) {
		oflux_log_debug("       ...(repeated %u times)\n"
			, repeat_count);
	}
}

const char * AtomicPooled::atomic_class_str = "Pooled   ";

void
AtomicPool::log_snapshot_waiters() const
{
	std::deque<EventBasePtr>::const_iterator itr = _q.begin();
	while(itr != _q.end()) {
		oflux_log_debug("   None  %s\n", (*itr)->flow_node()->getName());
		++itr;
	}
}

void * 
AtomicPool::get_data()
{
        void * ptr = NULL;
        if(_dq.size()) {
                ptr = _dq.front();
                _dq.pop_front();
        }
        return ptr;
}

void 
AtomicPool::put_data(void * p)
{
        assert(p);
        _dq.push_back(p);
}

EventBasePtr 
AtomicPool::get_waiter()
{
        EventBasePtr r(NULL);
        if(_q.size()) {
                r = _q.front();
                _q.pop_front();
        }
        return r;
}

void 
AtomicPool::put_waiter(EventBasePtr & ev)
{
        _q.push_back(ev);
}

const void * 
AtomicPool::get(Atomic * & a_out,const void * key) 
{
        static int _to_use;
        //assert(_ap_list == NULL || _ap_list->next() != _ap_list);
	oflux_log_trace2("ap::get() [%d] %p\n", oflux_self(), this);
        if(_ap_list) {
                a_out = _ap_list;
                _ap_list = _ap_list->next();
                if(_ap_list == NULL) {
                        _ap_last = NULL;
                }
                reinterpret_cast<AtomicPooled *>(a_out)->next(NULL);
        } else {
                a_out = new AtomicPooled(*this,NULL);
        }
        //assert(_ap_list == NULL || _ap_list->next() != _ap_list);
        assert(*(a_out->data()) == NULL);
        //assert(a_out != _ap_list);
        return &_to_use;
}

void 
AtomicPool::put(AtomicPooled * ap)
{
        //assert(_ap_list == NULL || _ap_list->next() != _ap_list);
        //assert(ap != _ap_list);
        if(ap->next() == NULL && ap != _ap_last) {
                assert(*(ap->data()) == NULL);
                ap->next(_ap_list);
                if(_ap_list == NULL) {
                        _ap_last = ap;
                }
                _ap_list = ap;
        }
        //assert(_ap_list == NULL || _ap_list->next() != _ap_list);
}

AtomicPool::~AtomicPool()
{
        AtomicPooled * next;
        AtomicPooled * on = _ap_list;
        while(on) {
                next = on->next();
                delete on;
                on = next;
        }
}

void 
AtomicPooled::release(
	  std::vector<EventBasePtr > & rel_ev
	, EventBasePtr &)
{
	if(_pool.waiter_count()) {
		oflux_log_trace2("apd::release() [%d] %p wc %p data\n", oflux_self(), this, _data);
		EventBasePtr w_ev = _pool.get_waiter();
		AtomicsHolder & w_atomics = w_ev->atomics();
		HeldAtomic * re_w_ptr = w_atomics.get(w_atomics.working_on());
		*(re_w_ptr->atomic()->data()) = _data;
		_data = NULL;
		rel_ev.push_back(w_ev);
	} else {
		oflux_log_trace2("apd::release() [%d] %p no wc %p data\n", oflux_self(), this, _data);
		if(_data) { // return it to the pool
			_pool.put_data(_data);
		}
		_data = NULL;
		_pool.put(this);
	}
}


bool AtomicPoolWalker::next(const void * & key, Atomic * &atom)
{
        bool res = (_n != NULL);
        key = NULL;
        if(res) {
                atom = _n;
                _n = _n->next();
        }
        return res;
}

bool TrivialWalker::next(const void * & key, Atomic * &atom)
{
        bool res = _more;
        if(res) {
                key = NULL;
                atom = &_atom;
                _more = false;
        }
        return res;
}

} // namespace atomic
} // namespace oflux
