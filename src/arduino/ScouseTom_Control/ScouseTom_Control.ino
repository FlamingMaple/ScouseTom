/*
Arduino Control Code for breadboard and PCB version - controlled by Matlab code (for now) ScouseTom_Init ScouseTom_Start etc.

Overview of commands is SOMEWHERE
########
To do:

Check that sources and sinks arent the same for a line in the protocol

Tidy up:
- everything under "do stuff" would be easier if it was replaced with inline functions - that way they could be jumped too in visual studio
- rearrange code into more logical tabs - the multifreq stuff within "CS_comm" for example
Debug mode - use #IF to set verbose debug mode

Pulse trains - so kirill can replicate thomas' experiments on nerve - repeated stim should demonstrate refactory period. Also could be used to replicate BOLD experimental paradigm - with

Lower power consumption - it is possible to set pmc_enable_sleep to save some power, or even put the SAM3X into "backup" mode, awakened by interupt on RX pin, but not sure how
also it is possible to turn off/lower clocks when not in use, and turn off adc's etc. my googling failed me. THis is unlikely to reduce the consumption by much however.
########

Jimmy wrote this so blame him

*/


//#####################################
// Libraries to include

#include <Wire.h> //i2c for digipot

//#include "BreadboardPins.h" // Pins for breadboard version - used by kirill and me (testing)
#include "PCBPins.h" // Pins for PCB version - these have been altered to more logical layout for PCB
#include "Stim.h" //Constants for stimulator things, with definitions of functions
#include "Switches.h" // Constants for switching channels
#include "CS_comm.h" // Constants used in serial communication with Current Source
#include "PC_comm.h" // Constants used in communicaltion with PC
#include "Pins.h" // constants used in indicator pins and reseting ALL pins to defaults etc.
#include "Errors.h" // error codes and warning messages
#include "Messages.h" // OK messages and other misc. things
#include "System_Control.h" //control constants - idle time definition etc.
#include "Injection.h" // injection defaults - max number of protocol lines etc.
#include "Compliance.h" // compliance check defaults and consts

/*############ CS Communications stuff - consts in CS_Comm.h ############*/

char CS_inputBuffer[CS_buffSize]; //char buffer to store output from current source
int CS_inputFinished = 0; //flag for complete response from current source
int LongDispWind = 0; // flag for if we have more than 3 digit length of repeats/freqs/prt so that we use the shorter text on the bottom window
char CS_outputBuffer[50]; // char array buffer for output strings to CS
int CS_commgoodness = 1; // flag for current communication goodness

/*############ PC Communications stuff - consts in PC_Comm.h ############*/

char PC_outputBuffer[50]; // char array buffer for output strings to PC
int PC_commgoodness = 1;
int PC_inputgoodness = 0;
/*############ Injection Stuff  - consts in Injection.h ############*/

int Injection[maxInjections][2] = { 0 }; //number of injections in protocol - max 200 to avoid dynamic memory allocation
int NumInj = 0; //number of injection pairs in protocol - set from PC comm
int NumFreq = 0; // number of frequencies (and corresponding amplitudes) to use - set from PC comm
int NumElec = 0; // number of electrodes used - this is used in contact check at the moment, but likely used for dual systems too
int NumRep = 0; // number of time whole protocol is repeated - total recording time is MeasTime*NumFreq*NumRep

int curNumRep = 0; //current number of repeats - this is either NumRep for normal recording, or set to 1 by compliance check

long  Amp[maxFreqs] = { 0 }; //amplitude in uA - container for max 20
long  Freq[maxFreqs] = { 0 }; //freq in Hz - contaier for max 20 set in
long MeasTime[maxFreqs] = { 0 }; //injection time in microseconds - set by user (USER SELECTS MILLISECONDS BUT SCALED IN MICROSECONDS AS DUE IS FASTER)
long curMeasTime = 0; // current measurement time changed by compliance check, or one of the MeasTime vars 

int FreqOrder[maxFreqs] = { 0 }; // order of the frequencies - initilised
long curFreqIdx = 0; // index of frequency vector current being injected
long prevFreq = 0; //previously used Frequency

long StartElapsed_CS = 0;// time since CS_Start was called
long StartTime_CS = 0; //time when CS_Start() was called

int iContact = 0; // counter for contact check loop
int ContactEndofSeq = 0; // flag for whether contact check is finished
long ContactTime = 0; // contact impedance measurement time in us

long BadElecs[maxBadElecs] = { 0 }; // bad electrodes
int NumBadElec = 0; // number of bad electrodes 

int Switch_goodness = 0; //flag for whether switches are behaving themselves

/*############ Indicator Pin things - consts in Pins.h and PCBPins.h ############*/

int pulses[NumInd] = { 0 }; //pulses left to do on the indicator channels
int indpinstate[NumInd] = { 0 }; //current state of the indicator pins
int iInd = 0; // counter for for loop in ind pins ISR (save defining all the time)
int IndinterruptCtr[NumInd] = { 0 }; // iterations of the interrupt routine for each pin channel

int pulsesleft = 0; // number of pulses left to do - used in indpins_check
int pulseleftptr = 0; // pointer for for loop in indpins_check, defined here for speed
int indpulseson = 0; // are pulses active? this flag is used to prevent checking pulses left when we know there are none

int iTrigChk = 0; //iteration of indicator pin check loop

/*############ Stimulation Trigger stuff - consts in Stim.h ############*/

long StimTriggerTime = 0; //time between stimulation triggers in microseconds
long StimOffset = 0; // time stimulation occurs after injection pair switch
long StimOffsetCurrent = 0; // offset for current stimulation - this is set to be either StimOffset or 0
long StimPulseWidth = 0; // width of stimulation pulse in microseconds - received from PC
int StimPulseWidthTicks = 0; // width of pulse in 1.5 uS Ticks of TC4 handler

