/*
 * The MIT License (MIT)
 * 
 * Copyright (c) 2015 Charles J. Cliffe
 * Copyright (c) 2019 Franco Venturi - changes for SDRplay API version 3
 *                                     and Dual Tuner for RSPduo

 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "SoapySDRPlay3.hpp"

// globals declared in Registration.cpp
extern bool isSdrplayApiOpen;
extern sdrplay_api_DeviceT *deviceSelected;

static sdrplay_api_DeviceT rspDevs[SDRPLAY_MAX_DEVICES];

SoapySDRPlay3::SoapySDRPlay3(const SoapySDR::Kwargs &args)
{
    std::string label = args.at("label");

    std::string baseLabel = "SDRplay3 Dev";

    size_t posidx = label.find(baseLabel);

    if (posidx == std::string::npos)
    {
        SoapySDR_logf(SOAPY_SDR_WARNING, "Can't find Dev string in args");
        throw std::runtime_error("Can't find Dev string in args");
    }
    // retrieve device index
    unsigned int devIdx = label.at(posidx + baseLabel.length()) - 0x30;

    // retrieve hwVer and serNo by API
    unsigned int nDevs = 0;

    sdrplay_api_ErrT err;

    if (isSdrplayApiOpen == false) {
        sdrplay_api_Open();
        isSdrplayApiOpen = true;
    }

    sdrplay_api_GetDevices(&rspDevs[0], &nDevs, SDRPLAY_MAX_DEVICES);

    if (devIdx >= nDevs) {
        SoapySDR_logf(SOAPY_SDR_WARNING, "Can't determine hwVer/serNo");
        throw std::runtime_error("Can't determine hwVer/serNo");
    }

    err = sdrplay_api_ApiVersion(&ver);
    if (err != sdrplay_api_Success)
    {
        sdrplay_api_UnlockDeviceApi();
        SoapySDR_logf(SOAPY_SDR_ERROR, "ApiVersion Error: %s", sdrplay_api_GetErrorString(err));
        throw std::runtime_error("ApiVersion() failed");
    }
    if (ver != SDRPLAY_API_VERSION)
    {
        SoapySDR_logf(SOAPY_SDR_WARNING, "sdrplay_api version: '%.3f' does not equal build version: '%.3f'", ver, SDRPLAY_API_VERSION);
    }

    device = rspDevs[devIdx];
    if (device.hwVer == SDRPLAY_RSPduo_ID) {
        device.tuner = rspDuoModeStringToTuner(args.at("rspduo_mode"));
        sdrplay_api_RspDuoModeT rspDuoMode = rspDuoModeStringToRspDuoMode(args.at("rspduo_mode"));
        // if master device is available, select device as master
        if ((rspDuoMode & sdrplay_api_RspDuoMode_Master) && (device.rspDuoMode & sdrplay_api_RspDuoMode_Master))
        {
            rspDuoMode = sdrplay_api_RspDuoMode_Master;
        }
        else if (rspDuoMode & sdrplay_api_RspDuoMode_Slave)
        {
            rspDuoMode = sdrplay_api_RspDuoMode_Slave;
        }
        device.rspDuoMode = rspDuoMode;
    } else {
        device.tuner = sdrplay_api_Tuner_A;
        device.rspDuoMode = sdrplay_api_RspDuoMode_Unknown;
    }
    err = sdrplay_api_SelectDevice(&device);
    if (err != sdrplay_api_Success)
    {
        sdrplay_api_UnlockDeviceApi();
        SoapySDR_logf(SOAPY_SDR_ERROR, "SelectDevice Error: %s", sdrplay_api_GetErrorString(err));
        throw std::runtime_error("SelectDevice() failed");
        return;
    }
    sdrplay_api_UnlockDeviceApi();
    deviceSelected = &device;

    // Enable (= sdrplay_api_DbgLvl_Verbose) API calls tracing,
    // but only for debug purposes due to its performance impact.
    sdrplay_api_DebugEnable(device.dev, sdrplay_api_DbgLvl_Disable);

    err = sdrplay_api_GetDeviceParams(device.dev, &deviceParams);
    if (err != sdrplay_api_Success)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "GetDeviceParams Error: %s", sdrplay_api_GetErrorString(err));
        throw std::runtime_error("GetDeviceParams() failed");
        return;
    }
    chParams = device.tuner == sdrplay_api_Tuner_B ? deviceParams->rxChannelB : deviceParams->rxChannelA;

    // set sample rate
    uint32_t sampleRate = 2000000;
    deviceParams->devParams->fsFreq.fsHz = sampleRate;
    reqSampleRate = sampleRate;
    chParams->ctrlParams.decimation.decimationFactor = 1;
    chParams->ctrlParams.decimation.enable = 0;
    chParams->tunerParams.rfFreq.rfHz = 100000000;
    deviceParams->devParams->ppm = 0.0;
    chParams->tunerParams.ifType = sdrplay_api_IF_Zero;
    chParams->tunerParams.bwType = sdrplay_api_BW_1_536;
    chParams->tunerParams.gain.gRdB = 40;
    chParams->tunerParams.gain.LNAstate = (device.hwVer == SDRPLAY_RSP2_ID ||
        device.hwVer == SDRPLAY_RSPduo_ID || device.hwVer == SDRPLAY_RSP1A_ID)
        ? 4: 1;

    // this may change later according to format
    shortsPerWord = 1;
    bufferLength = bufferElems * elementsPerSample * shortsPerWord;

    chParams->ctrlParams.agc.enable = sdrplay_api_AGC_100HZ;
    chParams->ctrlParams.dcOffset.DCenable = 1;

    chParams->ctrlParams.dcOffset.IQenable = 1;
    chParams->ctrlParams.agc.setPoint_dBfs = -30;
    chParams->ctrlParams.agc.knee_dBfs = 0;
    chParams->ctrlParams.agc.decay_ms = 0;
    chParams->ctrlParams.agc.decay_ms = 0;
    chParams->ctrlParams.agc.hang_ms = 0;
    chParams->ctrlParams.agc.syncUpdate = 0;
    chParams->ctrlParams.agc.LNAstate = chParams->tunerParams.gain.LNAstate;

    if (device.hwVer == SDRPLAY_RSP2_ID) {
        chParams->rsp2TunerParams.antennaSel = sdrplay_api_Rsp2_ANTENNA_A;
        chParams->rsp2TunerParams.amPortSel = sdrplay_api_Rsp2_AMPORT_2;
        deviceParams->devParams->rsp2Params.extRefOutputEn = 0;
        chParams->rsp2TunerParams.biasTEnable = 0;
        chParams->rsp2TunerParams.rfNotchEnable = 0;
    }
    else if (device.hwVer == SDRPLAY_RSPduo_ID) {
        chParams->rspDuoTunerParams.tuner1AmPortSel = sdrplay_api_RspDuo_AMPORT_2;
        deviceParams->devParams->rspDuoParams.extRefOutputEn = 0;
        chParams->rspDuoTunerParams.biasTEnable = 0;
        chParams->rspDuoTunerParams.rfNotchEnable = 0;
        chParams->rspDuoTunerParams.rfDabNotchEnable = 0;
    }

    _bufA = 0;
    _bufB = 0;
    useShort = true;

    streamActive = false;
}

SoapySDRPlay3::~SoapySDRPlay3(void)
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

    if (streamActive)
    {
        sdrplay_api_Uninit(device.dev);
    }
    streamActive = false;
    sdrplay_api_ReleaseDevice(&device);
    deviceSelected = nullptr;

    if (isSdrplayApiOpen == true) {
        sdrplay_api_Close();
        isSdrplayApiOpen = false;
    }

    _bufA = 0;
    _bufB = 0;
}

/*******************************************************************
 * Identification API
 ******************************************************************/

