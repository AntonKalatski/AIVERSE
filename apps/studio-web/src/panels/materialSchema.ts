// Browser-only material authoring schema. Native MaterialOps remains authoritative.
export type FieldValue=string|number|boolean;
export type MaterialData=Record<string,unknown>;
export interface MaterialLibrary {library:Record<string,MaterialData>}
export const MATERIAL_EDIT_KEYS=[
  'metallic','roughness','cutoff','teamStrength','occlusionStrength',
  'emissiveStrength','normalScale','doubleSided','unlit','castShadow',
  'receiveShadow','vertexColors','alpha','team','description',
  'texture','normalMap','emissiveMap','teamMask','metallicRoughnessMap','occlusionMap'
] as const;
export type MaterialKey=typeof MATERIAL_EDIT_KEYS[number];
export const MATERIAL_PRESETS=['matte','metal','glass'] as const;
export type MaterialPreset=typeof MATERIAL_PRESETS[number];
export type MaterialPatch=Partial<Record<MaterialKey,FieldValue>>;
export interface MaterialField {
  key:MaterialKey; label:string; type:'text'|'number'|'boolean'|'asset'; value:FieldValue;
  assetKinds?: Array<'image'>;
  editable:true; min?:number; max?:number; step?:number;
  options?:Array<{label:string;value:string|number}>;
}
const fields:Array<Omit<MaterialField,'value'|'editable'>>=[
 {key:'metallic',label:'Metallic',type:'number',min:0,max:1,step:.01},
 {key:'roughness',label:'Roughness',type:'number',min:0,max:1,step:.01},
 {key:'cutoff',label:'Alpha cutoff',type:'number',min:0,max:1,step:.01},
 {key:'teamStrength',label:'Team strength',type:'number',min:0,max:1,step:.01},
 {key:'occlusionStrength',label:'Occlusion strength',type:'number',min:0,max:1,step:.01},
 {key:'emissiveStrength',label:'Emissive strength',type:'number',min:0,max:100,step:.1},
 {key:'normalScale',label:'Normal scale',type:'number',min:0,max:16,step:.1},
 {key:'doubleSided',label:'Double sided',type:'boolean'},
 {key:'unlit',label:'Unlit',type:'boolean'},
 {key:'castShadow',label:'Cast shadow',type:'boolean'},
 {key:'receiveShadow',label:'Receive shadow',type:'boolean'},
 {key:'vertexColors',label:'Vertex colors',type:'boolean'},
 {key:'alpha',label:'Alpha mode',type:'text',options:[{label:'Opaque',value:'opaque'},{label:'Mask',value:'mask'},{label:'Blend',value:'blend'}]},
 {key:'team',label:'Team tint',type:'text',options:[{label:'None',value:'none'},{label:'Tint',value:'tint'},{label:'Mask',value:'mask'},{label:'Alpha',value:'alpha'},{label:'Vertex',value:'vertex'}]},
 {key:'description',label:'Description',type:'text'},
 {key:'texture',label:'Albedo texture',type:'asset',assetKinds:['image']},
 {key:'normalMap',label:'Normal map',type:'asset',assetKinds:['image']},
 {key:'emissiveMap',label:'Emission map',type:'asset',assetKinds:['image']},
 {key:'teamMask',label:'Team mask',type:'asset',assetKinds:['image']},
 {key:'metallicRoughnessMap',label:'Metallic / roughness map',type:'asset',assetKinds:['image']},
 {key:'occlusionMap',label:'Occlusion map',type:'asset',assetKinds:['image']}
];
const defaults:Record<MaterialKey,FieldValue>={
 metallic:0,roughness:1,cutoff:.5,teamStrength:1,occlusionStrength:1,
 emissiveStrength:0,normalScale:1,doubleSided:false,unlit:false,castShadow:true,
 receiveShadow:true,vertexColors:false,alpha:'opaque',team:'none',description:'',
 texture:'',normalMap:'',emissiveMap:'',teamMask:'',metallicRoughnessMap:'',occlusionMap:''
};
export function materialFields(material:MaterialData,patch:MaterialPatch={}):MaterialField[]{
 return fields.map(field=>{
  const source=material[field.key];
  const expected=field.type==='text'||field.type==='asset'?'string':field.type;
  const valid=typeof source===expected&&(typeof source!=='number'||Number.isFinite(source));
  const value=patch[field.key]??(valid?source as FieldValue:defaults[field.key]);
  return {...field,value,editable:true};
 });
}
export function materialNames(data:MaterialLibrary):string[]{
 return Object.entries(data.library)
  .filter(([,entry])=>typeof entry==='object'&&entry!==null&&!Array.isArray(entry))
  .map(([name])=>name).sort((a,b)=>a.localeCompare(b));
}
export function materialSwatchColor(material:MaterialData):string{
 const raw=material.color;
 if(!Array.isArray(raw)||raw.length<3)return 'rgb(147 167 196)';
 const rgb=raw.slice(0,3).map(value=>typeof value==='number'&&Number.isFinite(value)
  ? Math.round(Math.max(0,Math.min(1,value))*255):160);
 return `rgb(${rgb.join(' ')})`;
}
export function editablePatch(patch:MaterialPatch):MaterialPatch{
 const out:MaterialPatch={};
 for(const key of MATERIAL_EDIT_KEYS){const v=patch[key];if(v!==undefined)out[key]=v;}
 return out;
}
