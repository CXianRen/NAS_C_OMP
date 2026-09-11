#!/usr/bin/env python3
"""Exercise automatic generation, build reuse, flags and source-set changes."""
from pathlib import Path
import json,os,shutil,subprocess,tempfile,re
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='npb-auto-make-check-') as scratch:
 d=Path(scratch);source=d/'source';source.mkdir()
 for name in ('CG','IS'):shutil.copytree(root/name,source/name)
 for name in ('common','sys','tools'):(source/name).symlink_to(root/name,target_is_directory=True)
 command=['make','-f',str(root/'Makefile'),'BENCHMARKS=CG IS','CLASS=S','-j2',f'NPB_DIR={source}',f'BUILD_DIR={d}/build',f'BIN_DIR={d}/bin']
 def make(flags='',instrument=1):
  p=subprocess.run(command+[f'CPPFLAGS={flags}',f'INSTRUMENT={instrument}'],cwd=root,capture_output=True,text=True)
  assert p.returncode==0,p.stdout+p.stderr
 def stamps():return {p.name:p.stat().st_mtime_ns for p in (d/'bin').iterdir()}
 def run_is():
  p=subprocess.run([str(d/'bin/IS.S')],cwd=d,env=dict(os.environ,NPB_TIME_REPORT='1',OMP_NUM_THREADS='4'),capture_output=True,text=True,timeout=30)
  assert p.returncode==0 and re.search(r'Verification\s*=\s*SUCCESSFUL',p.stdout),p.stdout+p.stderr
  return p.stdout
 def manifest(name):return json.loads((d/'build'/f'{name}.S/instrumented/instrumentation.json').read_text())
 make();run_is();before=stamps();make();assert before==stamps(),'unchanged build compiled again'
 print('PASS: normal make automatically generates both benchmarks; unchanged make reuses binaries',flush=True)
 previous=manifest('IS');make('-DSCHED_CYCLIC');run_is();after=manifest('IS')
 assert before!=stamps() and '-DSCHED_CYCLIC' in after['compiler_args']
 assert [r['line'] for r in previous['regions']] != [r['line'] for r in after['regions']]
 print('PASS: changed CPPFLAGS regenerates the active IS branch and passes numerical verification',flush=True)
 extra=source/'CG/src/extra.c';extra.write_text('int extra_function(void) { return 1; }\n')
 make('-DSCHED_CYCLIC');assert (d/'build/CG.S/instrumented/extra.c').exists()
 before=stamps();extra.unlink();make('-DSCHED_CYCLIC')
 assert not (d/'build/CG.S/instrumented/extra.c').exists() and before!=stamps()
 print('PASS: adding/removing a source file regenerates and removes obsolete generated C',flush=True)
 before=stamps();make('-DSCHED_CYCLIC',instrument=0)
 assert before!=stamps() and 'time report' not in run_is()
 for binary in (d/'bin').iterdir():
  symbols=subprocess.check_output(['nm',str(binary)],text=True)
  assert not re.search(r'\b(?:npb_regions|npb_region_count|npb_time_(?:active|enabled|start|stop|nowait_start|sync|read))$',symbols,re.M),symbols
 before=stamps();make('-DSCHED_CYCLIC',instrument=0)
 assert before==stamps(),'unchanged plain build compiled again'
 make('-DSCHED_CYCLIC',instrument=1)
 assert before!=stamps() and 'time report' in run_is()
 print('PASS: switching instrumentation off/on rebuilds correctly; plain binaries have no region hooks or report',flush=True)
