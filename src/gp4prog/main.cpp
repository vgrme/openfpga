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

#include <cstring>
#include <cmath>
#include <unistd.h>
#include <log.h>
#include <gpdevboard.h>

using namespace std;

void ShowUsage();
void ShowVersion();

const char *BitFunction(SilegoPart part, size_t bitno);

void WriteBitstream(string fname, vector<uint8_t> bitstream);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Entry point

int main(int argc, char* argv[])
{
	Severity console_verbosity = Severity::NOTICE;

	bool reset = false;
	bool test = false;
	unsigned rcOscFreq = 0;
	string downloadFilename, uploadFilename;
	bool programNvram = false;
	bool force = false;
	uint8_t patternId = 0;
	bool patternIdSpecified = false;
	bool readProtect = false;
	double voltage = 0.0;
	vector<int> nets;

	//Parse command-line arguments
	for(int i=1; i<argc; i++)
	{
		string s(argv[i]);

		//Let the logger eat its args first
		if(ParseLoggerArguments(i, argc, argv, console_verbosity))
			continue;

		else if(s == "--help")
		{
			ShowUsage();
			return 0;
		}
		else if(s == "--version")
		{
			ShowVersion();
			return 0;
		}
		else if(s == "-r" || s == "--reset")
		{
			reset = true;
		}
		else if(s == "-R" || s == "--read")
		{
			if(i+1 < argc)
			{
				uploadFilename = argv[++i];
			}
			else
			{
				printf("--read requires an argument\n");
				return 1;
			}
		}
		else if(s == "-t" || s == "--test-socket")
			test = true;
		else if(s == "-T" || s == "--trim")
		{
			if(i+1 < argc)
			{
				const char *value = argv[++i];
				if(!strcmp(value, "25k"))
					rcOscFreq = 25000;
				else if(!strcmp(value, "2M"))
					rcOscFreq = 2000000;
				else
				{
					printf("--trim argument must be 25k or 2M\n");
					return 1;
				}
			}
			else
			{
				printf("--trim requires an argument\n");
				return 1;
			}
		}
		else if(s == "-e" || s == "--emulate")
		{
			if(!downloadFilename.empty())
			{
				printf("only one --emulate or --program option can be specified\n");
				return 1;
			}
			if(i+1 < argc)
			{
				downloadFilename = argv[++i];
			}
			else
			{
				printf("--emulate requires an argument\n");
				return 1;
			}
		}
		else if(s == "--program")
		{
			if(!downloadFilename.empty())
			{
				printf("only one --emulate or --program option can be specified\n");
				return 1;
			}
			if(i+1 < argc)
			{
				downloadFilename = argv[++i];
				programNvram = true;
			}
			else
			{
				printf("--program requires an argument\n");
				return 1;
			}
		}
		else if(s == "--force")
			force = true;
		else if(s == "--pattern-id")
		{
			patternIdSpecified = true;

			if(i+1 < argc)
			{
				char *arg = argv[++i];
				long id = strtol(arg, &arg, 10);
				if(*arg == '\0' && id >= 0 && id <= 255)
					patternId = id;
				else
				{
					printf("--pattern-id argument must be a number between 0 and 255\n");
					return 1;
				}
			}
			else
			{
				printf("--pattern-id requires an argument\n");
				return 1;
			}
		}
		else if(s == "--read-protect")
			readProtect = true;
		else if(s == "-v" || s == "--voltage")
		{
			if(i+1 < argc)
			{
				char *endptr;
				voltage = strtod(argv[++i], &endptr);
				if(*endptr)
				{
					printf("--voltage must be a decimal value\n");
					return 1;
				}
				if(!(voltage == 0.0 || (voltage >= 1.71 && voltage <= 5.5)))
				{
					printf("--voltage %.3g outside of valid range\n", voltage);
					return 1;
				}
			}
			else
			{
				printf("--voltage requires an argument\n");
				return 1;
			}
		}
		else if(s == "-n" || s == "--nets")
		{
			if(i+1 < argc)
			{
				char *arg = argv[++i];
				do
				{
					long net = strtol(arg, &arg, 10);
					if(*arg && *arg != ',')
					{
						printf("--nets must be a comma-separate list of net numbers\n");
						return 1;
					}
					if(net < 1 || net > 20 || net == 11)
					{
						printf("--nets used with an invalid net %ld\n", net);
						return 1;
					}
					nets.push_back(net);
				} while(*arg++);
			}
			else
			{
				printf("--nets requires an argument\n");
				return 1;
			}
		}

		//assume it's the bitstream file if it's the first non-switch argument
		else if( (s[0] != '-') && (downloadFilename == "") )
		{
			downloadFilename = s;
		}

		else
		{
			printf("Unrecognized command-line argument \"%s\", use --help\n", s.c_str());
			return 1;
		}
	}

	//Set up logging
	g_log_sinks.emplace(g_log_sinks.begin(), new STDLogSink(console_verbosity));

	//Print header
	if(console_verbosity >= Severity::NOTICE)
		ShowVersion();

	//Open the dev board
	hdevice hdev = OpenBoard();
	if(!hdev)
		return 1;

	//If we're run with no bitstream and no reset flag, stop now without changing board configuration
	if(downloadFilename.empty() && uploadFilename.empty() && voltage == 0.0 && nets.empty() &&
	   rcOscFreq == 0 && !test && !reset)
	{
		LogNotice("No actions requested, exiting\n");
		return 0;
	}

	//Light up the status LED
	if(!SetStatusLED(hdev, 1))
		return 1;

	//See if any of the options require knowing what part we use.
	SilegoPart detectedPart = SilegoPart::UNRECOGNIZED;
	vector<uint8_t> programmedBitstream;
	BitstreamKind bitstreamKind;
	if(!(uploadFilename.empty() && downloadFilename.empty() && rcOscFreq == 0 && !test && !programNvram))
	{
		if(!DetectPart(hdev, detectedPart, programmedBitstream, bitstreamKind))
		{
			SetStatusLED(hdev, 0);
			return 1;
		}
	}

	if(programNvram && bitstreamKind != BitstreamKind::EMPTY)
	{
		if(!force)
		{
			LogError("Non-empty part detected; refusing to program without --force\n");
			SetStatusLED(hdev, 0);
			return 1;
		}
		else
		{
			LogNotice("Non-empty part detected and --force is specified; proceeding\n");
		}
	}

	//We already have the programmed bitstream, so simply write it to a file
	if(!uploadFilename.empty())
	{
		LogNotice("Writing programmed bitstream to %s\n", uploadFilename.c_str());
		WriteBitstream(uploadFilename, programmedBitstream);
	}

	//Do a socket test before doing anything else, to catch failures early
	if(test)
	{
		if(!SocketTest(hdev, detectedPart))
		{
			LogError("Socket test has failed\n");
			SetStatusLED(hdev, 0);
			return 1;
		}
		else
		{
			LogNotice("Socket test has passed\n");
		}
	}

	//If we're resetting, do that
	if(reset)
	{
		LogNotice("Resetting board I/O and signal generators\n");
		if(!Reset(hdev))
			return 1;
	}

	//If we need to trim oscillator, do that before programming
	uint8_t rcFtw = 0;
	if(rcOscFreq != 0)
	{
		if(voltage == 0.0)
		{
			LogError("Trimming oscillator requires specifying target voltage\n");
			return 1;
		}

		LogNotice("Trimming oscillator for %d Hz at %.3g V\n", rcOscFreq, voltage);
		LogIndenter li;
		if(!TrimOscillator(hdev, detectedPart, voltage, rcOscFreq, rcFtw))
			return 1;
	}

	//If we're programming, do that first
	if(!downloadFilename.empty())
	{
		vector<uint8_t> newBitstream = ReadBitstream(downloadFilename);
		if(newBitstream.empty())
			return 1;
		if(newBitstream.size() != BitstreamLength(detectedPart) / 8)
		{
			LogError("Provided bitstream has incorrect length for selected part\n");
			SetStatusLED(hdev, 0);
			return 1;
		}

		//TODO: Make this work for chips other than SLG46620V?

		//Set trim value reg<1981:1975>
		newBitstream[246] |= rcFtw << 7;
		newBitstream[247] |= rcFtw >> 1;

		//Set pattern ID reg<2031:2038>
		if(patternIdSpecified)
		{
			newBitstream[253] |= patternId << 7;
			newBitstream[254] |= patternId >> 1;
		}

		//Read out the pattern ID and print it
		unsigned int patternID =
			( (newBitstream[254] << 1) & 0xff ) |
			( (newBitstream[253] >> 7) & 0xff );
		LogNotice("Bitstream ID code: 0x%02x\n", patternID);

		//Set read protection reg<2039>
		//OR with the existing value: we can set the read protect bit here, but not overwrite the bit if
		//it was set by gp4par. If you REALLY need to unprotect a bitstream, do it by hand in a text editor.
		newBitstream[254] |= ((uint8_t)readProtect) << 7;
		if(newBitstream[254] & 0x80)
			LogNotice("Read protection: enabled\n");
		else
			LogNotice("Read protection: disabled\n");

		if(!programNvram)
		{
			//Load bitstream into SRAM
			LogNotice("Downloading bitstream into SRAM\n");
			LogIndenter li;
			if(!DownloadBitstream(hdev, newBitstream, DownloadMode::EMULATION))
				return 1;
		}
		else
		{
			//Program bitstream into NVM
			LogNotice("Programming bitstream into NVM\n");
			LogIndenter li;
			if(!DownloadBitstream(hdev, newBitstream, DownloadMode::PROGRAMMING))
				return 1;

			//TODO: Figure out how to make this play nicely with read protection?
			LogNotice("Verifying programmed bitstream\n");
			size_t bitstreamLength = BitstreamLength(detectedPart) / 8;
			vector<uint8_t> bitstreamToVerify;
			if(!UploadBitstream(hdev, bitstreamLength, bitstreamToVerify))
				return 1;
			bool failed = false;
			for(size_t i = 0; i < bitstreamLength * 8; i++)
			{
				bool expectedBit = ((newBitstream     [i/8] >> (i%8)) & 1) == 1;
				bool actualBit   = ((bitstreamToVerify[i/8] >> (i%8)) & 1) == 1;
				if(expectedBit != actualBit)
				{
					LogNotice("Bit %4zd differs: expected %d, actual %d",
					          i, (int)expectedBit, (int)actualBit);
					failed = true;

					//Explain what undocumented bits do; most of these are also trimming values, and so
					//it is normal for them to vary even if flashing the exact same bitstream many times.
					const char *bitFunction = BitFunction(detectedPart, i);
					if(bitFunction)
						LogNotice(" (bit meaning: %s)\n", bitFunction);
					else
						LogNotice("\n");
				}
			}

			if(failed)
				LogError("Verification failed\n");
			else
				LogNotice("Verification passed\n");
		}

		//Developer board I/O pins become stuck after both SRAM and NVM programming;
		//resetting them explicitly makes LEDs and outputs work again.
		LogDebug("Unstucking I/O pins after programming\n");
		IOConfig ioConfig;
		for(size_t i = 2; i <= 20; i++)
			ioConfig.driverConfigs[i] = TP_RESET;
		if(!SetIOConfig(hdev, ioConfig))
			return 1;
	}

	if(voltage != 0.0)
	{
		//Configure the signal generator for Vdd
		LogNotice("Setting Vdd to %.3g V\n", voltage);
		if(!ConfigureSiggen(hdev, 1, voltage))
			return 1;
	}

	if(!nets.empty())
	{
		//Set the I/O configuration on the test points
		LogNotice("Setting I/O configuration\n");

		IOConfig config;
		for(int net : nets)
		{
			config.driverConfigs[net] = TP_FLOAT;
			config.ledEnabled[net] = true;
			config.expansionEnabled[net] = true;
		}
		if(!SetIOConfig(hdev, config))
			return 1;
	}

	//Check that we didn't break anything
	if(!CheckStatus(hdev))
	{
		LogError("Fault condition detected during final check, exiting\n");
		SetStatusLED(hdev, 0);
		return 1;
	}

	//Done
	LogNotice("Done\n");
	SetStatusLED(hdev, 0);

	USBCleanup(hdev);
	return 0;
}

