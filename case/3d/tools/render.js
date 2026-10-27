// Render STL parts to PNGs with three.js in headless Chromium.
// Usage: node render.js spec.json   (run in the clipsnag:local image: Playwright + Chromium)
// spec: { out: dir, size: [w,h], parts: [{file, color, offset:[x,y,z], opacity}],
//         views: [{name, dir:[x,y,z], up:[0,0,1], zoom, section: {axis, at}}] }
const fs = require('fs');
const path = require('path');
const { chromium } = require('/app/node_modules/playwright');

const spec = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));

const html = `<!doctype html><html><head><style>html,body{margin:0;background:#eef1f4}</style>
<script type="importmap">{"imports":{"three":"https://cdn.jsdelivr.net/npm/three@0.160.0/build/three.module.js","three/addons/":"https://cdn.jsdelivr.net/npm/three@0.160.0/examples/jsm/"}}</script>
</head><body><script type="module">
import * as THREE from 'three';
import { STLLoader } from 'three/addons/loaders/STLLoader.js';
const spec = window.SPEC;
const [W,H] = spec.size;
const r = new THREE.WebGLRenderer({antialias:true, preserveDrawingBuffer:true});
r.setSize(W,H); r.setPixelRatio(1); r.shadowMap.enabled = true;
r.localClippingEnabled = true;
r.outputColorSpace = THREE.SRGBColorSpace; r.toneMapping = THREE.ACESFilmicToneMapping; r.toneMappingExposure = 1.25;
document.body.appendChild(r.domElement);
const scene = new THREE.Scene();
scene.background = new THREE.Color(spec.bg || '#eef1f4');
scene.add(new THREE.HemisphereLight(0xffffff, 0x8899aa, 1.1));
const key = new THREE.DirectionalLight(0xffffff, 2.8); key.position.set(-60,-80,120); key.castShadow = true;
key.shadow.mapSize.set(2048,2048); Object.assign(key.shadow.camera,{left:-80,right:80,top:80,bottom:-80,near:1,far:400});
scene.add(key);
const fill = new THREE.DirectionalLight(0xffffff, 0.6); fill.position.set(90,40,60); scene.add(fill);
const loader = new STLLoader();
const meshes = [];
const box = new THREE.Box3();
for (const p of spec.parts) {
  const buf = await (await fetch('/files/' + encodeURIComponent(p.file))).arrayBuffer();
  const g = loader.parse(buf); g.computeVertexNormals();
  const m = new THREE.Mesh(g, new THREE.MeshStandardMaterial({color:p.color||'#ffffff', roughness:0.55, metalness:0.0,
      transparent: (p.opacity??1) < 1, opacity: p.opacity??1, side: THREE.DoubleSide, clippingPlanes: []}));
  m.castShadow = true; m.receiveShadow = true;
  if (p.offset) m.position.set(...p.offset);
  if (p.rotate) m.rotation.set(...p.rotate.map(d=>d*Math.PI/180));
  scene.add(m); meshes.push(m); box.expandByObject(m);
}
for (const d of (spec.decals || [])) {
  const tex = await new THREE.TextureLoader().loadAsync('/files/' + encodeURIComponent(d.image));
  tex.colorSpace = THREE.SRGBColorSpace; tex.anisotropy = 8;
  const plane = new THREE.Mesh(new THREE.PlaneGeometry(d.w, d.h), new THREE.MeshBasicMaterial({map: tex, toneMapped: false}));
  const u = new THREE.Vector3(...d.u), v = new THREE.Vector3(...d.v), n = new THREE.Vector3().crossVectors(u, v);
  plane.quaternion.setFromRotationMatrix(new THREE.Matrix4().makeBasis(u, v, n));
  plane.position.set(...d.center);
  if (d.offset) plane.position.add(new THREE.Vector3(...d.offset));
  scene.add(plane);
}
const center = box.getCenter(new THREE.Vector3());
const radius = box.getSize(new THREE.Vector3()).length()/2;
const ground = new THREE.Mesh(new THREE.PlaneGeometry(1000,1000), new THREE.ShadowMaterial({opacity:0.18}));
ground.position.z = box.min.z - 0.01; ground.receiveShadow = true; scene.add(ground);
const cam = new THREE.PerspectiveCamera(spec.fov||28, W/H, 0.1, 2000);
window.renderView = (v) => {
  const d = new THREE.Vector3(...v.dir).normalize();
  const dist = radius / Math.sin((cam.fov*Math.PI/180)/2) * (v.zoom||1.15);
  const tgt = v.target ? new THREE.Vector3(...v.target) : center;
  cam.position.copy(tgt).addScaledVector(d, dist);
  cam.up.set(...(v.up||[0,0,1])); cam.lookAt(tgt);
  ground.visible = v.ground !== false;
  for (const m of meshes) {
    m.material.clippingPlanes = [];
    if (v.section) {
      const n = new THREE.Vector3(...v.section.normal);
      m.material.clippingPlanes = [new THREE.Plane(n, -v.section.at)];
    }
  }
  r.render(scene, cam);
  return r.domElement.toDataURL('image/png');
};
window.READY = true;
</script></body></html>`;

(async () => {
  const browser = await chromium.launch({ args: ['--use-gl=swiftshader', '--enable-webgl', '--ignore-gpu-blocklist'] });
  const page = await browser.newPage({ viewport: { width: spec.size[0], height: spec.size[1] } });
  page.on('console', (m) => console.log('[page]', m.text()));
  page.on('pageerror', (e) => console.log('[pageerror]', e.message));
  await page.route('http://local/**', async (route) => {
    const u = new URL(route.request().url());
    if (u.pathname === '/') return route.fulfill({ contentType: 'text/html', body: html });
    if (u.pathname.startsWith('/files/')) {
      const f = decodeURIComponent(u.pathname.slice(7));
      return route.fulfill({ contentType: 'application/octet-stream', body: fs.readFileSync(f) });
    }
    return route.fulfill({ status: 404, body: '' });
  });
  await page.addInitScript((s) => { window.SPEC = s; }, spec);
  await page.goto('http://local/');
  await page.waitForFunction('window.READY === true', null, { timeout: 120000 });
  for (const v of spec.views) {
    const data = await page.evaluate((v) => window.renderView(v), v);
    fs.writeFileSync(path.join(spec.out, v.name + '.png'), Buffer.from(data.split(',')[1], 'base64'));
    console.log('wrote', v.name);
  }
  await browser.close();
})();
