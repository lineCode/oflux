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
//
// state:
//    empty [1]
//      head->next == 0x1
//      && head == tail
// trans:
//  push.1->2
//  (nothing else is legal)
//
//
// state:
//    held0 [2]
//      head->next == 0x0
//      && head == tail
// trans:
//  push.2->3
//  pop.2->1
//  (nothing else is legal)
//
// state:
//    heldM [3]
//      head->next > 0x1
//      && head != tail
// trans:
//  pop.3->(2,3)
//  (nothing else is legal)
//

#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <cassert>
#include <deque>
#include "lockfree/OFluxMachineSpecific.h"

#define dprintf printf

namespace event {
struct Event {
	Event(int i) : has(-1), waiting_on(-1), id(i), next(NULL) {}

	int has;
	int waiting_on;
	int id;
	Event * next;
};

struct LocalEventList { // use in a single thread
	LocalEventList() : head(NULL), tail(NULL) {}

	void push(Event *e) {
		assert(e->id);
		e->next = NULL;
		if(tail) {
			tail->next = e;
			tail = e;
		} else {
			head = e;
			tail = e;
		}
	}
	Event * pop() {
		Event * r = NULL;
		if(head && head->id) {
			r = head;
			if(tail == head) {
				tail = NULL;
			}
			head = head->next;
			r->next = NULL;
			if(!r->id) {
				oflux::lockfree::store_load_barrier();
			}
		}
		assert( (!r) || r->id);
		return r;
	}
		

	Event * head;
	Event * tail;
};

template<typename T>
inline T * mk(T * p)
{
	size_t u = reinterpret_cast<size_t>(p);
	return reinterpret_cast<T *>(u | 0x0001);
}

template<typename T>
inline T * unmk(T * p)
{
	size_t u = reinterpret_cast<size_t>(p);
	return reinterpret_cast<T *>(u & ~0x0001);
}

template<typename T>
inline bool mkd(T * p)
{
	return reinterpret_cast<size_t>(p) & 0x0001;
}

template<typename T>
inline bool is_one(T* p)
{
	return reinterpret_cast<size_t>(p) == 0x0001;
}

struct EventList { // thread safe
	EventList() : head(new Event(0)), tail(head) 
	{ head->next = mk(head->next); }
	//
	// states:
	// 1. empty    and nothing   has it
	// 2. empty    and something has it
	// 3. nonempty and something has it
	//
	// done by lots at the same time:
	// 1 -> 2 (push) -- does not queue the Event
	// 2 -> 3 (push) -- does queue the Event
	// these done only by the 'holder':
	// 3 -> 2 (pop)
	// 2 -> 1 (pop : returns NULL)
	//
	bool push(Event *e);
	Event * pop();

	inline bool marked() const 
	{ return is_one(head->next); }

