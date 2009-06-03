#ifndef _OFLUX_EVENT_H
#define _OFLUX_EVENT_H

/**
 * @file OFluxEvent.h
 * @author Mark Pichora
 *  This is the header file for the Event template(s).
 *  The class is broken into three layers (ascending as you 
 *  know more about the type(s) involved (input and output)
 */

#include <boost/shared_ptr.hpp>
#include "OFlux.h"
#include "OFluxFlow.h"
#include "OFluxAtomicHolder.h"
#include <vector>
#ifdef HAS_DTRACE
#include "ofluxprobe.h"
#else
#define OFLUX_NODE_START(X,Y,Z)
#define OFLUX_NODE_DONE(X)
#endif

#define _NODE_START(X,Y,Z) OFLUX_NODE_START(const_cast<char *>(X),Y,Z)
#define _NODE_DONE(X)      OFLUX_NODE_DONE(const_cast<char *>(X))

namespace oflux {

//forward decl
template<typename H> H * convert(typename H::base_type *);
template<typename H> const H * const_convert(const typename H::base_type *);


/**
 * @class EventBase
 * @brief the event base class which covers the low level common operations
 * Events hold a reference to the previous event to hold onto the memory.
 * This keeps it from being reclaimed to the heap before the current event
 * is done executing.
 */
class EventBase : public flow::NodeCounterIncrementer {
public:
	static boost::shared_ptr<EventBase> no_event;