int NumDelay = 0; //the total number of possible delays, as calcluated from the freq and the time gap - set by stim calcdelays
int Stim_delays[360] = { 0 }; //holds all possible delays up to a maximum of 360 - these are the number of ticks to wait before starting trigger
int Stim_phases[360] = { 0 }; // phases each of the delays equate too - cast to int because who cares about .5 of a degree of phase?
int Stim_PhaseOrder[360] = { 0 }; //order of the phase delays - shuffled and sent to PC every time it gets to the end

int d1 = 0; //current delay before stimulation trigger - in ticks of TC4 handler
int d2 = 0;// time to stop stimulation trigger - d1+Stimpulsewidth - in ticks of TC4 handler
int StiminterruptCtr = 0; // counter of the number of ticks of the TC4 handler since pmark pulse
int Stim_pinstate = 0; //current state of the stimulator pin IND_STIM

int Stim_goflag = 0; // flag for setting whether we should be stimulating at the moment - this is needed as I had to start the TC4 handler *before* setting the Stim_ready flag, so a few of the TC4 handler would run before stim should start
int Stim_ready = 0; // are we ready to stimulate again? this is so we ignore phase markers within the stim pulse

int CS_PhaseMarker = 0; // phase in degrees which phase marker occurs on the current source - set so that delay of 0 in stim routine occurs at ~0 phase
int PMARK_TEST_FLAG = 0; // flag used in PMARK check routines - this is set high by ISR_PMARK_TEST if all working ok
int CS_Pmarkgoodness = 0; // flag to confirm Phasemarker has been checked ok

/*########### Stimulation Voltage stuff - consts in Stim.h ############*/

int StimWiperValue = 0; // Wiper position for setting voltage of stimulation - must be 0-256 although usable range is between 215 and 250 with 215 approx 10V and 250 ~3V

/*########### Compliance Check stuff  ############*/


int curComplianceCheckOffset = 0; // current offset for checking compliance - this is set either by compliance check, or when starting normal injection

int ComplianceCheckMode = 0; // flag for whether we are in compliance check mode - this is used to decide which state to go back to after stopping injection
int iCompCheck = 0; // counter for compliance value iteration
int iCompCheckFreq = 0; // counter for frequency used in compliance check

unsigned long CompBitMask[CompMaskNum] = { 0 }; // 8 32 bit masks for the high or low bits for the compliance status of that protocol line. This prevents saving 245 8bit boolean types

int CompCheckFlag = 0; //flag in switch loop if we need to check compliance or not - saved checking it a million timmes

int CompFreqModeBackUp = 0; // back up of freq mode - comp check always in single freq mode inject
int CompStimModeBackup = 0; //back up of stim mode - not needed during compliance hcek

int FirstCompCheck = 0; // flag for whether we have done the first check
int curCompliance = 0; // the compliance currently in use

/*########### System Control stuff - consts in System_Control.h ############*/

int SingleFreqMode = 0; // flag for single frequency mode
int StimMode = 0; // flag for Stimulation mode - only stimulation stuff if needed
int state = 0; // what the system should be doing each iteration

long lastidle = 0; // timing for idle mode - if been idle for a few seconds then change display (maybe send heartbeat message to pc)
int checkidle = 1; //should we check idle?

int FirstInj = 0; // flag for doing the first injection - so we dont wait to switch at start
int SwitchesProgrammed = 0; // flag for whether the switches are programmed or not
int Switchflag = 0; // do we need to switch?
int Stimflag = 0; // should we stimulate?

long lastInjSwitch = 0; //time when channels were switched - SingleFreqMode
long lastFreqSwitch = 0; //time when Freq was last changed - MultipleFreqMode
long lastStimTrigger = 0; //time when stimulation trigger was last activated
long currentMicros = 0; // current time in micros

int iFreq = 0; //current frequency
int iPrt = 0; //current protocol line
int iRep = 0; //current protocol repetition
int iStim = 0; // current stimulation number

/*ALL DEFINITIONS DONE FINIALLY!*/