	Event * head;
	Event * tail;
	// state encoding:
	//
	// 1. (empty) head == tail, head->next == 0x0001 
	// 2. (held0) head == tail, head->next == NULL
	// 3. (heldM) head != tail, head->next > 0x0001 
	//
	// transitions:
	// 2 -> 1 : cas head->next from NULL to 0x0001
	// 1 -> 2 : cas head->next from 0x0001 to NULL
	// 3 -> 2 : cas head       from head to head->next
	// 2 -> 3 : cas tail->next from NULL to e
	// (of course 3 -> 3 case is there for push and pop)
	//
	// conflicts:
	//
	// 1->2 /\ 1->2 : cas ensures winner (one returns bool true)
	// 1->2 /\ 2->1 : cas ensures winner
	// 1->2 /\ 2->3 : 
	//   thread_A: sees head->next == 0x0001
	//   thread_B: sees head->next == NULL
	//  if thread_B is wrong, cas on tail->next=head->next from NULL to 0x1
	//      fails
	//  if thread_A is wrong, case on head->next from 0x1 to NULL fails
	// 1->2 /\ 3->2 :
	//   thread_A: sees head->next == 0x0001
	//   thread_B: sees head->next > 0x0001
	//     uhmm....
	// 2->1 /\ 2->1 : cas ensures winner (one returns bool true)
	// 2->1 /\ 2->3 : cas on same loc from NULL to e vs 0x1 (one winner)
	// 2->1 /\ 3->2 : 
	//   thread_A: sees head->next = NULL
	//   thread_B: sees head->next > 0x1
	//  if thread_A is wrong, then cas on head->next from NULL fails
	//  if thread_B is wrong, ...
	// 2->3 /\ 2->3 : even if one is wrong about the real tail, still
	//       only one of these gets its cas on a tail->next done
	// 2->3 /\ 3->2 :
	//   thread_A: sees head->next = NULL
	//   thread_B: sees head->next > 0x0001
	//  if thread_A is wrong his cas on tail->next:
	//      succeeds: addition to tail but head != tail
	//      fails: need to follow tail
	//  if thread_B is wrong, head = tail, head->next == NULL
	//      cas on head from head to head->next ...
	// 3->2 /\ 3->2 : one wins the cas on the head
};

#define check_tail(h,t) \
   { Event * ch = h; \
     while(unmk(ch) && unmk(ch->next)) ch = ch->next; \
     assert((t->next != NULL) || ch == t); \
   }

bool
EventList::push(Event * e)
{
	e->next = NULL;
	Event * t = NULL;

	if(!e->id) {
		oflux::lockfree::store_load_barrier();
	}
	int id = e->id;
	assert(id);
	int w_on = e->waiting_on;
	int hs = e->has;
	e->id = 0;
	e->waiting_on = -1;
	e->has = -1;
	Event * h = NULL;
	Event * hn = NULL;
	while(1) {
		h = head;
		hn = h->next;
		if(is_one(hn) && __sync_bool_compare_and_swap(
				  &(head->next)
				, 0x0001
				, NULL)) {
			// 1->2
			tail = head;
			e->id = id;
			e->waiting_on = w_on;
			e->has = hs;
			return true;
		} else {
			// (2,3)->3
			t = tail;
			Event * old_t = t;
			while(unmk(t) && unmk(t->next)) {
				t = t->next;
			}
			if(t != tail && t->next == NULL) {
				__sync_bool_compare_and_swap(&tail,old_t,t);
			}
			//check_tail(h,t);
			if(unmk(t) && __sync_bool_compare_and_swap(&(t->next),NULL,e)) {
				tail = e;
				t->id = id;
				t->waiting_on = w_on;
				t->has = hs;
				break;
			}
		}
	}
	return false;
}

Event *
EventList::pop()
{
	Event * r = NULL;
	Event * h = NULL;
	Event * hn = NULL;
	Event * t = NULL;
	while(1) {
		h = head;
		hn = h->next;
		t = tail;
		Event * old_t = t;
		while(unmk(t) && unmk(t->next)) {
			t = t->next;
		}
		if(t != tail && t->next == NULL) {
			__sync_bool_compare_and_swap(&tail,old_t,t);
		}
		//check_tail(h,t);
		if(h != tail && hn != NULL 
				&& !is_one(hn)
				&& h->id != 0
				&& __sync_bool_compare_and_swap(
					&head
					, h
					, hn)) {
			// 3->(2,3)
			r = h;
			assert(r->id);
			r->next = NULL;
			break;
		} else if(hn==NULL && __sync_bool_compare_and_swap(
				  &(head->next)
				, hn
				, 0x0001)) {
			// 2->1
			break; //empty
		}
	}
	return r;
}

} // namespace event

namespace atomic {

struct Exclusive {
	void release(std::deque<event::Event *> & el);
	bool acquire_or_wait(event::Event * e);

	void dump();

	event::EventList waiters;
	int index;
};

void
Exclusive::dump()
{
	const char * state =
		(event::is_one(waiters.head->next) ?
			"empty [1]"
		: (waiters.head->next == NULL ?
			"held0 [2]" :       
			"heldM [3]"));

	printf(" atomic[%d] in state %s\n"
		, index
		, state);
	printf("  head = ");
	event::Event * e = waiters.head;
	while(e != NULL && !is_one(e)) {
		printf("%d->",e->id);
		e  = e->next;
	}
	if(e == NULL) {
		printf("(0x0)\n");
	} else if(event::is_one(e)) {
		printf("(0x1)\n");
	} else {
		printf("\n");
	}
	printf("  tail = %d\n",waiters.tail->id);
}

void
Exclusive::release(std::deque<event::Event *> & el)
{
	event::Event * e = waiters.pop();
	if(e) { 
		assert(e->id);
		e->has = index;
		e->waiting_on = -1;
		el.push_back(e);
	}
}

bool
Exclusive::acquire_or_wait(event::Event * e)
{
	e->waiting_on = index;
	e->has = -1;
	bool acqed = waiters.push(e);
	if(acqed) {
		e->waiting_on = -1;
		e->has = index;
	}
	return acqed;
}


} // namespace atomic

size_t num_threads = 2;
size_t num_events_per_thread = 10;
size_t num_atomics = 2;
size_t iterations = 1000;

pthread_barrier_t barrier;
pthread_barrier_t end_barrier;

atomic::Exclusive atomics[1024];

#define DUMPATOMICS_ \
	for(size_t i = 0; i < num_atomics; ++i) { atomics[i].dump(); }
#define DUMPATOMICS //DUMPATOMICS_

