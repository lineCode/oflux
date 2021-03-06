#ifndef _OFLUX_EVENT_H
#define _OFLUX_EVENT_H
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

/**
 * @file OFluxEvent.h
 * @author Mark Pichora
 *  This is the header file for the Event template(s).
 *  The class is broken into three layers (ascending as you 
 *  know more about the type(s) involved (input and output).
 *  If your code creates events, this header is needed.
 */

#include "event/OFluxEventBase.h"
#include "atomic/OFluxAtomicHolder.h"
#include "OFluxIOConversion.h"
#include "flow/OFluxFlowNode.h"
#include "flow/OFluxFlowFunctions.h"

#include "OFluxLogging.h"

namespace oflux {


//forward decl
template<typename H> H * convert(typename H::base_type *);
template<typename H> const H * const_convert(const typename H::base_type *);


/**
 * @class EventBaseTyped
 * @brief intermediate event template
 * The execution function is specified as a template parameter.  
 * Concrete structures are manipulated as input and output.
 * Only the execute implementation is missing.
 */
template< typename Detail >
class EventBaseTyped : public EventBase {
public:
	EventBaseTyped(   EventBaseSharedPtr & predecessor
			, const IOConversionBase<typename Detail::In_> *im_io_convert
			, flow::Node * flow_node)
		: EventBase(predecessor,flow_node,_atomics)
		, _im_io_convert(im_io_convert)
		, _atomics(flow_node->isGuardsCompletelySorted())
	{
		std::vector<flow::GuardReference *> & vec = 
			flow_node->guards();
		for(int i = 0; i < (int) vec.size(); i++) {
			_atomics.add(vec[i]);
		}
		pr_output_type()->next(NULL);
	}
	virtual ~EventBaseTyped()
	{
		// recover splayed outputs
		typename Detail::Out_ * output_m = 
			pr_output_type()->next();
		while(output_m) {
			typename Detail::Out_ * next_output_m = output_m->next();
			delete output_m;
			output_m = next_output_m;
		}
		delete _im_io_convert;
	}
	virtual OutputWalker output_type()
	{ 
		OutputWalker ow(pr_output_type()); 
		return ow;
	}
	virtual const void * input_type() 
	{ return _im_io_convert ? _im_io_convert->value() : NULL; }
protected:
	inline const typename Detail::In_ * pr_input_type() const 
	{ return _im_io_convert ? _im_io_convert->value() : NULL; }
	inline typename Detail::Out_ * pr_output_type() 
	{ return &_ot; }
public:
	inline typename Detail::Atoms_ * atomics_argument() 
	{ return &_am; }

protected:
	typename Detail::Out_ _ot;
	typename Detail::Atoms_ _am;
private:
	const IOConversionBase<typename Detail::In_> * _im_io_convert;
	atomic::AtomicsHolder _atomics;
};

/**
 * @class Event
 * @brief the final Event template containing the execution function
 * The execution function is specified as a template parameter.  
 * This highest level of the template hierarchy know the concrete data
 * structures manipulated as input and output.
 */
template< typename Detail >
class Event : public EventBaseTyped<Detail> {
public:
	Event(    EventBaseSharedPtr & predecessor
		, const IOConversionBase<typename Detail::In_> * im_io_convert
		, flow::Node * flow_node)
		: EventBaseTyped<Detail>(predecessor,im_io_convert,flow_node)
	{
		oflux_log_trace2("[%d] Event cstr %s %p\n"
			, oflux_self()
			, flow_node->getName()
			, this);
	}
	virtual ~Event()
	{
		oflux_log_trace2("[%d] ~Event %s %p\n"
			, oflux_self()
			, EventBase::flow_node()->getName()
			, this);
	}
	virtual int execute()
	{ 
		EventBaseTyped<Detail>::atomics_argument()->fill(&(this->atomics()));
		EventBase::flow_node()->_executions++;
		const char * ev_name __attribute__((unused)) = 
			EventBase::flow_node()->getName();
		PUBLIC_NODE_START(
			  this
			, ev_name
			, EventBase::flow_node()->getIsSource()
			, EventBase::flow_node()->getIsDetached());
		int res = (*Detail::nfunc)(
			  EventBaseTyped<Detail>::pr_input_type()
			, EventBaseTyped<Detail>::pr_output_type()
			, EventBaseTyped<Detail>::atomics_argument()); 
		if (!res) EventBase::release();
		PUBLIC_NODE_DONE(this,ev_name);
		return res;
	}
};

/**
 * @class ErrorEvent
 * @brief A version of Event for error handler events
 * This kind of event has a C function associated with it that takes 
 * the error code as the final argument in its parameter list
 */
template< typename Detail >
class ErrorEvent : public EventBaseTyped<Detail> {
public:
	ErrorEvent(       EventBaseSharedPtr & predecessor
                        , const IOConversionBase<typename Detail::In_> * im_io_convert
                        , flow::Node * flow_node)
		: EventBaseTyped<Detail>(predecessor,im_io_convert,flow_node)
		, _error_im(EventBaseTyped<Detail>::pr_input_type()
			? * EventBaseTyped<Detail>::pr_input_type()
			: _dont_care_im)
	{}
	virtual int execute()
	{ 
		EventBaseTyped<Detail>::atomics_argument()->fill(&(this->atomics()));
		EventBase::flow_node()->_executions++;
		const char * ev_name __attribute__((unused)) = 
			EventBase::flow_node()->getName();
		PUBLIC_NODE_START(
			  this
			, ev_name
			, EventBase::flow_node()->getIsSource()
			, EventBase::flow_node()->getIsDetached());
		int res = (*Detail::nfunc)(
			  &_error_im
			, convert<typename Detail::Out_>(EventBaseTyped<Detail>::pr_output_type())
			, EventBaseTyped<Detail>::atomics_argument()
			, EventBase::error_code()); 
		if (!res) EventBase::release();
		PUBLIC_NODE_DONE(this,EventBase::flow_node()->getName());
		return res;
	}
private:
	typename Detail::In_ _error_im;
	static typename Detail::In_ _dont_care_im;
};

template<typename Detail>
typename Detail::In_ ErrorEvent<Detail>::_dont_care_im;

/**
 * @brief create an Event from a flow::Node
 * (this is a factory function)
 * @param pred_node_ptr  pointer to predecessor event
 * @param fn  flow node
 * @param im_io_convert  a converter for the input to this event
 *
 * @return smart pointer to the new event (heap allocated)
 **/
template< typename Detail >
EventBasePtr
create(   EventBaseSharedPtr pred_node_ptr
	, const void * im_io_convert
	, flow::Node *fn)
{
	return mk_EventBasePtr(new Event<Detail>(
		  pred_node_ptr
		, reinterpret_cast<const IOConversionBase<typename Detail::In_> *>(im_io_convert)
		, fn));
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
template< typename Detail >
EventBasePtr 
create_error(
	  EventBaseSharedPtr pred_node_ptr
	, const void * im_io_convert
	, flow::Node * fn)
{
	return mk_EventBasePtr(new ErrorEvent<Detail>(
		  pred_node_ptr
		, reinterpret_cast<const IOConversionBase<typename Detail::In_> *>(im_io_convert)
		, fn));
}


}; // namespace

#endif // _OFLUX_EVENT_H
