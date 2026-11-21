import { useMemo } from 'react';

/**
 * Default splash art when a game has none of its own: a layered landscape at dusk (sky, stars, sun glow,
 * four mountain ridges with haze). Seeded by the project name, so each project keeps its own palette and skyline.
 */
const PALETTES = [
  { sky: ['#070b1f', '#1b1f4a', '#5b3a72', '#e0866a'], sun: '#ffc98a', ridges: ['#3b2d5c', '#2a2147', '#1a1631', '#0d0b1c'], haze: '#c77a8a' },   // dusk
  { sky: ['#03121c', '#0b3447', '#16707e', '#7fd6c2'], sun: '#d9fff4', ridges: ['#145565', '#0d3f4d', '#082b36', '#041820'], haze: '#5fb8b0' },   // ocean
  { sky: ['#160a04', '#4a1f0a', '#a3501c', '#f2b35c'], sun: '#ffe2a3', ridges: ['#7a3a18', '#5a2a12', '#3c1b0b', '#1f0e06'], haze: '#e08a45' },   // desert
  { sky: ['#050f0b', '#103326', '#2c6b4a', '#a8d99a'], sun: '#f4ffd6', ridges: ['#2a5a3e', '#1d432e', '#132e20', '#091910'], haze: '#7ab88a' },   // forest
  { sky: ['#05060f', '#141a3a', '#2c4b8f', '#9fc4ff'], sun: '#e8f1ff', ridges: ['#2a3a6e', '#1d2a52', '#141d3a', '#0a0f20'], haze: '#7c9ad6' },   // night
];

function rng(seed: string) {
  let h = 2166136261;
  for (const c of seed) { h ^= c.charCodeAt(0); h = Math.imul(h, 16777619); }
  return () => { h ^= h << 13; h ^= h >>> 17; h ^= h << 5; return ((h >>> 0) % 100000) / 100000; };
}

/** A ridge line across W at base height y with amplitude a: sum of three seeded sines plus a little jitter. */
function ridge(r: () => number, W: number, H: number, y: number, a: number, rough: number): string {
  const f = [0.6 + r() * 0.8, 1.6 + r() * 1.6, 4 + r() * 4], p = [r() * 6.28, r() * 6.28, r() * 6.28];
  const pts: string[] = [];
  for (let x = 0; x <= W; x += 8) {
    const t = x / W * Math.PI * 2;
    const v = Math.sin(t * f[0] + p[0]) * 0.6 + Math.sin(t * f[1] + p[1]) * 0.3 + Math.sin(t * f[2] + p[2]) * 0.1 * rough + (r() - 0.5) * 0.04 * rough;
    pts.push(`${x},${(y - v * a).toFixed(1)}`);
  }
  return `M0,${H} L${pts.join(' L')} L${W},${H} Z`;
}

export default function SplashArt({ seed }: { seed: string }) {
  const art = useMemo(() => {
    const r = rng(seed || 'aiverse');
    const pal = PALETTES[Math.floor(r() * PALETTES.length)];
    const W = 1600, H = 900;
    const sun = { x: W * (0.55 + r() * 0.3), y: H * (0.42 + r() * 0.1) };
    const stars = Array.from({ length: 110 }, () => ({ x: r() * W, y: r() * H * 0.5, s: r() < 0.08 ? 1.6 : 0.6 + r() * 0.6, o: 0.25 + r() * 0.6 }));
    const ridges = [0.6, 0.68, 0.77, 0.88].map((k, i) => ({ d: ridge(r, W, H, H * k, H * (0.09 - i * 0.012), 1 + i * 0.6), c: pal.ridges[i] }));
    return { pal, W, H, sun, stars, ridges };
  }, [seed]);
  const { pal, W, H, sun, stars, ridges } = art;
  return <svg className="splash-art" viewBox={`0 0 ${W} ${H}`} preserveAspectRatio="xMidYMid slice" aria-hidden>
    <defs>
      <linearGradient id="sa-sky" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0" stopColor={pal.sky[0]}/><stop offset="0.38" stopColor={pal.sky[1]}/>
        <stop offset="0.62" stopColor={pal.sky[2]}/><stop offset="0.78" stopColor={pal.sky[3]}/>
      </linearGradient>
      <radialGradient id="sa-sun" cx={sun.x} cy={sun.y} r={H * 0.55} gradientUnits="userSpaceOnUse">
        <stop offset="0" stopColor={pal.sun} stopOpacity="0.95"/><stop offset="0.06" stopColor={pal.sun} stopOpacity="0.85"/>
        <stop offset="0.07" stopColor={pal.sun} stopOpacity="0.35"/><stop offset="0.35" stopColor={pal.haze} stopOpacity="0.18"/>
        <stop offset="1" stopColor={pal.haze} stopOpacity="0"/>
      </radialGradient>
      <linearGradient id="sa-haze" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0" stopColor={pal.haze} stopOpacity="0"/><stop offset="1" stopColor={pal.haze} stopOpacity="0.35"/>
      </linearGradient>
    </defs>
    <rect width={W} height={H} fill="url(#sa-sky)"/>
    <g className="splash-art__stars">{stars.map((s, i) => <circle key={i} cx={s.x} cy={s.y} r={s.s} fill="#fff" opacity={s.o}/>)}</g>
    <rect width={W} height={H} fill="url(#sa-sun)"/>
    {ridges.map((rd, i) => <g key={i} className={'splash-art__ridge splash-art__ridge--' + i}>
      <path d={rd.d} fill={rd.c}/>
      {i < 3 && <path d={rd.d} fill="url(#sa-haze)"/>}
    </g>)}
  </svg>;
}
