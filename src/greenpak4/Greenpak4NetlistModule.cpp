/***********************************************************************************************************************
 * Copyright (C) 2016 Andrew Zonenberg and contributors                                                                *
 *                                                                                                                     *
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General   *
 * Public License as published by the Free Software Foundation; either version 2.1 of the License, or (at your option) *
 * any later version.                                                                                                  *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for     *
 * more details.                                                                                                       *
 *                                                                                                                     *
 * You should have received a copy of the GNU Lesser General Public License along with this program; if not, you may   *
 * find one here:                                                                                                      *
 * https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt                                                              *
 * or you may search the http://www.gnu.org website for the version 2.1 license, or you may write to the Free Software *
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA                                      *
 **********************************************************************************************************************/
 
#include "Greenpak4.h"
#include <stdio.h>
#include <stdlib.h>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

Greenpak4NetlistModule::Greenpak4NetlistModule(Greenpak4Netlist* parent, std::string name, json_object* object)
	: m_parent(parent)
{
	printf("Module %s...\n", name.c_str());
	
	json_object_iterator end = json_object_iter_end(object);
	for(json_object_iterator it = json_object_iter_begin(object);
		!json_object_iter_equal(&it, &end);
		json_object_iter_next(&it))
	{
		//See what we got
		string name = json_object_iter_peek_name(&it);
		json_object* child = json_object_iter_peek_value(&it);
		
		//Whatever it is, it should be an object
		if(!json_object_is_type(child, json_type_object))
		{
			fprintf(stderr, "ERROR: module child should be of type object but isn't\n");
			exit(-1);
		}
		
		//Go over the children's children and process it
		json_object_iterator end = json_object_iter_end(child);
		for(json_object_iterator it2 = json_object_iter_begin(child);
			!json_object_iter_equal(&it2, &end);
			json_object_iter_next(&it2))
		{
			//See what we got
			string cname = json_object_iter_peek_name(&it2);
			json_object* cobject = json_object_iter_peek_value(&it2);
			
			//Whatever it is, it should be an object
			if(!json_object_is_type(cobject, json_type_object))
			{
				fprintf(stderr, "ERROR: module child should be of type object but isn't\n");
				exit(-1);
			}
			
			//Load ports
			if(name == "ports")
			{
				//Make sure it doesn't exist
				if(m_ports.find(cname) != m_ports.end())
				{
					fprintf(stderr, "ERROR: Attempted redeclaration of module port \"%s\"\n", name.c_str());
					exit(-1);
				}
				
				//Create the port
				Greenpak4NetlistPort* port = new Greenpak4NetlistPort(this, cname, cobject);
				m_ports[cname] = port;
			}
			
			//Load cells
			else if(name == "cells")
				LoadCell(cname, cobject);
			
			//Load net names
			else if(name == "netnames")
				LoadNetName(cname, cobject);
			
			//Whatever it is, we don't want it
			else
			{
				fprintf(stderr, "ERROR: Unknown top-level JSON object \"%s\"\n", name.c_str());
				exit(-1);
			}
		}
	}
}

Greenpak4NetlistModule::~Greenpak4NetlistModule()
{
	//Clean up in reverse order
	
	//ports don't depend on anything but nodes
	for(auto x : m_ports)
		delete x.second;
	m_ports.clear();
	
	//then nodes at end
	for(auto x : m_nodes)
		delete x.second;
	m_nodes.clear();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Loading

Greenpak4NetlistNode* Greenpak4NetlistModule::GetNode(int32_t netnum)
{
	//See if we already have a node with this number.
	//If not, create it
	if(m_nodes.find(netnum) == m_nodes.end())
		m_nodes[netnum] = new Greenpak4NetlistNode;
		
	return m_nodes[netnum];
}

void Greenpak4NetlistModule::LoadCell(std::string name, json_object* object)
{
	printf("    Cell %s\n", name.c_str());
}

void Greenpak4NetlistModule::LoadNetName(std::string name, json_object* object)
{
	printf("    Net name %s\n", name.c_str());
}