void setup() {
	// setup PC connection
	//Serial.begin(115200); //THIS IS FOR DEBUGGING ONLY

	init_pins(); // make sure switches are closed asap and set all low
	reset_pins();
	reset_pins_pwr();
	reset_ind();
	SwitchesPwrOn();

	// setup CS connection
	Serial1.begin(57600); // 57600 fastest baud that worked with AD chip - sparkfun connector may allow 115200 which would be nice
	CS_commgoodness = CS_init(); // make sure CS is off asap


	Wire.begin(); // start I2C
	Stim_SetDigipot(StimOffValue); // set potentiometer to highest resistance to minimise current draw

	randomSeed(analogRead(0)); // seed random number generator assumes A0 is floating!

	// setup PC connection
	Serial.begin(115200);

	Serial.println("Established contact");
	establishContact();

	/*########################################################
	SETUP TIMERS FOR STIMULATOR TRIGGER AND FOR INDICATOR PINS
	##########################################################*/

	//number comes from here https://github.com/ivanseidel/DueTimer/blob/master/TimerCounter.md
	//set timer interupts - this might possible conflict with servo library as I didnt check.....
	pmc_set_writeprotect(false);		 // disable write protection for pmc registers
	pmc_enable_periph_clk(ID_TC4);	 // enable peripheral clock TC4 this means T4 on TC1 channel 1  - this is the timer for the stim trigger output
	pmc_enable_periph_clk(ID_TC8); // enable TC8 or instance T8 on timer TC2 channel 2 - this is the timer for indicator pins
	//pmc_enable_periph_clk(ID_TC6); // enable TC6 or instance T6 on timer TC2 channel 0 - this is the timer for the fake pmark

	// set up timers and interupts - set channel on timers, set to "wave mode" meaning an output rather than "capture" to read ticks
	TC_Configure(TC1, 1, TC_CMR_WAVE | TC_CMR_WAVSEL_UP_RC | TC_CMR_TCCLKS_TIMER_CLOCK1); // use TC1 channel 1 in "count up mode" using MCLK /2 clock1 to give 42MHz
	TC_Configure(TC2, 2, TC_CMR_WAVE | TC_CMR_WAVSEL_UP_RC | TC_CMR_TCCLKS_TIMER_CLOCK2); // use TC2 channel 2 in "count up mode" using MCLK /8 clock1 to give 10.5MHz
	//TC_Configure(TC2, 0, TC_CMR_WAVE | TC_CMR_WAVSEL_UP_RC | TC_CMR_TCCLKS_TIMER_CLOCK4); // use TC2 channel 0 in "count up mode" using MCLK /128 clock1 to give 656.25 kHz

	TC_SetRC(TC1, 1, 63); // count 63 ticks on the 42MHz clock before calling the overflow routine - this gives an interupt every 1.5 uS
	TC_SetRC(TC2, 2, 105); // count 105 ticks on the 10.5MHz clock before calling the overflow routine - this gives an interupt every 10 uS
	//TC_SetRC(TC2, 0, 110); // count 110 ticks on the 656.25 kHz clock before calling the overflow routine - this gives an interupt every 167uS (equal to pmark at 6khz - kirills target freq)
	//TC_Start(TC1, 1); //start stim trig timer
	//TC_Start(TC2, 2); //start ind timer
	//TC_Start(TC2, 0); //start pmark fake timer

	// enable timer interrupts on the timer for stim tigger output
	TC1->TC_CHANNEL[1].TC_IER = TC_IER_CPCS;   // IER = interrupt enable register
	TC1->TC_CHANNEL[1].TC_IDR = ~TC_IER_CPCS;  // IDR = interrupt disable register
	//enable timer interupts on timer setup for indicators
	TC2->TC_CHANNEL[2].TC_IER = TC_IER_CPCS;   // IER = interrupt enable register
	TC2->TC_CHANNEL[2].TC_IDR = ~TC_IER_CPCS;  // IDR = interrupt disable register
	//enable timer interupts on timer setup for fake pmark
	//TC2->TC_CHANNEL[0].TC_IER = TC_IER_CPCS;   // IER = interrupt enable register
	//TC2->TC_CHANNEL[0].TC_IDR = ~TC_IER_CPCS;  // IDR = interrupt disable register

	//Enable the interrupt in the nested vector interrupt controller
	// TC4_IRQn where 4 is the timer number * timer channels (3) + the channel number (=(1*3)+1) for timer1 channel1
	NVIC_EnableIRQ(TC4_IRQn);
	// TC8_IRQn where 8 is the timer number * timer channels (3) + the channel number (=(2*3)+2) for timer2 channel2
	NVIC_EnableIRQ(TC8_IRQn);
	// TC6_IRQn where 6 is the timer number * timer channels (3) + the channel number (=(2*3)+0) for timer2 channel0
	//NVIC_EnableIRQ(TC6_IRQn);

	/*#######################################################
	Finished with timer stuff
	#########################################################*/

	Serial.println("timer set ok");


	/*#############CS INIT#############*/

	Serial.println("initialising");

	// initialise current source again and check the phase marker connection

	CS_commgoodness = CS_init();

	//Serial.print("init done");

	if (CS_commgoodness)
	{
		// check phase marker if current source connection ok
		CS_Pmarkgoodness = CheckPmark();

		if (CS_Pmarkgoodness)
		{
			Serial.print(CS_commokmsg); // everything is totally fine!
		}
		else
		{
			Serial.print(CS_pmarkerrmsg);
		}
	}

	else
	{
		Serial.print(CS_commerrmsg);
	}


	/*#############Switch INIT#############*/

	Switch_goodness = Switch_init();



	// fuckery #################################
	/*
	boolean comtmp = 0;

	comtmp = CompStatusReadAll();
	Serial.print("comptmp is: ");
	Serial.println(comtmp);


	CS_sendsettings(100, 1000);
	CS_start();
	delay(1000);

	CompProcessSingle(1);
	//CompStatusWrite(1, 1);

	comtmp = CompStatusReadAll();
	Serial.print("comptmp is: ");
	Serial.println(comtmp);

	CS_stop();
	*/

}

void loop() {

	//int statein = state;

	indpins_check(); // get rid of ind pins

	char c = '#'; //placeholder value 

	//read serial byte if one is available
	if (Serial.available() > 0)
	{

		c = Serial.read();
		//Serial.print(c);
	}

	// if byte has been read then get new CMD
	if (c != '#')
	{
		getCMD(c);
	}

	/*
	if (state != statein)
	{
	Serial.print("CMD change: State changed from ");
	Serial.print(statein);
	Serial.print(" to ");
	Serial.println(state);
	}
	*/

	// do stuff based on what the state is
	dostuff();
	/*
	if (state != statein)
	{
	Serial.print("dostuff change: State changed from ");
	Serial.print(statein);
	Serial.print(" to ");
	Serial.println(state);
	}
	*/

}

