# PANode : Minimal PortAudio binding for Node.js

A simplest portaudio binding for node.js on Windows and macOS 

## Features

Receive samples from default audio input device(microphone) and
send samples into default output device(speaker).


## How to compile this

macOS:

```
npm install
make
```

Windows:

```
npm install
compile.bat
```

This depends on node-gyp.


## API demo: Recording and Playing 

recplay.js is a demo app.

```javascript
let PortAudio=require('./build/Release/PA.node');  // read native plugin

const freq=48000;
PortAudio.initSampleBuffers(freq,freq); // Initialize buffers in plugin

PortAudio.startMic(); // Start input from microphone
PortAudio.startSpeaker(); // Start playing with speaker

function getVolumeBar(l16sample) {
  const vol=Math.abs(l16sample);
  const bar = vol / 1024;
  const space = 32-bar;
  return "*".repeat(bar)+" ".repeat(space); 
}

setInterval(()=>{
  const samples=PortAudio.getRecordedSamples(); // Receive samples from microphone
  let maxSample=0;
  for(let i=0;i<samples.length;i++) {
    const sample=samples[i];    
    if(sample>maxSample) maxSample=sample; 
  }
  console.log("volume:",getVolumeBar(maxSample));
  PortAudio.pushSamplesForPlay(samples);  // Send samples to speaker
  PortAudio.discardRecordedSamples(samples.length);   // Discard samples in record buffer in plugin
},25);

```