std::string SoapySDRPlay3::getDriverKey(void) const
{
    return "SDRplay3";
}

std::string SoapySDRPlay3::getHardwareKey(void) const
{
    return device.SerNo;
}

SoapySDR::Kwargs SoapySDRPlay3::getHardwareInfo(void) const
{
    // key/value pairs for any useful information
    // this also gets printed in --probe
    SoapySDR::Kwargs hwArgs;

    hwArgs["sdrplay_api_api_version"] = std::to_string(ver);
    hwArgs["sdrplay_api_hw_version"] = std::to_string(device.hwVer);

    return hwArgs;
}

/*******************************************************************
 * Channels API
 ******************************************************************/

size_t SoapySDRPlay3::getNumChannels(const int dir) const
{
    if (device.hwVer == SDRPLAY_RSPduo_ID && device.rspDuoMode == sdrplay_api_RspDuoMode_Dual_Tuner) {
        return (dir == SOAPY_SDR_RX) ? 2 : 0;
    }
    return (dir == SOAPY_SDR_RX) ? 1 : 0;
}

/*******************************************************************
 * Antenna API
 ******************************************************************/

std::vector<std::string> SoapySDRPlay3::listAntennas(const int direction, const size_t channel) const
{
    std::vector<std::string> antennas;

    if (direction == SOAPY_SDR_TX) {
        return antennas;
    }

    if (device.hwVer == SDRPLAY_RSP1_ID || device.hwVer == SDRPLAY_RSP1A_ID) {
        antennas.push_back("RX");
    }
    else if (device.hwVer == SDRPLAY_RSP2_ID) {
        antennas.push_back("Antenna A");
        antennas.push_back("Antenna B");
        antennas.push_back("Hi-Z");
    }
    else if (device.hwVer == SDRPLAY_RSPduo_ID) {
        antennas.push_back("Tuner 1 50 ohm");
        antennas.push_back("Tuner 2 50 ohm");
        antennas.push_back("Tuner 1 Hi-Z");
    }
    return antennas;
}

void SoapySDRPlay3::setAntenna(const int direction, const size_t channel, const std::string &name)
{
    // Check direction
    if ((direction != SOAPY_SDR_RX) || (device.hwVer == SDRPLAY_RSP1_ID) || (device.hwVer == SDRPLAY_RSP1A_ID)) {
        return;       
    }

    std::lock_guard <std::mutex> lock(_general_state_mutex);

    if (device.hwVer == SDRPLAY_RSP2_ID)
    {
        bool changeToAntennaA_B = false;

        if (name == "Antenna A")
        {
            chParams->rsp2TunerParams.antennaSel = sdrplay_api_Rsp2_ANTENNA_A;
            changeToAntennaA_B = true;
        }
        else if (name == "Antenna B")
        {
            chParams->rsp2TunerParams.antennaSel = sdrplay_api_Rsp2_ANTENNA_B;
            changeToAntennaA_B = true;
        }
        else if (name == "Hi-Z")
        {
            chParams->rsp2TunerParams.amPortSel = sdrplay_api_Rsp2_AMPORT_1;

            if (streamActive)
            {
                sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Rsp2_AmPortSelect);
            }
        }

        if (changeToAntennaA_B)
        {
        
            //if we are currently High_Z, make the switch first.
            if (chParams->rsp2TunerParams.amPortSel == sdrplay_api_Rsp2_AMPORT_1)
            {
                chParams->rsp2TunerParams.amPortSel = sdrplay_api_Rsp2_AMPORT_2;

                if (streamActive)
                {
                    sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Rsp2_AmPortSelect);
                }
            }
            else
            {
                if (streamActive)
                {
                    sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Rsp2_AntennaControl);
                }
            }
        }
    }
    else if (device.hwVer == SDRPLAY_RSPduo_ID)
    {
        bool changeToTuner1_2 = false;
        if (name == "Tuner 1 50 ohm")
        {
            chParams->rspDuoTunerParams.tuner1AmPortSel = sdrplay_api_RspDuo_AMPORT_2;
            if (device.tuner != sdrplay_api_Tuner_A)
            {
                device.tuner = sdrplay_api_Tuner_A;
                changeToTuner1_2 = true;
            }
        }
        else if (name == "Tuner 2 50 ohm")
        {
            chParams->rspDuoTunerParams.tuner1AmPortSel = sdrplay_api_RspDuo_AMPORT_2;
            if (device.tuner != sdrplay_api_Tuner_B)
            {
                device.tuner = sdrplay_api_Tuner_B;
                changeToTuner1_2 = true;
            }
        }
        else if (name == "Tuner 1 HiZ")
        {
            chParams->rspDuoTunerParams.tuner1AmPortSel = sdrplay_api_RspDuo_AMPORT_1;
            if (device.tuner != sdrplay_api_Tuner_A)
            {
                device.tuner = sdrplay_api_Tuner_A;
                changeToTuner1_2 = true;
            }
        }

        if (changeToTuner1_2)
        {
            changeToTuner1_2 = false;
            sdrplay_api_SwapRspDuoActiveTuner(device.dev, &device.tuner, chParams->rspDuoTunerParams.tuner1AmPortSel);
        }

        if (streamActive)
        {
            sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_RspDuo_AmPortSelect);
        }
    }
}

std::string SoapySDRPlay3::getAntenna(const int direction, const size_t channel) const
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

    if (direction == SOAPY_SDR_TX)
    {
        return "";
    }

    if (device.hwVer == SDRPLAY_RSP2_ID)
    {
        if (chParams->rsp2TunerParams.amPortSel == sdrplay_api_Rsp2_AMPORT_1) {
            return "Hi-Z";
        }
        else if (chParams->rsp2TunerParams.antennaSel == sdrplay_api_Rsp2_ANTENNA_A) {
            return "Antenna A";
        }
        else {
            return "Antenna B";  
        }
    }
    else if (device.hwVer == SDRPLAY_RSPduo_ID)
    {
        if (chParams->rspDuoTunerParams.tuner1AmPortSel == sdrplay_api_RspDuo_AMPORT_1) {
            return "Tuner 1 Hi-Z";
        }
        else if (device.tuner == sdrplay_api_Tuner_A) {
            return "Tuner 1 50 ohm";
        }
        else {
            return "Tuner 2 50 ohm";  
        }
    }
    else
    {
        return "RX";
    }
}

