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
#include "OFluxConfiguration.h"
#include "OFluxLogging.h"
#include <set>
#include <string>
#include <dirent.h>
#include <sys/types.h>


namespace oflux {

DirPluginSource::DirPluginSource(const char * pluginxmldir)
	: _hidden_vector(0)
	, _index(0)
{
	std::set<std::string>* vec_ptr = new std::set<std::string>();
	_hidden_vector = vec_ptr;
	DIR * dir = ::opendir(pluginxmldir);
	if(!dir) {
		oflux_log_warn("xml::Reader::readxmldir() directory %s does not exist or cannot be opened\n",pluginxmldir);
                return;
	}
	struct dirent * dir_entry;
	while((dir_entry = ::readdir(dir)) != NULL) {
		std::string filename = dir_entry->d_name;
                size_t found = filename.find_last_of(".");
                if(filename.substr(found+1) == "xml" ) {
                        filename = (std::string) pluginxmldir + "/" + filename;
                        vec_ptr->insert(filename);
                }
        }
	::closedir(dir);
}

DirPluginSource::~DirPluginSource()
{
	std::set<std::string>* vec_ptr = 
		reinterpret_cast<std::set<std::string>* >(_hidden_vector);
	delete vec_ptr;
}

const char *
DirPluginSource::nextXmlFile()
{
	std::set<std::string>* vec_ptr = 
		reinterpret_cast<std::set<std::string>* >(_hidden_vector);
	const char * res = 0;
	if(vec_ptr) {
		std::set<std::string>::const_iterator itr = vec_ptr->begin();
		int i = 0;
		while(i < _index && itr != vec_ptr->end()) {
			++itr;
			++i;
		}
		if(itr != vec_ptr->end()) {
			res = (*itr).c_str();
			++_index;
		}
	}
	return res;
}


SimpleCharArrayPluginSource::SimpleCharArrayPluginSource(const char * plugin_names[])
	: _pnames_orig(plugin_names)
	, _pnames(plugin_names)
{
}

const char *
SimpleCharArrayPluginSource::nextXmlFile()
{
	const char * res = *_pnames;
	if(res) {
		++_pnames;
	}
	return res;
}

} // namespace oflux
