// Offline: decode KHR_draco_mesh_compression (and meshopt) in a glTF/GLB so the
// AIVERSE importer can read it (the runtime has no Draco decoder on purpose).
// Usage: node decompress_draco.cjs <tooling-dir-with-node_modules> <input.glb> <new-output.glb>
// tooling needs @gltf-transform/core, @gltf-transform/extensions, draco3dgltf.
const fs=require('fs'),path=require('path'),{createRequire}=require('module');
(async()=>{
 const [tooling,input,output]=process.argv.slice(2);
 if(!output||fs.existsSync(output))throw Error('Expected tooling input new-output (output must not exist)');
 const load=createRequire(path.resolve(tooling,'package.json'));
 const {NodeIO}=load('@gltf-transform/core'),{ALL_EXTENSIONS}=load('@gltf-transform/extensions'),draco3d=load('draco3dgltf');
 const io=new NodeIO().registerExtensions(ALL_EXTENSIONS).registerDependencies({'draco3d.decoder':await draco3d.createDecoderModule()});
 const doc=await io.read(input);
 let removed=[];
 for(const ext of doc.getRoot().listExtensionsUsed()){const n=ext.extensionName;
  if(n==='KHR_draco_mesh_compression'||n==='EXT_meshopt_compression'){ext.dispose();removed.push(n);}}
 await io.write(output+'.partial.glb',doc);fs.renameSync(output+'.partial.glb',output);
 console.log(JSON.stringify({input,output,decoded:removed,meshes:doc.getRoot().listMeshes().length}));
})().catch(e=>{console.error(e.message||e);process.exit(1);});