/*******************************************************************
 * Frontend corrections API
 ******************************************************************/

bool SoapySDRPlay3::hasDCOffsetMode(const int direction, const size_t channel) const
{
    return true;
}

void SoapySDRPlay3::setDCOffsetMode(const int direction, const size_t channel, const bool automatic)
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

    //enable/disable automatic DC removal
    chParams->ctrlParams.dcOffset.DCenable = (unsigned char)automatic;
    chParams->ctrlParams.dcOffset.IQenable = (unsigned char)automatic;
}

bool SoapySDRPlay3::getDCOffsetMode(const int direction, const size_t channel) const
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

    return (bool)chParams->ctrlParams.dcOffset.DCenable;
}

bool SoapySDRPlay3::hasDCOffset(const int direction, const size_t channel) const
{
    //is a specific DC removal value configurable?
    return false;
}

/*******************************************************************
 * Gain API
 ******************************************************************/

std::vector<std::string> SoapySDRPlay3::listGains(const int direction, const size_t channel) const
{
    //list available gain elements,
    //the functions below have a "name" parameter
    std::vector<std::string> results;

    results.push_back("IFGR");
    results.push_back("RFGR");

    return results;
}

bool SoapySDRPlay3::hasGainMode(const int direction, const size_t channel) const
{
    return true;
}

void SoapySDRPlay3::setGainMode(const int direction, const size_t channel, const bool automatic)
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

    chParams->ctrlParams.agc.enable = sdrplay_api_AGC_DISABLE;

    if (automatic == true) {
        chParams->ctrlParams.agc.enable = sdrplay_api_AGC_100HZ;
    }
}

bool SoapySDRPlay3::getGainMode(const int direction, const size_t channel) const
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

    return (chParams->ctrlParams.agc.enable == sdrplay_api_AGC_DISABLE)? false: true;
}

void SoapySDRPlay3::setGain(const int direction, const size_t channel, const std::string &name, const double value)
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

   bool doUpdate = false;

   if (name == "IFGR")
   {
      //Depending of the previously used AGC context, the real applied 
      // gain may be either gRdB or current_gRdB, so apply the change if required value is different 
      //from one of them.
      if ((chParams->tunerParams.gain.gRdB != (int)value) || ((int)chParams->tunerParams.gain.gainVals.curr != (int)value))
      {
         chParams->tunerParams.gain.gRdB = (int)value;
         doUpdate = true;
      }
   }
   else if (name == "RFGR")
   {
      if (chParams->tunerParams.gain.LNAstate != (int)value) {

          chParams->tunerParams.gain.LNAstate = (int)value;
          chParams->ctrlParams.agc.LNAstate = chParams->tunerParams.gain.LNAstate;
          doUpdate = true;
      }
   }
   if ((doUpdate == true) && (streamActive))
   {
      sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Tuner_Gr);
   }
}

double SoapySDRPlay3::getGain(const int direction, const size_t channel, const std::string &name) const
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

   if (name == "IFGR")
   {
       return chParams->tunerParams.gain.gainVals.curr;
   }
   else if (name == "RFGR")
   {
      return chParams->tunerParams.gain.LNAstate;
   }

   return 0;
}

SoapySDR::Range SoapySDRPlay3::getGainRange(const int direction, const size_t channel, const std::string &name) const
{
   if (name == "IFGR")
   {
      return SoapySDR::Range(20, 59);
   }
   else if ((name == "RFGR") && (device.hwVer == SDRPLAY_RSP1_ID))
   {
      return SoapySDR::Range(0, 3);
   }
   else if ((name == "RFGR") && (device.hwVer == SDRPLAY_RSP2_ID))
   {
      return SoapySDR::Range(0, 8);
   }
   else if ((name == "RFGR") && (device.hwVer == SDRPLAY_RSPduo_ID))
   {
      return SoapySDR::Range(0, 9);
   }
   else if ((name == "RFGR") && (device.hwVer == SDRPLAY_RSP1A_ID))
   {
      return SoapySDR::Range(0, 9);
   }
    return SoapySDR::Range(20, 59);
}

/*******************************************************************
 * Frequency API
 ******************************************************************/

void SoapySDRPlay3::setFrequency(const int direction,
                                 const size_t channel,
                                 const std::string &name,
                                 const double frequency,
                                 const SoapySDR::Kwargs &args)
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

   if (direction == SOAPY_SDR_RX)
   {
      if ((name == "RF") && (chParams->tunerParams.rfFreq.rfHz != (uint32_t)frequency))
      {
         chParams->tunerParams.rfFreq.rfHz = (uint32_t)frequency;
         if (streamActive)
         {
            sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Tuner_Frf);
         }
      }
      else if ((name == "CORR") && (deviceParams->devParams->ppm != frequency))
      {
         deviceParams->devParams->ppm = frequency;
         if (streamActive)
         {
            sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Dev_Ppm);
         }
      }
   }
}

double SoapySDRPlay3::getFrequency(const int direction, const size_t channel, const std::string &name) const
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

    if (name == "RF")
    {
        return (double)chParams->tunerParams.rfFreq.rfHz;
    }
    else if (name == "CORR")
    {
        return deviceParams->devParams->ppm;
    }

    return 0;
}

std::vector<std::string> SoapySDRPlay3::listFrequencies(const int direction, const size_t channel) const
{
    std::vector<std::string> names;
    names.push_back("RF");
    names.push_back("CORR");
    return names;
}

SoapySDR::RangeList SoapySDRPlay3::getFrequencyRange(const int direction, const size_t channel,  const std::string &name) const
{
    SoapySDR::RangeList results;
    if (name == "RF")
    {
       results.push_back(SoapySDR::Range(10000, 2000000000));
    }
    return results;
}

SoapySDR::ArgInfoList SoapySDRPlay3::getFrequencyArgsInfo(const int direction, const size_t channel) const
{
    SoapySDR::ArgInfoList freqArgs;

    return freqArgs;
}

/*******************************************************************
 * Sample Rate API
 ******************************************************************/