	EventBase(boost::shared_ptr<EventBase> predecessor,flow::Node *flow_node)
		: flow::NodeCounterIncrementer(flow_node)
		, _predecessor(predecessor)
		, _error_code(0)
		//, _flow_node(flow_node)
		{
			std::vector<flow::GuardReference *> & vec = 
				flow_node->guards();
			for(int i = 0; i < (int) vec.size(); i++) {
				_atomics.add(vec[i]);
			}
		}
	virtual ~EventBase() {}
	virtual OutputWalker output_type() = 0;
	virtual const void * input_type() = 0;
	void release() { _predecessor = no_event; }
	virtual int execute() = 0;
	Atomic * acquire(int & wtype, flow::GuardReference * & fgr, 
			AtomicsHolder & given_atomics)
		{
			int res = _atomics.acquire(given_atomics,
					input_type(),
					flow_node()->getName());
			HeldAtomic * ha = (res == -1 ? NULL : _atomics.get(res));
			wtype = (ha ? ha->wtype() : 0);
			fgr = (ha ? ha->flow_guard_ref() : NULL);
			return (ha ? ha->atomic() : NULL);
		}
	void release(std::vector<boost::shared_ptr<EventBase> > & released_events)
		{ _atomics.release(released_events); }
	inline void error_code(int ec) { _error_code = ec; }
	inline int error_code() { return _error_code; }
	inline AtomicsHolder & atomics() { return _atomics; }
	void log_snapshot();
	inline boost::shared_ptr<EventBase> & get_predecessor()
		{ return _predecessor; }
private:
	boost::shared_ptr<EventBase> _predecessor;
protected:
	int           _error_code;
	AtomicsHolder _atomics;
};

/**
 * @class EventBase2
 * @brief template which elaborates the basic input and output memory structures
 * This refinement of EventBase actually holds the memory used to 
 * output an event result.
 */
template<typename IT, typename OT, typename AM>
class EventBase2 : public EventBase {
public:
	EventBase2(boost::shared_ptr<EventBase> predecessor, flow::Node * flow_node)
		: EventBase(predecessor,flow_node)
		{
		}
	virtual ~EventBase2() {}
protected:
	/*
        inline const IT * pr_input_type() 
		{ EventBase * pred = get_predecessor();
		  return static_cast<IT*> (pred ? pred->output_type() : &theE); }
	*/
	inline OT * pr_output_type() { return &_ot; }
	inline AM * atomics_argument() { return &_am; }
protected:
	OT            _ot;
	AM            _am;
};

/**
 * @class EventBase3
 * @brief intermediate event template
 * The execution function is specified as a template parameter.  
 * Concrete structures are manipulated as input and output.
 * Only the execute implementation is missing.
 */
template<typename IM,typename OM, typename AM>
class EventBase3 : public EventBase2<typename IM::base_type,typename OM::base_type,AM> {
public:
	EventBase3(boost::shared_ptr<EventBase> predecessor,
			const IOConversionBase<IM> *im_io_convert,
			flow::Node * flow_node)
		: EventBase2<typename IM::base_type,typename OM::base_type,AM>(predecessor,flow_node)
		, _im_io_convert(im_io_convert)
		{
		  (convert<OM>(EventBase2<typename IM::base_type,typename OM::base_type,AM>::pr_output_type()))->next(NULL);
		}
	virtual ~EventBase3()
		{
		  // recover splayed outputs
		  OM * output_m = const_convert<OM>(EventBase2<typename IM::base_type, typename OM::base_type,AM>::pr_output_type())->next();
		  while(output_m) {
			OM * next_output_m = output_m->next();
			delete output_m;
			output_m = next_output_m;
		  }
                  delete _im_io_convert;
		}
	virtual OutputWalker output_type()
		{ 
			OutputWalker ow(convert<OM>(EventBase2<typename IM::base_type,typename OM::base_type,AM>::pr_output_type())); 
			return ow;
		}
	virtual const void * input_type() 
                { return _im_io_convert ? _im_io_convert->value() : NULL; }
protected:
	inline const IM * pr_input_type() const 
                { return _im_io_convert ? _im_io_convert->value() : NULL; }
private:
	const IOConversionBase<IM> * _im_io_convert;
};
/**
 * @class Event
 * @brief the final Event template containing the execution function
 * The execution function is specified as a template parameter.  
 * This highest level of the template hierarchy know the concrete data
 * structures manipulated as input and output.
 */
template<typename IM,typename OM, typename AM, int (*node_func)(const IM *, OM*, AM *)>
class Event : public EventBase3<IM,OM,AM> {
public:
	Event(boost::shared_ptr<EventBase> predecessor,
                        const IOConversionBase<IM> * im_io_convert,
                        flow::Node * flow_node)
		: EventBase3<IM,OM,AM>(predecessor,im_io_convert,flow_node)
		{}
	virtual int execute()
		{ 
		  EventBase2<typename IM::base_type,typename OM::base_type,AM>::atomics_argument()->fill(&(this->atomics()));
		  _NODE_START(EventBase::flow_node()->getName(), EventBase::flow_node()->getIsSource(), EventBase::flow_node()->getIsDetached());
		  int res = (*node_func)(
			EventBase3<IM,OM,AM>::pr_input_type(),
			convert<OM>(EventBase2<typename IM::base_type,typename OM::base_type,AM>::pr_output_type()),
			EventBase2<typename IM::base_type,typename OM::base_type,AM>::atomics_argument()
			); 
		  if (!res) EventBase::release();
		  _NODE_DONE(EventBase::flow_node()->getName());
		  return res;
		}
};

/**
 * @class ErrorEvent
 * @brief A version of Event for error handler events
 * This kind of event has a C function associated with it that takes 
 * the error code as the final argument in its parameter list
 */
template<typename IM,typename OM, typename AM, int (*node_func)(const IM *, OM*, AM*, int)>
class ErrorEvent : public EventBase3<IM,OM,AM> {
public:
	ErrorEvent(boost::shared_ptr<EventBase> predecessor,
                        const IOConversionBase<IM> * im_io_convert,
                        flow::Node * flow_node)
		: EventBase3<IM,OM,AM>(predecessor,im_io_convert,flow_node)
		, _error_im(* EventBase3<IM,OM,AM>::pr_input_type())
		{}
	virtual int execute()
		{ 
		  EventBase2<typename IM::base_type, typename OM::base_type,AM>::atomics_argument()->fill(&(this->atomics()));
		  _NODE_START(EventBase::flow_node()->getName(), EventBase::flow_node()->getIsSource(), EventBase::flow_node()->getIsDetached());
		  int res = (*node_func)(
			//EventBase3<IM,OM,AM>::pr_input_type(),
			&_error_im,
			convert<OM>(EventBase2<typename IM::base_type, typename OM::base_type,AM>::pr_output_type()), 
			EventBase2<typename IM::base_type,typename OM::base_type,AM>::atomics_argument(),
			EventBase::error_code()); 
		  if (!res) EventBase::release();
		  _NODE_DONE(EventBase::flow_node()->getName());
		  return res;
		}
private:
	IM _error_im;
};

/**
 * @brief create an Event from a flow::Node
 * (this is a factory function)
 * @param pred_node_ptr  pointer to predecessor event
 * @param fn  flow node
 * @param im_io_convert  a converter for the input to this event
 *
 * @return smart pointer to the new event (heap allocated)
 **/
template<typename IM,typename OM, typename AM, int (*node_func)(const IM *, OM*, AM*)>
boost::shared_ptr<EventBase> create(boost::shared_ptr<EventBase> pred_node_ptr, const void * im_io_convert,flow::Node *fn)
{
	return boost::shared_ptr<EventBase>(new Event<IM,OM,AM,node_func>(pred_node_ptr,reinterpret_cast<const IOConversionBase<IM> *>(im_io_convert),fn));
}

/**
 * @brief create an Event from a source flow::Node
 * (this is a factory function)
 * @param fn  the source flow node
 *
 * @return  smart pointer managed new source event (heap allocated)
 **/
template<typename IM,typename OM, typename AM, int (*node_func)(const IM *, OM*, AM*)>
boost::shared_ptr<EventBase> create_source(flow::Node *fn)
{
	return boost::shared_ptr<EventBase>(new Event<IM,OM,AM,node_func>(EventBase::no_event,NULL, fn));
}

/**
 * @brief create and ErrorEvent from a flow::Node
 * (this is a factory function)
 * @param pred_node_ptr  predecessor event
 * @param fn  flow node
 * @param im_io_convert  a converter for the input to this event
 *
 * @return smart pointer to the new error event (heap allocated)
 **/
template<typename IM,typename OM,typename AM, int (*node_func)(const IM *, OM*, AM*, int)>
boost::shared_ptr<EventBase> create_error(boost::shared_ptr<EventBase> pred_node_ptr, const void * im_io_convert,flow::Node * fn)
{
	return boost::shared_ptr<EventBase>(new ErrorEvent<IM,OM,AM,node_func>(pred_node_ptr,reinterpret_cast<const IOConversionBase<IM> *>(im_io_convert),fn));
}


}; // namespace

#endif // _OFLUX_EVENT_H
