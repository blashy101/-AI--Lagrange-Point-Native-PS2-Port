"""Exercise cold launches, both languages and interrupted writes against libmc mocks."""
from pathlib import Path
import os,subprocess,tempfile,json

def test():
 root=Path(__file__).resolve().parents[1]
 exe=root/'build/memory-card-test'
 log=[]
 with tempfile.TemporaryDirectory() as temp:
  card=Path(temp)
  def run(mode,edition,revision,**faults):
   env=dict(os.environ,CARD_ROOT=temp,**{k:str(v) for k,v in faults.items()})
   p=subprocess.run([str(exe),mode,str(edition),str(revision)],env=env,text=True,capture_output=True)
   assert p.returncode==0,(p.returncode,p.stdout,p.stderr)
   log.append(p.stdout.strip())
  run('empty',0,0)
  assert not list(card.iterdir())
  run('save',0,1)
  folder=card/'BILPNT-00100'
  assert (folder/'icon.sys').read_bytes()[:4]==b'PS2D'
  assert len((folder/'icon.sys').read_bytes())==964
  assert (folder/'COVER.ICN').read_bytes()==(root/'assets/save.icn').read_bytes()
  run('load',0,1)
  # Upgrade the previous build's descriptor without rewriting game records.
  records={p.name:p.read_bytes() for p in folder.glob('*.DAT')}
  previous_icon=b'legacy POINT.ICN model'
  (folder/'POINT.ICN').write_bytes(previous_icon)
  current_descriptor=(folder/'icon.sys').read_bytes()
  old_descriptor=current_descriptor.replace(b'COVER.ICN',b'POINT.ICN')
  (folder/'icon.sys').write_bytes(old_descriptor)
  (folder/'COVER.ICN').unlink()
  run('icon',0,1,CARD_SHORT_ICON=1,CARD_EXPECT_FAIL=1)
  assert (folder/'icon.sys').read_bytes()==old_descriptor
  assert (folder/'POINT.ICN').read_bytes()==previous_icon
  assert {p.name:p.read_bytes() for p in folder.glob('*.DAT')}==records
  run('load',0,1)
  run('icon',0,1)
  assert (folder/'icon.sys').read_bytes()==current_descriptor
  assert (folder/'COVER.ICN').read_bytes()==(root/'assets/save.icn').read_bytes()
  assert {p.name:p.read_bytes() for p in folder.glob('*.DAT')}==records
  timestamps={p.name:p.stat().st_mtime_ns for p in folder.iterdir()}
  run('icon',0,1)
  assert {p.name:p.stat().st_mtime_ns for p in folder.iterdir()}==timestamps
  run('empty',1,0)
  run('save',1,7)
  run('load',0,1);run('load',1,7)
  run('save',0,2);run('load',0,2)
  run('save',0,3,CARD_SHORT_WRITE=1,CARD_EXPECT_FAIL=1)
  run('load',0,2)
  assert 'BACKUP SAVE LOADED' in log[-1]
  run('save',0,4);run('load',0,4)
  # Corrupt the newest file; the older generation must still load.
  files=[folder/'EN0.DAT',folder/'EN1.DAT']
  newest=max(files,key=lambda p:int.from_bytes(p.read_bytes()[12:16],'little'))
  data=bytearray(newest.read_bytes());data[-1]^=1;newest.write_bytes(data)
  run('load',0,2)
  run('save',0,5,CARD_ABSENT=1,CARD_EXPECT_FAIL=1)
  run('save',0,5,CARD_UNFORMATTED=1,CARD_EXPECT_FAIL=1)
  run('save',0,5,CARD_FULL=1,CARD_EXPECT_FAIL=1)
  run('load',0,2);run('load',1,7)
  run('two',0,8);run('load',0,9)
  run('load',1,7)
  run('icon-save',0,9);run('load',0,10);run('load',1,7)
 print(json.dumps({'passed':True,'checks':log},indent=2))

if __name__=='__main__':test()