void SoapySDRPlay3::setSampleRate(const int direction, const size_t channel, const double rate)
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

    SoapySDR_logf(SOAPY_SDR_DEBUG, "Setting sample rate: %d", deviceParams->devParams->fsFreq.fsHz);

    if (direction == SOAPY_SDR_RX)
    {
       reqSampleRate = (uint32_t)rate;

       unsigned int decM;
       unsigned int decEnable;
       uint32_t sampleRate = getInputSampleRateAndDecimation(reqSampleRate, &decM, &decEnable, chParams->tunerParams.ifType);
       chParams->tunerParams.bwType = getBwEnumForRate(rate, chParams->tunerParams.ifType);

       if ((sampleRate != deviceParams->devParams->fsFreq.fsHz) || (decM != chParams->ctrlParams.decimation.decimationFactor) || (reqSampleRate != sampleRate))
       {
          deviceParams->devParams->fsFreq.fsHz = sampleRate;
          chParams->ctrlParams.decimation.enable = decEnable;
          chParams->ctrlParams.decimation.decimationFactor = decM;
          if (chParams->tunerParams.ifType == sdrplay_api_IF_Zero) {
              chParams->ctrlParams.decimation.wideBandSignal = 1;
          }
          else {
              chParams->ctrlParams.decimation.wideBandSignal = 0;
          }
          if (_bufA) { _bufA->reset = true; }
          if (_bufB) { _bufB->reset = true; }
          if (streamActive)
          {
             // beware that when the fs change crosses the boundary between
             // 2,685,312 and 2,685,313 the rx_callbacks stop for some
             // reason
             sdrplay_api_Update(device.dev, device.tuner, (sdrplay_api_ReasonForUpdateT) (sdrplay_api_Update_Dev_Fs | sdrplay_api_Update_Ctrl_Decimation));
          }
       }
    }
}

double SoapySDRPlay3::getSampleRate(const int direction, const size_t channel) const
{
   return reqSampleRate;
}

std::vector<double> SoapySDRPlay3::listSampleRates(const int direction, const size_t channel) const
{
    std::vector<double> rates;

    rates.push_back(250000);
    rates.push_back(500000);
    rates.push_back(1000000);
    rates.push_back(2000000);
    rates.push_back(2048000);
    rates.push_back(3000000);
    rates.push_back(4000000);
    rates.push_back(5000000);
    rates.push_back(6000000);
    rates.push_back(7000000);
    rates.push_back(8000000);
    rates.push_back(9000000);
    rates.push_back(10000000);
    
    return rates;
}

uint32_t SoapySDRPlay3::getInputSampleRateAndDecimation(uint32_t rate, unsigned int *decM, unsigned int *decEnable, sdrplay_api_If_kHzT ifType)
{
   if (ifType == sdrplay_api_IF_2_048)
   {
      if      (rate == 2048000) { *decM = 4; *decEnable = 1; return 8192000; }
   }
   else if (ifType == sdrplay_api_IF_0_450)
   {
      if      (rate == 1000000) { *decM = 2; *decEnable = 1; return 2000000; }
      else if (rate == 500000)  { *decM = 4; *decEnable = 1; return 2000000; }
   }
   else if (ifType == sdrplay_api_IF_Zero)
   {

      if      ((rate >= 200000)  && (rate < 500000))  { *decM = 8; *decEnable = 1; return 2000000; }
      else if ((rate >= 500000)  && (rate < 1000000)) { *decM = 4; *decEnable = 1; return 2000000; }
      else if ((rate >= 1000000) && (rate < 2000000)) { *decM = 2; *decEnable = 1; return 2000000; }
      else                                            { *decM = 1; *decEnable = 0; return rate; }
   }

   // this is invalid, but return something
   *decM = 1; *decEnable = 0; return rate;
}

/*******************************************************************
* Bandwidth API
******************************************************************/

void SoapySDRPlay3::setBandwidth(const int direction, const size_t channel, const double bw_in)
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

   if (direction == SOAPY_SDR_RX) 
   {
      if (getBwValueFromEnum(chParams->tunerParams.bwType) != bw_in)
      {
         chParams->tunerParams.bwType = sdrPlayGetBwMhzEnum(bw_in);
         if (streamActive)
         {
            sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Tuner_BwType);
         }
      }
   }
}

double SoapySDRPlay3::getBandwidth(const int direction, const size_t channel) const
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

   if (direction == SOAPY_SDR_RX)
   {
      return getBwValueFromEnum(chParams->tunerParams.bwType);
   }
   return 0;
}

std::vector<double> SoapySDRPlay3::listBandwidths(const int direction, const size_t channel) const
{
   std::vector<double> bandwidths;
   bandwidths.push_back(200000);
   bandwidths.push_back(300000);
   bandwidths.push_back(600000);
   bandwidths.push_back(1536000);
   bandwidths.push_back(5000000);
   bandwidths.push_back(6000000);
   bandwidths.push_back(7000000);
   bandwidths.push_back(8000000);
   return bandwidths;
}

SoapySDR::RangeList SoapySDRPlay3::getBandwidthRange(const int direction, const size_t channel) const
{
   SoapySDR::RangeList results;
   //call into the older deprecated listBandwidths() call
   for (auto &bw : this->listBandwidths(direction, channel))
   {
     results.push_back(SoapySDR::Range(bw, bw));
   }
   return results;
}

double SoapySDRPlay3::getRateForBwEnum(sdrplay_api_Bw_MHzT bwEnum)
{
   if (bwEnum == sdrplay_api_BW_0_200) return 250000;
   else if (bwEnum == sdrplay_api_BW_0_300) return 500000;
   else if (bwEnum == sdrplay_api_BW_0_600) return 1000000;
   else if (bwEnum == sdrplay_api_BW_1_536) return 2000000;
   else if (bwEnum == sdrplay_api_BW_5_000) return 5000000;
   else if (bwEnum == sdrplay_api_BW_6_000) return 6000000;
   else if (bwEnum == sdrplay_api_BW_7_000) return 7000000;
   else if (bwEnum == sdrplay_api_BW_8_000) return 8000000;
   else return 0;
}


sdrplay_api_Bw_MHzT SoapySDRPlay3::getBwEnumForRate(double rate, sdrplay_api_If_kHzT ifType)
{
   if (ifType == sdrplay_api_IF_Zero)
   {
      if      ((rate >= 200000)  && (rate < 300000))  return sdrplay_api_BW_0_200;
      else if ((rate >= 300000)  && (rate < 600000))  return sdrplay_api_BW_0_300;
      else if ((rate >= 600000)  && (rate < 1536000)) return sdrplay_api_BW_0_600;
      else if ((rate >= 1536000) && (rate < 5000000)) return sdrplay_api_BW_1_536;
      else if ((rate >= 5000000) && (rate < 6000000)) return sdrplay_api_BW_5_000;
      else if ((rate >= 6000000) && (rate < 7000000)) return sdrplay_api_BW_6_000;
      else if ((rate >= 7000000) && (rate < 8000000)) return sdrplay_api_BW_7_000;
      else                                            return sdrplay_api_BW_8_000;
   }
   else if ((ifType == sdrplay_api_IF_0_450) || (ifType == sdrplay_api_IF_1_620))
   {
      if      ((rate >= 200000)  && (rate < 500000))  return sdrplay_api_BW_0_200;
      else if ((rate >= 500000)  && (rate < 1000000)) return sdrplay_api_BW_0_300;
      else                                            return sdrplay_api_BW_0_600;
   }
   else
   {
      if      ((rate >= 200000)  && (rate < 500000))  return sdrplay_api_BW_0_200;
      else if ((rate >= 500000)  && (rate < 1000000)) return sdrplay_api_BW_0_300;
      else if ((rate >= 1000000) && (rate < 1536000)) return sdrplay_api_BW_0_600;
      else                                            return sdrplay_api_BW_1_536;
   }
}


