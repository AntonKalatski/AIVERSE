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
