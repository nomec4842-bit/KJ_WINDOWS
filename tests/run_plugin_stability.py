import pathlib, subprocess
root=pathlib.Path(__file__).resolve().parents[1]
build=root/'build/vst381'
plugins=[('surge_instrument','C:/Program Files/Common Files/VST3/Surge Synth Team/Surge XT.vst3','instrument'),('surge_effects','C:/Program Files/Common Files/VST3/Surge Synth Team/Surge XT Effects.vst3','effect'),('freeeq8','C:/Program Files/Common Files/VST3/FreeEQ8.vst3','effect')]
failed=False
for name,path,kind in plugins:
 logpath=build/('stability_'+name+'.log')
 with logpath.open('w') as log:
  try:
   result=subprocess.run([str(build/'bin/x64/Release/kj_vst3_probe.exe'),path,kind,'--require-audio'],cwd=build,stdout=log,stderr=subprocess.STDOUT,timeout=45)
   status='PASS' if result.returncode==0 else 'FAIL exit='+str(result.returncode)
  except subprocess.TimeoutExpired:
   status='FAIL watchdog timeout after 45 seconds';log.write('\n'+status+'\n')
 failed|=status!='PASS'
 print(name+': '+status,flush=True)
 print(logpath.read_text(),flush=True)
raise SystemExit(1 if failed else 0)
