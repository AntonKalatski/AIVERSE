const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {createRequire}=require('node:module');
const {bakeMaterials,linear,srgb}=require('./bake_materials.cjs');
(async()=>{
 const [tooling,tempRoot]=process.argv.slice(2);
 const load=createRequire(path.resolve(tooling,'package.json'));
 const {Document}=load('@gltf-transform/core');const sharp=load('sharp');
 const dir=fs.mkdtempSync(path.join(tempRoot,'vivid-material-test-'));
 // Deliberately different R and A: coverage must come from R, not input alpha.
 const source=Buffer.from([0,70,80,255,255,90,100,0]);
 await sharp(source,{raw:{width:2,height:1,channels:4}}).png().toFile(path.join(dir,'mask.png'));
 const doc=new Document();
 const hair=doc.createMaterial('hair'),cloth=doc.createMaterial('cloth');
 await bakeMaterials(doc,{materials:[
  {material_name:'hair',kind:'hair',textures:{alpha_r:'mask.png'},params:{alpha_channel:'r',roughness:.6}},
  {material_name:'cloth',kind:'cloth',textures:{mask:'mask.png'},params:{mask_channel:'r',diffuse_color_1:[.1,.2,.3,1],diffuse_color_2:[.8,.7,.6,1],roughness:.7,metallic:0}}
 ]},dir,sharp);
 const h=await sharp(hair.getBaseColorTexture().getImage()).raw().toBuffer();
 assert.deepEqual([...h],[255,255,255,0,255,255,255,255]);
 assert.equal(hair.getAlphaMode(),'MASK');
 const c=await sharp(cloth.getBaseColorTexture().getImage()).raw().toBuffer();
 for(let i=0;i<3;i++) {
  assert.equal(c[i],Math.round(srgb([.1,.2,.3][i])*255));
  assert.equal(c[4+i],Math.round(srgb([.8,.7,.6][i])*255));
 }
 for(const v of [0,.01,.3,.8,1])assert.ok(Math.abs(linear(srgb(v))-v)<1e-6);
 // Path validation tested with a real material rather than relying on decoder errors.
 const unsafe=new Document();unsafe.createMaterial('hair');
 await assert.rejects(()=>bakeMaterials(unsafe,{materials:[{material_name:'hair',kind:'hair',textures:{alpha_r:'../outside.png'},params:{}}]},dir,sharp),/Unsafe texture path/);
 console.log('PASS: red-channel coverage, cloth mask/linear-sRGB colours, unsafe path rejection');
})().catch(e=>{console.error(e);process.exitCode=1;});
