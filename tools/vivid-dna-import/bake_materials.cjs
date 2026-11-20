// Offline material-channel conversion: source UE masks -> standard glTF PBR.
// No generated character art. Original skin/normal images remain unchanged.
const fs=require('node:fs');
const path=require('node:path');
const crypto=require('node:crypto');
const clamp=x=>Math.max(0,Math.min(1,x));
const linear=x=>x<=.04045?x/12.92:Math.pow((x+.055)/1.055,2.4);
const srgb=x=>x<=.0031308?x*12.92:1.055*Math.pow(x,1/2.4)-.055;
const mix=(a,b,t)=>a+(b-a)*t;
const smooth=(a,b,x)=>{const t=clamp((x-a)/(b-a));return t*t*(3-2*t);};
function sample(map,u,v,c=0,decode=false,repeat=true) {
  if(!map)return 1;
  u=repeat?u-Math.floor(u):clamp(u);v=repeat?v-Math.floor(v):clamp(v);
  const x=Math.min(map.width-1,Math.floor(u*map.width));
  const y=Math.min(map.height-1,Math.floor(v*map.height));
  const n=map.data[(y*map.width+x)*4+c]/255;
  return decode?linear(n):n;
}
async function bakeMaterials(doc,mapping,directory,sharp) {
 const report=[];
 const cache=new Map();
 async function read(relative) {
  if(!relative)return null;
  const file=path.resolve(directory,relative);
  if(!file.startsWith(path.resolve(directory)+path.sep))throw Error('Unsafe texture path');
  if(!cache.has(file)) {
   const raw=await sharp(file,{limitInputPixels:67108864}).ensureAlpha().raw().toBuffer({resolveWithObject:true});
   cache.set(file,{data:raw.data,width:raw.info.width,height:raw.info.height});
  }
  return cache.get(file);
 }
 async function texture(name,pixels,w,h) {
  const png=await sharp(pixels,{raw:{width:w,height:h,channels:4}}).png().toBuffer();
  return doc.createTexture(name).setImage(png).setMimeType('image/png');
 }
 for(const material of doc.getRoot().listMaterials()) {
  const spec=mapping.materials.find(s=>s.material_name===material.getName());
  if(!spec)continue;
  const p=spec.params,t=spec.textures||{};
  // Companion skin/teeth maps were outside the GLB: retain the source pixels.
  if(spec.kind==='skin' || spec.face_slot==='teeth') {
   const bindings = spec.kind==='skin'?['ao']:['basecolor','normal','mouth_occlusion'];
   for(const key of bindings) {
    if(!t[key])continue;
    const raw=await read(t[key]);
    const tex=await texture(material.getName()+'_'+key,raw.data,raw.width,raw.height);
    if(key==='basecolor')material.setBaseColorTexture(tex).setBaseColorFactor([1,1,1,1]);
    else if(key==='normal')material.setNormalTexture(tex);
    else material.setOcclusionTexture(tex).setOcclusionStrength(1);
    report.push({material:material.getName(),mode:'source '+key,path:t[key],width:raw.width,height:raw.height});
   }
  }
  let output=null,mode='',w=0,h=0;
  if(spec.kind==='cloth') {
   const mask=await read(t.mask),macro=await read(t.macro_overlay),ao=await read(t.ao);
   let detail=await read(t.detail_diffuse);
   if(!mask)throw Error('Cloth requires source mask');
   w=mask.width;h=mask.height;output=Buffer.alloc(w*h*4);
   // A highly tiled textile map needs a mip-like prefilter before baking.
   // Sampling its full-resolution pixels at 220 repeats aliases into large checks.
   if(detail) {
    const dw=Math.max(1,Math.min(detail.width,Math.floor(w/Math.max(1,p.detail_tiling))));
    const dh=Math.max(1,Math.min(detail.height,Math.floor(h/Math.max(1,p.detail_tiling))));
    const filtered=await sharp(detail.data,{raw:{width:detail.width,height:detail.height,channels:4}})
      .gamma().resize(dw,dh,{kernel:'lanczos3'}).raw().toBuffer();
    detail={data:filtered,width:dw,height:dh};
   }
   const channel={r:0,g:1,b:2,a:3}[p.mask_channel||'r'];
   if(channel===undefined)throw Error('Unknown mask channel');
   for(let y=0;y<h;y++)for(let x=0;x<w;x++) {
    const u=(x+.5)/w,v=(y+.5)/h,k=(y*w+x)*4;
    const factor=sample(mask,u,v,channel);
    for(let c=0;c<3;c++) {
     let value=mix(p.diffuse_color_1[c],p.diffuse_color_2[c],factor);
     if(detail)value*=mix(1,2*sample(detail,u*p.detail_tiling,v*p.detail_tiling,c,true),p.detail_strength);
     if(macro) {
      const m=sample(macro,u*p.macro_tiling,v*p.macro_tiling,c,true);
      const overlay=value<=.5?2*value*m:1-2*(1-value)*(1-m);
      value=mix(value,overlay,p.macro_strength);
     }
     if(ao)value*=mix(1,sample(ao,u,v),p.ao_amount);
     output[k+c]=Math.round(clamp(srgb(clamp(value)))*255);
    }
    output[k+3]=255;
   }
   material.setBaseColorFactor([1,1,1,1]).setMetallicFactor(p.metallic||0)
    .setRoughnessFactor(p.roughness).setMetallicRoughnessTexture(null);
   mode='cloth mask + authored colours + available detail/macro/AO';
  } else if(spec.kind==='hair'||/eyelashes/.test(material.getName())) {
   const source=spec.kind==='hair'?await read(t.alpha_r||t.alpha):await read(t.basecolor||t.alpha);
   if(!source)throw Error('Coverage texture missing: '+material.getName());
   w=source.width;h=source.height;output=Buffer.alloc(w*h*4);
   const channel={r:0,g:1,b:2,a:3}[p.alpha_channel||'r'];
   for(let k=0;k<output.length;k+=4) {
    output[k]=output[k+1]=output[k+2]=255;
    output[k+3]=source.data[k+channel];
   }
   material.setBaseColorFactor(spec.kind==='hair'?[1,1,1,1]:[...p.base_color.slice(0,3),1])
    .setAlphaMode('MASK').setAlphaCutoff(.35).setDoubleSided(true).setMetallicFactor(0).setRoughnessFactor(p.roughness);
   mode='source coverage channel -> alpha; neutral RGB';
  } else if(p.eye_pole_uv) {
   const atlas=await read(t.basecolor);if(!atlas)throw Error('Missing iris atlas');
   w=h=1024;output=Buffer.alloc(w*h*4);
   for(let y=0;y<h;y++)for(let x=0;x<w;x++) {
    const u=(x+.5)/w,v=(y+.5)/h,k=(y*w+x)*4;
    const au=.5+(u-p.eye_pole_uv[0])*p.uv_scale,av=.5+(v-p.eye_pole_uv[1])*p.uv_scale;
    const fibril=mix(p.fibril_to_min,p.fibril_to_max,clamp((sample(atlas,au,av,0,false,false)-p.fibril_from_min)/(p.fibril_from_max-p.fibril_from_min)));
    const limbus=clamp((sample(atlas,au,av,2,false,false)-p.limbus_start)/(1-p.limbus_start));
    const radius=Math.hypot(u-p.eye_pole_uv[0],v-p.eye_pole_uv[1]);
    const iris=smooth(p.iris_radius_in,p.iris_radius_out,radius);
    const pupil=1-smooth(p.pupil_radius_in,p.pupil_radius_out,radius);
    for(let c=0;c<3;c++) {
     const base=mix(mix(p.iris_color[c]*fibril,p.iris_dark[c],limbus),p.sclera_color[c],iris);
     output[k+c]=Math.round(clamp(srgb(mix(base,p.pupil_color[c],pupil)))*255);
    } output[k+3]=255;
   }
   material.setBaseColorFactor([1,1,1,1]).setRoughnessFactor(.12).setMetallicFactor(0);
   mode='source iris R/B + authored radial iris/pupil in mesh UV; no refraction';
  }
  if(output) {
   material.setBaseColorTexture(await texture(material.getName()+'_vivid_baked',output,w,h));
   // Baked eye image already lives in original mesh UV; do not apply atlas zoom twice.
   if(p.eye_pole_uv) {
    const info=material.getBaseColorTextureInfo();
    const ext=info.getExtension('KHR_texture_transform');if(ext)ext.dispose();
   }
   report.push({material:material.getName(),mode,width:w,height:h,pixelSha256:crypto.createHash('sha256').update(output).digest('hex')});
  }
 }
 return report;
}
module.exports={bakeMaterials,linear,srgb,sample};
