/** Persistable nested docking geometry; no React, renderer or genre dependencies. */
export const STUDIO_PANELS = ['hierarchy','assets','scene','game','materials','animation','inspector','console','lighting','shader_graph'] as const;
export type StudioPanel = typeof STUDIO_PANELS[number];
/** Single metadata source for the UI shell and AI-authored panel integrations. */
export const STUDIO_PANEL_TITLES: Record<StudioPanel,string> = {
  hierarchy: 'Hierarchy', assets: 'Assets', scene: 'Scene', game: 'Game',
  materials: 'Materials', animation: 'Animation', inspector: 'Inspector', console: 'Console', lighting: 'Lighting', shader_graph: 'Shader Graph'
};
export const STUDIO_PANEL_HOME: Record<StudioPanel,string> = {
  hierarchy: 'dock-left', assets: 'dock-left', scene: 'dock-center', game: 'dock-center',
  materials: 'dock-center', animation: 'dock-center', inspector: 'dock-right', console: 'dock-bottom', lighting: 'dock-right', shader_graph: 'dock-center'
};

export type DockAxis = 'horizontal' | 'vertical';
export type DockEdge = 'left' | 'right' | 'top' | 'bottom';
export interface DockStack { kind: 'stack'; id: string; tabs: StudioPanel[]; active: StudioPanel | null }
export interface DockSplit { kind: 'split'; id: string; axis: DockAxis; ratio: number; first: DockNode; second: DockNode }
export type DockNode = DockStack | DockSplit;
export interface DockTree { version: 3; root: DockNode }
export interface PreviousDockLayout { areas: Record<string, unknown>; active?: Record<string, unknown>; leftSize?: number; rightSize?: number; bottomSize?: number }
const ALL = new Set<string>(STUDIO_PANELS);
const STORAGE_KEY = 'aiverse.studio.dock.v3';
const PREVIOUS_KEY = 'aiverse.studio.dock.v2';
const MIN_RATIO = .12, MAX_RATIO = .88;
const MAX_DEPTH = 8, MAX_NODES = 48;
function known(value: unknown): value is StudioPanel { return typeof value === 'string' && ALL.has(value); }
function object(value: unknown): Record<string, unknown> | null {
  return value !== null && typeof value === 'object' && !Array.isArray(value) ? value as Record<string, unknown> : null;
}
function size(value: unknown, fallback: number, min: number, max: number): number {
  return typeof value === 'number' && Number.isFinite(value) ? Math.min(max,Math.max(min,value)) : fallback;
}
function stack(id: string, tabs: StudioPanel[], active: StudioPanel | null = tabs[0] ?? null): DockStack {
  return {kind:'stack',id,tabs,active};
}
function split(id: string, axis: DockAxis, ratio: number, first: DockNode, second: DockNode): DockSplit {
  return {kind:'split',id,axis,ratio,first,second};
}
export function defaultDockTree(): DockTree {
  return { version:3, root:split('root','vertical',.76,
    split('split-body-left','horizontal',.21,
      stack('dock-left',['hierarchy','assets'],'hierarchy'),
      split('split-body-right','horizontal',.77,
        stack('dock-center',['scene','game','materials','animation','shader_graph'],'scene'),
        stack('dock-right',['inspector','lighting'],'inspector'))),
    stack('dock-bottom',['console'],'console')) };
}
function traverse(node: DockNode, visitor: (n: DockNode)=>void): void {
  visitor(node);
  if(node.kind==='split') { traverse(node.first,visitor); traverse(node.second,visitor); }
}
export function dockStacks(tree: DockTree): DockStack[] {
  const results: DockStack[]=[];
  traverse(tree.root,node=>{ if(node.kind==='stack')results.push(node); });
  return results;
}
export function findDockStack(tree: DockTree, id: string): DockStack | null {
  return dockStacks(tree).find(s=>s.id===id) ?? null;
}
export function panelStack(tree: DockTree, panel: StudioPanel): string | null {
  return dockStacks(tree).find(s=>s.tabs.includes(panel))?.id ?? null;
}
export function normalizeDockTree(input: unknown): DockTree {
  const source=object(input);
  if(!source || !object(source.root))return defaultDockTree();
  let count=0;
  const seenIds=new Set<string>(), seenPanels=new Set<StudioPanel>();
  function visit(inputNode: unknown, depth: number): DockNode | null {
    const value=object(inputNode);
    if(!value || depth>MAX_DEPTH || ++count>MAX_NODES) return null;
    const rawId=typeof value.id==='string' && /^[a-z0-9_-]{1,60}$/i.test(value.id) ? value.id : "node-"+count;
    const id=seenIds.has(rawId)?"node-"+count+"-"+depth:rawId;
    seenIds.add(id);
    if(value.kind==='stack') {
      const tabs: StudioPanel[]=[];
      if(Array.isArray(value.tabs))for(const tab of value.tabs) {
        if(known(tab) && !seenPanels.has(tab)) { tabs.push(tab);seenPanels.add(tab); }
      }
      return stack(id,tabs,known(value.active) && tabs.includes(value.active) ? value.active : tabs[0] ?? null);
    }
    if(value.kind==='split') {
      const axis: DockAxis=value.axis==='vertical'?'vertical':'horizontal';
      const first=visit(value.first,depth+1),second=visit(value.second,depth+1);
      if(!first || !second)return null;
      return split(id,axis,size(value.ratio,.5,MIN_RATIO,MAX_RATIO),first,second);
    }
    return null;
  }
  const root=visit(source.root,0);
  return root ? {version:3,root} : defaultDockTree();
}
function update(node: DockNode, id: string, change: (node: DockNode)=>DockNode): DockNode {
  if(node.id===id)return change(node);
  return node.kind==='split'?{...node,first:update(node.first,id,change),second:update(node.second,id,change)}:node;
}
function remove(node: DockNode, panel: StudioPanel): DockNode {
  if(node.kind==='split')return {...node,first:remove(node.first,panel),second:remove(node.second,panel)};
  if(!node.tabs.includes(panel))return node;
  const tabs=node.tabs.filter(x=>x!==panel);
  return {...node,tabs,active:node.active===panel?tabs[0]??null:node.active};
}
function newId(tree: DockTree, prefix: string): string {
  const ids=new Set<string>();traverse(tree.root,n=>ids.add(n.id));
  for(let i=1;i<=MAX_NODES+1;i++)if(!ids.has(prefix+"-"+i))return prefix+"-"+i;
  return prefix+"-"+(MAX_NODES+2);
}
export function moveTab(tree: DockTree, panel: StudioPanel, stackId: string, before?: StudioPanel): DockTree {
  const t=normalizeDockTree(tree);
  if(!known(panel) || !findDockStack(t,stackId))return t;
  const without=remove(t.root,panel);
  const root=update(without,stackId,n=>{
    if(n.kind!=='stack')return n;
    const tabs=[...n.tabs];
    const at=before?tabs.indexOf(before):-1;
    if(at<0)tabs.push(panel);else tabs.splice(at,0,panel);
    return {...n,tabs,active:panel};
  });
  return {version:3,root};
}
export function closeTab(tree: DockTree, panel: StudioPanel): DockTree {
  const t=normalizeDockTree(tree);
  return {version:3,root:remove(t.root,panel)};
}
export function activateTab(tree: DockTree, stackId: string, panel: StudioPanel): DockTree {
  const t=normalizeDockTree(tree);
  return {version:3,root:update(t.root,stackId,n=>n.kind==='stack' && n.tabs.includes(panel)?{...n,active:panel}:n)};
}
export function reopenTab(tree: DockTree, panel: StudioPanel, preferredStackId='dock-center'): DockTree {
  const t=normalizeDockTree(tree);
  const present=panelStack(t,panel);
  const target=present || (findDockStack(t,preferredStackId)?preferredStackId:dockStacks(t)[0]?.id);
  return target?moveTab(t,panel,target):t;
}
export function resizeDockSplit(tree: DockTree, id: string, ratio: number): DockTree {
  const t=normalizeDockTree(tree);
  if(!Number.isFinite(ratio))return t;
  return {version:3,root:update(t.root,id,n=>n.kind==='split'?{...n,ratio:size(ratio,.5,MIN_RATIO,MAX_RATIO)}:n)};
}
export function splitDockStack(tree: DockTree, panel: StudioPanel, stackId: string, side: DockEdge): DockTree {
  const t=normalizeDockTree(tree),target=findDockStack(t,stackId);
  if(!target || !known(panel))return t;
  const from=panelStack(t,panel);
  if(from===stackId && target.tabs.length===1)return t;
  if(dockStacks(t).length>=24)return t;
  const next=remove(t.root,panel);
  const splitId=newId(t,'split'), stackIdNew=newId(t,'dock');
  const axis:DockAxis=side==='left'||side==='right'?'horizontal':'vertical';
  const early=side==='left'||side==='top';
  const root=update(next,stackId,n=>{
    if(n.kind!=='stack')return n;
    const fresh=stack(stackIdNew,[panel],panel);
    return split(splitId,axis,.5,early?fresh:n,early?n:fresh);
  });
  return normalizeDockTree({version:3,root});
}
export function migrateDockV2(input: unknown): DockTree {
  const v=object(input),areas=object(v?.areas),active=object(v?.active);
  if(!areas)return defaultDockTree();
  const baseline=defaultDockTree();
  const byArea:Record<string,string>={left:'dock-left',center:'dock-center',right:'dock-right',bottom:'dock-bottom'};
  let root=baseline.root;
  const taken=new Set<StudioPanel>();
  for(const [area,stackId] of Object.entries(byArea)) {
    const raw=areas[area]; const tabs:StudioPanel[]=[];
    if(Array.isArray(raw))for(const x of raw)if(known(x)&&!taken.has(x)) {tabs.push(x);taken.add(x);}
    root=update(root,stackId,n=>n.kind==='stack'?{...n,tabs,active:known(active?.[area]) && tabs.includes(active[area] as StudioPanel)?active[area] as StudioPanel:tabs[0]??null}:n);
  }
  const left=size(v?.leftSize,260,180,600),right=size(v?.rightSize,300,180,600),bottom=size(v?.bottomSize,180,110,480);
  const top=update(root,'root',n=>n.kind==='split'?{...n,ratio:.12+(.88-.12)*Math.max(0,1-bottom/800)}:n);
  const withLeft=update(top,'split-body-left',n=>n.kind==='split'?{...n,ratio:size(left/1200,.21,.12,.88)}:n);
  const withRight=update(withLeft,'split-body-right',n=>n.kind==='split'?{...n,ratio:size(1-right/1000,.77,.12,.88)}:n);
  return normalizeDockTree({version:3,root:withRight});
}
export function readNestedDock(): DockTree {
  if(typeof window==='undefined')return defaultDockTree();
  try{
    const stored=window.localStorage.getItem(STORAGE_KEY);
    if(stored!==null)return normalizeDockTree(JSON.parse(stored) as unknown);
    const previous=window.localStorage.getItem(PREVIOUS_KEY);
    return previous!==null?migrateDockV2(JSON.parse(previous) as unknown):defaultDockTree();
  }catch{return defaultDockTree();}
}
export function saveNestedDock(tree: DockTree): void {
  if(typeof window==='undefined')return;
  try{window.localStorage.setItem(STORAGE_KEY,JSON.stringify(normalizeDockTree(tree)));}catch{}
}
