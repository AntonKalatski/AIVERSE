#!/usr/bin/env python3
"""Contract checks for shipped PBR maps and project installer collision safety."""
from pathlib import Path
import json,sys,tempfile,subprocess
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[2];pack=ROOT/'engine/assets/materials/terrain'
catalog=json.loads((pack/'catalog.json').read_text());library=json.loads((pack/'materials.json').read_text())
assert catalog['default']=='checkerboard' and len(catalog['materials'])==6
for item in catalog['materials']:
 name=item['id'];prefix=pack/'textures'/name
 maps={k:np.asarray(Image.open(str(prefix)+'_'+k+'_repeat.png')) for k in ('basecolor','normal','roughness','metallic','ao','orm','height','terrain_color','terrain_normal')}
 assert all(a.shape[:2]==(1024,1024) for a in maps.values()),name
 assert np.max(maps['metallic'])==0 and np.max(maps['orm'][...,2])==0,name
 assert np.array_equal(maps['orm'][...,0],maps['ao']) and np.array_equal(maps['orm'][...,1],maps['roughness']),name
 assert np.array_equal(maps['terrain_color'][...,:3],maps['basecolor']) and np.array_equal(maps['terrain_color'][...,3],maps['ao']),name
 assert np.array_equal(maps['terrain_normal'][...,:3],maps['normal']) and np.array_equal(maps['terrain_normal'][...,3],maps['roughness']),name
 n=maps['normal'].astype(float)/255*2-1
 assert np.max(abs(np.linalg.norm(n,axis=2)-1))<.012 and n[...,2].min()>0,name
 assert maps['height'].dtype in (np.dtype('uint16'),np.dtype('int32'))
 assert maps['roughness'].min()>=127 and maps['ao'].min()>=183,name
 if name!='checkerboard':
  for kind in ('basecolor','height'):
   a=maps[kind];assert np.array_equal(a[0],a[-1]) and np.array_equal(a[:,0],a[:,-1]),(name,kind)
 m=library['terrain_'+name];assert m['metallic']==0 and not m['vertexColors']
 for k in ('texture','normalMap','metallicRoughnessMap','occlusionMap'):assert (ROOT/'engine/assets'/m[k]).is_file(),m[k]
print('PASS: 54 maps, dimensions, dielectric channels, normal lengths, packed parity, tile boundaries, material paths')
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'game.json').write_text('{"name":"fixture","terrain":{"existing":true}}');(p/'materials.json').write_text('{"custom":{"roughness":0.5}}')
 cmd=[sys.executable,str(ROOT/'tools/terrain/install_pbr_pack.py'),d]
 subprocess.run(cmd,check=True,capture_output=True)
 assert json.loads((p/'game.json').read_text())['terrain']=={'existing':True}
 assert 'custom' in json.loads((p/'materials.json').read_text())
 assert subprocess.run(cmd,capture_output=True).returncode!=0
print('PASS: installer preserves existing terrain/custom material; refuses repeated overwrite')
