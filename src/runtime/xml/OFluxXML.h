#ifndef _OFLUX_XML
#define _OFLUX_XML

#include <string>

/**
 * @file OFluxXML.h
 * @author Mark Pichora
 * This is an XML reader class that understands XML files produced by oflux
 */

namespace oflux {
 class PluginSourceAbstract;
namespace flow {
 class FunctionMapsAbstract;
 class Flow;
 class Library;
} // namespace flow
namespace xml {

/**
 * @class ReaderException
 * @brief exception class for oops-es in parsing etc
 */
class ReaderException : public std::exception {
public:
        ReaderException(const char * mesg)
        		: _mesg(mesg)
        {}

        virtual ~ReaderException() throw ()
        {}

        virtual const char* what () const throw () {
            return _mesg.c_str();
        }
private:
        std::string _mesg;
};

flow::Library * flow__Library_factory(const char * dir, const char * name);

/**
 * @brief read a flow from an XML file
 * @param filename of the xml file
 * @param fmaps the compiled-in symbol mappings
 * @param directory of XML plugin files
 * @param directory of the .so plugin files
 * @param plugin parameters data object
 * @param existing_flow that was loaded previously
 * @param atomics_style indicates the rev. of the guard structures used by
 *        this runtime
 * @returns a flow object
 **/
flow::Flow *
read(     const char * filename
	, flow::FunctionMapsAbstract *fmaps
	, PluginSourceAbstract * pluginxmldir
	, const char * pluginlibdir
	, void * initpluginparams
	, const flow::Flow * existing_flow
	, int atomics_style
        );

} // namespace xml
} // namespace oflux



#endif // _OFLUX_XML
