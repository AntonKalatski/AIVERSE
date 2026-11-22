#!/usr/bin/env python3
"""Procedural STRATA showcase scene for the website (numpy).

Writes rock.gltf (81,920 triangles), ground.gltf (104,882 triangles) and scene.json
(ground + 20 rocks) into the current directory. Triangles are Morton-ordered so
STRATA clusters are spatially local, as in DCC-exported meshes.

Usage (Linux, software GL; any desktop with AIVERSE_BUILD_SITE_SHOWCASE=ON):
  mkdir /tmp/strata-scene && cd /tmp/strata-scene && python3 <repo>/tools/site/strata_showcase_scene.py
  for v in normal cluster lod; do
    AIVERSE_CONTENT=/tmp/strata-scene STRATA_SCENE=scene.json STRATA_VIEW=$v STRATA_ALPHA=0.72 \
    STRATA_CAM="0,0,-8,19,200,24,50" AIVERSE_SCREENSHOT=strata-$v.ppm AIVERSE_EXIT_AFTER_FRAMES=30 \
    <build>/aiverse_strata_showcase
  done
"""
import numpy as np, json, struct, math, sys
rng=np.random.default_rng(7)
def noise3(p,seed,octaves=5):
    # sum of random-direction sinusoids (smooth, deterministic)
    r=np.random.default_rng(seed); v=np.zeros(len(p)); amp=1.0; freq=1.0
    for o in range(octaves):
        for k in range(6):
            d=r.normal(size=3); d/=np.linalg.norm(d); ph=r.uniform(0,6.283)
            v+=amp*np.sin(freq*(p@d)*2.1+ph)/6
        amp*=0.5; freq*=2.05
    return v
def icosphere(level):
    t=(1+5**.5)/2
    V=[(-1,t,0),(1,t,0),(-1,-t,0),(1,-t,0),(0,-1,t),(0,1,t),(0,-1,-t),(0,1,-t),(t,0,-1),(t,0,1),(-t,0,-1),(-t,0,1)]
    F=[(0,11,5),(0,5,1),(0,1,7),(0,7,10),(0,10,11),(1,5,9),(5,11,4),(11,10,2),(10,7,6),(7,1,8),(3,9,4),(3,4,2),(3,2,6),(3,6,8),(3,8,9),(4,9,5),(2,4,11),(6,2,10),(8,6,7),(9,8,1)]
    V=[np.array(v,float)/np.linalg.norm(v) for v in V]
    for _ in range(level):
        cache={};NF=[]
        def mid(a,b):
            k=(min(a,b),max(a,b))
            if k not in cache:
                m=V[a]+V[b]; V.append(m/np.linalg.norm(m)); cache[k]=len(V)-1
            return cache[k]
        for a,b,c in F:
            ab,bc,ca=mid(a,b),mid(b,c),mid(c,a); NF+= [(a,ab,ca),(b,bc,ab),(c,ca,bc),(ab,bc,ca)]
        F=NF
    return np.array(V),np.array(F,dtype=np.uint32)
def normals(P,F):
    N=np.zeros_like(P); fn=np.cross(P[F[:,1]]-P[F[:,0]],P[F[:,2]]-P[F[:,0]])
    for i in range(3): np.add.at(N,F[:,i],fn)
    return N/np.linalg.norm(N,axis=1,keepdims=True)
def morton_sort(P,F):
    c=P[F].mean(1); lo=c.min(0); q=((c-lo)/(c.max(0)-lo+1e-9)*1023).astype(np.uint64)
    def spread(v):
        v=v&0x3ff; v=(v|(v<<16))&0x30000ff; v=(v|(v<<8))&0x300f00f; v=(v|(v<<4))&0x30c30c3; v=(v|(v<<2))&0x9249249; return v
    code=spread(q[:,0])|(spread(q[:,1])<<np.uint64(1))|(spread(q[:,2])<<np.uint64(2))
    return F[np.argsort(code,kind='stable')]
def write(name,P,F,color,rough=0.85):
    F=morton_sort(P,F)
    N=normals(P,F).astype(np.float32); P=P.astype(np.float32); I=F.astype(np.uint32).ravel()
    bin_=P.tobytes()+N.tobytes()+I.tobytes()
    open(name+'.bin','wb').write(bin_)
    g={"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
     "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2,"material":0}]}],
     "materials":[{"pbrMetallicRoughness":{"baseColorFactor":color+[1],"metallicFactor":0,"roughnessFactor":rough}}],
     "buffers":[{"uri":name+".bin","byteLength":len(bin_)}],
     "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":P.nbytes,"target":34962},
                    {"buffer":0,"byteOffset":P.nbytes,"byteLength":N.nbytes,"target":34962},
                    {"buffer":0,"byteOffset":P.nbytes+N.nbytes,"byteLength":I.nbytes,"target":34963}],
     "accessors":[{"bufferView":0,"componentType":5126,"count":len(P),"type":"VEC3","min":P.min(0).tolist(),"max":P.max(0).tolist()},
                  {"bufferView":1,"componentType":5126,"count":len(N),"type":"VEC3"},
                  {"bufferView":2,"componentType":5125,"count":len(I),"type":"SCALAR"}]}
    json.dump(g,open(name+'.gltf','w')); print(name,len(P),'verts',len(F),'tris')
# rock
V,F=icosphere(6)
d=noise3(V,3,6)
P=V*(1+0.28*d[:,None]); P[:,1]*=0.72; P[:,1]-=P[:,1].min()*0.15
write("rock",P*1.0,F,[0.36,0.33,0.30])
# terrain heightfield 320x320 over 160m
n=230; xs=np.linspace(-80,80,n); X,Z=np.meshgrid(xs,xs)
pts=np.stack([X.ravel()/30,np.zeros(n*n),Z.ravel()/30],1)
H=noise3(pts,11,5)*3.0+np.hypot(X,Z).ravel()*0.04
P=np.stack([X.ravel(),H,Z.ravel()],1)
idx=np.arange(n*n).reshape(n,n); a=idx[:-1,:-1].ravel();b=idx[:-1,1:].ravel();c=idx[1:,:-1].ravel();e=idx[1:,1:].ravel()
F=np.concatenate([np.stack([a,c,b],1),np.stack([b,c,e],1)])
write("ground",P,F,[0.30,0.33,0.24],0.95)

# --- scene: ground + 20 rocks receding from the camera (deterministic) ---
import random
random.seed(5); n=230
Pg=np.frombuffer(open('ground.bin','rb').read(n*n*12),dtype=np.float32).reshape(n,n,3)
def h(x,z):
    i=min(max(int(round((z+80)/160*(n-1))),0),n-1); j=min(max(int(round((x+80)/160*(n-1))),0),n-1); return float(Pg[i,j,1])
inst=[{"id":"ground","model":"ground.gltf","position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]}]
k=0
def rock(x,z,s,ys=1.0):
    global k; inst.append({"id":f"r{k}","model":"rock.gltf","position":[x,h(x,z)-0.35*s*ys,z],"rotation":[random.uniform(-8,8),random.uniform(0,360),0],"scale":[s,s*ys,s]}); k+=1
for x,z,s in [(-3.5,6,3.2),(4.5,2,2.6),(0,-4,4.0)]: rock(x,z,s)
for i in range(26):
    z=-9-i*2.4-random.uniform(0,2); x=random.uniform(-14,14)*(1+abs(z)/30); rock(x,z,random.uniform(1.5,3.8),random.uniform(.7,1.2))
json.dump({"version":1,"instances":inst[:21]},open('scene.json','w')); print('scene.json',len(inst[:21]),'instances')
