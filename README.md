# Soapy SDR module for SDRPlay (API version 3)

## Building Soapy SDR Play 3

```
git clone https://github.com/fventuri/SoapySDRPlay3.git
cd SoapySDRPlay3
mkdir build
cd build
cmake ..
make
sudo make install
```

## Probing Soapy SDR Play 3

```
/etc/init.d/sdrplayService start
(systemctl start sdrplay_api)

SoapySDRUtil --probe="driver=sdrplay3"

######################################################
##     Soapy SDR -- the SDR abstraction library     ##
######################################################

Probe device driver=sdrplay3

----------------------------------------------------
-- Device identification
----------------------------------------------------
  driver=SDRplay3
  hardware=**********
  sdrplay_api_api_version=3.010000
  sdrplay_api_hw_version=***
...
```


## Dependencies

* Get SDR Play driver binaries 'API/HW driver v3.x' from - http://sdrplay.com/downloads
* SoapySDR - https://github.com/pothosware/SoapySDR/wiki

## Licensing information

The MIT License (MIT)

Copyright (c) 2015 Charles J. Cliffe

Copyright (c) 2019 Franco Venturi - changes for SDRplay API version 3
                                    and Dual Tuner for RSPduo

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

