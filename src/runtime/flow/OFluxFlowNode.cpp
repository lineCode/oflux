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
#include "flow/OFluxFlowNode.h"
#include "flow/OFluxFlowCase.h"
#include "flow/OFluxFlowGuard.h"
#include "flow/OFluxFlowCommon.h"
#include "OFluxLogging.h"
#include <algorithm>
#include <string.h>

namespace oflux {
namespace flow {


Successor::Successor(const char * name)
        : _name(name)
{
}

Successor::~Successor() 
{ 
        delete_deque_contents<Case>(_cases); 
}

void 
Successor::add(Case * fc, bool front) 
{ 
        if(front) {
                _cases.push_front(fc);
        } else {
                _cases.push_back(fc);
        }
}

void
Successor::remove(Case * fc)
{
	std::deque<Case *>::iterator itr = _cases.begin();
	bool fd = false;
	while(itr != _cases.end()) {
		if(fc == (*itr)) {
			_cases.erase(itr);
			delete fc;
			fd = true;
			break;
		}
		++itr;
	}
	assert(fd && "Successor::remove failed to find");
	if(!fd) {
		const char* error = "Successor::remove failed to find";
		const ssize_t ret = write(STDERR_FILENO, error, strlen(error));
		(void)ret;
		abort();
	}
}

Case * 
Successor::getByTargetName(const char *n)
{
	std::deque<Case *>::iterator itr =
		_cases.begin();
	std::string name = n;
	while(itr != _cases.end()) {
		if(name == (*itr)->targetNodeName()) {
			break;
		}
		++itr;
	}
	return  ( itr == _cases.end()
		? NULL
		: *itr );
}

Case * 
Successor::getByTargetNode(const Node *n)
{
	std::deque<Case *>::iterator itr =
		_cases.begin();
	while(itr != _cases.end()) {
		if(n == (*itr)->targetNode()) {
			break;
		}
		++itr;
	}
	return  ( itr == _cases.end()
		? NULL
		: *itr );
}

Case * 
Successor::get_successor(const void * a)
{
	Case * res = NULL;
	std::deque<Case *>::iterator itr = _cases.begin();
	while(itr != _cases.end() && res == NULL) {
		Case * flowcase = *itr;
		if(flowcase->satisfied(a)) {
			res = flowcase;
		}
		itr++;
	}
	return res;
}

std::string 
create_indention(int depth)
{
        std::string indention;
        for(int i = 0; i < depth; i++) {
                indention = indention + "  ";
        }
        if(depth) {
                indention = indention + "|_";
        }
        return indention;
}

void 
Successor::pretty_print(int depth, std::set<std::string> * visited)
{
        // do not print successor for now
        std::deque<Case *>::iterator itr = _cases.begin();
        while(itr != _cases.end()) {
                (*itr)->pretty_print(depth+1,visited);
                itr++;
        }
        oflux_log_info("%s..%s\n"
		, create_indention(depth+1).c_str()
		, _name.c_str());
}

SuccessorList::SuccessorList()
{
}

SuccessorList::~SuccessorList() 
{ 
        delete_map_contents<std::string, Successor>(_successorlist); 
}

bool
SuccessorList::has_successor_with_target(const Node * n) const
{
	std::map<std::string,Successor *>::const_iterator mitr = _successorlist.begin();
	while(mitr != _successorlist.end()) {
		if((*mitr).second->getByTargetNode(n)) {
			return true;
		}
		++mitr;
	}
	return false;
}

void 
SuccessorList::add(Successor * fs) 
{ 
        std::pair<std::string,Successor *> pr(fs->getName(),fs);
        _successorlist.insert(pr); 
}

void 
SuccessorList::remove(Successor * fs) 
{ 
        std::map<std::string, Successor *>::iterator itr = 
		_successorlist.find(fs->getName());
        assert(fs == (*itr).second);
	_successorlist.erase(itr);
	delete fs;
}

void 
SuccessorList::pretty_print(int depth, std::set<std::string> * visited)
{
        std::map<std::string, Successor *>::iterator itr = _successorlist.begin();
        while(itr != _successorlist.end()) { 
		Successor * succ = itr->second;
                succ->pretty_print(depth+1,visited);
                ++itr;
        }
        oflux_log_info("%s.\n", create_indention(depth+1).c_str());
}

int Node::_last_id=0;

Node::Node(       const char * name
		, const char * function_name
                , CreateNodeFn createfn
                , CreateDoorFn createdoorfn
                , bool is_error_handler
                , bool is_source
                , bool is_door
                , bool is_detached
                , const char * input_unionhash
                , const char * output_unionhash)
        : _instances(0)
        , _executions(0)
	, _id(__sync_fetch_and_add(&_last_id,1))
        , _name(name)
	, _function_name(function_name)
        , _createfn(createfn)
        , _createdoorfn(createdoorfn)
        , _is_error_handler(is_error_handler)
        , _is_source(is_source)
        , _is_door(is_door)
	, _is_initial(-1) // unknown
        , _is_detached(is_detached)
	, _successor_list(NULL)
	, _error_handler_case(new Case())
        , _this_case(new Case(name,this,NULL))
        , _input_unionhash(input_unionhash)
        , _output_unionhash(output_unionhash)
        , _is_guards_completely_sorted(false)
{ 
}

Node::~Node()
{
        delete_vector_contents<GuardReference>(_guard_refs);
	delete _this_case;
	delete _error_handler_case;
	delete _successor_list;
}

struct CompareGuardsInNode {
        // our version of a < comparator
        bool operator()(GuardReference * gr1, GuardReference * gr2)
        {
                return gr1->magic_number() < gr2->magic_number();
        }
};
        
void
Node::sortGuards()
{
        CompareGuardsInNode acompare;
        // sort all guard reference ascending by magic number
        // it does not matter to much how slow std::sort is.
        std::sort(_guard_refs.begin(), _guard_refs.end(),acompare);
        // determine once and for all if the sort has magic number dupes
        _is_guards_completely_sorted = true;
        int last_magic_number = -1;
        for(size_t i = 0; i < _guard_refs.size(); ++i) {
                int mn = _guard_refs[i]->magic_number();
                if(last_magic_number >= mn) {
                        _is_guards_completely_sorted = false;
                        break;
                }
                last_magic_number = mn;
        }
}       

void 
Node::setErrorHandler(Node *fn)
{
        _error_handler_case->setTargetNode(fn);
}

void 
Node::get_successors(std::vector<Case *> & successor_nodes, 
		const void * a,
		int return_code)
{
	if(return_code != 0) {
		if(_error_handler_case->targetNode() != NULL) {
			successor_nodes.push_back(_error_handler_case);
		}
		if(_is_source && !getIsInitial()) { // even on errors
			successor_nodes.push_back(_this_case);
		}
	} else {
		_successor_list->get_successors(successor_nodes,a);
	}
}

bool
Node::getIsInitial()
{
	if(_is_initial >= 0) {
		// nothing to do
	} else if(!getIsSource()) {
		_is_initial = 0;
	} else {
		_is_initial = ! _successor_list->has_successor_with_target(this);
	}
	return _is_initial;
}

void 
Node::add(GuardReference * fgr) 
{ 
	fgr->setLexicalIndex(_guard_refs.size());
	_guard_refs.push_back(fgr); 
}

void 
Node::log_snapshot()
{
#ifdef PROFILING
        oflux_log_info("%s (%c%c%c) %lld instances %lld executions (time real:avg %lf max %lf oflux:avg %lf max %lf)\n", 
                _name.c_str(),
                (_is_source ? 's' : '-'),
                (_is_detached ? 'd' : '-'),
                (_is_error_handler ? 'e' : '-'),
                _instances.value(),
                _executions.value(),
                _real_timer_stats.avg_usec(),
                _real_timer_stats.max_usec(),
                _oflux_timer_stats.avg_usec(),
                _oflux_timer_stats.max_usec());        
#else
        oflux_log_info("%s (%c%c%c) %lld instances %lld executions\n",
                _name.c_str(),
                (_is_source ? 's' : '-'),
                (_is_detached ? 'd' : '-'),
                (_is_error_handler ? 'e' : '-'),
                _instances.value(),
                _executions.value());
#endif
}

void 
Node::pretty_print(int depth, char context, std::set<std::string> * visited)
{
        oflux_log_info("%s%c %s\n", create_indention(depth).c_str(),context, _name.c_str());
        if (visited->find(_name)==visited->end()) {
                visited->insert(_name);
                // if a source node is some node's successor(depth>0 && is_source), do not re-print its sucessor list
                if((depth == 0 ) || (!_is_source)) {
                        if(_successor_list) _successor_list->pretty_print(depth+1,visited);
                }
        }
}


} // namespace flow
} // namespace oflux
