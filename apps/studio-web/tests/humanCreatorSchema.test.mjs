import test from 'node:test';
import assert from 'node:assert/strict';
import {readCreatorValue,normalizeCreatorNumber,rgbToHex,hexToRgb,creatorWireValue,activeExpressionCount} from '../src/panels/humanCreatorSchema.ts';
const dna={skin:{melanin:.42,melaninColor:[.52,.34,.25]},expressionRest:[0,.5,0],rig:{optionalBones:['a']}};
test('reads dotted DNA paths including array channels',()=>{
 assert.equal(readCreatorValue(dna,'skin.melanin'),.42);
 assert.equal(readCreatorValue(dna,'expressionRest.1'),.5);
 assert.equal(readCreatorValue(dna,'skin.missing.deep'),undefined);
});
test('numeric fields clamp to native limits and integers snap',()=>{
 const f={key:'skin.melanin',label:'',kind:'number',min:0,max:1,step:.01};
 assert.equal(normalizeCreatorNumber(f,2),1);assert.equal(normalizeCreatorNumber(f,-1),0);assert.equal(normalizeCreatorNumber(f,NaN),null);
 assert.equal(normalizeCreatorNumber({...f,min:4,max:8192,step:1},511.6),512);
});
test('colours round trip through hex',()=>{
 assert.equal(rgbToHex([1,0,.5]),'#ff0080');
 assert.deepEqual(hexToRgb('#ff0080'),[1,0,.502]);assert.equal(hexToRgb('red'),null);
});
test('wire values reject what native validation would reject',()=>{
 assert.deepEqual(creatorWireValue({key:'rig.optionalBones',label:'',kind:'bones'},' a, b ,,'),['a','b']);
 assert.equal(creatorWireValue({key:'id',label:'',kind:'string'},''),null);
 assert.equal(creatorWireValue({key:'seed',label:'',kind:'integer'},-3),null);
 assert.equal(creatorWireValue({key:'eyes.irisColor',label:'',kind:'color'},[1,2]),null);
});
test('counts active expression channels',()=>{
 assert.equal(activeExpressionCount({document:{dna}}),1);assert.equal(activeExpressionCount(null),0);
});
test('face mesh guess picks the skin mesh, never eyes/brows/hair',async()=>{
 const {guessFaceMeshes}=await import('../src/panels/humanCreatorSchema.ts');
 assert.deepEqual(guessFaceMeshes(['Ada_FaceMesh_LOD0','Ada_FaceMesh_LOD1','Eyebrows_M_Thin_CardsMesh_Group0_LOD0','f_med_nrw_body_LOD0','Ada_eyeLeft_LOD0']),['Ada_FaceMesh_LOD0']);
 assert.deepEqual(guessFaceMeshes(['body','Hair_S_Coil_CardsMesh_Group0_LOD0']),[]);
});
test('blend of two looks interpolates within schema limits',async()=>{
 const {blendLooks}=await import('../src/panels/humanCreatorSchema.ts');
 const schema=[{key:'skin.melanin',label:'',min:0,max:1,initial:.4},{key:'body.height',label:'',min:.8,max:1.2,initial:1}];
 const a={version:1,values:{'skin.melanin':.2},materials:{Face:'skin'}},b={version:1,values:{'skin.melanin':.8,'body.height':1.2},materials:{Face:'cloth'}};
 const mid=blendLooks(a,b,.5,schema);
 assert.ok(Math.abs(mid.values['skin.melanin']-.5)<1e-9);assert.ok(Math.abs(mid.values['body.height']-1.1)<1e-9);assert.equal(mid.materials.Face,'cloth');
 assert.deepEqual(blendLooks(a,b,0,schema).values,{'skin.melanin':.2});assert.equal(blendLooks(a,b,-3,schema).materials.Face,'skin');
});
test('face blend circle: archetype weights, regions and limits',async()=>{
 const {blendWeights,blendFace,archetypePoint,FACE_ARCHETYPES}=await import('../src/panels/humanCreatorSchema.ts');
 const w=blendWeights([0,0]);assert.ok(Math.abs(w.reduce((a,b)=>a+b,0)-1)<1e-9);assert.ok(w.every(v=>Math.abs(v-w[0])<1e-9),'centre = equal mix');
 const at=blendWeights(archetypePoint(2));assert.equal(at[2],1);
 const schema=[{key:'width',min:-.3,max:.3},{key:'noseWidth',min:-.3,max:.3},{key:'jawWidth',min:-.3,max:.3}];
 const broad=blendFace(archetypePoint(2),{},schema);assert.equal(broad.width,FACE_ARCHETYPES[2].values.width);
 const noNose=blendFace(archetypePoint(2),{'Нос':0},schema);assert.equal(noNose.noseWidth,undefined);assert.equal(noNose.jawWidth,.22);
 const tight=blendFace(archetypePoint(2),{},[{key:'width',min:-.1,max:.1}]);assert.equal(tight.width,.1);
});
test('sculpt zones name strokes the way the artist reads the face',async()=>{
 const {sculptZone}=await import('../src/panels/humanCreatorSchema.ts');
 assert.equal(sculptZone([.5,.5,1]),'Нос');assert.equal(sculptZone([.3,.5,.9]),'Скула слева');
 assert.equal(sculptZone([.7,.6,.9]),'Глаза справа');assert.equal(sculptZone([.5,.15,.9]),'Подбородок');
});
