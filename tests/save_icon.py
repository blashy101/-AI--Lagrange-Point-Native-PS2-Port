"""Validate model bounds, normals, UVs, animation and every texture word."""
from pathlib import Path
from struct import unpack_from
from PIL import Image

root=Path(__file__).resolve().parents[1]
data=(root/'assets/save.icn').read_bytes()
magic,shapes,flags,brightness,count=unpack_from('<IIIfI',data)
assert (magic,shapes,flags,brightness,count)==(0x10000,1,7,1.,36)
vertices=[unpack_from('<4h4h2h4B',data,20+i*24) for i in range(count)]
# The browser's front face starts at top-left in its Y-down coordinate space.
assert all(p<0 for p in vertices[0][:3]) and vertices[0][8:10]==(0,0)
assert vertices[1][1]>0 and vertices[1][8:10]==(0,4096)
assert vertices[2][0]>0 and vertices[2][1]>0 and vertices[2][8:10]==(4096,4096)
for v in vertices:
 assert all(abs(p)<=6144 for p in v[:3])
 assert sum(n*n for n in v[4:7])==4096**2
 assert all(0<=p<=4096 for p in v[8:10])
 assert v[10:]==(128,128,128,128)
for i in range(0,count,3):
 a,b,c=vertices[i:i+3]
 u=[b[j]-a[j] for j in range(3)];v=[c[j]-a[j] for j in range(3)]
 cross=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
 assert sum(cross[j]*a[4+j] for j in range(3))>0
offset=20+24*count
assert unpack_from('<IIfII',data,offset)==(1,1,1.,0,1)
assert unpack_from('<IIff',data,offset+20)==(0,1,0.,1.)
offset+=36
assert len(data)-offset==128*128*2
source=Image.open(root/'assets/save-cover.png').convert('RGB').tobytes()
for i in range(128*128):
 word=unpack_from('<H',data,offset+2*i)[0]
 r,g,b=source[3*i:3*i+3]
 assert word==(r>>3)|((g>>3)<<5)|((b>>3)<<10)|0x8000
print('Browser model: 36 outward-facing vertices, correct UV bounds, static animation and all 16384 PSMCT16 texels passed.')
