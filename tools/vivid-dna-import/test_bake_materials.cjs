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
 const source=Buffer.from([0,70,80,255,128,90,100,0]);
 await sharp(source,{raw:{width:2,height:1,channels:4}}).png().toFile(path.join(dir,'mask.png'));
 const doc=new Document();
 const hair=doc.createMaterial('hair'),cloth=doc.createMaterial('cloth');
 const lash=doc.createMaterial('eyelashes');
 const positions=doc.createAccessor().setType('VEC3').setArray(new Float32Array(9));
 const primitive=doc.createPrimitive().setMaterial(hair).setAttribute('POSITION',positions);
 const mesh=doc.createMesh().addPrimitive(primitive);
 await bakeMaterials(doc,{materials:[
  {material_name:'hair',kind:'hair',textures:{alpha_r:'mask.png'},params:{alpha_channel:'r',roughness:.6}},
  {material_name:'eyelashes',kind:'face_accessory',textures:{alpha:'mask.png',basecolor:'must-not-read.png'},params:{base_color:[.05,.04,.03,1],roughness:.7}},
  {material_name:'cloth',kind:'cloth',textures:{mask:'mask.png'},params:{mask_channel:'r',diffuse_color_1:[.1,.2,.3,1],diffuse_color_2:[.8,.7,.6,1],roughness:.7,metallic:0}}
 ]},dir,sharp);
 const h=await sharp(hair.getBaseColorTexture().getImage()).raw().toBuffer();
 assert.deepEqual([...h],[Math.round(srgb(70/255)*255),Math.round(srgb(80/255)*255),255,0,Math.round(srgb(90/255)*255),Math.round(srgb(100/255)*255),255,128]);
 assert.equal(hair.getAlphaMode(),'BLEND');
 assert.equal(lash.getAlphaMode(),'BLEND');
 assert.deepEqual([...await sharp(lash.getBaseColorTexture().getImage()).raw().toBuffer()],[255,255,255,0,255,255,255,128]);
 assert.equal(mesh.listPrimitives().length,2);
 const core=mesh.listPrimitives()[1];
 assert.equal(core.getAttribute('POSITION'),positions);
 assert.equal(core.getMaterial().getAlphaMode(),'MASK');
 assert.equal(core.getMaterial().getAlphaCutoff(),.5);
 assert.equal(core.getMaterial().getBaseColorTexture(),hair.getBaseColorTexture());
 const c=await sharp(cloth.getBaseColorTexture().getImage()).raw().toBuffer();
 for(let i=0;i<3;i++) {
  assert.equal(c[i],Math.round(srgb([.1,.2,.3][i])*255));
  assert.equal(c[4+i],Math.round(srgb([.1,.2,.3][i]+([.8,.7,.6][i]-[.1,.2,.3][i])*128/255)*255));
 }
 for(const v of [0,.01,.3,.8,1])assert.ok(Math.abs(linear(srgb(v))-v)<1e-6);
 // Path validation tested with a real material rather than relying on decoder errors.
 const unsafe=new Document();unsafe.createMaterial('hair');
 await assert.rejects(()=>bakeMaterials(unsafe,{materials:[{material_name:'hair',kind:'hair',textures:{alpha_r:'../outside.png'},params:{}}]},dir,sharp),/Unsafe texture path/);
 console.log('PASS: fractional red coverage, blended lashes, shared masked hair core, cloth linear-sRGB colours, unsafe path rejection');
})().catch(e=>{console.error(e);process.exitCode=1;});