double SoapySDRPlay3::getBwValueFromEnum(sdrplay_api_Bw_MHzT bwEnum)
{
   if      (bwEnum == sdrplay_api_BW_0_200) return 200000;
   else if (bwEnum == sdrplay_api_BW_0_300) return 300000;
   else if (bwEnum == sdrplay_api_BW_0_600) return 600000;
   else if (bwEnum == sdrplay_api_BW_1_536) return 1536000;
   else if (bwEnum == sdrplay_api_BW_5_000) return 5000000;
   else if (bwEnum == sdrplay_api_BW_6_000) return 6000000;
   else if (bwEnum == sdrplay_api_BW_7_000) return 7000000;
   else if (bwEnum == sdrplay_api_BW_8_000) return 8000000;
   else return 0;
}


sdrplay_api_Bw_MHzT SoapySDRPlay3::sdrPlayGetBwMhzEnum(double bw)
{
   if      (bw == 200000) return sdrplay_api_BW_0_200;
   else if (bw == 300000) return sdrplay_api_BW_0_300;
   else if (bw == 600000) return sdrplay_api_BW_0_600;
   else if (bw == 1536000) return sdrplay_api_BW_1_536;
   else if (bw == 5000000) return sdrplay_api_BW_5_000;
   else if (bw == 6000000) return sdrplay_api_BW_6_000;
   else if (bw == 7000000) return sdrplay_api_BW_7_000;
   else if (bw == 8000000) return sdrplay_api_BW_8_000;
   else return sdrplay_api_BW_0_200;
}

/*******************************************************************
* Settings API
******************************************************************/

sdrplay_api_TunerSelectT SoapySDRPlay3::rspDuoModeStringToTuner(std::string rspDuoMode)
{
   if (rspDuoMode == "Tuner A (Single Tuner)" || rspDuoMode == "Tuner A (Master/Slave)")
   {
      return sdrplay_api_Tuner_A;
   }
   else if (rspDuoMode == "Tuner B (Single Tuner)" || rspDuoMode == "Tuner B (Master/Slave)")
   {
      return sdrplay_api_Tuner_A;
   }
   else if (rspDuoMode == "Dual Tuner")
   {
      return sdrplay_api_Tuner_Both;
   }
   return sdrplay_api_Tuner_Neither;
}

sdrplay_api_RspDuoModeT SoapySDRPlay3::rspDuoModeStringToRspDuoMode(std::string rspDuoMode)
{
   if (rspDuoMode == "Tuner A (Single Tuner)" || rspDuoMode == "Tuner B (Single Tuner)")
   {
      return sdrplay_api_RspDuoMode_Single_Tuner;
   }
   else if (rspDuoMode == "Tuner A (Master/Slave)" || rspDuoMode == "Tuner B (Master/Slave)")
   {
      return (sdrplay_api_RspDuoModeT) (sdrplay_api_RspDuoMode_Master | sdrplay_api_RspDuoMode_Slave);
   }
   else if (rspDuoMode == "Dual Tuner")
   {
      return sdrplay_api_RspDuoMode_Dual_Tuner;
   }
   return sdrplay_api_RspDuoMode_Unknown;
}

std::string SoapySDRPlay3::rspDuoModetoString(sdrplay_api_TunerSelectT tuner, sdrplay_api_RspDuoModeT rspDuoMode)
{
   switch (rspDuoMode)
   {
   case sdrplay_api_RspDuoMode_Single_Tuner:
      if (tuner == sdrplay_api_Tuner_A) return "Tuner A (Single Tuner)";
      if (tuner == sdrplay_api_Tuner_B) return "Tuner B (Single Tuner)";
      break;
   case sdrplay_api_RspDuoMode_Dual_Tuner:
      return "Dual Tuner";
      break;
   case sdrplay_api_RspDuoMode_Master:
   case sdrplay_api_RspDuoMode_Slave:
      if (tuner == sdrplay_api_Tuner_A) return "Tuner A (Master/Slave)";
      if (tuner == sdrplay_api_Tuner_B) return "Tuner B (Master/Slave)";
      break;
   case sdrplay_api_RspDuoMode_Unknown:
      return "";
      break;
   }
   return "";
}

sdrplay_api_If_kHzT SoapySDRPlay3::stringToIF(std::string ifType)
{
   if (ifType == "Zero-IF")
   {
      return sdrplay_api_IF_Zero;
   }
   else if (ifType == "450kHz")
   {
      return sdrplay_api_IF_0_450;
   }
   else if (ifType == "1620kHz")
   {
      return sdrplay_api_IF_1_620;
   }
   else if (ifType == "2048kHz")
   {
      return sdrplay_api_IF_2_048;
   }
   return sdrplay_api_IF_Zero;
}

std::string SoapySDRPlay3::IFtoString(sdrplay_api_If_kHzT ifkHzT)
{
   switch (ifkHzT)
   {
   case sdrplay_api_IF_Zero:
      return "Zero-IF";
      break;
   case sdrplay_api_IF_0_450:
      return "450kHz";
      break;
   case sdrplay_api_IF_1_620:
      return "1620kHz";
      break;
   case sdrplay_api_IF_2_048:
      return "2048kHz";
      break;
   case sdrplay_api_IF_Undefined:
      return "";
      break;
   }
   return "";
}

