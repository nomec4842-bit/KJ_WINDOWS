import pathlib, subprocess, sys
root=pathlib.Path(__file__).resolve().parents[1]
build=root/'build/vst381'
args=[str(build/'bin/x64/Release/kj_stability_soak.exe'),'300',str(build/'bin/x64/Release/kj_vst3_fixture.vst3'),'C:/Program Files/Common Files/VST3/Surge Synth Team/Surge XT.vst3','C:/Program Files/Common Files/VST3/Surge Synth Team/Surge XT Effects.vst3']
with (build/'stability_soak.log').open('w') as log:
 try:
  result=subprocess.run(args,cwd=build,stdout=log,stderr=subprocess.STDOUT,timeout=420)
 except subprocess.TimeoutExpired:
  log.write('\nFAIL watchdog: test process exceeded 420 seconds\n')
  sys.exit(1)
print((build/'stability_soak.log').read_text())
sys.exit(result.returncode)