void ShowUsage()
{
	printf(//                                                                               v 80th column
		"Usage: gp4prog bitstream.txt\n"
		"    When run with no arguments, scans for the board but makes no config changes.\n"
		"    -q, --quiet\n"
		"        Causes only warnings and errors to be written to the console.\n"
		"        Specify twice to also silence warnings.\n"
		"    --verbose\n"
		"        Prints additional information about the design.\n"
		"    --debug\n"
		"        Prints lots of internal debugging information.\n"
		"    --force\n"
		"        Perform actions that may be potentially inadvisable.\n"
		"\n"
		"    The following options are instructions for the developer board. They are\n"
		"    executed in the order listed here, regardless of their order on command line.\n"
		"    -r, --reset\n"
		"        Resets the board:\n"
		"          * disables every LED;\n"
		"          * disables every expansion connector passthrough;\n"
		"          * disables Vdd supply.\n"
		"    -R, --read           <bitstream filename>\n"
		"        Uploads the bitstream stored in non-volatile memory.\n"
		"    -t, --test-socket\n"
		"        Verifies that every connection between socket and device is intact.\n"
		"    -T, --trim           [25k|2M]\n"
		"        Trims the RC oscillator to achieve the specified frequency.\n"
		"    -e, --emulate        <bitstream filename>\n"
		"        Downloads the specified bitstream into volatile memory.\n"
		"        Implies --reset --voltage 3.3.\n"
		"    --program            <bitstream filename>\n"
		"        Programs the specified bitstream into non-volatile memory.\n"
		"        THIS CAN BE DONE ONLY ONCE FOR EVERY INTEGRATED CIRCUIT.\n"
		"        Attempts to program non-empty parts will be rejected unless --force\n"
		"        is specified.\n"
		"    -v, --voltage        <voltage>\n"
		"        Adjusts Vdd to the specified value in volts (0V to 5.5V), ±70mV.\n"
		"    -n, --nets           <net list>\n"
		"        For every test point in the specified comma-separated list:\n"
		"          * enables a non-inverted LED, if any;\n"
		"          * enables expansion connector passthrough.\n");
}