void * run_thread(void *vp)
{
	int * ip = reinterpret_cast<int *>(vp);
	long acquire_count = 0;
	long wait_count = 0;
	long release_count = 0;
	long no_op_iterations = 0;

	std::deque<event::Event *> running_evl[2];
	//event::LocalEventList * from_other_thread = NULL;
	for(size_t i = 0 ; i < num_events_per_thread; ++i) {
		event::Event * ep = new event::Event(1+ num_events_per_thread * (*ip) + i);
		assert(ep->id);
		running_evl[0].push_back(ep);
	}

	int target_atomic = 0;
	event::Event * e = NULL;
	int id = 0;

	// try to distribute to each thread all the atomics
	// this is uncontended acquisition
	for(size_t a = *ip
			; a < num_atomics && running_evl[0].size() > 0
			; a+= num_threads) {
		e = running_evl[0].front();
		running_evl[0].pop_front();
		id = e->id;
		assert(id);
		if(atomics[a].acquire_or_wait(e)) {
			// got it right away (no competing waiters)
			dprintf("%d]%d}- %d acquired\n",*ip, (*ip)%num_atomics,id);
			running_evl[1].push_back(e);
			++acquire_count;
		} else {
			dprintf("%d]%d}- %d waited\n",*ip,(*ip)%num_atomics, id);
			++wait_count;
		}
	}
	
	pthread_barrier_wait(&barrier);
	for(size_t j = 0; j < iterations; ++j) {
		if(!running_evl[j%2].size()) {
			++no_op_iterations;
		}
		while(running_evl[j%2].size()) {
			e = running_evl[j%2].front();
			running_evl[j%2].pop_front();
			no_op_iterations = 0;
			int a_index = e->has;
			assert(e->id);
			dprintf("%d]   %d running\n",*ip,e->id);
			if(a_index >=0) {
				DUMPATOMICS
				dprintf("%d]%d} %d releasing\n",*ip,a_index,e->id);
				size_t pre_sz = running_evl[(j+1)%2].size();
				atomics[a_index].release(running_evl[(j+1)%2]);
				if(running_evl[(j+1)%2].size() > pre_sz) {
					dprintf("%d]%d} %d came out\n",*ip,a_index, running_evl[(j+1)%2].back()->id);
				} else {
					dprintf("%d]%d} nothing came out\n",*ip,a_index);
				}
				e->has = -1;
				++release_count;
			}
			a_index = target_atomic;
			target_atomic = (target_atomic+1)%num_atomics;
			id = e->id;
			assert(e->id);
			DUMPATOMICS
			if(atomics[a_index].acquire_or_wait(e)) {
				// got it right away (no competing waiters)
				dprintf("%d]%d} %d acquired\n",*ip,a_index, id);
				assert(e->id);
				running_evl[(j+1)%2].push_back(e);
				++acquire_count;
			} else {
				dprintf("%d]%d} %d waited\n",*ip,a_index, id);
				++wait_count;
			}
		}
	}
	pthread_barrier_wait(&end_barrier);
	size_t held_count = 0;
	size_t waiting_on = 0;
	std::deque<event::Event *>::iterator ditr = 
		running_evl[iterations%2].begin();
	while(ditr != running_evl[iterations%2].end()) {
		e = *ditr;
		if(!e->id) {
			break;
		}
		held_count += (e->has >= 0 ? 1 : 0);
		waiting_on += (e->waiting_on >= 0 ? 1 : 0);
		printf("%d]+  %d running with %d\n"
			, *ip, e->id, e->has);
		++ditr;
	}
	printf(" thread %d exited ac: %ld wc: %ld rc: %ld "
		"hc: %d wo: %d noi:%ld\n"
		, *ip
		, acquire_count
		, wait_count
		, release_count
		, held_count
		, waiting_on
		, no_op_iterations
		);
	return NULL;
}


int main(int argc, char  * argv[])
{
	if(argc >= 2) {
		num_threads = atoi(argv[1]);
	}
	if(argc >= 3) {
		iterations = atoi(argv[2]);
	}
	srand(iterations);
	if(argc >= 4) {
		num_events_per_thread = atoi(argv[3]);
	}
	if(argc >= 5) {
		num_atomics = atoi(argv[4]);
	}
	printf("%u threads, %u iterations, %u events/thread, %u resource items\n"
		, num_threads
		, iterations
		, num_events_per_thread
		, num_atomics);
        pthread_t tids[num_threads];
	int tns[num_threads];
	pthread_barrier_init(&barrier,NULL,num_threads);
	pthread_barrier_init(&end_barrier,NULL,num_threads);
	for(size_t a = 0; a < num_atomics; ++a) {
		atomics[a].index = a;
	}
        for(size_t i = 0; i < num_threads; ++i) {
		tns[i] = i;
                int err = pthread_create(&tids[i],NULL,run_thread,&(tns[i])); if(err != 0) {
                        exit(11);
                }
        }
	void * tret = NULL;
	for(size_t i = 0; i < num_threads; ++i) {
		int err = pthread_join(tids[i],&tret);
                if(err != 0) {
                        exit(12);
                }
        }
	size_t a_count = 0;
	for(size_t i = 0; i < num_atomics; ++i) {
		a_count += (atomics[i].waiters.marked() ? 1 : 0);
	}
	printf("program ac: %u\n",a_count);
	DUMPATOMICS_ // always
	fflush(stdout);

	return 0;
}