SoapySDR::ArgInfoList SoapySDRPlay3::getSettingInfo(void) const
{
    SoapySDR::ArgInfoList setArgs;
 
    if (device.hwVer == SDRPLAY_RSPduo_ID)
    {
       SoapySDR::ArgInfo RspDuoMode;
       RspDuoMode.key = "rspduo_mode";
       RspDuoMode.value = "Tuner A (Single Tuner)";
       RspDuoMode.name = "RSP Duo Mode";
       RspDuoMode.description = "RSP Duo Mode";
       RspDuoMode.type = SoapySDR::ArgInfo::STRING;
       RspDuoMode.options.push_back("Tuner A (Single Tuner)");
       RspDuoMode.options.push_back("Tuner A (Master/Slave)");
       RspDuoMode.options.push_back("Tuner B (Single Tuner)");
       RspDuoMode.options.push_back("Tuner B (Master/Slave)");
       RspDuoMode.options.push_back("Dual Tuner");
       setArgs.push_back(RspDuoMode);
    }

#ifdef RF_GAIN_IN_MENU
    if (device.hwVer == SDRPLAY_RSP2_ID)
    {
       SoapySDR::ArgInfo RfGainArg;
       RfGainArg.key = "rfgain_sel";
       RfGainArg.value = "4";
       RfGainArg.name = "RF Gain Select";
       RfGainArg.description = "RF Gain Select";
       RfGainArg.type = SoapySDR::ArgInfo::STRING;
       RfGainArg.options.push_back("0");
       RfGainArg.options.push_back("1");
       RfGainArg.options.push_back("2");
       RfGainArg.options.push_back("3");
       RfGainArg.options.push_back("4");
       RfGainArg.options.push_back("5");
       RfGainArg.options.push_back("6");
       RfGainArg.options.push_back("7");
       RfGainArg.options.push_back("8");
       setArgs.push_back(RfGainArg);
    }
    else if (device.hwVer == SDRPLAY_RSPduo_ID)
    {
       SoapySDR::ArgInfo RfGainArg;
       RfGainArg.key = "rfgain_sel";
       RfGainArg.value = "4";
       RfGainArg.name = "RF Gain Select";
       RfGainArg.description = "RF Gain Select";
       RfGainArg.type = SoapySDR::ArgInfo::STRING;
       RfGainArg.options.push_back("0");
       RfGainArg.options.push_back("1");
       RfGainArg.options.push_back("2");
       RfGainArg.options.push_back("3");
       RfGainArg.options.push_back("4");
       RfGainArg.options.push_back("5");
       RfGainArg.options.push_back("6");
       RfGainArg.options.push_back("7");
       RfGainArg.options.push_back("8");
       RfGainArg.options.push_back("9");
       setArgs.push_back(RfGainArg);
    }
    else if (device.hwVer == SDRPLAY_RSP1A_ID)
    {
       SoapySDR::ArgInfo RfGainArg;
       RfGainArg.key = "rfgain_sel";
       RfGainArg.value = "4";
       RfGainArg.name = "RF Gain Select";
       RfGainArg.description = "RF Gain Select";
       RfGainArg.type = SoapySDR::ArgInfo::STRING;
       RfGainArg.options.push_back("0");
       RfGainArg.options.push_back("1");
       RfGainArg.options.push_back("2");
       RfGainArg.options.push_back("3");
       RfGainArg.options.push_back("4");
       RfGainArg.options.push_back("5");
       RfGainArg.options.push_back("6");
       RfGainArg.options.push_back("7");
       RfGainArg.options.push_back("8");
       RfGainArg.options.push_back("9");
       setArgs.push_back(RfGainArg);
    }
    else
    {
       SoapySDR::ArgInfo RfGainArg;
       RfGainArg.key = "rfgain_sel";
       RfGainArg.value = "1";
       RfGainArg.name = "RF Gain Select";
       RfGainArg.description = "RF Gain Select";
       RfGainArg.type = SoapySDR::ArgInfo::STRING;
       RfGainArg.options.push_back("0");
       RfGainArg.options.push_back("1");
       RfGainArg.options.push_back("2");
       RfGainArg.options.push_back("3");
       setArgs.push_back(RfGainArg);
    }
#endif
    
    SoapySDR::ArgInfo AIFArg;
    AIFArg.key = "if_mode";
    AIFArg.value = IFtoString(chParams->tunerParams.ifType);
    AIFArg.name = "IF Mode";
    AIFArg.description = "IF frequency in kHz";
    AIFArg.type = SoapySDR::ArgInfo::STRING;
    AIFArg.options.push_back(IFtoString(sdrplay_api_IF_Zero));
    AIFArg.options.push_back(IFtoString(sdrplay_api_IF_0_450));
    AIFArg.options.push_back(IFtoString(sdrplay_api_IF_1_620));
    AIFArg.options.push_back(IFtoString(sdrplay_api_IF_2_048));
    setArgs.push_back(AIFArg);

    SoapySDR::ArgInfo IQcorrArg;
    IQcorrArg.key = "iqcorr_ctrl";
    IQcorrArg.value = "true";
    IQcorrArg.name = "IQ Correction";
    IQcorrArg.description = "IQ Correction Control";
    IQcorrArg.type = SoapySDR::ArgInfo::BOOL;
    setArgs.push_back(IQcorrArg);

    SoapySDR::ArgInfo SetPointArg;
    SetPointArg.key = "agc_setpoint";
    SetPointArg.value = "-30";
    SetPointArg.name = "AGC Setpoint";
    SetPointArg.description = "AGC Setpoint (dBfs)";
    SetPointArg.type = SoapySDR::ArgInfo::INT;
    SetPointArg.range = SoapySDR::Range(-60, 0);
    setArgs.push_back(SetPointArg);

    if (device.hwVer == SDRPLAY_RSP2_ID) // RSP2/RSP2pro
    {
       SoapySDR::ArgInfo ExtRefArg;
       ExtRefArg.key = "extref_ctrl";
       ExtRefArg.value = "true";
       ExtRefArg.name = "ExtRef Enable";
       ExtRefArg.description = "External Reference Control";
       ExtRefArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(ExtRefArg);

       SoapySDR::ArgInfo BiasTArg;
       BiasTArg.key = "biasT_ctrl";
       BiasTArg.value = "true";
       BiasTArg.name = "BiasT Enable";
       BiasTArg.description = "BiasT Control";
       BiasTArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(BiasTArg);

       SoapySDR::ArgInfo RfNotchArg;
       RfNotchArg.key = "rfnotch_ctrl";
       RfNotchArg.value = "true";
       RfNotchArg.name = "RfNotch Enable";
       RfNotchArg.description = "RF Notch Filter Control";
       RfNotchArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(RfNotchArg);
    }
    else if (device.hwVer == SDRPLAY_RSPduo_ID) // RSPduo
    {
       SoapySDR::ArgInfo ExtRefArg;
       ExtRefArg.key = "extref_ctrl";
       ExtRefArg.value = "true";
       ExtRefArg.name = "ExtRef Enable";
       ExtRefArg.description = "External Reference Control";
       ExtRefArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(ExtRefArg);

       SoapySDR::ArgInfo BiasTArg;
       BiasTArg.key = "biasT_ctrl";
       BiasTArg.value = "true";
       BiasTArg.name = "BiasT Enable";
       BiasTArg.description = "BiasT Control";
       BiasTArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(BiasTArg);

       SoapySDR::ArgInfo RfNotchArg;
       RfNotchArg.key = "rfnotch_ctrl";
       RfNotchArg.value = "true";
       RfNotchArg.name = "RfNotch Enable";
       RfNotchArg.description = "RF Notch Filter Control";
       RfNotchArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(RfNotchArg);

       SoapySDR::ArgInfo DabNotchArg;
       DabNotchArg.key = "dabnotch_ctrl";
       DabNotchArg.value = "true";
       DabNotchArg.name = "DabNotch Enable";
       DabNotchArg.description = "DAB Notch Filter Control";
       DabNotchArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(DabNotchArg);
    }
    else if (device.hwVer == SDRPLAY_RSP1A_ID) // RSP1A
    {
       SoapySDR::ArgInfo BiasTArg;
       BiasTArg.key = "biasT_ctrl";
       BiasTArg.value = "true";
       BiasTArg.name = "BiasT Enable";
       BiasTArg.description = "BiasT Control";
       BiasTArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(BiasTArg);

       SoapySDR::ArgInfo RfNotchArg;
       RfNotchArg.key = "rfnotch_ctrl";
       RfNotchArg.value = "true";
       RfNotchArg.name = "RfNotch Enable";
       RfNotchArg.description = "RF Notch Filter Control";
       RfNotchArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(RfNotchArg);

       SoapySDR::ArgInfo DabNotchArg;
       DabNotchArg.key = "dabnotch_ctrl";
       DabNotchArg.value = "true";
       DabNotchArg.name = "DabNotch Enable";
       DabNotchArg.description = "DAB Notch Filter Control";
       DabNotchArg.type = SoapySDR::ArgInfo::BOOL;
       setArgs.push_back(DabNotchArg);
    }

    return setArgs;
}

