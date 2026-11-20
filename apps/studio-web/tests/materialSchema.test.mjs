import test from 'node:test';
import assert from 'node:assert/strict';
import {MATERIAL_EDIT_KEYS,materialFields,materialNames,materialSwatchColor,editablePatch} from '../src/panels/materialSchema.ts';
test('PBR field metadata exactly matches native allowlist',()=>{
 assert.deepEqual(materialFields({}).map(f=>f.key),[...MATERIAL_EDIT_KEYS]);
});
test('draft values override material without mutating source',()=>{
 const source={metallic:.8,roughness:.15};
 const fields=materialFields(source,{roughness:.5});
 assert.equal(fields[0].value,.8);assert.equal(fields[1].value,.5);
 assert.deepEqual(source,{metallic:.8,roughness:.15});
});
test('invalid library entries are excluded',()=>{
 assert.deepEqual(materialNames({library:{gold:{metallic:1},bad:null,z:'string',wood:{}}}),['gold','wood']);
});
test('color swatch clamps invalid channels',()=>{
 assert.equal(materialSwatchColor({color:[1,0,.5]}),'rgb(255 0 128)');
 assert.equal(materialSwatchColor({color:[20,-1,NaN]}),'rgb(255 0 160)');
});
test('patch output strips unsupported fields',()=>{
 assert.deepEqual(editablePatch({roughness:.4,shaderGraph:'unsafe',texture:'../x'}),{roughness:.4});
});
