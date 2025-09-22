let PortAudio=require('./build/Release/PA.node'); 

const freq=48000;
PortAudio.initSampleBuffers(freq,freq);

PortAudio.startMic();
PortAudio.startSpeaker();

function getVolumeBar(l16sample) {
  const vol=Math.abs(l16sample);
  const bar = vol / 1024;
  const space = 32-bar;
  return "*".repeat(bar)+" ".repeat(space); 
}

setInterval(()=>{
  const samples=PortAudio.getRecordedSamples();
  let maxSample=0;
  for(let i=0;i<samples.length;i++) {
    const sample=samples[i];    
    if(sample>maxSample) maxSample=sample; 
  }
  console.log("volume:",getVolumeBar(maxSample));
  PortAudio.pushSamplesForPlay(samples);
  PortAudio.discardRecordedSamples(samples.length);  
},25);