void SoapySDRPlay3::writeSetting(const std::string &key, const std::string &value)
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

   if (device.hwVer == SDRPLAY_RSPduo_ID && key == "rspduo_mode")
   {
      changeRspDuoMode(value);
   }

#ifdef RF_GAIN_IN_MENU
   if (key == "rfgain_sel")
   {
      if      (value == "0") chParams->tunerParams.gain.LNAstate = 0;
      else if (value == "1") chParams->tunerParams.gain.LNAstate = 1;
      else if (value == "2") chParams->tunerParams.gain.LNAstate = 2;
      else if (value == "3") chParams->tunerParams.gain.LNAstate = 3;
      else if (value == "4") chParams->tunerParams.gain.LNAstate = 4;
      else if (value == "5") chParams->tunerParams.gain.LNAstate = 5;
      else if (value == "6") chParams->tunerParams.gain.LNAstate = 6;
      else if (value == "7") chParams->tunerParams.gain.LNAstate = 7;
      else if (value == "8") chParams->tunerParams.gain.LNAstate = 8;
      else                   chParams->tunerParams.gain.LNAstate = 9;
      chParams->ctrlParams.agc.LNAstate = chParams->tunerParams.gain.LNAstate;
      if (chParams->ctrlParams.agc.enable == sdrplay_api_AGC_DISABLE)
      {
         sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Tuner_Gr);
      }
   }
   else
#endif
   if (key == "if_mode")
   {
      if (chParams->tunerParams.ifType != stringToIF(value))
      {
         chParams->tunerParams.ifType = stringToIF(value);
         unsigned int decM;
         unsigned int decEnable;
         uint32_t sampleRate = getInputSampleRateAndDecimation(reqSampleRate, &decM, &decEnable, chParams->tunerParams.ifType);
         deviceParams->devParams->fsFreq.fsHz = sampleRate;
         chParams->tunerParams.bwType = getBwEnumForRate(reqSampleRate, chParams->tunerParams.ifType);
         if (streamActive)
         {
            chParams->ctrlParams.decimation.enable = 0;
            chParams->ctrlParams.decimation.decimationFactor = 1;
            chParams->ctrlParams.decimation.wideBandSignal = 1;
            sdrplay_api_Update(device.dev, device.tuner, (sdrplay_api_ReasonForUpdateT) (sdrplay_api_Update_Dev_Fs | sdrplay_api_Update_Tuner_BwType | sdrplay_api_Update_Tuner_IfType));
         }
      }
   }
   else if (key == "iqcorr_ctrl")
   {
      if (value == "false") chParams->ctrlParams.dcOffset.IQenable = 0;
      else                  chParams->ctrlParams.dcOffset.IQenable = 1;
      chParams->ctrlParams.dcOffset.DCenable = 1;
      if (streamActive)
      {
         sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Ctrl_DCoffsetIQimbalance);
      }
   }
   else if (key == "agc_setpoint")
   {
      chParams->ctrlParams.agc.setPoint_dBfs = stoi(value);
      if (streamActive)
      {
         sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Ctrl_Agc);
      }
   }
   else if (key == "extref_ctrl")
   {
      unsigned char extRef;
      if (value == "false") extRef = 0;
      else                  extRef = 1;
      if (device.hwVer == SDRPLAY_RSP2_ID)
      {
         deviceParams->devParams->rsp2Params.extRefOutputEn = extRef;
         if (streamActive)
         {
            sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Rsp2_ExtRefControl);
         }
      }
      if (device.hwVer == SDRPLAY_RSPduo_ID)
      {
         deviceParams->devParams->rspDuoParams.extRefOutputEn = extRef;
         if (streamActive)
         {
            sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_RspDuo_ExtRefControl);
         }
      }
   }
   else if (key == "biasT_ctrl")
   {
      unsigned char biasTen;
      if (value == "false") biasTen = 0;
      else                  biasTen = 1;
      if (device.hwVer == SDRPLAY_RSP2_ID)
      {
         chParams->rsp2TunerParams.biasTEnable = biasTen;
         if (streamActive)
         {
            sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Rsp2_BiasTControl);
         }
      }
      if (device.hwVer == SDRPLAY_RSPduo_ID)
      {
         chParams->rspDuoTunerParams.biasTEnable = biasTen;
         if (streamActive)
         {
            sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_RspDuo_BiasTControl);
         }
      }
      if (device.hwVer == SDRPLAY_RSP1A_ID)
      {
         chParams->rsp1aTunerParams.biasTEnable = biasTen;
         if (streamActive)
         {
            sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Rsp1a_BiasTControl);
         }
      }
   }
   else if (key == "rfnotch_ctrl")
   {
      unsigned char notchEn;
      if (value == "false") notchEn = 0;
      else                  notchEn = 1;
      if (device.hwVer == SDRPLAY_RSP2_ID)
      {
         chParams->rsp2TunerParams.rfNotchEnable = notchEn;
         if (streamActive)
         {
            sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Rsp2_RfNotchControl);
         }
      }
      if (device.hwVer == SDRPLAY_RSPduo_ID)
      {
        if (device.tuner == sdrplay_api_Tuner_A && chParams->rspDuoTunerParams.tuner1AmPortSel == sdrplay_api_RspDuo_AMPORT_1)
        {
          chParams->rspDuoTunerParams.tuner1AmNotchEnable = notchEn;
          if (streamActive)
          {
             sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_RspDuo_Tuner1AmNotchControl);
          }
        }
        if (chParams->rspDuoTunerParams.tuner1AmPortSel == sdrplay_api_RspDuo_AMPORT_2)
        {
          chParams->rspDuoTunerParams.rfNotchEnable = notchEn;
          if (streamActive)
          {
             sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_RspDuo_RfNotchControl);
          }
        }
      }
      if (device.hwVer == SDRPLAY_RSP1A_ID)
      {
         deviceParams->devParams->rsp1aParams.rfNotchEnable = notchEn;
         if (streamActive)
         {
            sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Rsp1a_RfNotchControl);
         }
      }
   }
   else if (key == "dabnotch_ctrl")
   {
      unsigned char dabNotchEn;
      if (value == "false") dabNotchEn = 0;
      else                  dabNotchEn = 1;
      if (device.hwVer == SDRPLAY_RSPduo_ID)
      {
         chParams->rspDuoTunerParams.rfDabNotchEnable = dabNotchEn;
         if (streamActive)
         {
            sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_RspDuo_RfDabNotchControl);
         }
      }
      if (device.hwVer == SDRPLAY_RSP1A_ID)
      {
         deviceParams->devParams->rsp1aParams.rfDabNotchEnable = dabNotchEn;
         if (streamActive)
         {
            sdrplay_api_Update(device.dev, device.tuner, sdrplay_api_Update_Rsp1a_RfDabNotchControl);
         }
      }
   }
}

