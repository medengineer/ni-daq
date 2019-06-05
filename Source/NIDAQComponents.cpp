/*
------------------------------------------------------------------

This file is part of the Open Ephys GUI
Copyright (C) 2019 Allen Institute for Brain Science and Open Ephys

------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <chrono>
#include <math.h>

#include "NIDAQComponents.h"

/** NIDAQmx static function necessary for syncing analog + digital inputs */
static int32 GetTerminalNameWithDevPrefix(NIDAQ::TaskHandle taskHandle, const char terminalName[], char triggerName[]);

static int32 GetTerminalNameWithDevPrefix(NIDAQ::TaskHandle taskHandle, const char terminalName[], char triggerName[])
{

	NIDAQ::int32	error = 0;
	char			device[256];
	NIDAQ::int32	productCategory;
	NIDAQ::uInt32	numDevices, i = 1;

	DAQmxErrChk(NIDAQ::DAQmxGetTaskNumDevices(taskHandle, &numDevices));
	while (i <= numDevices) {
		DAQmxErrChk(NIDAQ::DAQmxGetNthTaskDevice(taskHandle, i++, device, 256));
		DAQmxErrChk(NIDAQ::DAQmxGetDevProductCategory(device, &productCategory));
		if (productCategory != DAQmx_Val_CSeriesModule && productCategory != DAQmx_Val_SCXIModule) {
			*triggerName++ = '/';
			strcat(strcat(strcpy(triggerName, device), "/"), terminalName);
			break;
		}
	}

Error:
	return error;
}

NIDAQComponent::NIDAQComponent() : serial_number(0) {}
NIDAQComponent::~NIDAQComponent() {}

void NIDAQAPI::getInfo()
{
	//TODO
}

NIDAQmxDeviceManager::NIDAQmxDeviceManager() {}

NIDAQmxDeviceManager::~NIDAQmxDeviceManager() {}

void NIDAQmxDeviceManager::scanForDevices()
{

	char data[2048] = { 0 };
	NIDAQ::DAQmxGetSysDevNames(data, sizeof(data));

	StringArray deviceList; 
	deviceList.addTokens(&data[0], ", ", "\"");

	StringArray deviceNames;
	StringArray productList;

	for (int i = 0; i < deviceList.size(); i++)
		if (deviceList[i].length() > 0)
			devices.add(deviceList[i].toUTF8());

}

String NIDAQmxDeviceManager::getDeviceFromIndex(int index)
{
	return devices[index];
}

String NIDAQmxDeviceManager::getDeviceFromProductName(String productName)
{
	for (auto device : devices)
	{
		ScopedPointer<NIDAQmx> n = new NIDAQmx(STR2CHR(device));
		if (n->getProductName() == productName)
			return device;
	}
}

int NIDAQmxDeviceManager::getNumAvailableDevices()
{
	return devices.size();
}

NIDAQmx::NIDAQmx() : Thread("NIDAQmx_Thread") {};

NIDAQmx::NIDAQmx(const char* deviceName) 
	: Thread("NIDAQmx_Thread"),
	deviceName(deviceName)
{

	adcResolution = 14; //bits

	connect();

	isUSBDevice = false;
	if (productName.contains("USB"))
		isUSBDevice = true;

	sampleRates.add(1000.0f);
	sampleRates.add(1250.0f);
	sampleRates.add(1500.0f);
	sampleRates.add(2000.0f);
	sampleRates.add(2500.0f);

	if (!isUSBDevice)
	{
		sampleRates.add(3000.0f);
		sampleRates.add(3330.0f);
		sampleRates.add(4000.0f);
		sampleRates.add(5000.0f);
		sampleRates.add(6250.0f);
		sampleRates.add(8000.0f);
		sampleRates.add(10000.0f);
		sampleRates.add(12500.0f);
		sampleRates.add(15000.0f);
		sampleRates.add(20000.0f);
		sampleRates.add(25000.0f);
		sampleRates.add(30000.0f);
	}

	// Default to highest sample rate
	samplerate = sampleRates[sampleRates.size() - 1];

	// Default to largest voltage range
	voltageRange = aiVRanges[aiVRanges.size() - 1];

	// Disable all channels by default
	for (int i = 0; i < aiChannelEnabled.size(); i++)
		aiChannelEnabled.set(i, false);

	for (int i = 0; i < diChannelEnabled.size(); i++)
		diChannelEnabled.set(i, false);

}

