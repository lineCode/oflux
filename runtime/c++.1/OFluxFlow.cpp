#include "OFluxFlow.h"
#include "OFluxLogging.h"
#include "OFluxLibrary.h"
#include <stdlib.h>
#include <cassert>

/**
 * @file OFluxFLow.cpp
 * @author Mark Pichora
 * Flow implementation
 *   A flow is the resident data structure that captures the flow of the 
 *   program (it is read from an XML input to the oflux runtime)
 */

namespace oflux {

FlowFunctionMaps::FlowFunctionMaps(ConditionalMap cond_map[],
                ModularCreateMap create_map[],
                GuardTransMap guard_map[],
                AtomicMapMap atom_map_map[],
                IOConverterMap ioconverter_map[])
        : _cond_map(cond_map)
        , _create_map(create_map)
        , _guard_trans_map(guard_map)
        , _atom_map_map(atom_map_map)
        , _ioconverter_map(ioconverter_map)
{}

// linear searches are fast enough -- its just for XML converstion...

CreateNodeFn FlowFunctionMaps::lookup_node_function(const char * n)
{
        CreateNodeFn res = NULL;
        ModularCreateMap * mcm = _create_map;
        while(mcm->scope) {
                int sz = strlen(mcm->scope);
                if(strncmp(mcm->scope,n,sz) == 0) {
                        CreateMap * cm = mcm->map;
                        while(cm->name) {
                                if(strcmp(cm->name,n + sz) == 0) {
                                        res = cm->createfn;
                                        break;
                                }
                                cm++;
                        }
                        if(res) {
                                break;
                        }
                }
                mcm++;
        }
        return res;
}

ConditionFn FlowFunctionMaps::lookup_conditional(const char * n, int argno, int unionnumber)
{
        ConditionFn res = NULL;
        ConditionalMap * cm = _cond_map;
        while(cm->name) {
                if(strcmp(cm->name, n) == 0
                        && argno == cm->argno
                        && unionnumber == cm->unionnumber) {
                        res = cm->condfn;
                        break;
                }
                cm++;
        }
        return res;
}

GuardTransFn FlowFunctionMaps::lookup_guard_translator(const char * guardname,
                                int union_number, std::vector<int> & argnos)
{
        GuardTransFn res = NULL;
        GuardTransMap * ptr = _guard_trans_map;
        while(ptr->guardname != NULL) {
                if(strcmp(guardname, ptr->guardname) == 0
                                && union_number == ptr->union_number) {
                        bool allargs = true;
                        int i = 0;
                        for(i = 0; i < (int)argnos.size() && allargs; i++) {
                                if(ptr->argnos[i] == 0) {
                                        break;
                                }
                                allargs = (ptr->argnos[i] == argnos[i]);
                        }
                        allargs = allargs && ((int)argnos.size() == i);
                        if(allargs) {
                                res = ptr->guardtransfn;
                                break;
                        }
                }
                ptr++;
        }
        return res;
}

AtomicMapAbstract * FlowFunctionMaps::lookup_atomic_map(const char * guardname)
{
        AtomicMapAbstract * res = NULL;
        AtomicMapMap * ptr = _atom_map_map;
        while(ptr->guardname != NULL) {
                if(strcmp(guardname, ptr->guardname) == 0) {
                        res = ptr->amap;
                        break;
                }
                ptr++;
        }
        return res;
}

FlatIOConversionFun FlowFunctionMaps::lookup_io_conversion(int from_unionnumber, int to_unionnumber)
{
        FlatIOConversionFun res = NULL;
        IOConverterMap * ptr = _ioconverter_map;
        while(ptr->from > 0) {
                if(ptr->from == from_unionnumber && ptr->to == to_unionnumber) {
                        res = ptr->conversion_fun;
                        break;
                }
                ptr++;
        }
        return res;
}

FlowCondition::FlowCondition(ConditionFn condfn, bool is_negated)
        : _condfn(condfn)
        , _is_negated(is_negated)
        {}

template<class O>
void delete_vector_contents(std::vector<O *> & vo)
{
        for(int i = 0; i < (int) vo.size(); i++) {
                delete vo[i];
        }
}

template<typename K, typename O>
void delete_map_contents(std::map<K,O *> & mo)
{
        typename std::map<K,O *>::iterator mitr;
        mitr = mo.begin();
        while(mitr != mo.end()) {
                delete (*mitr).second;
                mitr++;
        }
}

std::string create_indention(int depth)
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

FlowCase::FlowCase(FlowNode *targetnode, FlowIOConverter *converter)
        : _targetnode(targetnode)
        , _io_converter(converter ? converter : &FlowIOConverter::standard_converter)
{
}

FlowIOConverter FlowIOConverter::standard_converter(create_trivial_io_conversion<const void>);

FlowCase::~FlowCase() 
{ 
        delete_vector_contents<FlowCondition>(_conditions); 
        if(_io_converter != NULL && _io_converter != &FlowIOConverter::standard_converter) {
                delete _io_converter;
        }
}

void FlowCase::add(FlowCondition *fc) 
{ 
        _conditions.push_back(fc); 
}

void FlowCase::pretty_print(int depth)
{
        _targetnode->pretty_print(depth+1);
}

FlowSuccessor::FlowSuccessor(const char * name)
        : _name(name)
{
}

FlowSuccessor::~FlowSuccessor() 
{ 
        delete_vector_contents<FlowCase>(_cases); 
}

void FlowSuccessor::add(FlowCase * fc) 
{ 
        _cases.push_back(fc); 
}

void FlowSuccessor::pretty_print(int depth)
{
        // do not print successor for now
        //oflux_log_info("%s%s\n", create_indention(depth).c_str(), _name.c_str());
        for(int i=0; i < (int) _cases.size(); i++) {
                _cases[i]->pretty_print(depth+1);
        }
}

FlowSuccessorList::FlowSuccessorList()
{
}

FlowSuccessorList::~FlowSuccessorList() 
{ 
        delete_map_contents<std::string, FlowSuccessor>(_successorlist); 
}

void FlowSuccessorList::add(FlowSuccessor * fs) 
{ 
        std::pair<std::string,FlowSuccessor *> pr(fs->getName(),fs);
        _successorlist.insert(pr); 
}

void FlowSuccessorList::pretty_print(int depth)
{
        SuccessorList::iterator itr = _successorlist.begin();
        while(itr != _successorlist.end()) { 
                itr->second->pretty_print(depth+1);
                ++itr;
        }
}

FlowNode::FlowNode(const char * name,
                CreateNodeFn createfn,
                bool is_error_handler,
                bool is_source,
                bool is_detached,
                int input_unionnumber,
                int output_unionnumber)
        : _instances(0)
        , _executions(0)
        , _name(name)
        , _createfn(createfn)
        , _is_error_handler(is_error_handler)
        , _is_source(is_source)
        , _is_detached(is_detached)
        , _this_case(this,NULL)
        , _input_unionnumber(input_unionnumber)
        , _output_unionnumber(output_unionnumber)
{ 
}

FlowNode::~FlowNode()
{
        delete_vector_contents<FlowGuardReference>(_guard_refs);
}

void FlowNode::setErrorHandler(FlowNode *fn)
{
        _error_handler_case.setTargetNode(fn);
}

void FlowNode::log_snapshot()
{
#ifdef PROFILING
        oflux_log_info("%s (%c%c%c) %d instances %d executions (time real:avg %lf max %lf oflux:avg %lf max %lf)\n", 
                _name.c_str(),
                (_is_source ? 's' : '-'),
                (_is_detached ? 'd' : '-'),
                (_is_error_handler ? 'e' : '-'),
                _instances,
                _executions,
                _real_timer_stats.avg_usec(),
                _real_timer_stats.max_usec(),
                _oflux_timer_stats.avg_usec(),
                _oflux_timer_stats.max_usec());        
#else
        oflux_log_info("%s (%c%c%c) %d instances %d executions\n",
                _name.c_str(),
                (_is_source ? 's' : '-'),
                (_is_detached ? 'd' : '-'),
                (_is_error_handler ? 'e' : '-'),
                _instances,
                _executions);
#endif
}

void FlowNode::pretty_print(int depth)
{
        oflux_log_info("%s%s\n", create_indention(depth).c_str(), _name.c_str());
        // if a source node is some node's successor(depth>0 && is_source), do not re-print its sucessor list
        if((depth == 0 ) || (!_is_source)) {
                _successor_list.pretty_print(depth+1);
        } 
}

Flow::~Flow() 
{ 
        delete_map_contents<std::string,FlowNode>(_nodes); 
        delete_map_contents<std::string,FlowGuard>(_guards); 
        delete_vector_contents<Library>(_libraries);
}

void Flow::log_snapshot()
{
        std::map<std::string, FlowNode *>::iterator mitr = _nodes.begin();
        while(mitr != _nodes.end()) {
                (*mitr).second->log_snapshot();
                mitr++;
        }
}

void Flow::pretty_print()
{
        for(int i=0; i < (int) _sources.size(); i++) {
                _sources[i]->pretty_print(0);
        }
        /*
        std::map<std::string, FlowNode *>::iterator mitr = _nodes.begin();
        while(mitr != _nodes.end()) {
                (*mitr).second->pretty_print(0);
                mitr++;
        }
        */
}

std::vector<FlowNode *> & Flow::sources()
{
        return _sources;
}

}; // namespace