void SoapySDRPlay3::changeRspDuoMode(const std::string &rspDuoModeString)
{
    sdrplay_api_TunerSelectT tuner = rspDuoModeStringToTuner(rspDuoModeString);
    sdrplay_api_RspDuoModeT rspDuoMode = rspDuoModeStringToRspDuoMode(rspDuoModeString);
    if ((rspDuoMode & sdrplay_api_RspDuoMode_Master) && (device.rspDuoMode & sdrplay_api_RspDuoMode_Master))
    {
        rspDuoMode = sdrplay_api_RspDuoMode_Master;
    }
    else if ((rspDuoMode & sdrplay_api_RspDuoMode_Slave) && (device.rspDuoMode & sdrplay_api_RspDuoMode_Slave))
    {
        rspDuoMode = sdrplay_api_RspDuoMode_Slave;
    }
    // if master device is available, select device as master
    else if (rspDuoMode & sdrplay_api_RspDuoMode_Master)
    {
        rspDuoMode = sdrplay_api_RspDuoMode_Master;
    }
    if (rspDuoMode == device.rspDuoMode && tuner != device.tuner)
    {
        device.tuner = tuner;
        sdrplay_api_SwapRspDuoActiveTuner(device.dev, &device.tuner, chParams->rspDuoTunerParams.tuner1AmPortSel);
    }
    else if (rspDuoMode != device.rspDuoMode)
    {
        sdrplay_api_ErrT err;

        SoapySDR_logf(SOAPY_SDR_INFO, "Changed RSPduo mode - going to run ReleaseDevice+SelectDevice");
        if (streamActive)
        {
            sdrplay_api_Uninit(device.dev);
        }
        streamActive = false;
        sdrplay_api_ReleaseDevice(&device);
        _bufA = 0;
        _bufB = 0;
        err = sdrplay_api_SelectDevice(&device);
        if (err != sdrplay_api_Success)
        {
            SoapySDR_logf(SOAPY_SDR_ERROR, "SelectDevice Error: %s", sdrplay_api_GetErrorString(err));
            throw std::runtime_error("SelectDevice() failed");
        }
    }
}

std::string SoapySDRPlay3::readSetting(const std::string &key) const
{
    std::lock_guard <std::mutex> lock(_general_state_mutex);

    if (device.hwVer == SDRPLAY_RSPduo_ID && key == "rspduo_mode")
    {
       return rspDuoModetoString(device.tuner, device.rspDuoMode);
    }

#ifdef RF_GAIN_IN_MENU
    if (key == "rfgain_sel")
    {
       if      (chParams->tunerParams.gain.LNAstate == 0) return "0";
       else if (chParams->tunerParams.gain.LNAstate == 1) return "1";
       else if (chParams->tunerParams.gain.LNAstate == 2) return "2";
       else if (chParams->tunerParams.gain.LNAstate == 3) return "3";
       else if (chParams->tunerParams.gain.LNAstate == 4) return "4";
       else if (chParams->tunerParams.gain.LNAstate == 5) return "5";
       else if (chParams->tunerParams.gain.LNAstate == 6) return "6";
       else if (chParams->tunerParams.gain.LNAstate == 7) return "7";
       else if (chParams->tunerParams.gain.LNAstate == 8) return "8";
       else                                               return "9";
    }
    else
#endif
    if (key == "if_mode")
    {
        return IFtoString(chParams->tunerParams.ifType);
    }
    else if (key == "iqcorr_ctrl")
    {
       if (chParams->ctrlParams.dcOffset.IQenable == 0) return "false";
       else                                             return "true";
    }
    else if (key == "agc_setpoint")
    {
       return std::to_string(chParams->ctrlParams.agc.setPoint_dBfs);
    }
    else if (key == "extref_ctrl")
    {
       unsigned char extRef = 0;
       if (device.hwVer == SDRPLAY_RSP2_ID) extRef = deviceParams->devParams->rsp2Params.extRefOutputEn;
      if (device.hwVer == SDRPLAY_RSPduo_ID) extRef = deviceParams->devParams->rspDuoParams.extRefOutputEn;
       if (extRef == 0) return "false";
       else             return "true";
    }
    else if (key == "biasT_ctrl")
    {
       unsigned char biasTen = 0;
       if (device.hwVer == SDRPLAY_RSP2_ID) biasTen = chParams->rsp2TunerParams.biasTEnable;
       if (device.hwVer == SDRPLAY_RSPduo_ID) biasTen = chParams->rspDuoTunerParams.biasTEnable;
       if (device.hwVer == SDRPLAY_RSP1A_ID) biasTen = chParams->rsp1aTunerParams.biasTEnable;
       if (biasTen == 0) return "false";
       else              return "true";
    }
    else if (key == "rfnotch_ctrl")
    {
       unsigned char notchEn = 0;
       if (device.hwVer == SDRPLAY_RSP2_ID) notchEn = chParams->rsp2TunerParams.rfNotchEnable;
       if (device.hwVer == SDRPLAY_RSPduo_ID)
       {
          if (device.tuner == sdrplay_api_Tuner_A && chParams->rspDuoTunerParams.tuner1AmPortSel == sdrplay_api_RspDuo_AMPORT_1)
          {
             notchEn = chParams->rspDuoTunerParams.tuner1AmNotchEnable;
          }
          if (chParams->rspDuoTunerParams.tuner1AmPortSel == sdrplay_api_RspDuo_AMPORT_2)
          {
             notchEn = chParams->rspDuoTunerParams.rfNotchEnable;
          }
       }
       if (device.hwVer == SDRPLAY_RSP1A_ID) notchEn = deviceParams->devParams->rsp1aParams.rfNotchEnable;
       if (notchEn == 0) return "false";
       else              return "true";
    }
    else if (key == "dabnotch_ctrl")
    {
       unsigned char dabNotchEn = 0;
       if (device.hwVer == SDRPLAY_RSPduo_ID) dabNotchEn = chParams->rspDuoTunerParams.rfDabNotchEnable;
       if (device.hwVer == SDRPLAY_RSP1A_ID) dabNotchEn = deviceParams->devParams->rsp1aParams.rfDabNotchEnable;
       if (dabNotchEn == 0) return "false";
       else                 return "true";
    }

    // SoapySDR_logf(SOAPY_SDR_WARNING, "Unknown setting '%s'", key.c_str());
    return "";
}