NIDAQmx::~NIDAQmx() {
}

String NIDAQmx::getProductName()
{
	return productName;
}

void NIDAQmx::connect()
{

	/* Get product name */
	char data[2048] = { 0 };
	NIDAQ::DAQmxGetDevProductType(STR2CHR(deviceName), &data[0], sizeof(data));
	productName = String(&data[0]);
	printf("Product Name: %s\n", productName);

	/* Get category type */
	NIDAQ::DAQmxGetDevProductCategory(STR2CHR(deviceName), &deviceCategory);
	printf("Device Category: %i\n", deviceCategory);

	/* Get simultaneous sampling supported */
	NIDAQ::bool32 supported = false;
	NIDAQ::DAQmxGetDevAISimultaneousSamplingSupported(STR2CHR(deviceName), &supported);
	simAISamplingSupported = (supported == 1);
	printf("Simultaneous sampling %ssupported\n", simAISamplingSupported ? "" : "NOT ");

	fflush(stdout);

	getAIChannels();
	getAIVoltageRanges();
	getDIChannels();

}

void NIDAQmx::getAIChannels()
{

	char data[2048];
	NIDAQ::DAQmxGetDevAIPhysicalChans(STR2CHR(deviceName), &data[0], sizeof(data));

	StringArray channel_list;
	channel_list.addTokens(&data[0], ", ", "\"");

	std::cout << "Found analog inputs:" << std::endl;
	for (int i = 0; i < channel_list.size(); i++)
	{
		if (channel_list[i].length() > 0)
		{
			printf("%s\n", channel_list[i].toUTF8());
			ai.add(AnalogIn(channel_list[i].toUTF8()));
			aiChannelEnabled.add(true);
		}
	}

	fflush(stdout);

}

void NIDAQmx::getAIVoltageRanges()
{

	NIDAQ::float64 data[512];
	NIDAQ::DAQmxGetDevAIVoltageRngs(STR2CHR(deviceName), &data[0], sizeof(data));

	printf("Detected voltage ranges:\n");
	for (int i = 0; i < 512; i += 2)
	{
		NIDAQ::float64 vmin = data[i];
		NIDAQ::float64 vmax = data[i + 1];
		if (int(vmin) == int(vmax)) 
			break;
		printf("Vmin: %f Vmax: %f \n", vmin, vmax);
		aiVRanges.add(VRange(vmin, vmax));
	}

	fflush(stdout);

}

void NIDAQmx::getDIChannels()
{

	char data[2048];
	//NIDAQ::DAQmxGetDevTerminals(STR2CHR(deviceName), &data[0], sizeof(data)); //gets all terminals
	//NIDAQ::DAQmxGetDevDIPorts(STR2CHR(deviceName), &data[0], sizeof(data));	//gets line name
	NIDAQ::DAQmxGetDevDILines(STR2CHR(deviceName), &data[0], sizeof(data));	//gets ports on line
	printf("Found digital inputs: \n");

	StringArray channel_list;
	channel_list.addTokens(&data[0], ", ", "\"");

	for (int i = 0; i < channel_list.size(); i++)
	{
		StringArray channel_type;
		channel_type.addTokens(channel_list[i], "/", "\"");
		if (channel_list[i].length() > 0)
		{
			printf("%s\n", channel_list[i].toUTF8());
			di.add(DigitalIn(channel_list[i].toUTF8()));
		}
	}

	fflush(stdout);

}

