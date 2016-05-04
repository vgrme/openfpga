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

#include "gp4par.h"

using namespace std;

/**
	@brief The main place-and-route logic
 */
bool DoPAR(Greenpak4Netlist* netlist, Greenpak4Device* device)
{
	labelmap lmap;
	
	//Create the graphs
	printf("\nCreating netlist graphs...\n");
	PARGraph* ngraph = NULL;
	PARGraph* dgraph = NULL;
	BuildGraphs(netlist, device, ngraph, dgraph, lmap);

	//Create and run the PAR engine
	Greenpak4PAREngine engine(ngraph, dgraph, lmap);
	if(!engine.PlaceAndRoute(lmap, true))
	{
		//Print the placement we have so far
		PrintPlacementReport(ngraph, device);
		
		printf("PAR failed\n");
		return false;
	}
		
	//Copy the netlist over
	unsigned int num_routes_used[2];
	CommitChanges(dgraph, device, num_routes_used);
	
	//Final DRC to make sure the placement is sane
	PostPARDRC(ngraph, device);
	
	//Print reports
	PrintUtilizationReport(ngraph, device, num_routes_used);
	PrintPlacementReport(ngraph, device);
	
	//Final cleanup
	delete ngraph;
	delete dgraph;
	return true;
}

/**
	@brief Do various sanity checks after the design is routed
 */