void dostuff()
{
	switch (state)
	{
	case 0: // this is the do nothing state
	{
		Serial.println("State case 0");
		//delay(10);

		if (checkidle)
		{
			lastidle = millis();
			checkidle = 0;
			Serial.println("Reset idle counter");
		}
		else
		{
			long currentmillis = millis();
			if ((currentmillis - lastidle) > idlewait)
			{
				CS_Disp(MSG_Idle);
				CS_Disp_Wind2(MSG_Idle_2);
				checkidle = 1;
				//here is where the pmc_enable_sleep_mode stuff would go
			}
		}
	}
	break;

	case 1: // start injection state
	{
		Serial.println("Injection starting case 1");

		if (PC_inputgoodness && CS_commgoodness) // only do anything if settings are ok
		{
			CS_Disp(MSG_Start);
			CS_Disp_Wind2(MSG_Start_2);


			//remove anything left from the current source buffer - we dont care about it anymore!
			CS_serialFlush();


			Serial.print(CS_commokmsg); // send ok msg to pc

			//pulse pins different amounts so we can find them in the EEG loading
			indChnIdent();


			//reset all counters
			iFreq = 0;
			iPrt = 0;
			iRep = 0;
			iStim = 0;

			//reset multifreq stuff
			curFreqIdx = 0;
			prevFreq = 0;


			//set variables based on values sent by user
			curMeasTime = MeasTime[0];
			curNumRep = NumRep;

			//if measurement time is 10ms or less, there is not enough time to check the compliance (as it takes about 5ms), so set the check time higher than switch time so it never happens

			curComplianceCheckOffset = SetComplianceOffset(curMeasTime);

			state = 2; //move to injecting state next loop
			FirstInj = 1; // flag that we are on the first injection
			SwitchesProgrammed = 0; // show that switches are not set

			if (SingleFreqMode) // see if we are in single freq mode and then set some of the settings that wont change
			{

				CS_AutoRangeOn(); //set ranging to normal
				CS_commgoodness = CS_sendsettings_check(Amp[iFreq], Freq[iFreq]); // send settings to current source
				if (!CS_commgoodness)
				{
					state = 0; // dont start injection if things are fucked
					Serial.print(CS_commerrmsg);
					CS_Disp(MSG_CS_SET_ERR);
					CS_Disp_Wind2(MSG_CS_SET_ERR_2);
				}
				else
				{
					// everything is ok - lets inject!
					Serial.print(CS_commokmsg);
					CS_Disp(MSG_CS_SET_OK);
					CS_Disp_Wind2(MSG_CS_SET_OK_2);
					// turn on switches ready for injecting and that
					SwitchesPwrOn();

					Serial.println("Switches POWERED ON");
					delay(50);
				}
			}
			else // we are in multifrequency mode and thus we need to set more stuff before we start injection
			{

				boolean AutoOffOK = CS_AutoRangeOff(); //set ranging to off
				boolean RangeSetOK = CS_SetRange(); // set range to max required

				if (!(AutoOffOK && RangeSetOK))
				{
					state = 0;
					Serial.print(CS_commerrmsg);
					CS_Disp(MSG_CS_SET_ERR);
					CS_Disp_Wind2(MSG_CS_SET_ERR_2);
				}

			}

		}
		else // if settings are not ok then dont do anything
		{
			if (!CS_commgoodness)
			{
				Serial.print(CS_commerrmsg);
			}
			else
			{
				Serial.print(CS_settingserrmsg);
			}
			state = 0;
			checkidle = 1;
		}
	}
	break;
	case 2: //injection
	{
		/* INJECTION!!! THIS IS THE MAIN EVENT FOLKS

		main flow of this is:

		SinglefrequencyMode - as the old "switchesserialV5" code. Current Source Started then check if MeasTime has elapsed then change switches

		MultiFreqMode - all freqs inejcted in random order before switching channels. Switches closed as CS stopped between each freq.

		Switchflag is set either by being the first iteration, or by MeasTime being elapsed. in single freqmode Switchflag is used for changing channels,
		but for multifreqmode it refers to switching frequencies.

		*/


		if (SingleFreqMode) // if only doing 1 frequency then we can copy and paste the code from previous iterations
		{
			if (!SwitchesProgrammed)
			{
				Serial.print("Channels are about to be programmed");
				/*
				Serial.print(Injection[iPrt][0]);
				Serial.print(" and ");
				Serial.println(Injection[iPrt][1]);*/
				///* debug trig */indpins_pulse(0, 0, 0, 2);
				SetSwitchesFixed(); // if switches havent been programmed then do that based on iPrt and take a set amount of time
			}

			if (FirstInj == 1) // if its the first time then switch straight away, otherwise check if the switch time has been met
				//if this is the first time we are injecting we need to send settings to the current source
			{

				if (StimMode)
				{
					stim_init(Freq[iFreq]);
				}


				///* debug trig */indpins_pulse(0, 0, 0, 1);

				//start current source
				StartTime_CS = micros();
				CS_start();
				
				///* debug trig */indpins_pulse(0, 0, 0, 1);

				//display some stuff on the front
				CS_Disp(MSG_SYS_RUN);
				CS_Disp_single(Amp[iFreq], Freq[iFreq], iRep, curNumRep);

				indpins_pulse(1, 0, 0, 0); //send start pulse to indicators

				lastInjSwitch = micros(); //start timer

				PC_sendupdate(); //send stuff to PC

				FirstInj = 0; // we dont want this to happen again
				Switchflag = 1; //we also want to switch the channels right now



				if (StimMode) {
					Stimflag = 1;
					lastStimTrigger = lastInjSwitch;
					StimOffsetCurrent = StimOffset;
				}


				// delay the start of injection to give the current source time to get ready
				StartElapsed_CS = micros() - StartTime_CS;

				if (StartElapsed_CS < (StartDelay_CS - 10))
				{
					delayMicroseconds(StartDelay_CS - StartElapsed_CS);
				}



			}
			else //if its not the first time, then see if we need to switch by checking time
			{
				currentMicros = micros();
				Serial.println("Checking time");
				if ((currentMicros - lastInjSwitch) > (curMeasTime))
				{
					Switchflag = 1; // if it is time to switch then set it to do that!
					/*sprintf(PC_outputBuffer, "Switch: %d", currentMicros - lastInjSwitch);
					Serial.println(PC_outputBuffer);*/
				}
				else // if it is not time to switch, then only do one of these with stim taking priority - im sorry for all the nested loops
				{

					if ((currentMicros - lastStimTrigger) > (StimTriggerTime + StimOffsetCurrent) && StimMode) // if after offset and we are in stim mode then
					{
						Stimflag = 1;
						digitalWriteDirect(PWR_STIM, HIGH); //turn off stimulator power supply

						/*sprintf(PC_outputBuffer, "Stim: %d", currentMicros - lastInjSwitch);
						Serial.println(PC_outputBuffer);*/
					}
					else if ((currentMicros - lastInjSwitch) > (curComplianceCheckOffset) && CompCheckFlag)
					{
						//check the compliance and do stuff based on the result
						//The iPrt counter is incremted when switching, thus the result needs to go into iPrt-1

						int CurrentPrt = iPrt - 1;
						if (CurrentPrt < 0) CurrentPrt = NumInj - 1;

						CompProcessSingle(CurrentPrt);
						CompCheckFlag = 0;

					}

				}


			}


			if (StimMode && Stimflag) //if we are in stim mode and time to stimulate is passed
			{
				Serial.println("Doing stim");
				//sprintf(PC_outputBuffer, "Stim: %d", currentMicros - lastInjSwitch);
				//Serial.println(PC_outputBuffer);

				stim_nextphase(); // do the next one
				Stimflag = 0; // reset stimulation flag
				StimOffsetCurrent = 0; // set offset to 0 as its not the first stimulation for this protocol line anymore
			}


			if (Switchflag) // if we have been told to switch
			{
				if (iRep == curNumRep) // if we have reached the total number of injections
				{
					state = 3; // do stop command
				}
				else // otherwise carry on with switching etc.
				{

					///* debug trig */indpins_pulse(0, 0, 0, 1);

					SwitchChn(); // switch channel
					StimOffsetCurrent = StimOffset; //reset the stimoffset as we are switching again
				}

				// indicator and stimulation things here too!!!!!!!

				if (iPrt == 1 && iRep > 0 && iRep < curNumRep) // if we have a new thing to display then update
				{
					CS_Disp_single(Amp[iFreq], Freq[iFreq], iRep, curNumRep);

					PC_sendupdate(); // send info to PC
					CompProcessMulti();// send compliance status to PC
				}
				Switchflag = 0;
				Stimflag = 0;
				CompCheckFlag = 1;
			}
		}
		else //MULTIFREQUENCY MODE
		{
			if (FirstInj) //if this is the first time it is called then setup straight away
			{

				CS_Disp(MSG_SYS_RUN_MULTI);
				Switchflag = 1;

				if (StimMode) Stimflag = 1;

				FirstInj = 0;
				Serial.println("Starting MultiInject");
				shuffle(FreqOrder, NumFreq);
				//send info to PC
				PC_sendupdate();

				delayMicroseconds(5000);//added delay here as startpulse below was happening so quickly after indChnIdent() at start, the start pulse was merging with the ID pulses! This took *way* too long to debug

				indpins_pulse(1, 0, 0, 0); //send start pulse to indicators
				//turn on power to switches
				SwitchesPwrOn();

				indpins_pulse(0, 0, 1, 0); // send switch pulse as processing code is expecting it 


			}
			else // if this is NOT the first time called, then check if time has elapsed before changing frequency
			{
				currentMicros = micros();
				if ((currentMicros - lastFreqSwitch) > (MeasTime[curFreqIdx] /*- SwitchTimeFix*/)) // time to switch is MeasTime, but we fixed the time taken to program switches in SetSwitchesFixed
				{
					Switchflag = 1; //set that we should switch now
				}
				else
				{
					if ((currentMicros - lastStimTrigger) > (StimTriggerTime + StimOffsetCurrent) && StimMode) // if enough time has elapsed AND we are in stim mode then
					{
						Stimflag = 1;
					}
					else if ((currentMicros - lastInjSwitch) > (curComplianceCheckOffset) && CompCheckFlag)
					{
						//check the compliance and do stuff based on the result
						//The iPrt counter is incremted when switching, thus the result needs to go into iPrt-1

						int CurrentPrt = iPrt - 1;
						if (CurrentPrt < 0) CurrentPrt = NumInj - 1;

						CompProcessSingle(CurrentPrt);
						CompCheckFlag = 0;

					}

				}

			}

			if (StimMode) //only do this if stim mode
			{
				if (Stimflag && (currentMicros - lastInjSwitch) > StimOffset) // if we have been told to switch and offset has passed
				{
					Stimflag = 0; // reset stimulation flag
					stim_nextphase(); // do the next one
					StimOffsetCurrent = 0; // set offset to 0 as its not the first stimulation for this protocol line anymore
				}
			}

			if (Switchflag) //if we have been told to switch then...
			{
				Serial.println("Doing switchflag stuff");
				if (iRep == curNumRep) //if total number of repetitions have been reached then stop this madness
				{
					state = 3;
				}
				else // if it is business as usual then change freq
				{
					if (iFreq == NumFreq) //if we have done all the frequencies then change injection channels
					{
						CS_next_chn(); // call function to increment iPrt and shuffle Freqorder again, and sent to PC

					}

					CS_next_freq(); // this function is the guts of multifreqmode - stop CS, send new settings, then start again
					StimOffsetCurrent = StimOffset; //reset the stimoffset as we are switching again
				}
				Switchflag = 0;
			}
		}



	}

	break;

	case 3: // stop injection state
	{
		CS_stop(); //stop current source
		stim_stop(); //stop stimulation
		Stim_SetDigipot(StimOffValue); // set stim voltage low again
		SwitchesPwrOff(); // turn off switch network
		reset_pins_pwr(); //turn off power to switches and stim - duplication here but only of 2 clock cycles so its ok...

		indpins_pulse(0, 1, 0, 0); // indicate injection has stopped

		//front panel stuff
		CS_Disp(MSG_SYS_STOP);
		CS_Disp_Wind2(MSG_SYS_STOP_2);

		Serial.println("Stopping injection");
		//Serial.print(CS_finishedmsg);

		reset_pins(); //over the top but reset all of the switches again
		digitalWriteDirect(IND_EX_1, LOW); //put the compliance flag to low

		if (!ComplianceCheckMode) //if we are NOT doing a compliance check
		{
			//then go back to idle like normal
			CompStatusReset(); //put all compliance status back to low
			state = 0;
			checkidle = 1;
		}
		else
		{
			//HERE IS WHERE WE GO BACK TO COMPLIANCE CHECK STATE!!!
			Serial.println("finished injecting, back to compliance state");
			state = 9;
		}

		//front panel stuff
		CS_Disp(MSG_SYS_STOP);
		CS_Disp_Wind2(MSG_SYS_STOP_2);

		Serial.println("Stopping injection");
		Serial.print(CS_finishedmsg);

		//remove anything left from the current source buffer - we dont care about it anymore!
		CS_serialFlush();

		//put the range stuff back to normal - in case we changed it doing multifreq
		CS_AutoRangeOn();
	}

	break;

	case 5: //start contact check
	{
		if (PC_inputgoodness && CS_commgoodness) // only do anything if settings are ok
		{
			CS_Disp(MSG_CONTACT_CHECK_RUN);
			CS_Disp_Wind2(MSG_CONTACT_CHECK_RUN_2);

			Serial.print(CS_commokmsg); // send ok msg to pc

			//pulse pins different amounts so we can find them in the EEG loading
			indChnIdent();

			//reset all counters
			iFreq = 0;
			iPrt = 0;
			iRep = 0;
			iStim = 0;
			iContact = 0;

			state = 6; //move to injecting state next loop
			FirstInj = 1; // flag that we are on the first injection
			SwitchesProgrammed = 0; // show that switches are not set
			ContactEndofSeq = 0; // restart contact seq

			curComplianceCheckOffset = ContactTime / 2;

			CS_AutoRangeOn(); //set ranging to normal

			CS_commgoodness = CS_sendsettings_check(ContactAmp, ContactFreq); // send settings to current source

			/* do this in the first iteration of the inject state - so the communication order the is the same!
			if (StimMode && CS_commgoodness) // initialise stimulator trigger if we are in stim mode
			{
			CS_commgoodness = stim_init(Freq[iFreq]);
			}
			*/

			if (!CS_commgoodness)
			{
				state = 0; // dont start injection if things are fucked
				Serial.print(CS_commerrmsg);
				CS_Disp(MSG_CS_SET_ERR);
				CS_Disp_Wind2(MSG_CS_SET_ERR_2);
			}
			else
			{
				Serial.print(CS_commokmsg);
				CS_Disp(MSG_CS_SET_OK);
				CS_Disp_Wind2(MSG_CS_SET_OK_2);

				SwitchesPwrOn(); //turn on switches
				delay(50); // cant remember what this delay is for!
			}
		}
		else // if settings are not ok then dont do anything
		{
			Serial.print(CS_settingserrmsg);
			state = 0;
			checkidle = 1;
		}
	}
	break;

	case 4: // initialise or "get settings" state
	{
		//do the CS init again
		Serial.println("initialising");
		CS_commgoodness = 0;
		CS_commgoodness = CS_init();

		if (CS_commgoodness)
		{
			Serial.print(CS_commokmsg);
		}
		else
		{
			Serial.print(CS_commerrmsg);
		}
		CS_Disp(MSG_SET_READ);
		CS_Disp_Wind2(MSG_SET_READ_2);


		//remove anything left from the current source buffer - we dont care about it anymore!
		CS_serialFlush();


		//get settings from PC
		PC_commgoodness = 0;
		PC_commgoodness = PC_getsettings();

		if (PC_commgoodness)
		{
			Serial.print(CS_commokmsg);
			CS_Disp(MSG_SET_READ_OK);
			CS_Disp_Wind2(MSG_SET_READ_OK_2);
		}
		else
		{
			//Serial.println(CS_settingserrmsg);
			Serial.print(CS_commerrmsg);
			CS_Disp(MSG_SET_READ_ERR);
			CS_Disp_Wind2(MSG_SET_READ_ERR_2);
		}

		/*check settings are ok - all sent to CS with verification ONCE
		not checked during injection as takes too long
		*/

		if (PC_commgoodness)
		{
			Serial.println("Checking inputs");
			PC_inputgoodness = checkinputs(); // check inputs

			if (!PC_inputgoodness) // moan if its not ok
			{
				Serial.println("INPUT CHECK FAILED");
				Serial.print(CS_settingserrmsg);
				CS_Disp(MSG_SET_ERR);
				CS_Disp_Wind2(MSG_SET_ERR_2);
			}
			else
			{
				PC_inputgoodness = CS_SetRange();

				if (PC_inputgoodness)
				{
					Serial.print(CS_commokmsg);
					CS_Disp(MSG_SET_CHK_OK);
					CS_Disp_Wind2(MSG_SET_CHK_OK_2);
				}
				else
				{
					CS_Disp(MSG_RNG_ERR);
					CS_Disp_Wind2(MSG_RNG_ERR_2);
				}
			}

		}


		state = 0; //back to idle when done
		checkidle = 1;

	}
	break;
	case	7: //check triggers
	{
		//this is badly coded because who cares...

		ind_high();
		delayMicroseconds(TrigCheckHighTime);
		ind_low();
		delayMicroseconds(TrigCheckLowTime);

		iTrigChk++;

		if (iTrigChk >= MaxTrigCheckLoops)
		{
			iTrigChk = 0;
			state = 0;
			Serial.print("ind done");
		}

	}
	break;
	case 6: //contact check
	{

		if (!SwitchesProgrammed)
		{
			Serial.print("I am about to program channels");

			SetSwitchesFixed_Contact(); // if switches havent been programmed then do that based on iPrt and take a set amount of time
		}

		if (FirstInj == 1) // if its the first time then switch straight away, otherwise check if the switch time has been met
			//if this is the first time we are injecting we need to send settings to the current source
		{

			//start current source
			StartTime_CS = micros();

			//start current source
			CS_start();
			//display some stuff on the front
			CS_Disp(MSG_CONTANT_CHECK_START);
			CS_Disp_Contact(iContact, NumElec);

			indpins_pulse(2, 0, 0, 0); //two pulses for indicating contact check
			//indpins_pulse(0, 0, 3, 0); // compatible with OLD CODE ONLY


			FirstInj = 0;
			Switchflag = 1;
			lastInjSwitch = micros();


			// delay the start of injection to give the current source time to get ready
			StartElapsed_CS = micros() - StartTime_CS;

			if (StartElapsed_CS < (StartDelay_CS - 10))
			{
				delayMicroseconds(StartDelay_CS - StartElapsed_CS);
			}




		}
		else //if its not the first time, then see if we need to switch by checking time
		{
			currentMicros = micros();
			Serial.println(currentMicros);
			if ((currentMicros - lastInjSwitch) > (ContactTime /*- SwitchTimeFix */)) // time to switch is MeasTime, but we fixed the time taken to program switches in SetSwitchesFixed
			{
				Switchflag = 1; // if it is time to switch then set it to do that!
				Serial.println("Switch!");
			}
			else if ((currentMicros - lastInjSwitch) > (curComplianceCheckOffset) && CompCheckFlag)
			{
				//check the compliance and do stuff based on the result
				//The iPrt counter is incremted when switching, thus the result needs to go into iPrt-1

				Serial.println("checking compliance");

				int CurrentPrt = iContact - 1;
				if (CurrentPrt < 0) CurrentPrt = NumElec - 1;

				CompProcessSingle(CurrentPrt);
				CompCheckFlag = 0;

			}



			if (Switchflag) // if we have been told to switch
			{
				if (ContactEndofSeq == 1) // if we have reached the total number of injections
				{
					CompProcessMulti();
					state = 3; // do stop command
					//for OLD CODE COMPATONLY
					//indpins_pulse(0, 0, 5, 0);
				}
				else // otherwise carry on with switching etc.
				{

					SwitchChn_Contact(); // switch channel
					CS_Disp_Contact(iContact, NumElec);
				}

				Switchflag = 0;
				CompCheckFlag = 1;

			}
		}

	}
	break;
	case	8: //check switches
	{
		//this is badly coded because who cares

		Serial.println("Checking Switches");

		Switch_goodness = Switch_init();

		state = 0;
		Serial.print("ind done 2");

	}
	break;
	case	9: //start compliance check
	{
		/*

		copy start inject but

		set curMeasTime


		*/

		Serial.println("Comp Check mode!");

		if (FirstCompCheck && PC_inputgoodness && CS_commgoodness) // only do anything if settings are ok
		{
			//save the previous variables to not break anything

			Serial.println("Doing first things");

			CompFreqModeBackUp = SingleFreqMode;
			CompStimModeBackup = StimMode;

			FirstCompCheck = 0;
			iCompCheckFreq = 0;
			iCompCheck = 0;

			Serial.print(Complianceokmsg);

			sendasciinum(CompCheckNum);
			sendasciinum(NumFreq);

		}
		else
		{
			Serial.println("Lets see what the compliance was shall we?");
			PC_sendcomplianceupdate();
			//send info about that checks
			sendasciinum(iCompCheck + 1);
			sendasciinum(iCompCheckFreq);
			sendasciinum(curCompliance);
		}

		//check if we need to do a comp check and set iComp and icompfreq here, or reset

		if (iCompCheckFreq == NumFreq) // if we have done all the freqs we need then move to next compliance value
		{
			iCompCheck++;
			iCompCheckFreq = 0;
		}


		if (iCompCheck >= CompCheckNum) // if we have done all the compliance values we need then stop and reset
		{

			Serial.println("We are done checking compliance");
			Serial.print(Complianceokmsg);
			ComplianceCheckMode = 0;
			state = 0; // dont start injection if things are fucked
			ResetAfterCompliance();
			checkidle = 1;

		}
		else
		{
			ComplianceCheckMode = 1;

			//if we ARE doing something, then do all this:

			Serial.print("Checking Compliance Values");

			Serial.print(CS_commokmsg); // send ok msg to pc

			//pulse pins different amounts so we can find them in the EEG loading
			indChnIdent();


			//reset all counters
			iFreq = iCompCheckFreq;
			iPrt = 0;
			iRep = 0;
			iStim = 0;

			//set variables for compliance check only
			curMeasTime = ComplianceCheckMeasTime;
			curNumRep = 2;
			curComplianceCheckOffset = ComplianceCheckOffset; //check for compliance half way through injection THIS ASSUMES INJECTION TIME IS AT LEAST 6ms AS IT TAKES 3ms TO CHECK COMPLIANCE

			state = 2; //move to injecting state next loop
			FirstInj = 1; // flag that we are on the first injection
			SwitchesProgrammed = 0; // show that switches are not set

			//force only 1 freq and no stim 
			SingleFreqMode = 1;
			StimMode = 0;

			//set the compliance

			curCompliance = Compliance * ComplianceScaleFactors[iCompCheck];

			Serial.print("Setting Compliance");

			bool compsetok = 0;

			compsetok = CS_SetCompliance(curCompliance);

			if (!compsetok)
			{
				Serial.println("Comp Set Problem");
			}

			Serial.print("setting Freq");
			CS_commgoodness = CS_sendsettings_check(Amp[iFreq], Freq[iFreq]); // send settings to current source



			if (!CS_commgoodness)
			{
				state = 0; // dont start injection if things are fucked
				checkidle = 1;
				ResetAfterCompliance();
				Serial.print(CS_commerrmsg);
				CS_Disp(MSG_CS_SET_ERR);
				CS_Disp_Wind2(MSG_CS_SET_ERR_2);
			}
			else
			{
				// everything is ok - lets inject!
				Serial.print(CS_commokmsg);
				// turn on switches ready for injecting and that
				SwitchesPwrOn();

				Serial.println("Switches POWERED ON");
				delay(50);

				//update compliance counter
				iCompCheckFreq++;



			}


		}

	}
	break;



	}
}