void ShowVersion()
{
	printf(
		"GreenPAK 4 programmer by Andrew D. Zonenberg and whitequark.\n"
		"\n"
		"License: LGPL v2.1+\n"
		"This is free software: you are free to change and redistribute it.\n"
		"There is NO WARRANTY, to the extent permitted by law.\n");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Part database

const char *BitFunction(SilegoPart part, size_t bitno)
{
	//The conditionals in this function are structured to resemble the structure of the datasheet.
	//This is because the datasheet accurately *groups* reserved bits according to function;
	//they simply black out the parts that aren't meant to be public, but do not mash them together.

	const char *bitFunction = NULL;

	switch(part)
	{
		case SLG46620V:
			if(bitno >= 570 && bitno <= 575)
				bitFunction = NULL;
			else if(bitno == 833)
				bitFunction = "ACMP5 speed double";
			else if(bitno == 835)
				bitFunction = "ACMP4 speed double";
			else if(bitno == 881)
				bitFunction = NULL;
			else if(bitno >= 887 && bitno <= 891)
				bitFunction = "Vref value fine tune";
			else if(bitno == 922)
				bitFunction = "bandgap 1x buffer enable";
			else if(bitno == 937)
				bitFunction = "Vref op amp chopper frequency select";
			else if(bitno == 938)
				bitFunction = "bandgap op amp offset chopper enable";
			else if(bitno == 939)
				bitFunction = "Vref op amp offset chopper enable";
			else if((bitno >= 1003 && bitno <= 1015) ||
			        (bitno >= 1594 && bitno <= 1599))
				bitFunction = NULL;
			else if(bitno >= 1975 && bitno <= 1981)
				bitFunction = "RC oscillator trimming value";
			else if((bitno >= 1982 && bitno <= 1987) ||
			        (bitno >= 1988 && bitno <= 1995) ||
			        (bitno >= 1996 && bitno <= 2001) ||
			        (bitno >= 2002 && bitno <= 2007) ||
			        (bitno >= 2013 && bitno <= 2014) ||
			        (bitno >= 2021 && bitno <= 2027) ||
			        (bitno >= 2028 && bitno <= 2029) ||
			        bitno == 2030)
				bitFunction = NULL;
			else if(bitno >= 2031 && bitno <= 2038)
				bitFunction = "pattern ID";
			else if(bitno == 2039)
				bitFunction = "read protection";
			else
				bitFunction = "see datasheet";
			break;

		default: LogFatal("Unknown part\n");
	}

	if(bitFunction == NULL)
		bitFunction = "unknown--reserved";

	return bitFunction;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Bitstream input/output

void WriteBitstream(string fname, vector<uint8_t> bitstream)
{
	FILE* fp = fopen(fname.c_str(), "wt");
	if(!fp)
	{
		LogError("Couldn't open %s for reading\n", fname.c_str());
		return;
	}

	fputs("index\t\tvalue\t\tcomment\n", fp);
	for(size_t i = 0; i < bitstream.size() * 8; i++)
	{
		int value = (bitstream[i / 8] >> (i % 8)) & 1;
		fprintf(fp, "%d\t\t%d\t\t//\n", (int)i, value);
	}

	fclose(fp);
}
