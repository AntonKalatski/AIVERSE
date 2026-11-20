import test from 'node:test';
import assert from 'node:assert/strict';
import {moveShaderGraphNode} from '../src/panels/shaderGraphLayout.ts';
test('dragging changes only the targeted node coordinate',()=>{
 const source={domain:'material',nodes:[{id:'a',op:'input',position:[30,40]},{id:'b',op:'constant',position:[200,70]}],outputs:{color:'b'}};
 const next=moveShaderGraphNode(source,'a',90,110);
 assert.deepEqual(next.nodes[0].position,[90,110]);
 assert.deepEqual(next.nodes[1].position,[200,70]);
 assert.deepEqual(source.nodes[0].position,[30,40]);
 assert.deepEqual(next.outputs,{color:'b'});
});
test('invalid drags do not change source and canvas is bounded',()=>{
 const g={nodes:[{id:'a',op:'input',position:[0,0]}]};
 assert.equal(moveShaderGraphNode(g,'a',NaN,0),g);
 assert.deepEqual(moveShaderGraphNode(g,'a',999999,-100).nodes[0].position,[2000,0]);
});