void PostPARDRC(PARGraph* netlist, Greenpak4Device* device)
{
	printf("\nPost-PAR design rule checks\n");
		
	//Check for nodes in the netlist that have no load
	for(uint32_t i=0; i<netlist->GetNumNodes(); i++)
	{
		auto node = netlist->GetNodeByIndex(i);
		auto src = static_cast<Greenpak4NetlistEntity*>(node->GetData());
		auto mate = node->GetMate();
		auto dst = static_cast<Greenpak4BitstreamEntity*>(mate->GetData());
		
		//Sanity check - must be fully PAR'd
		if(mate == NULL)
		{
			fprintf(
				stderr,
				"    ERROR: Node \"%s\" is not mapped to any site in the device\n",
				src->m_name.c_str());
			exit(-1);
		}
		
		//Do not warn if power rails have no load, that's perfectly normal
		if(dynamic_cast<Greenpak4PowerRail*>(dst) != NULL)
			continue;
			
		//If the node has no output ports, of course it won't have any loads
		if(dst->GetOutputPorts().size() == 0)
			continue;
			
		//If the node is an IOB configured as an output, there's no internal load for its output.
		//This is perfectly normal, obviously.
		Greenpak4NetlistCell* cell = dynamic_cast<Greenpak4NetlistCell*>(src);
		if( (cell != NULL) &&  ( (cell->m_type == "GP_IOBUF") || (cell->m_type == "GP_OBUF") ) )
			continue;
		
		//If we have no loads, warn
		if(node->GetEdgeCount() == 0)
		{
			printf(
				"    WARNING: Node \"%s\" has no load\n",
				src->m_name.c_str());
		}
		
	}
	
	//TODO: check floating inputs etc
	
	//Check invalid IOB configuration
	//TODO: driving an input-only pin etc - is this possible?
	for(auto it = device->iobbegin(); it != device->iobend(); it ++)
	{
		Greenpak4IOB* iob = it->second;
		
		auto signal = iob->GetOutputSignal();
		auto src = signal.GetRealEntity();
		
		//Check for analog output driving a pin not configured as analog for the input
		if( !iob->IsAnalogIbuf() &&
			(
				(dynamic_cast<Greenpak4VoltageReference*>(src) != NULL) ||
				(dynamic_cast<Greenpak4PGA*>(src) != NULL)
			)
		)
		{
			fprintf(stderr, "    ERROR: Pin %d is driven by an analog source (%s) but does not have IBUF_TYPE = ANALOG\n",
				it->first,
				signal.GetOutputName().c_str()
				);
			exit(-1);
		}
	}
	
	//Check for multiple ACMPs using different settings of ACMP0's output mux
	typedef pair<string, Greenpak4EntityOutput> spair;
	switch(device->GetPart())
	{
		case Greenpak4Device::GREENPAK4_SLG46620:
			{				
				auto pin6 = device->GetIOB(6)->GetOutput("");
				auto vdd = device->GetPower();
				auto gnd = device->GetGround();
				
				vector<spair> inputs;
				
				//Loop over each ACMP that could possibly use the ACMP0 (shared) mux
				for(unsigned int i=0; i<device->GetAcmpCount(); i++)
				{
					auto acmp = device->GetAcmp(i);
					auto input = acmp->GetInput();
					
					//If this comparator is not using one of ACMP0's inputs, we don't care
					//TODO: buffered pin 6 is a candidate too
					if((input != pin6) && (input != vdd) )
						continue;
						
					//Look up the instance name of the comparator. Sanity check that it's used.
					auto mate = acmp->GetPARNode()->GetMate();
					if(mate == NULL)
						continue;
					auto node = static_cast<Greenpak4NetlistEntity*>(mate->GetData());
					inputs.push_back(spair(node->m_name, input));
				}
					
				//Check the active inputs and make sure they're the same	
				Greenpak4EntityOutput shared_input = gnd;
				for(auto s : inputs)
				{
					//If the shared input isn't used, this is the new value for the mux
					if(shared_input == gnd)
						shared_input = s.second;
						
					//If the shared input is used, but has the same value, we're good - the sharing did its job
					if(shared_input == s.second)
						continue;

					//Problem! Incompatible mux settings
					fprintf(stderr,
						"    ERROR: Multiple comparators tried to simultaneously use different outputs from "
						"the ACMP0 input mux\n");
					for(auto p : inputs)
					{
						printf("        Comparator %10s requested %s\n",
							p.first.c_str(), p.second.GetOutputName().c_str());
					}
					exit(-1);
				}
				
				//If ACMP0 is not used, but we use its output, configure it
				//TODO: for better power efficiency, turn on only when a downstream comparator is on?
				if( (device->GetAcmp(0)->GetInput() == gnd) && (inputs.size() > 0) )
				{
					printf("    INFO: Enabling ACMP0 and configuring input mux, since output of mux is used "
						"but ACMP0 is not instantiated\n");
					
					auto acmp0 = device->GetAcmp(0);
					acmp0->SetInput(shared_input);
					acmp0->SetPowerEn(device->GetPowerOnReset()->GetOutput("RST_DONE"));
				}
			}
			break;
		
		default:
			break;
	}
	
	//Check for multiple oscillators with power-down enabled but not the same source
	Greenpak4LFOscillator* lfosc = device->GetLFOscillator();
	Greenpak4RingOscillator* rosc = device->GetRingOscillator();
	Greenpak4RCOscillator* rcosc = device->GetRCOscillator();
	vector<spair> powerdowns;
	if(lfosc->IsUsed() && lfosc->GetPowerDownEn() && !lfosc->IsConstantPowerDown())
		powerdowns.push_back(spair(lfosc->GetDescription(), lfosc->GetPowerDown()));
	if(rosc->IsUsed() && rosc->GetPowerDownEn() && !rosc->IsConstantPowerDown())
		powerdowns.push_back(spair(rosc->GetDescription(), rosc->GetPowerDown()));
	if(rcosc->IsUsed() && rcosc->GetPowerDownEn() && !rcosc->IsConstantPowerDown())
		powerdowns.push_back(spair(rcosc->GetDescription(), rcosc->GetPowerDown()));
	if(!powerdowns.empty())
	{
		Greenpak4EntityOutput src = device->GetGround();
		bool ok = true;
		for(auto p : powerdowns)
		{
			if(src.IsPowerRail())
				src = p.second;
			if(src != p.second)
				ok = false;
		}
		
		if(!ok)
		{
			fprintf(stderr,
				"    ERROR: Multiple oscillators have power-down enabled, but do not share the same power-down signal\n");
			for(auto p : powerdowns)
				printf("    Oscillator %10s powerdown is %s\n", p.first.c_str(), p.second.GetOutputName().c_str());
			exit(-1);
		}
	}
		
}

/**
	@brief Allocate and name a graph label
 */
uint32_t AllocateLabel(PARGraph*& ngraph, PARGraph*& dgraph, labelmap& lmap, std::string description)
{
	uint32_t nlabel = ngraph->AllocateLabel();
	uint32_t dlabel = dgraph->AllocateLabel();
	if(nlabel != dlabel)
	{
		fprintf(stderr, "INTERNAL ERROR: labels were allocated at the same time but don't match up\n");
		exit(-1);
	}
	
	lmap[nlabel] = description;
	
	return nlabel;
}