//function to read command from PC and then put system in "state"
void getCMD(char CMDIN)
{
	switch (CMDIN)
	{
	case 'X': //set to idle "do nothing" state (I dont know why you would need this)
		state = 0;
		break;
	case 'H': // Halt injection state
	{
		if (state != 0) // if system is NOT idle
		{
			state = 3;
			if (ComplianceCheckMode)
			{
				iCompCheck = CompCheckNum; // this is so the compliance check still ends properly
				Serial.println("Setting compcheck num");
			}
		}
		break;
	}
	case 'S': //start injection
	{
		if (state == 0) // if the system is idle ONLY
		{
			state = 1;
		}
		break;
	}
	case 'I': // Initialise settings - read from PC and set CS defaults
	{
		if (state == 0) // if the system is idle ONLY
		{
			state = 4;
		}
		break;
	}
	case 'C': //start contact impedance check
	{
		if (state == 0) // if the system is idle ONLY
		{
			state = 5;
		}
		break;
	}
	case 'T': // check trigger state
	{
		if (state == 0) // if the system is idle ONLY
		{
			state = 7;
		}
		break;
	}
	case 'W': // check switch state
	{
		if (state == 0) // if the system is idle ONLY
		{
			state = 8;
		}
		break;
	}
	case 'L': // check compliance state
	{
		if (state == 0) // if the system is idle ONLY
		{
			state = 9;
			FirstCompCheck = 1;
		}
		break;
	}

	default: // if not one of these commands then keep state the same
		state = state; //a bit didactic but hey
		break;
	}
}