void NIDAQmx::run()
{
	/* Derived from NIDAQmx: ANSI C Example program: ContAI-ReadDigChan.c */

	NIDAQ::int32	error = 0;
	char			errBuff[ERR_BUFF_SIZE] = { '\0' };

	/**************************************/
	/********CONFIG ANALOG CHANNELS********/
	/**************************************/

	NIDAQ::int32		ai_read = 0;
	static int			totalAIRead = 0;
	NIDAQ::TaskHandle	taskHandleAI = 0;

	/* Create an analog input task */
	if (isUSBDevice)
		DAQmxErrChk(NIDAQ::DAQmxCreateTask("AITask_USB", &taskHandleAI));
	else
		DAQmxErrChk(NIDAQ::DAQmxCreateTask("AITask_PXI", &taskHandleAI));

	/* Create a voltage channel for each analog input */
	for (int i = 0; i < ai.size(); i++)
		DAQmxErrChk (NIDAQ::DAQmxCreateAIVoltageChan(
		taskHandleAI,					//task handle
			STR2CHR(ai[i].id),			//NIDAQ physical channel name (e.g. dev1/ai1)
			"",							//user-defined channel name (optional)
			DAQmx_Val_Cfg_Default,		//input terminal configuration
			voltageRange.vmin,			//min input voltage
			voltageRange.vmax,			//max input voltage
			DAQmx_Val_Volts,			//voltage units
			NULL));

	/* Configure sample clock timing */
	DAQmxErrChk(NIDAQ::DAQmxCfgSampClkTiming(
		taskHandleAI,
		"",													//source : NULL means use internal clock
		samplerate,											//rate : samples per second per channel
		DAQmx_Val_Rising,									//activeEdge : (DAQmc_Val_Rising || DAQmx_Val_Falling)
		DAQmx_Val_ContSamps,								//sampleMode : (DAQmx_Val_FiniteSamps || DAQmx_Val_ContSamps || DAQmx_Val_HWTimedSinglePoint)
		MAX_ANALOG_CHANNELS * CHANNEL_BUFFER_SIZE));		//sampsPerChanToAcquire : 
																//If sampleMode == DAQmx_Val_FiniteSamps : # of samples to acquire for each channel
																//Elif sampleMode == DAQmx_Val_ContSamps : circular buffer size


	/* Get handle to analog trigger to sync with digital inputs */
	char trigName[256];
	DAQmxErrChk(GetTerminalNameWithDevPrefix(taskHandleAI, "ai/SampleClock", trigName));

	/************************************/
	/********CONFIG DIGITAL LINES********/
	/************************************/

	NIDAQ::int32		di_read = 0;
	static int			totalDIRead = 0;
	NIDAQ::TaskHandle	taskHandleDI = 0;

	char lineName[2048];
	NIDAQ::DAQmxGetDevDIPorts(STR2CHR(deviceName), &lineName[0], sizeof(lineName));

	/* Create a digital input task */
	if (isUSBDevice)
		DAQmxErrChk(NIDAQ::DAQmxCreateTask("DITask_USB", &taskHandleDI));
	else
		DAQmxErrChk(NIDAQ::DAQmxCreateTask("DITask_PXI", &taskHandleDI));

	/* Create a channel for each digital input */
	DAQmxErrChk(NIDAQ::DAQmxCreateDIChan(
		taskHandleDI,
		lineName,
		"",
		DAQmx_Val_ChanForAllLines));

	
	if (!isUSBDevice) //USB devices do not have an internal clock and instead use CPU, so we can't configure the sample clock timing
		DAQmxErrChk(NIDAQ::DAQmxCfgSampClkTiming(
			taskHandleDI,							//task handle
			trigName,								//source : NULL means use internal clock, we will sync to analog input clock
			samplerate,								//rate : samples per second per channel
			DAQmx_Val_Rising,						//activeEdge : (DAQmc_Val_Rising || DAQmx_Val_Falling)
			DAQmx_Val_ContSamps,					//sampleMode : (DAQmx_Val_FiniteSamps || DAQmx_Val_ContSamps || DAQmx_Val_HWTimedSinglePoint)
			CHANNEL_BUFFER_SIZE));					//sampsPerChanToAcquire : want to sync with analog samples per channel
														//If sampleMode == DAQmx_Val_FiniteSamps : # of samples to acquire for each channel
														//Elif sampleMode == DAQmx_Val_ContSamps : circular buffer size


	DAQmxErrChk(NIDAQ::DAQmxStartTask(taskHandleDI));
	DAQmxErrChk(NIDAQ::DAQmxStartTask(taskHandleAI));

	NIDAQ::int32 numSampsPerChan;
	if (isUSBDevice)
		numSampsPerChan = 100;
	else
		numSampsPerChan = 1000;

	NIDAQ::int32 arraySizeInSamps = ai.size() * numSampsPerChan;
	NIDAQ::float64 timeout = 5.0;

	while (!threadShouldExit())
	{

		DAQmxErrChk(NIDAQ::DAQmxReadAnalogF64(
			taskHandleAI,
			numSampsPerChan,
			timeout,
			DAQmx_Val_GroupByScanNumber,
			ai_data,
			arraySizeInSamps,
			&ai_read,
			NULL));

		if (isUSBDevice)
			DAQmxErrChk(NIDAQ::DAQmxReadDigitalU32(
				taskHandleDI,
				numSampsPerChan,
				timeout,
				DAQmx_Val_GroupByScanNumber,
				di_data_32,
				numSampsPerChan,
				&di_read,
				NULL));
		else 
			DAQmxErrChk(NIDAQ::DAQmxReadDigitalU8(
				taskHandleDI,
				numSampsPerChan,
				timeout,
				DAQmx_Val_GroupByScanNumber,
				di_data_8,
				numSampsPerChan,
				&di_read,
				NULL));

		std::chrono::milliseconds last_time;
		std::chrono::milliseconds t = std::chrono::duration_cast< std::chrono::milliseconds >(
			std::chrono::system_clock::now().time_since_epoch());
		long long t_ms = t.count()*std::chrono::milliseconds::period::num / std::chrono::milliseconds::period::den;
		if (ai_read>0) {
			printf("Read @ %i | ", t_ms);
			printf("Acquired %d AI samples. Total %d | ", (int)ai_read, (int)(totalAIRead += ai_read));
			printf("Acquired %d DI samples. Total %d\n", (int)di_read, (int)(totalDIRead += di_read));
			fflush(stdout);
		}

		float aiSamples[MAX_ANALOG_CHANNELS] = { 0 };
		uint64 linesEnabled = 0;
		int count = 0;
		for (int i = 0; i < arraySizeInSamps; i++)
		{
			int channel = i % MAX_ANALOG_CHANNELS;

			// Turn input signals on/off on the fly
			if (aiChannelEnabled[channel])
				aiSamples[channel] = ai_data[i];

			if (diChannelEnabled[channel])
				linesEnabled = linesEnabled | (1 << channel);

			if (i % MAX_ANALOG_CHANNELS == 0)
			{
				ai_timestamp++;
				if (isUSBDevice)
					eventCode = di_data_32[count++] & linesEnabled;
				else
					eventCode = di_data_8[count++] & linesEnabled;
				aiBuffer->addToBuffer(aiSamples, &ai_timestamp, &eventCode, 1);
			}

		}

		fflush(stdout);

	}

	/*********************************************/
	// DAQmx Stop Code
	/*********************************************/

	NIDAQ::DAQmxStopTask(taskHandleAI);
	NIDAQ::DAQmxClearTask(taskHandleAI);
	NIDAQ::DAQmxStopTask(taskHandleDI);
	NIDAQ::DAQmxClearTask(taskHandleDI);

	return;

Error:

	if (DAQmxFailed(error))
		NIDAQ::DAQmxGetExtendedErrorInfo(errBuff, ERR_BUFF_SIZE);

	if (taskHandleAI != 0) {
		// DAQmx Stop Code
		NIDAQ::DAQmxStopTask(taskHandleAI);
		NIDAQ::DAQmxClearTask(taskHandleAI);
	}

	if (taskHandleDI != 0) {
		// DAQmx Stop Code
		NIDAQ::DAQmxStopTask(taskHandleDI);
		NIDAQ::DAQmxClearTask(taskHandleDI);
	}
	if (DAQmxFailed(error))
		printf("DAQmx Error: %s\n", errBuff);
		fflush(stdout);

	return;

}

InputChannel::InputChannel()
{

}

InputChannel::InputChannel(String id) : id(id), enabled(true)
{
}

InputChannel::~InputChannel()
{
}

void InputChannel::setEnabled(bool enable)
{
	enabled = enable;
}

AnalogIn::AnalogIn()
{
}

AnalogIn::AnalogIn(String id) : InputChannel(id)
{
	voltageRange = VRange(-5, 5);
}

void AnalogIn::setVoltageRange(VRange range)
{
	voltageRange = range;
}


AnalogIn::~AnalogIn()
{

}

DigitalIn::DigitalIn(String id) : InputChannel(id)
{

}

DigitalIn::DigitalIn()
{

}

DigitalIn::~DigitalIn()
{

}



