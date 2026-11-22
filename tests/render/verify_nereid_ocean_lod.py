#!/usr/bin/env python3
"""Real native GPU acceptance: NEREID open-sea LOD over a large world.
Usage: python3 verify_nereid_ocean_lod.py /absolute/renderbench /KINGSTON/output [runs]
Variants: legacy 256 m patch (default), legacy 6 km uniform tiled patch, 6 km
stitched LOD rings, and no water (reference). Cameras sit ~3.9 km from the
origin: a low camera just before a legacy patch-cell switch, a high camera and
a grazing camera. Reports water triangles/draws, frame times, ocean coverage
below the horizon and the image change caused by a ring rebuild.
Frame times are whole-frame and the GPU is shared with other processes:
they are indicative, repeated `runs` times, never a benchmark claim.
"""
import base64, json, math, os, re, statistics, struct, subprocess, sys
from pathlib import Path

OX, OZ = 3000.0, -2500.0


def ppm(path):
    with open(path, 'rb') as f:
        assert f.readline().strip() == b'P6'
        line = f.readline()
        while line.startswith(b'#'):
            line = f.readline()
        w, h = map(int, line.split())
        assert int(f.readline()) == 255
        data = f.read()
    assert len(data) == w*h*3
    return w, h, data


def unit(a):
    l = math.sqrt(sum(x*x for x in a)); return [x/l for x in a]


def horizon_row(cam, height, distance=20000):
    eye, tgt = cam['eye'], cam['target']
    fwd = unit([t-e for t, e in zip(tgt, eye)])
    flat = unit([fwd[0], 0, fwd[2]])
    p = [eye[0]+flat[0]*distance, 0, eye[2]+flat[2]*distance]
    right = unit([fwd[1]*0-fwd[2]*1, fwd[2]*0-fwd[0]*0, fwd[0]*1-fwd[1]*0])
    up = [right[1]*fwd[2]-right[2]*fwd[1], right[2]*fwd[0]-right[0]*fwd[2], right[0]*fwd[1]-right[1]*fwd[0]]
    rel = [a-b for a, b in zip(p, eye)]
    depth = sum(a*b for a, b in zip(rel, fwd))
    scale = height/(2*depth*math.tan(math.radians(cam['fovYDegrees'])/2))
    return int(height/2-scale*sum(a*b for a, b in zip(rel, up)))


def coverage(img, ref, row0):
    w, h, a = img; _, _, b = ref
    row0 = max(0, min(h-1, row0))
    total = changed = 0
    for y in range(row0, h, 2):
        for x in range(0, w, 2):
            i = (y*w+x)*3
            total += 1
            if abs(a[i]-b[i])+abs(a[i+1]-b[i+1])+abs(a[i+2]-b[i+2]) > 6:
                changed += 1
    return changed/max(1, total)


def mean_diff(a, b):
    return sum(abs(x-y) for x, y in zip(a[2], b[2]))/len(a[2])