void TC4_Handler() //this is the ISR for the 667kHz timer - runs every 1.5 uS - this is as fast as I could reliably get it as worst case code takes 1.357 uS to run
{
	// We need to get the status to clear it and allow the interrupt to fire again
	TC_GetStatus(TC1, 1); //here TC2,1 means TIMER 2 channel 1

	if (Stim_goflag) //if we should go
	{
		//StiminterruptCtr++; //increment intrctr
		if (!Stim_pinstate && StiminterruptCtr >= d1) // check if timer is up and pulse still low
		{
			digitalWriteDirect(IND_STIM, 1); //write pin high
			Stim_pinstate = !Stim_pinstate;
		}
		else if (Stim_pinstate && StiminterruptCtr >= d2)
		{

			digitalWriteDirect(IND_STIM, 0); //write pin low
			Stim_pinstate = !Stim_pinstate;

			Stim_goflag = 0; //stop it from happening again
			TC_Stop(TC1, 1);

		}

		StiminterruptCtr++; //increment intrctr
	}
	else
	{
		StiminterruptCtr = 0; //reset intrcntr

	}
}

void TC8_Handler() // this is the ISR for the indicator pins - cycles through all of the pins if they should change their state - this runs every 10uS
{
	// We need to get the status to clear it and allow the interrupt to fire again
	TC_GetStatus(TC2, 2); //here TC2,2 means TIMER 2 channel 2

	for (iInd = 0; iInd < NumInd; iInd++) // cycle through the indicator pins
	{
		if (pulses[iInd] > 0) //if we have some pulses left to do
		{
			if (!indpinstate[iInd] && (IndinterruptCtr[iInd] < indpulsewidth)) // set pin high if less than pulse width
			{
				digitalWriteDirect(indpins[iInd], 1);
				indpinstate[iInd] = 1;
				digitalWriteDirect(IND_EX_3, 0); //dummy pulse for Actichamp
			}
			else if (indpinstate[iInd] && (IndinterruptCtr[iInd] > indpulsewidth)) //set pin low if greater than pulse wifth
			{
				digitalWriteDirect(indpins[iInd], 0);
				indpinstate[iInd] = 0;
				digitalWriteDirect(IND_EX_3, 1); //dummy pulse for Actichamp
			}
			IndinterruptCtr[iInd]++; //increment tick counter
			if (IndinterruptCtr[iInd] == indpulsewidthtotal) // if total pulse length (HIGH and LOW) happened then decrement pulses left
			{
				pulses[iInd]--;
				IndinterruptCtr[iInd] = 0;
			}

		}
	}
}

/*

void TC6_Handler() //this runs every 166uS which is the same rate as the pmark on the CS at 6kHz - fake pmark
{
// We need to get the status to clear it and allow the interrupt to fire again
TC_GetStatus(TC2, 0); //here TC2,2 means TIMER 2 channel 0
digitalWriteDirect(fakepmarkpin, 1);
digitalWriteDirect(fakepmarkpin, 0);
}
*/


//taken from http://forum.arduino.cc/index.php?topic=129868.15
inline void digitalWriteDirect(int pin, int val) {
	if (val) g_APinDescription[pin].pPort->PIO_SODR = g_APinDescription[pin].ulPin;
	else    g_APinDescription[pin].pPort->PIO_CODR = g_APinDescription[pin].ulPin;
}




