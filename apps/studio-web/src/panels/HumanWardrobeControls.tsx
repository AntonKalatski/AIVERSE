import {useEffect,useState,useId,cloneElement,type ReactElement} from 'react';
import type {HumanSocket,HumanFaceShape,HumanLook,HumanWardrobe,HumanWardrobeItem,HumanBuildStatus} from '../types';
import {invoke} from '../bridge';
import AssetReferencePicker from '../components/AssetReferencePicker';
import {StudioSection,StudioField,StudioInput,StudioSelect,StudioButton,StudioAlert} from '../ui';
const empty=():HumanWardrobe=>({version:1,hiddenMeshes:[],items:[]});
const slots=['top','bottom','feet','head','neck','wrist_left','wrist_right','ears','accessory_1','accessory_2'];
export default function HumanWardrobeControls({guid,look,face}:{guid:string;look:HumanLook;face:HumanFaceShape|null}) {
 const [recipe,setRecipe]=useState<HumanWardrobe>(empty);
 const [history,setHistory]=useState<HumanWardrobe[]>([]);
 const [info,setInfo]=useState<{meshes:string[];bones:string[];materials:string[]}|null>(null);
 const [busy,setBusy]=useState(false),[error,setError]=useState(''),[name,setName]=useState('character_assembled');
 const [size,setSize]=useState(1024),[job,setJob]=useState<HumanBuildStatus|null>(null);
 const edit=(next:HumanWardrobe)=>{setHistory(h=>[...h.slice(-31),recipe]);setRecipe(next);};
 const socketEdit=(index:number,patch:Partial<HumanSocket>)=>edit({...recipe,sockets:(recipe.sockets??[]).map((v,i)=>i===index?{...v,...patch}:v)});
 const itemEdit=(index:number,patch:Partial<HumanWardrobeItem>)=>edit({...recipe,items:recipe.items.map((v,i)=>i===index?{...v,...patch}:v)});
 const run=async(fn:()=>Promise<void>)=>{setBusy(true);setError('');try{await fn();}catch(e){setError(String(e));}finally{setBusy(false);}};
 useEffect(()=>{if(!job?.running)return;let live=true;const timer=setInterval(()=>{void invoke('studio.humanCharacterBuildStatus',{}).then(result=>{if(live){setJob(result);if(result.error)setError(result.error);}}).catch(e=>{if(live)setError(String(e));});},1000);return ()=>{live=false;clearInterval(timer);};},[job?.running]);
 useEffect(()=>{let live=true;void invoke('studio.humanCharacterBuildStatus',{}).then(result=>{if(live)setJob(result);}).catch(()=>{});return ()=>{live=false;};},[]);
 const disabled=busy||!!job?.running;
 const toggle=(values:string[],value:string)=>values.includes(value)?values.filter(v=>v!==value):[...values,value];
 return <StudioSection title="Одежда и сборка персонажа">
  {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
  <StudioButton disabled={disabled||!guid} onClick={()=>void run(async()=>{setInfo(await invoke('studio.humanWardrobeInfo',{guid}));})}>Загрузить меши и кости</StudioButton>
  <StudioButton disabled={disabled||!guid} onClick={()=>void run(async()=>{setInfo(await invoke('studio.humanWardrobeInfo',{guid}));edit(await invoke('studio.humanWardrobeFromAsset',{guid}));})}>Загрузить гардероб модели</StudioButton>
  <StudioAlert>Любой импортированный меш можно закрепить на кости или именованном сокете. Отдельный скелет сохраняет skinning и морфы; общий скелет нужен для совместимой одежды. Результат виден после сборки. Крепление не заменяет деформацию или физику волос.</StudioAlert>
  {info&&<details><summary>Видимость исходных мешей</summary>
   {info.meshes.map(mesh=><WardrobeField key={mesh} label={mesh}><StudioInput type="checkbox" aria-label={'Показывать '+mesh} checked={!recipe.hiddenMeshes.includes(mesh)} disabled={disabled} onChange={()=>edit({...recipe,hiddenMeshes:toggle(recipe.hiddenMeshes,mesh)})}/></WardrobeField>)}
  </details>}
  <details><summary>Сокеты · точки крепления</summary><div className="au-stack">
   {(recipe.sockets??[]).map((socket,index)=><div className="au-stack" key={index}>
    <WardrobeField label="Имя сокета"><StudioInput value={socket.name} disabled={disabled} onChange={e=>{const next=e.target.value;edit({...recipe,sockets:(recipe.sockets??[]).map((v,i)=>i===index?{...v,name:next}:v),items:recipe.items.map(v=>v.socket===socket.name?{...v,socket:next}:v)});}}/></WardrobeField>
    <WardrobeField label="Родительская кость"><StudioSelect value={socket.bone} disabled={disabled} onChange={e=>socketEdit(index,{bone:e.target.value})}><option value="">Выберите кость</option>{info?.bones.map((bone,i)=><option key={i}>{bone}</option>)}</StudioSelect></WardrobeField>
    {(['translation','rotation','scale'] as const).map(field=><details key={field}><summary>{{translation:'Положение сокета XYZ',rotation:'Поворот сокета XYZ (радианы)',scale:'Масштаб сокета XYZ'}[field]}</summary>{socket[field].map((value,axis)=><WardrobeField key={axis} label={'XYZ'[axis]}><StudioInput type="number" step={.01} value={value} disabled={disabled} onChange={e=>{const n=e.target.valueAsNumber;if(Number.isFinite(n)&&Math.abs(n)<=100&&(field!=='scale'||n>.001))socketEdit(index,{[field]:socket[field].map((v,i)=>i===axis?n:v)});}}/></WardrobeField>)}</details>)}
    <StudioButton disabled={disabled||recipe.items.some(i=>i.socket===socket.name)} onClick={()=>edit({...recipe,sockets:(recipe.sockets??[]).filter((_,i)=>i!==index)})}>Удалить неиспользуемый сокет</StudioButton>
   </div>)}
   <StudioButton disabled={disabled||!info||(recipe.sockets?.length??0)>=128} onClick={()=>edit({...recipe,sockets:[...(recipe.sockets??[]),{name:'socket_'+Date.now(),bone:'',translation:[0,0,0],rotation:[0,0,0],scale:[1,1,1]}]})}>Добавить сокет</StudioButton>
  </div></details>
  {recipe.items.map((item,index)=><details key={item.id} open><summary>{item.slot} · {index+1}</summary><div className="au-stack">
   <WardrobeField label="Включён"><StudioInput type="checkbox" aria-label={'Включить '+item.id} checked={item.enabled} disabled={disabled} onChange={e=>itemEdit(index,{enabled:e.target.checked})}/></WardrobeField>
   <AssetReferencePicker value={item.asset} kinds={['model']} label="SKM / меш / аксессуар" disabled={disabled} onChange={asset=>itemEdit(index,{asset})}/>
   <WardrobeField label="Слот" hint="Произвольное имя. Для нескольких креплений на одной кости используйте разные слоты."><StudioInput value={item.slot} disabled={disabled} onChange={e=>itemEdit(index,{slot:e.target.value})}/></WardrobeField>
   <WardrobeField label="Режим скелета"><StudioSelect value={item.independentRig?'separate':'shared'} disabled={disabled} onChange={e=>itemEdit(index,e.target.value==='separate'?{independentRig:true,attachmentRule:'keep_world'}:{independentRig:false,attachmentRule:undefined,socket:'',bone:'',translation:[0,0,0],rotation:[0,0,0],scale:[1,1,1]})}><option value="shared">Общий · совместимая одежда</option><option value="separate">Отдельный · крепление любого SKM</option></StudioSelect></WardrobeField>
   {item.independentRig&&<WardrobeField label="Правило крепления"><StudioSelect value={item.attachmentRule??''} disabled={disabled} onChange={e=>itemEdit(index,{attachmentRule:e.target.value?e.target.value as 'keep_world'|'snap':undefined})}><option value="">Исходная привязка</option><option value="keep_world">Сохранить исходное положение</option><option value="snap">Совместить с костью / сокетом</option></StudioSelect></WardrobeField>}
   <WardrobeField label="Именованный сокет"><StudioSelect value={item.socket??''} disabled={disabled} onChange={e=>itemEdit(index,{socket:e.target.value,bone:'',...(e.target.value?{independentRig:true,attachmentRule:item.attachmentRule??'keep_world'}:{})})}><option value="">Напрямую к кости</option>{(recipe.sockets??[]).map((socket,i)=><option key={i} value={socket.name}>{socket.name}</option>)}</StudioSelect></WardrobeField>
   <WardrobeField label="Материал"><StudioSelect value={item.finish} disabled={disabled} onChange={e=>itemEdit(index,{finish:e.target.value as 'cloth'|'source'})}><option value="cloth">Ткань · настройки Clothing</option><option value="source">Исходный PBR · металл, украшения</option></StudioSelect></WardrobeField>
   <WardrobeField label="Кость крепления"><StudioSelect value={item.bone} disabled={disabled||!!item.socket} onChange={e=>itemEdit(index,{bone:e.target.value,...(e.target.value?{independentRig:true,attachmentRule:item.attachmentRule??'snap'}:{})})}><option value="">Исходный скелет / мировое положение</option>{info?.bones.map((bone,i)=><option key={i}>{bone}</option>)}</StudioSelect></WardrobeField>
   {(['translation','rotation','scale'] as const).map(field=><details key={field}><summary>{{translation:'Смещение XYZ (единицы кости)',rotation:'Поворот XYZ (радианы)',scale:'Масштаб XYZ'}[field]}</summary>{item[field].map((value,axis)=><WardrobeField key={axis} label={'XYZ'[axis]}><StudioInput type="number" step={.01} value={value} disabled={disabled} onChange={e=>{const n=e.target.valueAsNumber;if(Number.isFinite(n)&&Math.abs(n)<=100&&(field!=='scale'||n>.001))itemEdit(index,{[field]:item[field].map((v,i)=>i===axis?n:v)});}}/></WardrobeField>)}</details>)}
   <details><summary>Скрывать меши при надевании</summary>{info?.meshes.map(mesh=><WardrobeField key={mesh} label={mesh}><StudioInput type="checkbox" aria-label={'Скрывать '+mesh} checked={item.hideMeshes.includes(mesh)} disabled={disabled} onChange={()=>itemEdit(index,{hideMeshes:toggle(item.hideMeshes,mesh)})}/></WardrobeField>)}</details>
   {item.masks.map((mask,mi)=><div className="au-stack" key={mi}>
    <WardrobeField label="Материал тела для маски"><StudioSelect value={mask.material} disabled={disabled} onChange={e=>itemEdit(index,{masks:item.masks.map((m,i)=>i===mi?{...m,material:e.target.value}:m)})}><option value="">Выберите материал тела</option>{info?.materials.map((m,i)=><option key={i}>{m}</option>)}</StudioSelect></WardrobeField>
    <AssetReferencePicker value={mask.texture} kinds={['image']} label="Маска видимости · чёрное скрыто, белое видно" disabled={disabled} onChange={texture=>itemEdit(index,{masks:item.masks.map((m,i)=>i===mi?{...m,texture}:m)})}/>
    <StudioButton disabled={disabled} onClick={()=>itemEdit(index,{masks:item.masks.filter((_,i)=>i!==mi)})}>Убрать маску</StudioButton>
   </div>)}
   <div className="au-row"><StudioButton disabled={disabled||!info||item.masks.length>=16} onClick={()=>itemEdit(index,{masks:[...item.masks,{material:'',texture:''}]})}>Добавить маску тела</StudioButton><StudioButton disabled={disabled} onClick={()=>edit({...recipe,items:recipe.items.filter((_,i)=>i!==index)})}>Убрать вещь</StudioButton></div>
  </div></details>)}
  <div className="au-row"><StudioButton disabled={disabled||!info||recipe.items.length>=32} onClick={()=>edit({...recipe,items:[...recipe.items,{id:'item_'+Date.now(),asset:'',slot:slots.find(s=>!recipe.items.some(i=>i.enabled&&i.slot===s))??'attachment_'+Date.now(),enabled:true,bone:'',translation:[0,0,0],rotation:[0,0,0],scale:[1,1,1],finish:'cloth',hideMeshes:[],masks:[]}]})}>Добавить одежду / аксессуар</StudioButton><StudioButton disabled={disabled||!history.length} onClick={()=>{setRecipe(history[history.length-1]);setHistory(history.slice(0,-1));}}>Отменить изменение</StudioButton></div>
  <WardrobeField label="Имя собранного персонажа" hint="Новый ассет в content/Characters. Существующие сборки не перезаписываются."><StudioInput value={name} disabled={disabled} onChange={e=>setName(e.target.value)}/></WardrobeField>
  <WardrobeField label="Максимальный размер запекаемых текстур"><StudioSelect value={size} disabled={disabled} onChange={e=>setSize(Number(e.target.value))}>{[256,512,1024,2048,4096].map(n=><option key={n}>{n}</option>)}</StudioSelect></WardrobeField>
  <StudioButton variant="primary" disabled={disabled||!guid||!(/^[A-Za-z0-9_-]{1,64}$/).test(name)} onClick={()=>void run(async()=>{setJob(await invoke('studio.humanCharacterBuild',{guid,look,wardrobe:recipe,name,textureSize:size,face}));})}>{job?.running?'Собирается…':'Собрать персонажа'}</StudioButton>
  {job?.running&&<StudioAlert>Импорт, запекание материалов и масок, сборка мешей, проверка повторным импортом GLB…</StudioAlert>}
  {job?.path&&<><StudioAlert tone="success">{job.path} · {job.meshes} мешей · {job.materials} материалов · {job.vertices} вершин. Рецепт сохранён рядом в source.json.</StudioAlert><StudioButton disabled={disabled||!job.guid} onClick={()=>void run(async()=>{const current=await invoke('studio.humanAppearanceInfo',{});await invoke('studio.humanAppearanceApply',{guid:job.guid!,revision:current.preview.revision,face:null,look:{version:1,values:{},materials:{}}});})}>Показать собранного персонажа</StudioButton></>}
 </StudioSection>;
}

function WardrobeField({label,hint,children}:{label:string;hint?:string;children:ReactElement<{id?:string}>}) {
 const id=useId();return <StudioField label={label} hint={hint} htmlFor={id}>{cloneElement(children,{id})}</StudioField>;
}