def main():
    binary, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    runs = int(sys.argv[3]) if len(sys.argv) > 3 else 3
    out.mkdir(parents=True, exist_ok=True)
    # Small island behind the start position: the bench needs a model.
    v = [OX-1500, 1, OZ-30, OX-1440, 1, OZ-30, OX-1440, 1, OZ+30, OX-1500, 1, OZ+30]
    raw = struct.pack('<12f', *v)+struct.pack('<6H', 0, 2, 1, 0, 3, 2)
    gltf = {'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': [0]}], 'nodes': [{'mesh': 0}],
            'meshes': [{'primitives': [{'attributes': {'POSITION': 0}, 'indices': 1}]}],
            'buffers': [{'byteLength': len(raw), 'uri': 'data:application/octet-stream;base64,'+base64.b64encode(raw).decode()}],
            'bufferViews': [{'buffer': 0, 'byteLength': 48}, {'buffer': 0, 'byteOffset': 48, 'byteLength': 12}],
            'accessors': [{'bufferView': 0, 'componentType': 5126, 'count': 4, 'type': 'VEC3',
                           'min': [OX-1500, 1, OZ-30], 'max': [OX-1440, 1, OZ+30]},
                          {'bufferView': 1, 'componentType': 5123, 'count': 6, 'type': 'SCALAR'}]}
    (out/'island.gltf').write_text(json.dumps(gltf))
    edge = math.floor((OX+71.5)/256)*256-0.5  # 2815.5: 0.5 m before a legacy 256 m cell switch
    cameras = [
        {'name': 'low-cell-edge', 'eye': [edge, 6, OZ], 'target': [edge+400, 2, OZ], 'fovYDegrees': 60},
        {'name': 'high', 'eye': [OX, 400, OZ], 'target': [OX+2500, 0, OZ+300], 'fovYDegrees': 60},
        {'name': 'grazing-far', 'eye': [OX, 30, OZ], 'target': [OX+5000, 0, OZ-900], 'fovYDegrees': 60},
    ]
    style = {'deepColor': [0.02, 0.16, 0.24], 'absorption': 0.35, 'depthTint': 0.8, 'opacity': 0.9}
    base = {'model': str(out/'island.gltf'), 'width': 960, 'height': 540, 'near': .2, 'far': 12000,
            'cameras': cameras, 'sunDirection': [-.5, .7, -.4], 'sunColor': [1, 1, 1], 'sunIntensity': 1,
            'skyColor': [.55, .65, .8], 'groundColor': [.1, .1, .1], 'fogDensity': 0, 'clearColor': [.55, .65, .8]}
    sea = {'id': 1, 'kind': 'ocean', 'shape': 'infinite', 'surfaceY': 0, 'bottomY': -200, 'style': style}
    variants = {
        'nowater': None,
        'patch256': dict(sea),
        'patch6000-tiled': dict(sea, oceanPatchRadius=6000, rippleTileGrid=8, lod='near'),
        'rings6000': dict(sea, oceanLod='rings', oceanPatchRadius=6000, oceanInnerCell=4, oceanInnerCells=16,
                          lod='medium', rippleTileGrid=4),
    }

    def render(name, surface, cam, eye=None, samples=30):
        scene = json.loads(json.dumps(base))
        if surface is not None:
            scene['nereidWater'] = {'surfaces': [surface]}
        if eye is not None:
            scene['cameras'][cam]['eye'] = eye[0]
            scene['cameras'][cam]['target'] = eye[1]
        (out/(name+'.scene.json')).write_text(json.dumps(scene))
        env = dict(os.environ); env.pop('AIVERSE_NEREID_DIAGNOSTIC', None); env.pop('AIVERSE_BENCH_QUICK', None)
        with (out/(name+'.log')).open('w') as log:
            rc = subprocess.run([str(binary), '--scene', str(out/(name+'.scene.json')), '--output', str(out/(name+'.json')),
                                 '--capture', str(out/(name+'.ppm')), '--warmup', '10', '--samples', str(samples),
                                 '--camera', str(cam), '--freeze-time', '0', '--profile', 'low'],
                                env=env, stdout=log, stderr=subprocess.STDOUT, timeout=300).returncode
        rep = json.loads((out/(name+'.json')).read_text())
        text = (out/(name+'.log')).read_text()
        assert rc == 0 and rep.get('complete') and not rep.get('error'), (name, rep.get('error'))
        assert 'GL frame error' not in text and all(int(x) == 0 for x in re.findall(r'pipelineFailures=(\d+)', text)), name
        return rep, ppm(out/(name+'.ppm'))

    results = {}
    images = {}
    for ci, cam in enumerate(cameras):
        for vn, surf in variants.items():
            rows = []
            for run in range(runs):
                rep, img = render(f'{vn}-{cam["name"]}-r{run}', surf, ci)
                frames = [f for f in rep['frames'] if f['camera'] == cam['name']] or rep['frames']
                rows.append({'cpuFrameMs': statistics.median(f['cpuFrameMs'] for f in frames),
                             'gpuMs': statistics.median([f['gpuMs'] for f in frames if isinstance(f.get('gpuMs'), (int, float))] or [float('nan')]),
                             'drawCalls': frames[-1]['drawCalls'],
                             'waterDraws': frames[-1].get('nereidWaterDraws', 0),
                             'waterTriangles': frames[-1].get('nereidWaterTriangles', 0)})
                images[vn, ci] = img
            results.setdefault(cam['name'], {})[vn] = {
                'waterTriangles': rows[-1]['waterTriangles'], 'waterDraws': rows[-1]['waterDraws'],
                'drawCalls': rows[-1]['drawCalls'],
                'cpuFrameMsMedianPerRun': [round(r['cpuFrameMs'], 3) for r in rows],
                'gpuMsMedianPerRun': [None if math.isnan(r['gpuMs']) else round(r['gpuMs'], 3) for r in rows]}
            print(cam['name'], vn, results[cam['name']][vn], flush=True)
        h = images['nowater', ci][1]
        # Region the authored 6 km radius must cover (rows below the 5.5 km
        # point), and the whole band down from the true horizon (informative:
        # any finite ocean leaves a sliver below the horizon from altitude).
        inside = horizon_row(cam, h, 5500)+1
        horizon = horizon_row(cam, h)+3
        for vn in variants:
            if vn != 'nowater':
                results[cam['name']][vn]['oceanCoverageWithinRadius'] = round(coverage(images[vn, ci], images['nowater', ci], inside), 4)
                results[cam['name']][vn]['oceanCoverageToHorizon'] = round(coverage(images[vn, ci], images['nowater', ci], horizon), 4)
        print(cam['name'], 'coverage', {vn: (results[cam['name']][vn].get('oceanCoverageWithinRadius'), results[cam['name']][vn].get('oceanCoverageToHorizon')) for vn in variants}, flush=True)
    # Ring rebuild popping: move the camera 0.4 m across a 32 m snap boundary vs
    # 0.4 m inside one cell; the crossing must not change the image more.
    step = 32.0
    x0 = math.floor((OX+40)/step)*step
    pops = {}
    for label, a, b in [('inside', x0-12.2, x0-11.8), ('crossing', x0-0.2, x0+0.2)]:
        imgs = []
        for i, x in enumerate([a, b]):
            _, img = render(f'pop-{label}-{i}', variants['rings6000'], 0, ([x, 6, OZ], [x+400, 2, OZ]), samples=30)
            imgs.append(img)
        pops[label] = round(mean_diff(imgs[0], imgs[1]), 4)
    print('ring rebuild image change', pops, flush=True)
    low, ring = results['low-cell-edge'], results['low-cell-edge']['rings6000']
    verdict = {
        'ringsCoverAllCameras': all(results[c['name']]['rings6000']['oceanCoverageWithinRadius'] >= 0.99 for c in cameras),
        'ringTrianglesBounded': all(results[c['name']]['rings6000']['waterTriangles'] < 20000 for c in cameras),
        'ringRebuildNoPop': pops['crossing'] <= pops['inside']*1.5+0.5,
    }
    (out/'ocean-lod-results.json').write_text(json.dumps({'binary': str(binary), 'runs': runs, 'cameras': cameras,
        'results': results, 'ringRebuildImageChange': pops, 'verdict': verdict}, indent=2))
    assert all(verdict.values()), verdict
    print('PASS', verdict)


if __name__ == '__main__':
    main()
