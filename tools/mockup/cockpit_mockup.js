// WipEout VR cockpit mockup (Cockpit C, full canopy). Inlined into one HTML
// file by build_cockpit_mockup.py together with three.js and the exported data.
//
// Coordinates. The game is +X right, +Y down, +Z forward, in game units. Here
// everything is converted once on load to three.js axes (x, -y, -z): +X right,
// +Y up, +Z back, still in game units. The whole cockpit (hull, cabin, frame,
// dash) is part of the ship, so it is authored in ship space. It is written in
// metres *at the reference scale* (REF = 128 units per metre, the current
// world_scale placeholder) relative to the game's own eye point ship_cockpit()
// = (0, -128, 0): P(r, u, f) is r metres right, u up and f forward of the eye.
// Changing world_scale scales the whole ship and world together about the eye
// (lesson 29), so it changes no angle: only the physical size of everything
// against the wearer's body (eye adjust in cm, the body reference, IPD).
'use strict';

// The cockpit is sized in metres for a real pilot. REF is the world_scale it
// is built at (units per metre) and EYE_Y the eye's height above the ship
// origin in units (three.js +y up; game y = -EYE_Y). Both are set per build:
// REF = world_scale, and EYE_Y either the game's ship_cockpit() (128) or
// fitted from a seated pilot whose tub floor stays inside every hull (fitEye).
let REF = 128;
let EYE_Y = 128;
const GAME_EYE_Y = 128;     // ship_cockpit() = (0,-128,0) in game space
// world_scale candidates (2026-10-04, round 3); 128 with the game's eye is the round-2 reference
const SCALE_OPTIONS = [
	{ ws: 128, eye: 'game', label: '128, game eye (round 2)' },
	{ ws: 112, eye: 'fit', label: '112' },
	{ ws: 100, eye: 'fit', label: '100' },
	{ ws: 88, eye: 'fit', label: '88 (decided 2026-10-04)' },
];
const RECOMMENDED_WS = 88; // the user's decision (2026-10-04)
// the seated pilot: eye 0.92 m above the tub floor (0.14 m cushion + 0.78 m
// seated eye height), floor at least 2 cm above the hull's bottom
const FIT = { floorToEye: 0.92, margin: 0.02, footprint: { r: [-0.22, 0, 0.22], f: [-0.30, -0.10, 0.10, 0.30, 0.50] } };
const D = window.MOCKUP;    // manifest + base64 blobs, see build_cockpit_mockup.py
const Q = new URLSearchParams(location.search);
const SHOT = Q.get('shot') === '1';

function P(r, u, f) { return new THREE.Vector3(r * REF, EYE_Y + u * REF, -f * REF); }
const deg = (a) => a * Math.PI / 180;

// --------------------------------------------------------------------------
// Data decoding

function b64bytes(s) {
	const bin = atob(s);
	const out = new Uint8Array(bin.length);
	for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
	return out;
}

function loadImage(b64) {
	return new Promise((resolve, reject) => {
		const img = new Image();
		img.onload = () => resolve(img);
		img.onerror = reject;
		img.src = 'data:image/png;base64,' + b64;
	});
}

// A captured vertex stream: per vertex 3 f32 pos, 2 f32 uv, 4 u8 colour.
function decodeStream(bytes) {
	const n = bytes.byteLength / 24;
	const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
	const pos = new Float32Array(n * 3), uv = new Float32Array(n * 2), col = new Uint8Array(n * 4);
	for (let i = 0; i < n; i++) {
		const o = i * 24;
		pos[i * 3] = dv.getFloat32(o, true);
		pos[i * 3 + 1] = -dv.getFloat32(o + 4, true);
		pos[i * 3 + 2] = -dv.getFloat32(o + 8, true);
		uv[i * 2] = dv.getFloat32(o + 12, true);
		uv[i * 2 + 1] = dv.getFloat32(o + 16, true);
		for (let k = 0; k < 4; k++) col[i * 4 + k] = bytes[o + 20 + k];
	}
	return { n, pos, uv, col };
}

function streamGeometry(s, first, count) {
	const g = new THREE.BufferGeometry();
	const a = first * 3, b = (first + count) * 3;
	g.setAttribute('position', new THREE.BufferAttribute(s.pos.slice(a * 3, b * 3), 3));
	g.setAttribute('uv', new THREE.BufferAttribute(s.uv.slice(a * 2, b * 2), 2));
	g.setAttribute('vcol', new THREE.BufferAttribute(s.col.slice(a * 4, b * 4), 4, true));
	return g;
}

// --------------------------------------------------------------------------
// Materials

// The game's shader: texture * vertex colour * 2, discard where alpha is 0,
// distance fade between RENDER_FADEOUT_NEAR and _FAR (stippled here instead
// of blended). The inside of the hull (back faces) is dimmed so the cut-out
// reads as an interior.
function gameMaterial(tex, interior) {
	return new THREE.ShaderMaterial({
		uniforms: { map: { value: tex }, interior: { value: interior }, fade: { value: new THREE.Vector2(48000, 64000) } },
		vertexShader: `
			attribute vec4 vcol;
			varying vec2 vUv; varying vec4 vCol; varying float vDist;
			void main() {
				vUv = uv; vCol = vcol;
				vec4 mv = modelViewMatrix * vec4(position, 1.0);
				vDist = length(mv.xyz);
				gl_Position = projectionMatrix * mv;
			}`,
		fragmentShader: `
			uniform sampler2D map; uniform float interior; uniform vec2 fade;
			varying vec2 vUv; varying vec4 vCol; varying float vDist;
			float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
			void main() {
				vec4 c = texture2D(map, vUv) * vCol;
				if (c.a < 0.5) discard;
				float f = smoothstep(fade.y, fade.x, vDist);
				if (f < hash(gl_FragCoord.xy)) discard;
				vec3 rgb = c.rgb * 2.0;
				if (!gl_FrontFacing) rgb *= interior;
				gl_FragColor = vec4(rgb, 1.0);
			}`,
		side: THREE.DoubleSide,
	});
}

// Flat-shaded authored parts: colour * (ambient + sun), normals from screen
// derivatives so any triangle soup shades flat.
const SUN = new THREE.Vector3(0.35, 1.0, 0.45).normalize();
function flatMaterial(hex, opts = {}) {
	return new THREE.ShaderMaterial({
		uniforms: {
			color: { value: new THREE.Color(hex) },
			sun: { value: SUN },
			ambient: { value: opts.ambient ?? 0.55 },
			emissive: { value: opts.emissive ?? 0.0 },
			alpha: { value: 1.0 },
		},
		vertexShader: `
			varying vec3 vWorld;
			void main() {
				vec4 w = modelMatrix * vec4(position, 1.0);
				vWorld = w.xyz;
				gl_Position = projectionMatrix * viewMatrix * w;
			}`,
		fragmentShader: `
			uniform vec3 color; uniform vec3 sun; uniform float ambient; uniform float emissive; uniform float alpha;
			varying vec3 vWorld;
			void main() {
				vec3 n = normalize(cross(dFdx(vWorld), dFdy(vWorld)));
				if (dot(n, cameraPosition - vWorld) < 0.0) n = -n;
				float d = max(dot(n, sun), 0.0);
				float l = ambient + (1.0 - ambient) * d + emissive;
				gl_FragColor = vec4(color * l, alpha);
			}`,
		side: THREE.DoubleSide,
		extensions: { derivatives: true },
	});
}

function glassMaterial(opacity) {
	return new THREE.ShaderMaterial({
		uniforms: { opacity: { value: opacity } },
		vertexShader: `
			varying vec3 vWorld;
			void main() { vec4 w = modelMatrix * vec4(position, 1.0); vWorld = w.xyz; gl_Position = projectionMatrix * viewMatrix * w; }`,
		fragmentShader: `
			uniform float opacity; varying vec3 vWorld;
			void main() {
				vec3 n = normalize(cross(dFdx(vWorld), dFdy(vWorld)));
				vec3 v = normalize(cameraPosition - vWorld);
				float fres = pow(1.0 - abs(dot(n, v)), 4.0);
				gl_FragColor = vec4(vec3(0.62, 0.85, 1.0), opacity + 0.10 * fres);
			}`,
		transparent: true, depthWrite: false, side: THREE.DoubleSide,
		extensions: { derivatives: true },
	});
}

// --------------------------------------------------------------------------
// Geometry helpers (all in ship space, game units, three.js axes)

// A box beam from a to b, w wide (across, perpendicular to `side` hint) and
// d deep. `hint` is a vector roughly along the beam's depth direction.
function beamGeometry(a, b, w, d, hint) {
	const len = a.distanceTo(b);
	const g = new THREE.BoxGeometry(w, d, len);
	const z = b.clone().sub(a).normalize();
	let y = (hint || new THREE.Vector3(0, 1, 0)).clone();
	y.sub(z.clone().multiplyScalar(y.dot(z)));
	if (y.lengthSq() < 1e-6) y = new THREE.Vector3(1, 0, 0).sub(z.clone().multiplyScalar(z.x));
	y.normalize();
	const x = new THREE.Vector3().crossVectors(y, z);
	const m = new THREE.Matrix4().makeBasis(x, y, z);
	m.setPosition(a.clone().add(b).multiplyScalar(0.5));
	g.applyMatrix4(m);
	return g;
}

// A polyline of beams with joint blocks. Width/depth in metres.
function tube(points, wM, dM, hint) {
	const gs = [];
	for (let i = 0; i + 1 < points.length; i++) {
		const h = typeof hint === 'function' ? hint(points[i], points[i + 1]) : hint;
		gs.push(beamGeometry(points[i], points[i + 1], wM * REF, dM * REF, h));
	}
	for (let i = 1; i + 1 < points.length; i++) {
		const a = points[i].clone().sub(points[i - 1]).normalize(), b = points[i + 1].clone().sub(points[i]).normalize();
		if (a.dot(b) > Math.cos(deg(20))) continue;   // gentle bend: the beams overlap enough
		const s = Math.max(wM, dM) * REF * 1.0;
		const j = new THREE.BoxGeometry(s, s, s);
		j.translate(points[i].x, points[i].y, points[i].z);
		gs.push(j);
	}
	return mergeGeometries(gs);
}

function mergeGeometries(gs) {
	let n = 0;
	const parts = gs.map((g) => { const gi = g.index ? g.toNonIndexed() : g; n += gi.attributes.position.count; return gi; });
	const pos = new Float32Array(n * 3);
	let o = 0;
	for (const g of parts) { pos.set(g.attributes.position.array, o); o += g.attributes.position.array.length; }
	const out = new THREE.BufferGeometry();
	out.setAttribute('position', new THREE.BufferAttribute(pos, 3));
	return out;
}

// Quad strip loft between two polylines of the same length.
function loft(a, b) {
	const pos = [];
	for (let i = 0; i + 1 < a.length; i++) {
		const p = [a[i], a[i + 1], b[i + 1], b[i]];
		pos.push(...p[0].toArray(), ...p[1].toArray(), ...p[2].toArray());
		pos.push(...p[0].toArray(), ...p[2].toArray(), ...p[3].toArray());
	}
	const g = new THREE.BufferGeometry();
	g.setAttribute('position', new THREE.BufferAttribute(new Float32Array(pos), 3));
	return g;
}

// Convex polygon (list of points) as a fan.
function polygon(pts) {
	const pos = [];
	for (let i = 1; i + 1 < pts.length; i++) pos.push(...pts[0].toArray(), ...pts[i].toArray(), ...pts[i + 1].toArray());
	const g = new THREE.BufferGeometry();
	g.setAttribute('position', new THREE.BufferAttribute(new Float32Array(pos), 3));
	return g;
}

// A box from 8 corner points: bottom quad b0..b3 and top quad t0..t3 (same order).
function hexahedron(b, t) {
	const faces = [[b[0], b[1], b[2], b[3]], [t[0], t[1], t[2], t[3]],
		[b[0], b[1], t[1], t[0]], [b[1], b[2], t[2], t[1]], [b[2], b[3], t[3], t[2]], [b[3], b[0], t[0], t[3]]];
	return mergeGeometries(faces.map(polygon));
}

// Axis-aligned box in eye-relative metres: r0..r1, u0..u1, f0..f1.
function boxM(r0, r1, u0, u1, f0, f1) {
	const a = P(r0, u0, f0), b = P(r1, u1, f1);
	const g = new THREE.BoxGeometry(Math.abs(b.x - a.x), Math.abs(b.y - a.y), Math.abs(b.z - a.z));
	g.translate((a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2);
	return g;
}

// --------------------------------------------------------------------------
// Hull: cut away the cabin volume (lesson 30: keep the hull whole, cut only
// the cabin). Polygons are clipped against the box planes; the parts outside
// are kept, with uv and colour interpolated.

function clipPoly(poly, axis, value, keepAbove) {
	// poly: array of {p:[x,y,z], uv:[u,v], c:[r,g,b,a]}; returns [inside?, outside]
	const ins = [], outs = [];
	const side = (v) => (keepAbove ? v.p[axis] - value : value - v.p[axis]); // >0 = inside the box half-space
	for (let i = 0; i < poly.length; i++) {
		const a = poly[i], b = poly[(i + 1) % poly.length];
		const sa = side(a), sb = side(b);
		if (sa >= 0) ins.push(a); else outs.push(a);
		if ((sa >= 0) !== (sb >= 0)) {
			const t = sa / (sa - sb);
			const lerp = (x, y) => x.map((v, k) => v + (y[k] - v) * t);
			const m = { p: lerp(a.p, b.p), uv: lerp(a.uv, b.uv), c: lerp(a.c, b.c) };
			ins.push(m); outs.push(m);
		}
	}
	return [ins.length >= 3 ? ins : null, outs.length >= 3 ? outs : null];
}

function cutHull(geo, box) {
	// box: {x0,x1,y0,y1,z0,z1} in ship space (three axes, units). y1 may be Infinity.
	const P_ = geo.attributes.position.array, U = geo.attributes.uv.array, C = geo.attributes.vcol.array;
	const pos = [], uv = [], col = [];
	let removed = 0;
	const planes = [[0, box.x0, true], [0, box.x1, false], [1, box.y0, true], [1, box.y1, false], [2, box.z0, true], [2, box.z1, false]];
	const emit = (poly) => {
		for (let i = 1; i + 1 < poly.length; i++) {
			for (const v of [poly[0], poly[i], poly[i + 1]]) { pos.push(...v.p); uv.push(...v.uv); col.push(...v.c); }
		}
	};
	for (let t = 0; t < P_.length / 9; t++) {
		let poly = [0, 1, 2].map((k) => {
			const i = t * 3 + k;
			return { p: [P_[i * 3], P_[i * 3 + 1], P_[i * 3 + 2]], uv: [U[i * 2], U[i * 2 + 1]], c: [C[i * 4], C[i * 4 + 1], C[i * 4 + 2], C[i * 4 + 3]] };
		});
		let cut = false;
		for (const [axis, value, above] of planes) {
			if (!isFinite(value)) continue;
			const [ins, outs] = clipPoly(poly, axis, value, above);
			if (outs) emit(outs);
			if (!ins) { poly = null; break; }
			if (outs) cut = true;
			poly = ins;
		}
		if (poly) removed++; // the part inside the cabin volume is dropped
	}
	const g = new THREE.BufferGeometry();
	g.setAttribute('position', new THREE.BufferAttribute(new Float32Array(pos), 3));
	g.setAttribute('uv', new THREE.BufferAttribute(new Float32Array(uv), 2));
	g.setAttribute('vcol', new THREE.BufferAttribute(new Uint8Array(col.map(Math.round)), 4, true));
	g.userData.removed = removed;
	return g;
}

// --------------------------------------------------------------------------
// Instruments: canvases drawn with the game's own font, speedo facia and
// weapon icons. Sizes are in game HUD pixels (the 320x240-era layout units
// hud.c uses before ui_scale); each placement says how many millimetres one
// HUD pixel is in the cockpit.

let K = 6; // canvas pixels per HUD pixel (the asset sheet and pod faces change it while drawing)
const FONT = {};       // size -> {img, glyphs, space, tinted: Map}
const IMG = {};        // speedo, wicons[]
const UI_DEFAULT = [128, 128, 128], UI_ACCENT = [123, 98, 12];

function tintCanvas(img, rgb) {
	const c = document.createElement('canvas');
	c.width = img.width; c.height = img.height;
	const x = c.getContext('2d');
	x.drawImage(img, 0, 0);
	x.globalCompositeOperation = 'source-in';
	// the game multiplies by the vertex colour * 2 (128 = 1.0)
	x.fillStyle = `rgb(${Math.min(255, rgb[0] * 2)},${Math.min(255, rgb[1] * 2)},${Math.min(255, rgb[2] * 2)})`;
	x.fillRect(0, 0, c.width, c.height);
	return c;
}

function fontTinted(size, rgb) {
	const f = FONT[size];
	const key = rgb.join(',');
	if (!f.tinted.has(key)) f.tinted.set(key, tintCanvas(f.img, rgb));
	return f.tinted.get(key);
}

function textWidth(str, size) {
	const f = FONT[size];
	let w = 0;
	for (const ch of str) w += ch === ' ' ? f.space : (f.glyphs[ch] ? f.glyphs[ch][2] : 0);
	return w;
}

// x, y in HUD pixels
function drawText(ctx, str, x, y, size, rgb) {
	const f = FONT[size];
	const src = fontTinted(size, rgb);
	for (const ch of str) {
		if (ch === ' ') { x += f.space; continue; }
		const g = f.glyphs[ch];
		if (!g) continue;
		ctx.drawImage(src, g[0], g[1], g[2], g[3], x * K, y * K, g[2] * K, g[3] * K);
		x += g[2];
	}
}

function timeString(t) {
	const ms = Math.floor(t * 1000);
	const tenths = Math.floor(ms / 100) % 10, secs = Math.floor(ms / 1000) % 60, mins = Math.floor(ms / 60000);
	return `${Math.floor(mins / 10) % 10}${mins % 10}e${Math.floor(secs / 10)}${secs % 10}f${tenths}`; // e=':' f='.'
}

// hud.c speedo: bars are skewed quads (hud_draw_speedo_bar), thrust first in
// red at alpha 128, then speed in the bar colours, then the facia texture.
function drawSpeedoBars(ctx, ox, oy, f, override) {
	const S = D.speedo;
	if (f <= 0) return;
	if (f - Math.floor(f) > 0.9) f = Math.ceil(f);
	f = Math.min(f, 13);
	const bars = Math.floor(f);
	const bar = (a, b, t) => {
		const lerp = (p, q) => p + (q - p) * t;
		const lc = override || a.color;
		const rc = override || a.color.map((v, k) => lerp(v, b.color[k]));
		const rh = lerp(a.height, b.height);
		const tl = [a.x + 1, a.y], bl = [a.x + 1 - a.height / S.skew, a.y + a.height];
		const tr = [lerp(a.x + 1, b.x), lerp(a.y, b.y)], br = [tr[0] - rh / S.skew, tr[1] + rh];
		const grad = ctx.createLinearGradient((ox + tl[0]) * K, 0, (ox + tr[0]) * K, 0);
		const css = (c) => `rgba(${Math.min(255, c[0] * (override ? 1 : 1))},${c[1]},${c[2]},${(c[3] ?? 255) / 255})`;
		grad.addColorStop(0, css(lc)); grad.addColorStop(1, css(rc));
		ctx.fillStyle = grad;
		ctx.beginPath();
		ctx.moveTo((ox + tl[0]) * K, (oy + tl[1]) * K);
		ctx.lineTo((ox + tr[0]) * K, (oy + tr[1]) * K);
		ctx.lineTo((ox + br[0]) * K, (oy + br[1]) * K);
		ctx.lineTo((ox + bl[0]) * K, (oy + bl[1]) * K);
		ctx.closePath();
		ctx.fill();
	};
	for (let i = 1; i < bars; i++) bar(S.bars[i - 1], S.bars[i], 1);
	if (bars > 12) return;
	let last = f - bars + 0.1;
	if (last <= 0) return;
	last = Math.min(last, 1);
	const lb = bars === 0 ? 1 : bars;
	bar(S.bars[lb - 1], S.bars[lb], last);
}

// The instrument catalogue. w/h in HUD pixels, minText = smallest glyph
// height drawn (HUD pixels), for the legibility check.
const INSTRUMENTS = {
	speedo: { label: 'Speed + thrust', w: 132, h: 26, minText: null, draw(ctx, s) {
		drawSpeedoBars(ctx, 2, 5, s.thrust / 65, [255, 0, 0, 128]);
		drawSpeedoBars(ctx, 2, 5, s.speedBars, null);
		ctx.drawImage(IMG.speedo, 2 * K, 0, IMG.speedo.width * K, IMG.speedo.height * K);
	} },
	shield: { label: 'Status slot (shield timer)', w: 64, h: 24, minText: 8, draw(ctx, s) {
		const on = s.shield > 0;
		drawText(ctx, 'SHIELD', 2, 2, '8', on ? UI_DEFAULT : UI_ACCENT);
		const segs = 8, filled = Math.ceil(segs * s.shield);
		for (let i = 0; i < segs; i++) {
			ctx.fillStyle = i < filled ? 'rgb(120,200,255)' : 'rgba(80,90,110,0.55)';
			ctx.fillRect((2 + i * 7.5) * K, 13 * K, 6 * K, 8 * K);
		}
	} },
	lap: { label: 'Lap', w: 64, h: 30, minText: 8, draw(ctx, s) {
		drawText(ctx, 'LAP', 5, 2, '8', UI_ACCENT);
		const n = String(s.lap);
		drawText(ctx, n, 2, 12, '16', UI_DEFAULT);
		const w = textWidth(n, '16');
		drawText(ctx, 'OF', 2 + w + 2, 20, '8', UI_ACCENT);
		drawText(ctx, '3', 2 + w + 4 + textWidth('OF', '8'), 12, '16', UI_DEFAULT);
	} },
	position: { label: 'Position', w: 64, h: 30, minText: 8, draw(ctx, s) {
		drawText(ctx, 'POSITION', 2, 2, '8', UI_ACCENT);
		const n = String(s.position);
		drawText(ctx, n, this.w / 2 - textWidth(n, '16') / 2, 12, '16', UI_DEFAULT);
	} },
	laptime: { label: 'Lap time', w: 96, h: 32, minText: 8, draw(ctx, s) {
		drawText(ctx, timeString(s.lapTime), 2, 2, '16', UI_DEFAULT);
		drawText(ctx, 'LAP RECORD', 2, 22, '8', UI_ACCENT);
		drawText(ctx, timeString(57.5), 6 + textWidth('LAP RECORD', '8'), 22, '8', UI_DEFAULT);
	} },
	// championship lives: hud.c draws UI_ICON_STAR (drfonts.cmp #9, 12x12) per life, 13 px apart
	lives: { label: 'Lives (championship)', w: 2 + 13 * 3, h: 14, minText: null, draw(ctx, s) {
		for (let i = 0; i < s.lives; i++) ctx.drawImage(IMG.star, (1 + 13 * i) * K, 1 * K, 12 * K, 12 * K);
	} },
	// previous lap times: hud.c draws them in UI_SIZE_8 accent, 10 px apart, above the current one
	prevlaps: { label: 'Previous laps', w: 70, h: 20, minText: 8, draw(ctx, s) {
		for (let i = 0; i < s.prevLaps.length && i < 2; i++) drawText(ctx, timeString(s.prevLaps[i]), 2, 1 + 10 * i, '8', UI_ACCENT);
	} },
	weapon: { label: 'Weapon', w: 36, h: 36, minText: null, draw(ctx, s) {
		ctx.strokeStyle = 'rgba(246,196,24,0.55)';
		ctx.lineWidth = K * 0.8;
		ctx.strokeRect(1 * K, 1 * K, 34 * K, 34 * K);
		if (s.weapon >= 0) ctx.drawImage(IMG.wicons[s.weapon], 2 * K, 2 * K, 32 * K, 32 * K);
	} },
};


// A pod face as the build will draw it: the face texture (tex texels, times k
// here), cleared to the bezel colour, each element drawn at its texel position
// with K = its ui_scale. The same draw functions draw the game font and art.
const FACE_K = 2;
function composeFace(ctx, pnl, insts, k, visible) {
	ctx.imageSmoothingEnabled = false;
	ctx.fillStyle = '#0b0d10';
	ctx.fillRect(0, 0, pnl.tex[0] * k, pnl.tex[1] * k);
	const keep = K;
	for (const it of insts) {
		if (it.panel !== pnl.id || it.tx === undefined || (visible && !visible(it.id))) continue;
		ctx.save();
		ctx.translate(it.tx * k, it.ty * k);
		K = it.scale * k;
		it.def.draw(ctx, FACE_STATE.current);
		ctx.restore();
	}
	K = keep;
}
const FACE_STATE = { current: null };

// Canvas sizes from the game font's glyph widths (called once the font loads).
function sizeInstruments() {
	const I = INSTRUMENTS;
	I.shield.w = Math.max(textWidth('SHIELD', '8'), 60) + 4;
	I.lap.w = 2 + textWidth('3', '16') + 2 + textWidth('OF', '8') + 2 + textWidth('3', '16') + 6;
	I.position.w = textWidth('POSITION', '8') + 4;
	I.laptime.w = Math.max(textWidth('00e00f0', '16'), textWidth('LAP RECORD', '8') + 4 + textWidth('00e57f5', '8')) + 6;
	I.prevlaps.w = textWidth('00e00f0', '8') + 4;
}

// --------------------------------------------------------------------------
// Cockpit parts shared by the variations

const TEAM_NAME = ['AG SYSTEMS', 'AURICOM', 'QIREX', 'FEISAR'];
const PILOT_NAME = ['John Dekka', 'Daniel Chang', 'Arial Tetsuo', 'Anastasia Cherovoski', 'Kel Solaar', 'Arian Tetsuo', 'Sofia de la Rente', 'Paul Jackson'];
const PILOT_TEAM = [0, 0, 1, 1, 2, 2, 3, 3];
const COL = {
	cabin: 0x30343b, cabinDark: 0x1c1e22, seat: 0x3b3337, frameLight: 0xa9b0b9, frameDark: 0x4a4f58,
	frameGun: 0x353a42, bezel: 0x141619, body: 0xd07a30,
};

// Material slots. The geometry names a slot, never a colour. The fixed slots
// below are authored colours; trim_primary and trim_secondary are sampled at
// runtime from the ship's own textures (sampleTrim). glass is the canopy.
const SLOTS = {
	frame: COL.frameLight, frame_dark: COL.frameDark, frame_gun: COL.frameGun, cabin: COL.cabin,
	cabin_dark: COL.cabinDark, seat: COL.seat, bezel: COL.bezel, lens: 0x0c1a24,
};
const SLOT_EMISSIVE = { trim_primary: 0.25, trim_secondary: 0.25, lens: 0.3 };

// The cabin: the volume cut out of the hull (lesson 30: keep the hull whole,
// cut only the cabin), in eye-relative metres at REF. The opening runs from
// behind the seat to just ahead of the chest; the legs go forward under the
// ship's own deck. The eye is 0.92 m above the floor: room for a
// semi-reclined seat (seated eye height about 0.78 m) on a 0.14 m cushion.
// Round 1 (C1-C3) opened the hull from behind the seat to just ahead of the
// chest. Cockpit H (round 3) opens it further, to 0.62 m over the legs and
// ±0.38 m wide, so the dash sits in the opening below the deck line: with the
// pilot fitted lower at smaller world_scales the deck is close under the eye,
// and a dash on top of it would hide the instruments and the nose.
const CABIN_ROUND1 = { halfWidth: 0.34, floor: -0.92, front: 0.25, rear: -0.36 };
const CABIN_H = { halfWidth: 0.38, floor: -0.92, front: 0.62, rear: -0.36 };
let CABIN = CABIN_ROUND1;

// H.top(r, f): u of the hull's top surface at (r, f), or null (ray cast on the
// uncut hull). The tub walls run from the floor up to it, so the tub never
// shows outside the hull, whatever its shape.
function sampleLine(n, fn) { const out = []; for (let i = 0; i <= n; i++) out.push(fn(i / n)); return out; }

function cabinParts(add, accent, H, o = {}) {
	const c = CABIN, hw = c.halfWidth;
	const top = (r, f) => { const u = H.top(r, f); return u === null ? c.floor : Math.max(c.floor, u - 0.002); };
	const N = 10;
	const wall = (pts) => {
		// pts: [r, f] along the wall; strip from the floor to the hull surface
		const lo = pts.map(([r, f]) => P(r, c.floor, f)), hi = pts.map(([r, f]) => P(r, top(r, f), f));
		add('tub', loft(lo, hi), 'cabin');
		return hi;
	};
	const left = wall(sampleLine(N, (t) => [-hw, c.rear + (c.front - c.rear) * t]));
	const right = wall(sampleLine(N, (t) => [hw, c.rear + (c.front - c.rear) * t]));
	const front = wall(sampleLine(N, (t) => [-hw + 2 * hw * t, c.front]));
	wall(sampleLine(N, (t) => [-hw + 2 * hw * t, c.rear]));
	add('tub', boxM(-hw, hw, c.floor - 0.02, c.floor, c.rear, c.front), 'cabin_dark');
	// coaming: a rim along the cut edge, on the hull surface
	const rw = o.rim ?? 0.04, rh = o.rimH ?? 0.03;
	for (const edge of [left, right, front]) add('sills', tube(edge, rw, rh), 'frame_dark');
	if (o.accentRim !== false) {
		for (const edge of [left, right]) add('sills', tube(edge.map((v) => v.clone().add(new THREE.Vector3(0, rh * REF * 0.6, 0))), rw * 0.5, rh * 0.4), 'trim_primary');
	}
	// seat: cushion and a reclined back
	add('seat', boxM(-0.21, 0.21, c.floor, c.floor + 0.14, -0.30, 0.16), 'seat');
	add('seat', hexahedron(
		[P(-0.21, c.floor + 0.10, -0.33), P(0.21, c.floor + 0.10, -0.33), P(0.21, c.floor + 0.10, -0.23), P(-0.21, c.floor + 0.10, -0.23)],
		[P(-0.20, -0.30, -0.36), P(0.20, -0.30, -0.36), P(0.20, -0.30, -0.29), P(-0.20, -0.30, -0.29)]), 'seat');
	return { left, right, front };
}

// Rear bulkhead behind the head (Star Fox ddee4ff), with a head pad. It rises
// from the cabin floor to `top`, a little wider than the cabin.
function bulkhead(add, accent, top, f = -0.40, w = CABIN.halfWidth + 0.06, o = {}) {
	const b = CABIN.floor;
	add('bulkhead', hexahedron(
		[P(-w, b, f - 0.05), P(w, b, f - 0.05), P(w, b, f), P(-w, b, f)],
		[P(-w * 0.9, top, f - 0.05), P(w * 0.9, top, f - 0.05), P(w * 0.9, top, f), P(-w * 0.9, top, f)]), 'frame_dark');
	add('bulkhead', boxM(-w * 0.9, w * 0.9, top - 0.025, top + 0.01, f - 0.055, f + 0.006), 'trim_primary');
	// headrest tower: the bulkhead's centre rises behind the head, carrying the pad
	if (top < 0.10) add('headrest', boxM(-0.13, 0.13, top - 0.02, 0.10, f - 0.05, f), 'frame_dark');
	add('headrest', boxM(-0.10, 0.10, -0.15, 0.07, f, f + 0.06), 'cabin_dark');  // head pad
}

// Roll hoop: posts from the bulkhead's top corners to the canopy strut tips,
// and a crossbar joining the tips (Star Fox 1d68555: the frame needs a visible
// load path to the cabin).
function rollHoop(add, base, tips, t, color = 'frame') {
	add('hoop', tube([base[0], tips[0]], t, t), color);
	add('hoop', tube([base[1], tips[1]], t, t), color);
	add('hoop', tube([tips[0], tips[1]], t, t), color);
}

const mirror = (pts) => pts.map((v) => new THREE.Vector3(-v.x, v.y, v.z));

// The canopy sill: points on the hull surface at half-width w(f), from f0 to f1.
function sillLine(H, w, f0, f1, n = 8, lift = 0.012) {
	return sampleLine(n, (t) => {
		const f = f0 + (f1 - f0) * t, r = -w(f);
		const u = H.top(r, f);
		return P(r, (u === null ? CABIN.floor : u) + lift, f);
	});
}

// Panels carry instruments. A panel is a bezel box whose face is centred at
// `at` (eye-relative metres), w x h metres, facing the eye point (or turned by
// `tilt` = [yawDeg, pitchDeg] from that). Instruments sit 3 mm in front of the
// face at 2D offsets in mm (+x right, +y up on the face). style 'glass' is a
// reflector plate: a faint glass with additive readouts.
function panelFrame(pnl) {
	const c = P(...pnl.at);
	const n = P(0, 0, 0).sub(c).normalize();
	if (pnl.tilt) n.applyEuler(new THREE.Euler(deg(pnl.tilt[1]), deg(pnl.tilt[0]), 0, 'YXZ'));
	const x = new THREE.Vector3().crossVectors(new THREE.Vector3(0, 1, 0), n).normalize();
	const y = new THREE.Vector3().crossVectors(n, x);
	return { c, n, x, y };
}

function panelGeometry(pnl) {
	const { c, n, x, y } = panelFrame(pnl);
	const d = (pnl.depth ?? 0.05) * REF;
	const g = new THREE.BoxGeometry(pnl.w * REF, pnl.h * REF, d);
	g.translate(0, 0, -d / 2);
	g.applyMatrix4(new THREE.Matrix4().makeBasis(x, y, n).setPosition(c));
	return g;
}

// A pedestal from the hull deck up to a point under a panel.
function pedestal(H, r, f, uTop, w, d) {
	const u0 = H.top(r, f) ?? CABIN.floor;
	return boxM(r - w / 2, r + w / 2, u0 - 0.03, uTop, f - d / 2, f + d / 2);
}

// C1's slim teardrop frame (also the hybrid's): 2.4 cm struts, a raked
// windscreen from 0.85 m up the deck, top rails back to the roll hoop.
function slimFrame(add, H) {
	const t = 0.024;                               // 2.4 cm struts
	// teardrop canopy: a raked windscreen from far up the deck, rails back to the hoop.
	// The sill keeps 5 cm outside the cabin opening and narrows only ahead of it.
	const open = CABIN.front, hw = CABIN.halfWidth + 0.05;
	const cw = CABIN === CABIN_ROUND1
		? (f) => 0.41 - 0.14 * Math.max(0, f - 0.1) / 0.75 - 0.02 * Math.max(0, -f - 0.2) / 0.2
		: (f) => (f <= open ? hw : hw - (hw - 0.27) * Math.min(1, (f - open) / (0.85 - open)));
	const sL = sillLine(H, cw, 0.85, -0.40, 10);
	const sR = mirror(sL);
	const aL = [sL[0], P(-0.32, -0.16, 0.56), P(-0.25, 0.30, 0.10)];   // A-pillar
	const tL = [aL[2], P(-0.29, 0.33, -0.12), P(-0.27, 0.31, -0.42)];  // top rail to the hoop
	const aR = mirror(aL), tR = mirror(tL);
	add('struts', tube(aL, t, t), 'frame');
	add('struts', tube(aR, t, t), 'frame');
	add('struts', tube([aL[2], P(0, 0.345, 0.08), aR[2]], t, t), 'frame');   // header
	add('struts', tube(tL, t, t), 'frame');
	add('struts', tube(tR, t, t), 'frame');
	add('sills', tube(sL, t * 0.8, t * 0.8), 'frame');                      // canopy sill rails
	add('sills', tube(sR, t * 0.8, t * 0.8), 'frame');
	bulkhead(add, null, -0.06);
	rollHoop(add, [P(-0.37, -0.06, -0.42), P(0.37, -0.06, -0.42)], [tL[2], tR[2]], 0.03);
	// glass: windscreen, sides, top
	const ws0 = sampleLine(8, (k) => sL[0].clone().lerp(sR[0], k));
	const ws1 = sampleLine(8, (k) => aL[2].clone().lerp(aR[2], k).add(new THREE.Vector3(0, Math.sin(Math.PI * k) * 0.04 * REF, 0)));
	add('glass', loft(ws0, ws1));
	add('glass', loft(sampleLine(10, (k) => pathAt([...aL, ...tL.slice(1)], k)), sampleLine(10, (k) => pathAt(sL, k))));
	add('glass', loft(sampleLine(10, (k) => pathAt([...aR, ...tR.slice(1)], k)), sampleLine(10, (k) => pathAt(sR, k))));
	add('glass', loft(tL, tR));
	return { sL, sR, aL, aR, tL, tR };
}


// The hybrid's dash: C2's full-width shell and two angled pods at about ±27°,
// with the nose clear between them, plus a weapon keystone. Each pod carries
// one face texture at its planned resolution (the `tex` size, 0.547 mm per
// texel); instruments are placed in texels and drawn at an integer ui_scale,
// the way the build will draw them with the game's own 2D UI calls.
const POD = { w: 0.28, h: 0.16, tex: [512, 292] };
function hybridDash(add, H) {
	const pods = [
		{ id: 'left', group: 'podL', at: [-0.235, -0.335, 0.47], w: POD.w, h: POD.h, depth: 0.08, tex: POD.tex },
		{ id: 'right', group: 'podR', at: [0.235, -0.335, 0.47], w: POD.w, h: POD.h, depth: 0.08, tex: POD.tex },
		{ id: 'weapon', group: 'weaponPod', at: [0, -0.405, 0.555], w: 0.07, h: 0.07, depth: 0.04, tex: [128, 128] },
	];
	for (const p of pods.slice(0, 2)) {
		add(p.group, panelGeometry(p), 'bezel');
		const pf = panelFrame(p);
		// glareshield lip over the pod, with a trim edge along its front
		const a = pf.c.clone().addScaledVector(pf.y, (p.h / 2 + 0.008) * REF), wv = pf.x.clone().multiplyScalar((p.w / 2 + 0.012) * REF), nv = pf.n.clone().multiplyScalar(-0.10 * REF);
		const up = (v, k) => v.clone().addScaledVector(pf.y, k * REF);
		const lipB = [a.clone().sub(wv), a.clone().add(wv), a.clone().add(wv).add(nv), a.clone().sub(wv).add(nv)];
		const lipT = [up(lipB[0], 0.022).addScaledVector(pf.n, 0.03 * REF), up(lipB[1], 0.022).addScaledVector(pf.n, 0.03 * REF), up(lipB[2], 0.015), up(lipB[3], 0.015)];
		add(p.group, hexahedron(lipB, lipT), 'frame_dark');
		add(p.group, tube([lipT[0].clone().addScaledVector(pf.n, 0.002 * REF), lipT[1].clone().addScaledVector(pf.n, 0.002 * REF)], 0.008, 0.008), 'trim_secondary');
	}
	// dash shell: one body across the cabin under both pods, low in the centre
	// (below the nose line), resting on the hull deck
	// the shell spans the cabin opening above the knees (bottom 0.52 m below the eye)
	const dl = (r) => (Math.abs(r) < 0.12 ? -0.455 : -0.455 + (Math.abs(r) - 0.12) * 0.25);
	const xs = [-CABIN.halfWidth, -0.12, 0.12, CABIN.halfWidth];
	for (let i = 0; i + 1 < xs.length; i++) {
		const r0 = xs[i], r1 = xs[i + 1];
		const b0 = -0.52, b1 = -0.52;
		add('dash', hexahedron(
			[P(r0, b0, 0.58), P(r1, b1, 0.58), P(r1, b1, 0.36), P(r0, b0, 0.36)],
			[P(r0, dl(r0), 0.58), P(r1, dl(r1), 0.58), P(r1, dl(r1) - 0.01, 0.38), P(r0, dl(r0) - 0.01, 0.38)]), 'cabin');
	}
	// weapon keystone: a small display standing on the dash centre, trimmed
	const k = pods[2];
	add(k.group, panelGeometry(k), 'bezel');
	add(k.group, panelGeometry({ ...k, w: k.w + 0.016, h: k.h + 0.016, depth: 0.025, at: k.at.map((v, i) => v + (i === 2 ? 0.006 : 0)) }), 'trim_primary');
	add(k.group, boxM(-0.025, 0.025, -0.462, k.at[1] - k.h / 2 + 0.005, k.at[2] + 0.005, k.at[2] + 0.04), 'frame_dark');
	const I = INSTRUMENTS;
	return {
		panels: pods,
		instruments: [
			{ id: 'lap', panel: 'left', tx: 18, ty: 18, scale: 2 },
			{ id: 'position', panel: 'left', tx: 512 - 18 - 2 * I.position.w, ty: 18, scale: 2 },
			{ id: 'lives', panel: 'left', tx: 512 - 18 - 2 * I.lives.w, ty: 92, scale: 2 },
			{ id: 'prevlaps', panel: 'left', tx: Math.round((512 - 2 * I.laptime.w) / 2) + 4, ty: 292 - 2 * I.laptime.h - 26 - 2 * I.prevlaps.h - 8, scale: 2 },
			{ id: 'laptime', panel: 'left', tx: Math.round((512 - 2 * I.laptime.w) / 2), ty: 292 - 2 * I.laptime.h - 26, scale: 2 },
			{ id: 'speedo', panel: 'right', tx: Math.round((512 - 3 * I.speedo.w) / 2), ty: 30, scale: 3 },
			{ id: 'shield', panel: 'right', tx: Math.round((512 - 3 * I.shield.w) / 2), ty: 292 - 3 * I.shield.h - 30, scale: 3 },
			{ id: 'weapon', panel: 'weapon', tx: Math.round((128 - 3 * I.weapon.w) / 2), ty: Math.round((128 - 3 * I.weapon.h) / 2), scale: 3 },
		],
	};
}

// --------------------------------------------------------------------------
// The variations. H, the hybrid, is the approved design (2026-10-04); C1-C3
// stay selectable for comparison.

const VARIATIONS = [
	{
		id: 'H', name: 'Hybrid: slim frame, split dash', cabin: CABIN_H,
		blurb: "C1's slim teardrop frame with C2's full-width dash: angled pods at ±27°, the nose clear between them, and the weapon on a keystone display between the pods at their focal distance.",
		build(add, accent, H) {
			cabinParts(add, accent, H, { rim: 0.035, rimH: 0.025 });
			slimFrame(add, H);
			return hybridDash(add, H);
		},
	},
	{
		id: 'C1', name: 'Slim frame, centre cluster',
		blurb: 'Thin silver struts and a raked windscreen. One compact binnacle on the centre line, below the nose.',
		build(add, accent, H) {
			cabinParts(add, accent, H, { rim: 0.035, rimH: 0.025 });
			slimFrame(add, H);
			// dash: one binnacle on a pedestal, below the nose line
			const panel = { id: 'binnacle', at: [0, -0.40, 0.48], w: 0.40, h: 0.19, depth: 0.06 };
			add('dash', panelGeometry(panel), COL.bezel);
			add('dash', pedestal(H, 0, 0.53, -0.47, 0.14, 0.08), COL.cabin);
			// a short hood over the binnacle keeps sun off the readouts
			const pf = panelFrame(panel);
			const hood = [-1, 1].map((sx) => pf.c.clone().addScaledVector(pf.x, sx * 0.20 * REF).addScaledVector(pf.y, 0.095 * REF));
			add('dash', hexahedron(
				[hood[0], hood[1], hood[1].clone().addScaledVector(pf.n, 0.04 * REF), hood[0].clone().addScaledVector(pf.n, 0.04 * REF)],
				[hood[0].clone().addScaledVector(pf.y, 0.012 * REF), hood[1].clone().addScaledVector(pf.y, 0.012 * REF),
					hood[1].clone().addScaledVector(pf.n, 0.045 * REF).addScaledVector(pf.y, 0.012 * REF), hood[0].clone().addScaledVector(pf.n, 0.045 * REF).addScaledVector(pf.y, 0.012 * REF)]), COL.frameDark);
			return {
				panels: [panel],
				instruments: [
					{ id: 'speedo', panel: 'binnacle', x: 0, y: 56, mm: 1.25 },
					{ id: 'lap', panel: 'binnacle', x: -140, y: 2, mm: 1.15 },
					{ id: 'weapon', panel: 'binnacle', x: 0, y: 2, mm: 1.15 },
					{ id: 'position', panel: 'binnacle', x: 140, y: 2, mm: 1.15 },
					{ id: 'laptime', panel: 'binnacle', x: -96, y: -60, mm: 1.05 },
					{ id: 'shield', panel: 'binnacle', x: 128, y: -60, mm: 1.05 },
				],
			};
		},
	},
	{
		id: 'C2', name: 'Fighter frame, split dash',
		blurb: 'Heavy gunmetal frame with a centre spine over the head and a second canopy bow. Full-width dash split left and right, leaving the nose clear; weapon on the windscreen header.',
		build(add, accent, H) {
			cabinParts(add, accent, H, { rim: 0.06, rimH: 0.04 });
			const t = 0.055, td = 0.045;
			const cw = (f) => 0.42 - 0.12 * Math.max(0, f - 0.1) / 0.6;
			const sL = sillLine(H, cw, 0.66, -0.40);
			const sR = mirror(sL);
			const aL = [sL[0], P(-0.33, -0.10, 0.50), P(-0.25, 0.19, 0.36)];   // A-pillar, more upright
			const tL = [aL[2], P(-0.31, 0.32, 0.12), P(-0.31, 0.34, -0.10), P(-0.28, 0.31, -0.42)];
			const aR = mirror(aL), tR = mirror(tL);
			const header = [aL[2], P(0, 0.215, 0.38), aR[2]];
			add('struts', tube(aL, t, td), COL.frameGun);
			add('struts', tube(aR, t, td), COL.frameGun);
			add('struts', tube(header, t * 1.3, td * 1.4), COL.frameGun);
			add('struts', tube(tL, t, td), COL.frameGun);
			add('struts', tube(tR, t, td), COL.frameGun);
			// second canopy bow from the sill over the head
			const bf = -0.02;
			const bowSillL = P(-cw(bf), (H.top(-cw(bf), bf) ?? CABIN.floor) + 0.012, bf);
			const bow = [bowSillL, P(-0.41, -0.05, bf), P(-0.32, 0.26, bf), P(0, 0.385, bf), P(0.32, 0.26, bf), P(0.41, -0.05, bf), new THREE.Vector3(-bowSillL.x, bowSillL.y, bowSillL.z)];
			add('struts', tube(bow, t, td), COL.frameGun);
			// centre spine: header to the hoop crossbar, over the head
			add('struts', tube([P(0, 0.215, 0.38), P(0, 0.35, 0.16), P(0, 0.385, bf), P(0, 0.36, -0.25), P(0, 0.31, -0.42)], 0.05, 0.05), COL.frameGun);
			add('struts', tube(sL, t * 0.9, td), COL.frameGun);
			add('struts', tube(sR, t * 0.9, td), COL.frameGun);
			bulkhead(add, accent, 0.0, -0.40, CABIN.halfWidth + 0.08);
			rollHoop(add, [P(-0.38, 0.0, -0.42), P(0.38, 0.0, -0.42)], [tL[3], tR[3]], 0.055, 'frame_gun');
			// glass
			const ws0 = sampleLine(8, (k) => sL[0].clone().lerp(sR[0], k));
			const ws1 = sampleLine(8, (k) => pathAt(header, k));
			add('glass', loft(ws0, ws1));
			add('glass', loft(sampleLine(10, (k) => pathAt([...aL, ...tL.slice(1)], k)), sampleLine(10, (k) => pathAt(sL, k))));
			add('glass', loft(sampleLine(10, (k) => pathAt([...aR, ...tR.slice(1)], k)), sampleLine(10, (k) => pathAt(sR, k))));
			add('glass', loft(tL, tR));
			// dash: glareshield across the cabin, two angled pods, centre gap over the nose
			const pods = [
				{ id: 'left', at: [-0.235, -0.335, 0.47], w: 0.26, h: 0.15, depth: 0.08, tilt: [0, 0] },
				{ id: 'right', at: [0.235, -0.335, 0.47], w: 0.26, h: 0.15, depth: 0.08, tilt: [0, 0] },
				{ id: 'header', at: [0, 0.125, 0.39], w: 0.072, h: 0.072, depth: 0.03 },
			];
			for (const p of pods.slice(0, 2)) {
				add('dash', panelGeometry(p), COL.bezel);
				const pf = panelFrame(p);
				// glareshield lip over each pod (the header readout hangs from the header)
				const a = pf.c.clone().addScaledVector(pf.y, 0.085 * REF), wv = pf.x.clone().multiplyScalar(0.145 * REF), nv = pf.n.clone().multiplyScalar(-0.10 * REF);
				add('dash', hexahedron(
					[a.clone().sub(wv), a.clone().add(wv), a.clone().add(wv).add(nv), a.clone().sub(wv).add(nv)],
					[a.clone().sub(wv).addScaledVector(pf.y, 0.03 * REF).addScaledVector(pf.n, 0.03 * REF), a.clone().add(wv).addScaledVector(pf.y, 0.03 * REF).addScaledVector(pf.n, 0.03 * REF),
						a.clone().add(wv).add(nv).addScaledVector(pf.y, 0.02 * REF), a.clone().sub(wv).add(nv).addScaledVector(pf.y, 0.02 * REF)]), COL.frameGun);
			}
			// one dash body across the cabin under both pods, low in the centre
			// (below the nose line, which starts about 37 degrees down)
			const dl = (r) => (Math.abs(r) < 0.12 ? -0.455 : -0.455 + (Math.abs(r) - 0.12) * 0.25);
			const xs = [-0.38, -0.12, 0.12, 0.38];
			for (let i = 0; i + 1 < xs.length; i++) {
				const r0 = xs[i], r1 = xs[i + 1];
				const b0 = (H.top(r0, 0.42) ?? CABIN.floor) - 0.03, b1 = (H.top(r1, 0.42) ?? CABIN.floor) - 0.03;
				add('dash', hexahedron(
					[P(r0, b0, 0.56), P(r1, b1, 0.56), P(r1, b1, 0.36), P(r0, b0, 0.36)],
					[P(r0, dl(r0), 0.56), P(r1, dl(r1), 0.56), P(r1, dl(r1) - 0.01, 0.38), P(r0, dl(r0) - 0.01, 0.38)]), COL.cabin);
			}
			add('dash', panelGeometry(pods[2]), COL.bezel);
			add('dash', tube([P(0, 0.215 - 0.03, 0.38), P(0, 0.125 + 0.03, 0.385)], 0.02, 0.02), COL.frameGun);
			return {
				panels: pods,
				instruments: [
					{ id: 'lap', panel: 'left', x: -64, y: 38, mm: 1.12 },
					{ id: 'position', panel: 'left', x: 64, y: 38, mm: 1.12 },
					{ id: 'laptime', panel: 'left', x: 0, y: -30, mm: 1.12 },
					{ id: 'speedo', panel: 'right', x: 0, y: 30, mm: 1.35 },
					{ id: 'shield', panel: 'right', x: 0, y: -36, mm: 1.2 },
					{ id: 'weapon', panel: 'header', x: 0, y: 0, mm: 1.6 },
				],
			};
		},
	},
	{
		id: 'C3', name: 'Minimal frame, reflector glass',
		blurb: 'Almost frameless bubble: thin sill rails, the bulkhead and the roll hoop. The readouts are lit onto a combiner glass on the lower canopy, floating over the nose.',
		build(add, accent, H) {
			cabinParts(add, accent, H, { rim: 0.03, rimH: 0.02 });
			const t = 0.018;
			const cw = (f) => 0.41 - 0.16 * Math.max(0, f - 0.05) / 0.7;
			const sL = sillLine(H, cw, 0.78, -0.40, 10);
			const sR = mirror(sL);
			add('struts', tube(sL, t, t), COL.frameLight);
			add('struts', tube(sR, t, t), COL.frameLight);
			bulkhead(add, accent, -0.10);
			const tipL = P(-0.27, 0.30, -0.42), tipR = P(0.27, 0.30, -0.42);
			rollHoop(add, [P(-0.37, -0.10, -0.42), P(0.37, -0.10, -0.42)], [tipL, tipR], 0.032);
			// frameless bubble: arches from the sill up to a crown, lofted
			const crown = (k) => {           // k: 0 at the front (sill), 1 at the hoop
				const f = 0.78 + (-0.42 - 0.78) * k;
				return f;
			};
			const arches = [];
			for (let i = 0; i <= 10; i++) {
				const k = i / 10, f = crown(k);
				const sl = sL[i], w = cw(f);
				const h = Math.sin(Math.min(1, k * 1.9) * Math.PI / 2);   // rises from the front sill to full height
				const topU = sl.y / REF - EYE_Y / REF + (0.30 - (sl.y - EYE_Y) / REF) * h;
				arches.push(sampleLine(12, (j) => {
					const a = Math.PI * (1 - j / 12), cc = Math.cos(a), ss = Math.sin(a);
					const r = Math.sign(cc) * Math.pow(Math.abs(cc), 0.8) * w * (0.35 + 0.65 * h) + 0;
					const u0 = (sl.y - EYE_Y) / REF;
					return P(r, u0 + (topU - u0) * Math.pow(ss, 0.7), f);
				}));
			}
			for (let i = 0; i + 1 < arches.length; i++) add('glass', loft(arches[i], arches[i + 1]));
			// combiner: a glass plate rising from a projector on the deck
			// The readouts are projected onto the lower windscreen. The image is
			// rectified for the eye, so it is drawn as a plate facing the eye at
			// the glass's depth; the coated zone is a faint tint, with no frame.
			const plate = { id: 'combiner', at: [0, -0.255, 0.63], w: 0.50, h: 0.20, style: 'glass' };
			const pf = panelFrame(plate);
			const corners = [[-1, -1], [1, -1], [1, 1], [-1, 1]].map(([a, b]) => pf.c.clone().addScaledVector(pf.x, a * plate.w / 2 * REF).addScaledVector(pf.y, b * plate.h / 2 * REF));
			add('combiner', polygon(corners));
			// the projector: a low slot on the deck ahead of the cabin, under the nose line
			const pu = H.top(0, 0.40) ?? CABIN.floor;
			add('dash', boxM(-0.10, 0.10, pu - 0.02, pu + 0.012, 0.36, 0.44), COL.frameDark);
			add('dash', boxM(-0.085, 0.085, pu + 0.012, pu + 0.015, 0.37, 0.43), 0x0c1a24, { emissive: 0.3 });  // lens
			return {
				panels: [plate],
				instruments: [
					{ id: 'speedo', panel: 'combiner', x: 0, y: 52, mm: 1.45 },
					{ id: 'lap', panel: 'combiner', x: -172, y: 6, mm: 1.3 },
					{ id: 'position', panel: 'combiner', x: 172, y: 6, mm: 1.3 },
					{ id: 'weapon', panel: 'combiner', x: 0, y: -2, mm: 1.3 },
					{ id: 'laptime', panel: 'combiner', x: -100, y: -60, mm: 1.2 },
					{ id: 'shield', panel: 'combiner', x: 132, y: -60, mm: 1.2 },
				],
			};
		},
	},
];

// Point at fraction k along a polyline (by length).
function pathAt(pts, k) {
	let total = 0;
	const seg = [];
	for (let i = 0; i + 1 < pts.length; i++) { const l = pts[i].distanceTo(pts[i + 1]); seg.push(l); total += l; }
	let d = k * total;
	for (let i = 0; i < seg.length; i++) {
		if (d <= seg[i] || i === seg.length - 1) return pts[i].clone().lerp(pts[i + 1], seg[i] ? Math.min(1, d / seg[i]) : 0);
		d -= seg[i];
	}
	return pts[pts.length - 1].clone();
}


// --------------------------------------------------------------------------
// Trim colours, sampled from the ship's own textures (2026-10-04 decision).
//
// Rule (the build will run the same rule in C when the ships load):
//  1. For every triangle of the ship model, take 6 fixed barycentric samples
//     (the 3 permutations of (2/3, 1/6, 1/6) and of (1/6, 5/12, 5/12)), each
//     weighted by area / 6 in game units squared.
//  2. Sample colour = texel (nearest) * vertex colour * 2, as the game's shader
//     shows it, clamped to 255. Transparent texels are skipped.
//  3. Keep samples with HSV saturation >= 0.35 and value >= 0.20 (drops white,
//     grey and black: logos, numbers, glass, engine grilles).
//  4. Histogram the kept weight by hue in 24 bins of 15°, smoothed [1 2 1]
//     around the circle. Primary = the weighted mean RGB of the samples in the
//     peak bin and its two neighbours.
//  5. Secondary = the same for the strongest smoothed bin at least 45° (3 bins)
//     from the primary peak with at least 15% of its weight.
//  6. Both are brought to a value (max channel) between 0.70 and 0.95,
//     keeping hue and saturation: the PSX paint is dark and noisy, and trim
//     has to read as an accent. If there is no secondary, it is the primary
//     at 55% of that value.
//  7. If under 0.5% of the area is saturated at all, the trim is neutral: the
//     frame silver and frame grey slots.
const TRIM_RULE = { samples: 6, minSat: 0.35, minVal: 0.20, bins: 24, smooth: [1, 2, 1], secondaryMinBins: 3, secondaryMinWeight: 0.15, fallbackValue: 0.55, valueRange: [0.70, 0.95], neutralBelowShare: 0.005 };
const BARY = [[2 / 3, 1 / 6, 1 / 6], [1 / 6, 2 / 3, 1 / 6], [1 / 6, 1 / 6, 2 / 3], [1 / 6, 5 / 12, 5 / 12], [5 / 12, 1 / 6, 5 / 12], [5 / 12, 5 / 12, 1 / 6]];

function rgbToHsv(r, g, b) {
	const mx = Math.max(r, g, b), mn = Math.min(r, g, b), d = mx - mn;
	let h = 0;
	if (d > 0) {
		if (mx === r) h = ((g - b) / d) % 6; else if (mx === g) h = (b - r) / d + 2; else h = (r - g) / d + 4;
		h *= 60; if (h < 0) h += 360;
	}
	return [h, mx ? d / mx : 0, mx / 255];
}

function sampleTrim(stream, info, px, aw, ah) {
	const R = TRIM_RULE, nb = R.bins;
	const bins = new Float64Array(nb), acc = Array.from({ length: nb }, () => [0, 0, 0, 0]);
	let total = 0;
	for (let t = 0; t < info.count; t++) {
		const v0 = (info.first + t) * 3;
		const p = [0, 1, 2].map((k) => new THREE.Vector3(stream.pos[(v0 + k) * 3], stream.pos[(v0 + k) * 3 + 1], stream.pos[(v0 + k) * 3 + 2]));
		const area = p[1].clone().sub(p[0]).cross(p[2].clone().sub(p[0])).length() / 2;
		if (area <= 0) continue;
		const w = area / R.samples;
		for (const b of BARY) {
			let u = 0, v = 0; const vc = [0, 0, 0];
			for (let k = 0; k < 3; k++) {
				u += b[k] * stream.uv[(v0 + k) * 2]; v += b[k] * stream.uv[(v0 + k) * 2 + 1];
				for (let c = 0; c < 3; c++) vc[c] += b[k] * stream.col[(v0 + k) * 4 + c];
			}
			const x = Math.min(aw - 1, Math.floor(u * aw)), y = Math.min(ah - 1, Math.floor(v * ah)), o = (y * aw + x) * 4;
			if (px[o + 3] === 0) continue;
			const rgb = [0, 1, 2].map((c) => Math.min(255, px[o + c] * vc[c] / 127.5));
			total += w;
			const [h, sat, val] = rgbToHsv(...rgb);
			if (sat < R.minSat || val < R.minVal) continue;
			const bi = Math.floor(h / (360 / nb)) % nb;
			bins[bi] += w;
			for (let c = 0; c < 3; c++) acc[bi][c] += rgb[c] * w;
			acc[bi][3] += w;
		}
	}
	const sm = new Float64Array(nb);
	for (let i = 0; i < nb; i++) sm[i] = R.smooth[0] * bins[(i + nb - 1) % nb] + R.smooth[1] * bins[i] + R.smooth[2] * bins[(i + 1) % nb];
	const meanAround = (i) => {
		const m = [0, 0, 0, 0];
		for (const j of [i - 1, i, i + 1]) { const a = acc[(j + nb) % nb]; for (let c = 0; c < 4; c++) m[c] += a[c]; }
		return m[3] > 0 ? [m[0] / m[3], m[1] / m[3], m[2] / m[3]] : null;
	};
	const norm = (rgb) => {
		const mx = Math.max(...rgb, 1) / 255, tgt = Math.min(R.valueRange[1], Math.max(R.valueRange[0], mx));
		return rgb.map((c) => Math.round(c * tgt / mx));
	};
	let pk = 0;
	for (let i = 1; i < nb; i++) if (sm[i] > sm[pk]) pk = i;
	const sat = bins.reduce((a, b) => a + b, 0);
	const primaryRaw = meanAround(pk) || [128, 128, 128];
	let sk = -1;
	for (let i = 0; i < nb; i++) {
		const dist = Math.min(Math.abs(i - pk), nb - Math.abs(i - pk));
		if (dist >= R.secondaryMinBins && sm[i] >= R.secondaryMinWeight * sm[pk] && (sk < 0 || sm[i] > sm[sk])) sk = i;
	}
	const primary = norm(primaryRaw);
	let secondary = sk >= 0 ? norm(meanAround(sk)) : primary.map((c) => Math.round(c * R.fallbackValue));
	const share = total ? sat / total : 0;
	if (share < R.neutralBelowShare) {
		const rgbOf = (h) => [(h >> 16) & 255, (h >> 8) & 255, h & 255];
		primary.splice(0, 3, ...rgbOf(SLOTS.frame)); secondary = rgbOf(SLOTS.frame_dark); sk = -2;
	}
	const hex = (c) => (c[0] << 16) | (c[1] << 8) | c[2];
	return {
		primary, secondary, primaryHex: hex(primary), secondaryHex: hex(secondary),
		primaryHue: pk * (360 / nb), secondaryHue: sk >= 0 ? sk * (360 / nb) : null, secondaryFallback: sk === -1, neutral: sk === -2, saturatedShare: share,
	};
}

// --------------------------------------------------------------------------
// Geometry checks: segment/triangle, point/triangle distance

const _e1 = new THREE.Vector3(), _e2 = new THREE.Vector3(), _p = new THREE.Vector3(), _q = new THREE.Vector3(), _t = new THREE.Vector3();
function segTri(o, d, a, b, c, tmax) {
	// o + t d, t in (eps, tmax); Moller-Trumbore
	_e1.subVectors(b, a); _e2.subVectors(c, a);
	_p.crossVectors(d, _e2);
	const det = _e1.dot(_p);
	if (Math.abs(det) < 1e-9) return false;
	const inv = 1 / det;
	_t.subVectors(o, a);
	const u = _t.dot(_p) * inv;
	if (u < 0 || u > 1) return false;
	_q.crossVectors(_t, _e1);
	const v = d.dot(_q) * inv;
	if (v < 0 || u + v > 1) return false;
	const t = _e2.dot(_q) * inv;
	segTriT = t;
	return t > 1e-4 && t < tmax;
}
let segTriT = 0;

function closestOnTri(p, a, b, c) {
	const t = new THREE.Triangle(a, b, c);
	return t.closestPointToPoint(p, new THREE.Vector3());
}

function trianglesOf(geo, matrix) {
	const a = geo.attributes.position.array, out = [];
	for (let i = 0; i < a.length; i += 9) {
		const v = [0, 3, 6].map((k) => new THREE.Vector3(a[i + k], a[i + k + 1], a[i + k + 2]).applyMatrix4(matrix));
		out.push(v);
	}
	return out;
}

// --------------------------------------------------------------------------
// App

const IDENT = new THREE.Matrix4();

async function main() {
	const M = D.manifest;
	const ships = decodeStream(b64bytes(D.bin.ships));
	const track = decodeStream(b64bytes(D.bin.track));
	const sceneS = decodeStream(b64bytes(D.bin.scene));
	const sky = decodeStream(b64bytes(D.bin.sky));
	const secBytes = b64bytes(D.bin.track_sections);
	const imgs = {};
	await Promise.all(Object.entries(D.png).map(async ([k, v]) => { imgs[k] = await loadImage(v); }));
	for (const sz of ['16', '12', '8']) FONT[sz] = { img: imgs['hud_font' + sz], glyphs: M.font[sz].glyphs, space: M.font[sz].space, tinted: new Map() };
	IMG.speedo = imgs.hud_speedo;
	IMG.star = imgs.hud_icon_star;
	IMG.wicons = [];
	for (let i = 0; i < M.wicons; i++) IMG.wicons.push(imgs['hud_wicon_' + i]);
	sizeInstruments();

	const tex = (img) => {
		const t = new THREE.Texture(img);
		t.flipY = false; t.needsUpdate = true;
		t.magFilter = THREE.LinearFilter; t.minFilter = THREE.LinearMipmapLinearFilter;
		t.anisotropy = 4;
		return t;
	};

	// ---- renderer, scene
	THREE.ColorManagement.enabled = false;
	const canvas = document.getElementById('view');
	const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, preserveDrawingBuffer: true });
	renderer.outputColorSpace = THREE.LinearSRGBColorSpace;
	renderer.setPixelRatio(SHOT ? 1 : Math.min(2, window.devicePixelRatio));
	renderer.autoClear = false;
	const scene = new THREE.Scene();

	const world = new THREE.Group();
	scene.add(world);
	world.add(new THREE.Mesh(streamGeometry(track, 0, track.n / 3), gameMaterial(tex(imgs.track_atlas), 1.0)));
	world.add(new THREE.Mesh(streamGeometry(sceneS, 0, sceneS.n / 3), gameMaterial(tex(imgs.scene_atlas), 1.0)));
	const skyScene = new THREE.Scene();
	const skyMat = gameMaterial(tex(imgs.sky_atlas), 1.0);
	skyMat.uniforms.fade.value.set(1e9, 2e9);
	skyMat.depthWrite = false; skyMat.depthTest = false;
	const skyMesh = new THREE.Mesh(streamGeometry(sky, 0, sky.n / 3), skyMat);
	skyScene.add(skyMesh);
	const shipTex = tex(imgs.ships_atlas);
	// trim colours for all 8 ships, sampled from their own textures
	const atlasPx = (() => {
		const c = document.createElement('canvas');
		c.width = imgs.ships_atlas.width; c.height = imgs.ships_atlas.height;
		const x = c.getContext('2d');
		x.drawImage(imgs.ships_atlas, 0, 0);
		return x.getImageData(0, 0, c.width, c.height).data;
	})();
	const TRIM = M.ships.map((si) => sampleTrim(ships, si, atlasPx, imgs.ships_atlas.width, imgs.ships_atlas.height));

	// ---- track path
	const nSec = secBytes.byteLength / 24;
	const sdv = new DataView(secBytes.buffer, secBytes.byteOffset, secBytes.byteLength);
	const secPts = [], secUp = [];
	for (let i = 0; i < nSec; i++) {
		const f = (k) => sdv.getFloat32(i * 24 + k * 4, true);
		secPts.push(new THREE.Vector3(f(0), -f(1), -f(2)));
		secUp.push(new THREE.Vector3(f(3), -f(4), -f(5)).normalize());
	}
	const curve = new THREE.CatmullRomCurve3(secPts, true, 'centripetal');
	const lapLen = curve.getLength();
	const NS = 6000;
	const path = [];
	for (let i = 0; i < NS; i++) {
		const u = i / NS;
		const t = curve.getUtoTmapping(u);
		const x = t * nSec, i0 = Math.floor(x) % nSec, i1 = (i0 + 1) % nSec, fr = x - Math.floor(x);
		const up = secUp[i0].clone().lerp(secUp[i1], fr).normalize();
		path.push({ p: curve.getPointAt(u), T: curve.getTangentAt(u), up });
	}
	// lateral curvature (toward the inside of the turn), smoothed, for the bank
	const ds = lapLen / NS;
	const lat = new Float32Array(NS), latDir = [];
	for (let i = 0; i < NS; i++) {
		const a = path[(i - 3 + NS) % NS].T, b = path[(i + 3) % NS].T;
		const dT = b.clone().sub(a).divideScalar(6 * ds);
		const up = path[i].up, T = path[i].T;
		const side = new THREE.Vector3().crossVectors(T, up).normalize();  // right
		lat[i] = dT.dot(side);                                            // >0: turning right
		latDir.push(side);
	}
	const latS = new Float32Array(NS), W = 40;
	for (let i = 0; i < NS; i++) { let s = 0; for (let k = -W; k <= W; k++) s += lat[(i + k + NS) % NS]; latS[i] = s / (2 * W + 1); }

	// ---- state
	const num = (k, d) => (Q.has(k) ? parseFloat(Q.get(k)) : d);
	const S = {
		ship: num('ship', 0) | 0,
		variation: Q.get('var') || 'H',
		view: Q.get('view') || 'cockpit',
		yaw: num('yaw', 0), pitch: num('pitch', 0),
		eyeF: num('eyef', 0), eyeU: num('eyeu', 0),        // cm, physical
		ws: num('ws', RECOMMENDED_WS),
		eyeMode: Q.get('eye') === 'game' ? 'game' : 'fit',
		bank: Q.get('bank') || 'level',
		at: num('at', 0.60), speed: num('speed', 10000),
		playing: !SHOT && Q.get('play') !== '0',
		hover: 220,
		show: { hull: true, tub: true, sills: true, seat: true, struts: true, glass: true, bulkhead: true, headrest: true, hoop: true, dash: true,
			podL: true, podR: true, weaponPod: true, body: Q.get('body') === '1',
			speedo: true, shield: true, lap: true, position: true, laptime: true, lives: true, prevlaps: true, weapon: true },
		orbit: [num('oyaw', -135), num('opitch', 18), num('odist', 4.2)],
	};
	for (const k of (Q.get('hide') || '').split(',').filter(Boolean)) S.show[k] = false;
	const LOOKS = { fwd: [0, 0], left: [75, -5], right: [-75, -5], back: [150, 0], down: [0, -40], up: [0, 35] };
	if (Q.get('look') && LOOKS[Q.get('look')]) [S.yaw, S.pitch] = LOOKS[Q.get('look')];

	// ---- rig
	const rig = new THREE.Group();          // ship frame (yaw/pitch, roll per bank mode)
	scene.add(rig);
	const pivot = new THREE.Group();        // pivots the cockpit about the eye (alt bank mode)
	rig.add(pivot);
	const cockpit = new THREE.Group();
	pivot.add(cockpit);
	const head = new THREE.Group();
	rig.add(head);
	const camera = new THREE.PerspectiveCamera(90, 1, 2, 70000);
	head.add(camera);
	const outsideCam = new THREE.PerspectiveCamera(50, 1, 2, 70000);
	rig.add(outsideCam);

	// Frame per-eye FOV, mono approximation: symmetric horizontal (mean of L/R),
	// the real vertical split.
	const FOV = { l: (1.028 + 0.885) / 2, r: (1.028 + 0.885) / 2, u: 0.864, d: 1.048 };
	const fovAspect = (Math.tan(FOV.l) + Math.tan(FOV.r)) / (Math.tan(FOV.u) + Math.tan(FOV.d));
	function setFrameProjection(cam) {
		const n = cam.near;
		cam.projectionMatrix.makePerspective(-Math.tan(FOV.l) * n, Math.tan(FOV.r) * n, Math.tan(FOV.u) * n, -Math.tan(FOV.d) * n, n, cam.far);
		cam.projectionMatrixInverse.copy(cam.projectionMatrix).invert();
	}

	// ---- build the cockpit for (ship, variation)
	let built = null;
	const mats = new Map();
	const flat = (hex, o = {}) => {
		const k = hex + ':' + (o.emissive || 0);
		if (!mats.has(k)) mats.set(k, flatMaterial(hex, o));
		return mats.get(k);
	};
	const glassMat = glassMaterial(0.045);
	const combinerMat = glassMaterial(0.10);
	const hullMat = gameMaterial(shipTex, 0.55);

	// ---- the eye from a seated pilot: the tub floor (FIT.floorToEye below the
	// eye) must clear the bottom of every hull shape by FIT.margin over the
	// seat and leg footprint, so the eye is one point shared by all 8 ships
	const shapeTris = {};
	const hullTrisOf = (shipIndex) => {
		if (!shapeTris[shipIndex]) { const si = M.ships[shipIndex]; shapeTris[shipIndex] = trianglesOf(streamGeometry(ships, si.first, si.count), IDENT); }
		return shapeTris[shipIndex];
	};
	function hullBottom(tris, x, z) {
		const o = new THREE.Vector3(x, -4000, z), dir = new THREE.Vector3(0, 1, 0);
		let best = null;
		for (const t of tris) if (segTri(o, dir, t[0], t[1], t[2], 1e5)) { const y = -4000 + segTriT; if (best === null || y < best) best = y; }
		return best;
	}
	function fitEye(ws) {
		let need = -Infinity;
		for (const shipIndex of [0, 1, 3, 5]) {
			const tris = hullTrisOf(shipIndex);
			for (const r of FIT.footprint.r) for (const f of FIT.footprint.f) {
				const b = hullBottom(tris, r * ws, -f * ws);
				if (b !== null) need = Math.max(need, b);
			}
		}
		return +(need + (FIT.margin + FIT.floorToEye) * ws).toFixed(1);
	}
	function applyScale() {
		REF = S.ws;
		EYE_Y = S.eyeMode === 'game' ? GAME_EYE_Y : fitEye(S.ws);
	}

	function build() {
		applyScale();
		while (cockpit.children.length) cockpit.remove(cockpit.children[0]);
		const shipInfo = M.ships[S.ship];
		const team = PILOT_TEAM[shipInfo.pilot];
		const trim = TRIM[S.ship];
		const accent = 'trim_primary';
		const V = VARIATIONS.find((v) => v.id === S.variation) || VARIATIONS[0];
		CABIN = V.cabin || CABIN_ROUND1;
		const parts = [];
		const add = (group, geo, hex, o) => {
			// slot: a material slot name (see SLOTS) or, in the older variations, a colour
			const slot = typeof hex === 'string' ? hex : null;
			let mat;
			if (group === 'glass') mat = glassMat;
			else if (group === 'combiner') mat = combinerMat;
			else {
				const c = slot ? (slot === 'trim_primary' ? trim.primaryHex : slot === 'trim_secondary' ? trim.secondaryHex : SLOTS[slot]) : hex;
				mat = flat(c, { emissive: (o && o.emissive) ?? SLOT_EMISSIVE[slot] ?? 0 });
			}
			const mesh = new THREE.Mesh(geo, mat);
			mesh.userData.group = group;
			mesh.userData.slot = group === 'glass' || group === 'combiner' ? 'glass' : (slot || 'authored');
			if (group === 'glass' || group === 'combiner') mesh.renderOrder = 10;
			cockpit.add(mesh);
			parts.push(mesh);
		};
		// H.top(r, f): the hull's top surface (uncut), eye-relative metres
		const hullFull = streamGeometry(ships, shipInfo.first, shipInfo.count);
		const fullTris = trianglesOf(hullFull, IDENT);
		const down = new THREE.Vector3(0, -1, 0);
		const H = {
			top(r, f) {
				const o = new THREE.Vector3(r * REF, 4000, -f * REF);
				let best = null;
				for (const t of fullTris) {
					if (segTri(o, down, t[0], t[1], t[2], 1e5)) {
						const y = 4000 - segTriT;
						if (best === null || y > best) best = y;
					}
				}
				return best === null ? null : (best - EYE_Y) / REF;
			},
		};
		const layout = V.build(add, accent, H);
		// hull with the cabin cut out
		const c = CABIN;
		const box = { x0: -(c.halfWidth + 0.005) * REF, x1: (c.halfWidth + 0.005) * REF, y0: EYE_Y + (c.floor + 0.01) * REF, y1: Infinity,
			z0: -c.front * REF, z1: -c.rear * REF };
		const hullGeo = cutHull(hullFull, box);
		const hull = new THREE.Mesh(hullGeo, hullMat);
		hull.userData.group = 'hull';
		cockpit.add(hull);
		// panels and instruments
		const panels = Object.fromEntries((layout.panels || []).map((p) => [p.id, { ...p, frame: panelFrame(p) }]));
		const inst = [], faces = [];
		// face panels: one texture per panel at its planned resolution (x FACE_K here)
		for (const pnl of Object.values(panels)) {
			if (!pnl.tex) continue;
			const { c: pc, n, x, y } = pnl.frame;
			const cv = document.createElement('canvas');
			cv.width = pnl.tex[0] * FACE_K; cv.height = pnl.tex[1] * FACE_K;
			const ct = new THREE.CanvasTexture(cv);
			ct.anisotropy = 8;
			const geo = new THREE.PlaneGeometry(pnl.w * REF, pnl.h * REF);
			geo.applyMatrix4(new THREE.Matrix4().makeBasis(x, y, n).setPosition(pc.clone().addScaledVector(n, 0.003 * REF)));
			const mesh = new THREE.Mesh(geo, new THREE.MeshBasicMaterial({ map: ct, side: THREE.DoubleSide }));
			mesh.userData.group = pnl.group;
			mesh.userData.face = pnl.id;
			cockpit.add(mesh);
			faces.push({ pnl, canvas: cv, ctx: cv.getContext('2d'), texture: ct, mesh });
		}
		for (const it of layout.instruments) {
			if (it.tx !== undefined) {
				// an element of a face panel: its rectangle, for the checks
				const def = INSTRUMENTS[it.id], pnl = panels[it.panel];
				const { c: pc, n, x, y } = pnl.frame;
				const texelMm = pnl.w * 1000 / pnl.tex[0];
				const mm = it.scale * texelMm;
				const cx = (it.tx + def.w * it.scale / 2) * texelMm - pnl.w * 500, cy = pnl.h * 500 - (it.ty + def.h * it.scale / 2) * texelMm;
				const wU = def.w * mm / 1000 * REF, hU = def.h * mm / 1000 * REF;
				const pos = pc.clone().addScaledVector(x, cx / 1000 * REF).addScaledVector(y, cy / 1000 * REF).addScaledVector(n, 0.003 * REF);
				inst.push({ ...it, def, mm, n, x, y, pos, wU, hU, texelMm });
				continue;
			}
			const def = INSTRUMENTS[it.id];
			const pnl = panels[it.panel];
			const { c: pc, n, x, y } = pnl.frame;
			const cv = document.createElement('canvas');
			cv.width = def.w * K; cv.height = def.h * K;
			const ct = new THREE.CanvasTexture(cv);
			ct.anisotropy = 8;
			const glass = pnl.style === 'glass';
			const mat = new THREE.MeshBasicMaterial({ map: ct, transparent: glass, blending: glass ? THREE.AdditiveBlending : THREE.NormalBlending, depthWrite: !glass, side: THREE.DoubleSide });
			const wU = def.w * it.mm / 1000 * REF, hU = def.h * it.mm / 1000 * REF;
			const geo = new THREE.PlaneGeometry(wU, hU);
			const pos = pc.clone().addScaledVector(x, it.x / 1000 * REF).addScaledVector(y, it.y / 1000 * REF).addScaledVector(n, 0.003 * REF);
			geo.applyMatrix4(new THREE.Matrix4().makeBasis(x, y, n).setPosition(pos));
			const mesh = new THREE.Mesh(geo, mat);
			mesh.userData.group = it.id;
			mesh.renderOrder = glass ? 11 : 0;
			cockpit.add(mesh);
			inst.push({ ...it, def, canvas: cv, ctx: cv.getContext('2d'), texture: ct, mesh, n, x, y, pos, wU, hU });
		}
		built = { V, layout, parts, hull, inst, faces, panels, trim, team, shipInfo, H, box, noseF: shipInfo.max[2] / REF * 0.98, removed: hullGeo.userData.removed };
		buildBody();
		applyVisibility();
		runChecks();
		updateInfo();
	}

	// ---- body reference (physical metres, so it shows what world_scale means)
	let body = null, bodyMat = null;
	function buildBody() {
		if (body) head.parent.remove(body);
		body = new THREE.Group();
		const m = (r, u, f) => new THREE.Vector3(r * S.ws, u * S.ws, -f * S.ws);
		// drawn as an x-ray in the outside view, so it shows through hull and tub
		const mat = flatMaterial(COL.body, { emissive: 0.05 });
		mat.transparent = true;
		bodyMat = mat;
		const limb = (a, b, t) => { const g = beamGeometry(a, b, t * S.ws, t * S.ws); body.add(new THREE.Mesh(g, mat)); };
		for (const sgn of [-1, 1]) {
			limb(m(sgn * 0.10, -0.80, -0.12), m(sgn * 0.11, -0.66, 0.36), 0.15);   // thigh
			limb(m(sgn * 0.11, -0.66, 0.36), m(sgn * 0.12, -0.86, 0.66), 0.11);    // shin
			limb(m(sgn * 0.21, -0.27, -0.10), m(sgn * 0.25, -0.52, 0.10), 0.09);   // upper arm
			limb(m(sgn * 0.25, -0.52, 0.10), m(sgn * 0.17, -0.47, 0.36), 0.08);    // forearm
		}
		const torso = new THREE.Mesh(beamGeometry(m(0, -0.80, -0.16), m(0, -0.24, -0.12), 0.40 * S.ws, 0.22 * S.ws), mat);
		torso.userData.outsideOnly = true;
		body.add(torso);
		const headM = new THREE.Mesh(new THREE.SphereGeometry(0.11 * S.ws, 16, 12), mat);
		headM.position.copy(m(0, 0.02, -0.07));
		headM.userData.outsideOnly = true;
		body.add(headM);
		head.parent.add(body);
	}

	function applyVisibility() {
		for (const o of cockpit.children) {
			const g = o.userData.group === 'combiner' ? 'dash' : o.userData.group;
			o.visible = S.show[g] !== false;
		}
		if (body) {
			body.visible = S.show.body || S.view === 'outside';
			for (const o of body.children) if (o.userData.outsideOnly) o.visible = S.view === 'outside';
		}
	}

	// ---- per-frame pose
	const state = { lapTime: 31.4, lap: 3, position: 3, weapon: 2, shield: 0, thrust: 60, speedBars: 10, lives: 3, prevLaps: [58.6, 57.9] };
	FACE_STATE.current = state;
	function eyeLocal() { return new THREE.Vector3(0, EYE_Y + S.eyeU / 100 * S.ws, -(S.eyeF / 100) * S.ws); }

	function sample(u) {
		const x = ((u % 1) + 1) % 1 * NS, i0 = Math.floor(x) % NS, i1 = (i0 + 1) % NS, fr = x - Math.floor(x);
		const a = path[i0], b = path[i1];
		return {
			p: a.p.clone().lerp(b.p, fr), T: a.T.clone().lerp(b.T, fr).normalize(), up: a.up.clone().lerp(b.up, fr).normalize(),
			lat: latS[i0] * (1 - fr) + latS[i1] * fr,
		};
	}

	function basis(T, up) {
		const z = T.clone().negate();                                      // three: -z forward
		const x = new THREE.Vector3().crossVectors(T, up).normalize();     // right
		const y = new THREE.Vector3().crossVectors(z, x).normalize();
		return new THREE.Matrix4().makeBasis(x, y, z);
	}

	let bankDeg = 0;
	function pose() {
		const s = sample(S.at);
		// steering bank: proportional to yaw rate, as the game's angle.z follows angular_velocity.y
		const yawRate = s.lat * S.speed;                                  // rad/s
		const bank = THREE.MathUtils.clamp(yawRate * 0.40, -deg(35), deg(35));
		bankDeg = bank * 180 / Math.PI;
		const right = new THREE.Vector3().crossVectors(s.T, s.up).normalize();
		const upBank = s.up.clone().multiplyScalar(Math.cos(bank)).addScaledVector(right, Math.sin(bank)).normalize();
		const mFollow = basis(s.T, upBank);                                  // ship incl. camber + bank
		const mLevel = basis(s.T, new THREE.Vector3(0, 1, 0));               // yaw + pitch only
		const shipPos = s.p.clone().addScaledVector(s.up, S.hover);
		const eye = eyeLocal();
		const eyeWorld = eye.clone().applyMatrix4(mFollow).add(shipPos);
		const mRig = S.bank === 'follow' ? mFollow : mLevel;
		rig.quaternion.setFromRotationMatrix(mRig);
		rig.position.copy(eyeWorld).sub(eye.clone().applyQuaternion(rig.quaternion));
		// cockpit relative to the rig: identity, or (alt mode) the bank, about the eye
		pivot.position.copy(eye);
		cockpit.position.copy(eye).negate();
		if (S.bank === 'cockpit') {
			const rel = new THREE.Matrix4().copy(mLevel).invert().multiply(mFollow);
			pivot.quaternion.setFromRotationMatrix(rel);
		} else pivot.quaternion.identity();
		head.position.copy(eye);
		camera.rotation.set(deg(S.pitch), deg(S.yaw), 0, 'YXZ');
		if (body) { body.position.copy(eye); body.quaternion.copy(pivot.quaternion); }
		// outside camera orbits the eye
		const [oy, op, od] = S.orbit;
		const dist = od * 128;   // in ship units, so the ship frames the same at every world_scale
		outsideCam.position.set(
			eye.x + dist * Math.cos(deg(op)) * Math.sin(deg(oy)),
			eye.y + dist * Math.sin(deg(op)),
			eye.z + dist * Math.cos(deg(op)) * Math.cos(deg(oy)));
		rig.updateMatrixWorld(true);
		outsideCam.lookAt(new THREE.Vector3(0, EYE_Y - 40, 0).applyMatrix4(rig.matrixWorld));
		// instrument state, animated
		const tt = performance.now() / 1000;
		state.speedBars = THREE.MathUtils.clamp(12.2 - Math.abs(yawRate) * 1.6, 6, 12.6);
		state.thrust = 65 * THREE.MathUtils.clamp(1 - Math.abs(yawRate) * 0.08, 0.6, 1);
		state.lapTime = 31.4 + (SHOT ? 0 : (tt % 60));
		state.shield = SHOT ? 0.62 : Math.max(0, 1 - ((tt % 20) / 6.67));
		state.weapon = SHOT ? 2 : Math.floor(tt / 8) % M.wicons;
	}

	function drawInstruments() {
		for (const f of built.faces) {
			composeFace(f.ctx, f.pnl, built.inst, FACE_K, (id) => S.show[id] !== false);
			f.texture.needsUpdate = true;
		}
		for (const it of built.inst) {
			if (!it.canvas) continue;
			const ctx = it.ctx;
			ctx.imageSmoothingEnabled = false;
			ctx.clearRect(0, 0, it.canvas.width, it.canvas.height);
			const glass = (built.layout.panels.find((p) => p.id === it.panel) || {}).style === 'glass';
			ctx.fillStyle = glass ? '#000' : '#0b0d10';
			ctx.fillRect(0, 0, it.canvas.width, it.canvas.height);
			it.def.draw(ctx, state);
			it.texture.needsUpdate = true;
		}
	}

	// ---- checks: legibility, instrument occlusion / poke-through, clearance
	let checks = {};
	function runChecks() {
		const eye = eyeLocal();
		const occl = [];       // [tris, label]
		const hullTris = trianglesOf(built.hull.geometry, IDENT);
		occl.push([hullTris, 'hull']);
		for (const m of built.parts) if (m.userData.group !== 'glass' && m.userData.group !== 'combiner' && m.visible !== false) occl.push([trianglesOf(m.geometry, IDENT), m.userData.group]);
		const res = { instruments: {}, clearance: {} };
		for (const it of built.inst) {
			if (!S.show[it.id]) continue;
			// legibility: smallest glyph height, projected, in degrees
			const toEye = eye.clone().sub(it.pos);
			const dist = toEye.length();
			const cosv = Math.abs(toEye.clone().normalize().dot(it.n));
			const minPx = it.def.minText;
			const hM = minPx ? minPx * it.mm / 1000 : null;
			const ang = hM ? 2 * Math.atan((hM * REF * cosv) / 2 / dist) * 180 / Math.PI : null;
			// occlusion: rays from the eye to a 5x4 grid on the face
			let blocked = 0, total = 0;
			const by = new Set();
			for (let i = 0; i < 5; i++) for (let j = 0; j < 4; j++) {
				const pt = it.pos.clone().addScaledVector(it.x, (i / 4 - 0.5) * it.wU * 0.96).addScaledVector(it.y, (j / 3 - 0.5) * it.hU * 0.96);
				const d = pt.clone().sub(eye);
				total++;
				let hit = false;
				for (const [tris, label] of occl) {
					for (const t of tris) if (segTri(eye, d, t[0], t[1], t[2], 0.999)) { hit = true; by.add(label); break; }
					if (hit) break;
				}
				if (hit) blocked++;
			}
			// poke-through: any edge of hull/part triangles crossing the face
			const face = [
				it.pos.clone().addScaledVector(it.x, -it.wU / 2).addScaledVector(it.y, -it.hU / 2),
				it.pos.clone().addScaledVector(it.x, it.wU / 2).addScaledVector(it.y, -it.hU / 2),
				it.pos.clone().addScaledVector(it.x, it.wU / 2).addScaledVector(it.y, it.hU / 2),
				it.pos.clone().addScaledVector(it.x, -it.wU / 2).addScaledVector(it.y, it.hU / 2)];
			let pokes = 0;
			const pokeBy = new Set();
			for (const [tris, label] of occl) for (const t of tris) for (let e = 0; e < 3; e++) {
				const a = t[e], b = t[(e + 1) % 3], d = b.clone().sub(a);
				if (segTri(a, d, face[0], face[1], face[2], 1) || segTri(a, d, face[0], face[2], face[3], 1)) { pokes++; pokeBy.add(label); }
			}
			res.instruments[it.id] = {
				distance_m: +(dist / REF).toFixed(3), text_deg: ang && +ang.toFixed(2), legible: ang ? ang >= 0.6 : null,
				blocked: `${blocked}/${total}`, blocked_by: [...by], pokes, poke_by: [...pokeBy],
				size_mm: [Math.round(it.def.w * it.mm), Math.round(it.def.h * it.mm)],
			};
		}
		// clearance from the eye (physical metres at the current world_scale)
		for (const [tris, label] of occl) {
			let best = Infinity;
			for (const t of tris) best = Math.min(best, closestOnTri(eye, t[0], t[1], t[2]).distanceTo(eye));
			const prev = res.clearance[label];
			res.clearance[label] = Math.min(prev ?? Infinity, best / S.ws);
		}
		for (const k in res.clearance) res.clearance[k] = +res.clearance[k].toFixed(3);
		// how much of the nose ridge (centre line, f = 0.9 m to the tip) the
		// cockpit parts hide, as seen from the eye
		let noseVis = 0, noseN = 0;
		for (let i = 0; i <= 24; i++) {
			const f = 0.9 + (built.noseF - 0.9) * i / 24;
			const u = built.H.top(0, f);
			if (u === null) continue;
			const pt = P(0, u + 0.01, f), d = pt.clone().sub(eye);
			let hit = false;
			for (const [tris, label] of occl) {
				if (label === 'hull') continue;
				for (const t of tris) if (segTri(eye, d, t[0], t[1], t[2], 0.999)) { hit = true; break; }
				if (hit) break;
			}
			noseN++;
			if (!hit) noseVis++;
		}
		res.nose_visible = noseN ? +(noseVis / noseN).toFixed(2) : null;
		const ridge = built.H.top(0, 0);
		res.eye = { world_scale: S.ws, mode: S.eyeMode, game: [0, -EYE_Y, 0], above_ridge_m: ridge === null ? null : +(-ridge).toFixed(3), floor_game_y: +(-(EYE_Y + CABIN.floor * REF)).toFixed(1) };
		res.hull_tris_removed = built.removed;
		checks = res;
		document.getElementById('checks').textContent = JSON.stringify(res);
		renderChecks();
	}

	// ---- UI
	const $ = (id) => document.getElementById(id);
	function updateInfo() {
		const si = built.shipInfo;
		const L = (si.max[2] - si.min[2]) / S.ws, Wd = (si.max[0] - si.min[0]) / S.ws, H = (si.max[1] - si.min[1]) / S.ws;
		const kmh = S.speed / S.ws * 3.6;
		$('scaleinfo').innerHTML = `ship ${L.toFixed(2)} x ${Wd.toFixed(2)} x ${H.toFixed(2)} m (L x W x H)<br>` +
			`eye (0, ${(-EYE_Y).toFixed(1)}, 0) game units (${S.eyeMode === 'game' ? 'the game\'s ship_cockpit()' : 'fitted, seated pilot'}); ${checks.eye ? checks.eye.above_ridge_m.toFixed(2) : '?'} m above the deck ridge<br>` +
			`track ~${(2600 / S.ws).toFixed(0)}-${(3500 / S.ws).toFixed(0)} m wide; speed ${kmh.toFixed(0)} km/h`;
		const team = TEAM_NAME[built.team], pilot = PILOT_NAME[si.pilot];
		$('caption').textContent = `${built.V.id} ${built.V.name}  |  ${team} (${pilot}, ${si.name})  |  ` +
			`${S.view === 'outside' ? 'outside' : 'look ' + (Q.get('look') || `${S.yaw.toFixed(0)}/${S.pitch.toFixed(0)}`)}  |  ` +
			`${S.ws} u/m, eye y ${(-EYE_Y).toFixed(0)}  |  ${{ level: 'level in banks', follow: 'bank-follow', cockpit: 'cockpit banks, horizon level' }[S.bank]}`;
		$('blurb').textContent = built.V.blurb;
	}

	function renderChecks() {
		const rows = Object.entries(checks.instruments).map(([k, v]) =>
			`<tr><td>${INSTRUMENTS[k].label}</td><td>${v.distance_m.toFixed(2)}</td><td class="${v.legible === false ? 'bad' : ''}">${v.text_deg ?? '-'}</td>` +
			`<td class="${v.blocked.startsWith('0/') ? '' : 'bad'}">${v.blocked}${v.blocked_by.length ? ' ' + v.blocked_by.join(',') : ''}</td><td class="${v.pokes ? 'bad' : ''}">${v.pokes}</td></tr>`).join('');
		const cl = Object.entries(checks.clearance).map(([k, v]) => `${k} ${v.toFixed(2)}`).join(', ');
		$('checkview').innerHTML = `<table><tr><th>instrument</th><th>m</th><th>text&deg;</th><th>blocked</th><th>poke</th></tr>${rows}</table>` +
			`<div class="small">eye clearance (m): ${cl}</div><div class="small">nose ridge visible past the dash: ${Math.round(checks.nose_visible * 100)}%</div>` +
			`<div class="small">bank now ${bankDeg.toFixed(1)}&deg;</div>`;
	}

	function buildUI() {
		const shipSel = $('ship');
		M.ships.forEach((s, i) => {
			const o = document.createElement('option');
			o.value = i; o.textContent = `${TEAM_NAME[PILOT_TEAM[s.pilot]]}: ${PILOT_NAME[s.pilot]} (${s.name})`;
			shipSel.appendChild(o);
		});
		shipSel.value = S.ship;
		shipSel.onchange = () => { S.ship = +shipSel.value; build(); syncTrim(); };
		const css = (c) => `rgb(${c[0]},${c[1]},${c[2]})`;
		$('trim').innerHTML = M.ships.map((si, i) => `<div class="trow" data-i="${i}"><span class="sw" style="background:${css(TRIM[i].primary)}"></span>` +
			`<span class="sw" style="background:${css(TRIM[i].secondary)}"></span> ${TEAM_NAME[PILOT_TEAM[si.pilot]]} ${si.name}</div>`).join('');
		const vs = $('variations');
		for (const v of VARIATIONS) {
			const b = document.createElement('button');
			b.textContent = `${v.id} ${v.name}`;
			b.onclick = () => { S.variation = v.id; build(); syncButtons(); };
			b.dataset.v = v.id;
			vs.appendChild(b);
		}
		const comps = $('components');
		for (const k of Object.keys(S.show)) {
			const l = document.createElement('label');
			const cb = document.createElement('input');
			cb.type = 'checkbox'; cb.checked = S.show[k];
			cb.onchange = () => { S.show[k] = cb.checked; applyVisibility(); runChecks(); };
			l.appendChild(cb); l.append(' ' + (INSTRUMENTS[k] ? INSTRUMENTS[k].label : k));
			comps.appendChild(l);
		}
		const slider = (id, key, fmt, rebuild) => {
			const el = $(id), out = $(id + 'v');
			el.value = S[key];
			const upd = () => { S[key] = +el.value; out.textContent = fmt(S[key]); if (rebuild) rebuild(); };
			el.oninput = upd; out.textContent = fmt(S[key]);
		};
		slider('eyef', 'eyeF', (v) => `${v} cm`, () => { buildBody(); runChecks(); });
		slider('eyeu', 'eyeU', (v) => `${v} cm`, () => { buildBody(); runChecks(); });
		slider('ws', 'ws', (v) => `${v} u/m`, () => { S.eyeMode = 'fit'; build(); syncScale(); });
		for (const o of SCALE_OPTIONS) {
			const b = document.createElement('button');
			b.textContent = o.label; b.dataset.ws = o.ws; b.dataset.eye = o.eye;
			b.onclick = () => { S.ws = o.ws; S.eyeMode = o.eye; $('ws').value = o.ws; $('wsv').textContent = `${o.ws} u/m`; build(); syncScale(); };
			$('scales').appendChild(b);
		}
		syncScale();
		slider('speed', 'speed', (v) => `${v} u/s`, updateInfo);
		slider('at', 'at', (v) => v.toFixed(3));
		$('bank').value = S.bank;
		$('bank').onchange = () => { S.bank = $('bank').value; updateInfo(); };
		$('play').onclick = () => { S.playing = !S.playing; $('play').textContent = S.playing ? 'Pause' : 'Play'; };
		$('play').textContent = S.playing ? 'Pause' : 'Play';
		$('viewsel').value = S.view;
		$('viewsel').onchange = () => { S.view = $('viewsel').value; applyVisibility(); updateInfo(); };
		for (const [k, v] of Object.entries(LOOKS)) {
			const b = document.createElement('button');
			b.textContent = k;
			b.onclick = () => { [S.yaw, S.pitch] = v; S.view = 'cockpit'; $('viewsel').value = 'cockpit'; applyVisibility(); updateInfo(); };
			$('looks').appendChild(b);
		}
		syncButtons();
		syncTrim();
		// drag = head look (cockpit) or orbit (outside)
		let drag = null;
		canvas.addEventListener('pointerdown', (e) => { drag = [e.clientX, e.clientY]; canvas.setPointerCapture(e.pointerId); });
		canvas.addEventListener('pointerup', () => { drag = null; });
		canvas.addEventListener('pointermove', (e) => {
			if (!drag) return;
			const dx = e.clientX - drag[0], dy = e.clientY - drag[1];
			drag = [e.clientX, e.clientY];
			if (S.view === 'outside') { S.orbit[0] -= dx * 0.3; S.orbit[1] = THREE.MathUtils.clamp(S.orbit[1] + dy * 0.3, -80, 80); }
			else { S.yaw += dx * 0.25; S.pitch = THREE.MathUtils.clamp(S.pitch + dy * 0.25, -85, 85); }
		});
		canvas.addEventListener('wheel', (e) => { if (S.view === 'outside') S.orbit[2] = THREE.MathUtils.clamp(S.orbit[2] * (e.deltaY > 0 ? 1.1 : 0.9), 1, 40); });
	}
	function syncScale() { for (const b of $('scales').children) b.classList.toggle('on', +b.dataset.ws === S.ws && b.dataset.eye === S.eyeMode); }
	function syncTrim() { for (const r of $('trim').children) r.classList.toggle('on', +r.dataset.i === S.ship); }
	function syncButtons() { for (const b of $('variations').children) b.classList.toggle('on', b.dataset.v === S.variation); }

	// ---- render
	function resize() {
		const w = canvas.clientWidth, h = canvas.clientHeight;
		renderer.setSize(w, h, false);
	}
	function render() {
		pose();
		glassMat.uniforms.opacity.value = S.view === 'outside' ? 0.14 : 0.045;
		if (bodyMat) {
			const xray = S.view === 'outside';
			bodyMat.depthTest = !xray; bodyMat.uniforms.alpha.value = xray ? 0.7 : 1;
			for (const o of body.children) o.renderOrder = xray ? 20 : 0;
		}
		rig.updateMatrixWorld(true);
		const cam = S.view === 'outside' ? outsideCam : camera;
		const w = canvas.clientWidth, h = canvas.clientHeight;
		renderer.setScissorTest(false);
		renderer.setClearColor(0x000000, 1);
		renderer.clear();
		let vw = w, vh = h, vx = 0, vy = 0;
		if (cam === camera) {
			setFrameProjection(camera);
			if (w / h > fovAspect) { vw = Math.round(h * fovAspect); vx = Math.round((w - vw) / 2); } else { vh = Math.round(w / fovAspect); vy = Math.round((h - vh) / 2); }
		} else {
			outsideCam.aspect = w / h; outsideCam.updateProjectionMatrix();
		}
		renderer.setViewport(vx, vy, vw, vh);
		renderer.setScissor(vx, vy, vw, vh);
		renderer.setScissorTest(true);
		// ?bg=black: the cockpit alone on black, for comparing with the build's harness render
		world.visible = Q.get('bg') !== 'black';
		skyScene.visible = Q.get('bg') !== 'black';
		cam.updateMatrixWorld(true);
		skyMesh.position.setFromMatrixPosition(cam.matrixWorld).add(new THREE.Vector3(0, -M.sky_y_offset, 0));
		renderer.render(skyScene, cam);
		renderer.clearDepth();
		renderer.render(scene, cam);
		renderer.setScissorTest(false);
	}

	let last = performance.now(), lastInst = 0;
	function frame(now) {
		const dt = Math.min(0.1, (now - last) / 1000);
		last = now;
		if (S.playing) { S.at = (S.at + dt * S.speed / lapLen) % 1; $('at').value = S.at; $('atv').textContent = S.at.toFixed(3); }
		if (now - lastInst > 50) { drawInstruments(); lastInst = now; }
		render();
		if (!SHOT) { renderChecksLive(); requestAnimationFrame(frame); }
	}
	let lastCheckUI = 0;
	function renderChecksLive() { const n = performance.now(); if (n - lastCheckUI > 500) { lastCheckUI = n; renderChecks(); } }


	// ---- representative ship per hull shape (the 8 ships are 4 hulls)
	const SHAPES = [
		{ id: 'feisar', name: 'Feisar', ship: 0 }, { id: 'qirex', name: 'Qirex', ship: 1 },
		{ id: 'ag_systems', name: 'AG Systems', ship: 3 }, { id: 'auricom', name: 'Auricom', ship: 5 },
	];
	const shapeOf = (shipIndex) => {
		const si = M.ships[shipIndex];
		return SHAPES.find((sh) => { const r = M.ships[sh.ship]; return r.count === si.count && r.max[2] === si.max[2] && r.max[0] === si.max[0]; });
	};
	const toGame = (a) => { const out = new Array(a.length); for (let i = 0; i < a.length; i += 3) { out[i] = +a[i].toFixed(1); out[i + 1] = +(-a[i + 1]).toFixed(1); out[i + 2] = +(-a[i + 2]).toFixed(1); } return out; };
	const geoTris = (g) => (g.index ? g.toNonIndexed() : g).attributes.position.array;

	// ---- ?export=geometry: the hybrid's parts per hull shape, in game units
	function exportGeometry() {
		const out = { shapes: [], trim: [] };
		for (const sh of SHAPES) {
			S.ship = sh.ship; S.variation = 'H';
			build();
			const counter = {};
			const parts = built.parts.map((m) => {
				const g = m.userData.group;
				counter[g] = (counter[g] || 0) + 1;
				return { name: `${g}.${counter[g]}`, group: g, slot: m.userData.slot, tris: toGame(geoTris(m.geometry)) };
			});
			const faces = built.layout.panels.map((pnl) => {
				const f = built.panels[pnl.id].frame;
				const corner = (sx, sy) => f.c.clone().addScaledVector(f.x, sx * pnl.w / 2 * REF).addScaledVector(f.y, sy * pnl.h / 2 * REF).addScaledVector(f.n, 0.003 * REF).toArray();
				return {
					id: pnl.id, group: pnl.group, texture: pnl.tex, texel_mm: +(pnl.w * 1000 / pnl.tex[0]).toFixed(4),
					// corners: top-left, top-right, bottom-right, bottom-left (texture u right, v down)
					corners: toGame([...corner(-1, 1), ...corner(1, 1), ...corner(1, -1), ...corner(-1, -1)]),
					elements: built.layout.instruments.filter((it) => it.panel === pnl.id).map((it) => ({
						id: it.id, x: it.tx, y: it.ty, ui_scale: it.scale, size: [INSTRUMENTS[it.id].w * it.scale, INSTRUMENTS[it.id].h * it.scale],
					})),
				};
			});
			out.shapes.push({ id: sh.id, name: sh.name, models: M.ships.map((si, i) => [si, i]).filter(([, i]) => shapeOf(i) === sh).map(([si, i]) => ({ index: i, name: si.name, tris: si.count, bbox: [si.min, si.max] })), parts, faces });
		}
		for (let i = 0; i < M.ships.length; i++) out.trim.push({ ship: i, name: M.ships[i].name, shape: shapeOf(i).id, ...TRIM[i] });
		const c = CABIN;
		out.cabin_cut = { x: [-(c.halfWidth + 0.005) * REF, (c.halfWidth + 0.005) * REF], y_max: -(EYE_Y + (c.floor + 0.01) * REF), z: [c.rear * REF, c.front * REF] };
		out.trim_rule = TRIM_RULE;
		out.world_scale = S.ws;
		out.eye = [0, -EYE_Y, 0];
		out.eye_mode = S.eyeMode;
		out.fit = FIT;
		out.instruments = Object.fromEntries(Object.entries(INSTRUMENTS).map(([k, v]) => [k, { label: v.label, size_hud_px: [v.w, v.h], min_text_hud_px: v.minText }]));
		document.getElementById('export').textContent = JSON.stringify(out);
		document.title = 'READY';
	}

	// ---- ?sheet=assets: every component on its own, per-hull variants, trim, faces
	async function assetSheet() {
		document.body.classList.add('shot');
		pose();  // fills the instrument state the faces show
		const W = 2400, TW = 380, TH = 300;
		const sheet = document.createElement('canvas');
		sheet.width = W; sheet.height = 4200;
		sheet.style.cssText = 'position:absolute;left:0;top:0;z-index:5';
		document.body.appendChild(sheet);
		const ctx = sheet.getContext('2d');
		ctx.fillStyle = '#0d0f12'; ctx.fillRect(0, 0, W, sheet.height);
		const text = (str, x, y, size = 15, color = '#cfd6df', weight = '') => { ctx.font = `${weight} ${size}px Helvetica, Arial, sans-serif`; ctx.fillStyle = color; ctx.fillText(str, x, y); };
		const heading = (str, y) => { text(str, 20, y, 22, '#f0c840', 'bold'); return y + 14; };
		renderer.setPixelRatio(1);
		renderer.setSize(TW, TH, false);
		const tileScene = new THREE.Scene();
		const tileCam = new THREE.PerspectiveCamera(26, TW / TH, 1, 5000);
		const renderTile = (meshes, dir, x, y, w = TW, h = TH) => {
			renderer.setSize(w, h, false);
			while (tileScene.children.length) tileScene.remove(tileScene.children[0]);
			const bb = new THREE.Box3();
			for (const m of meshes) { const c = new THREE.Mesh(m.geometry, m.material); c.renderOrder = m.renderOrder; tileScene.add(c); bb.expandByObject(c); }
			const sph = bb.getBoundingSphere(new THREE.Sphere());
			tileCam.aspect = w / h; tileCam.updateProjectionMatrix();
			const d = sph.radius / Math.sin(deg(tileCam.fov / 2)) * 1.02;
			tileCam.position.copy(sph.center).addScaledVector(dir.clone().normalize(), d);
			tileCam.lookAt(sph.center);
			renderer.setViewport(0, 0, w, h); renderer.setScissorTest(false);
			renderer.setClearColor(0x22262c, 1); renderer.clear();
			renderer.render(tileScene, tileCam);
			ctx.drawImage(renderer.domElement, 0, 0, w, h, x, y, w, h);
			return bb;
		};
		const dims = (bb) => {
			const s3 = bb.getSize(new THREE.Vector3());
			return `${(s3.x / REF).toFixed(2)} W x ${(s3.y / REF).toFixed(2)} H x ${(s3.z / REF).toFixed(2)} L m  (${s3.x.toFixed(0)} x ${s3.y.toFixed(0)} x ${s3.z.toFixed(0)} u)`;
		};
		const groupMeshes = (g) => [...built.parts.filter((m) => g.includes(m.userData.group)), ...built.faces.filter((f) => g.includes(f.pnl.group)).map((f) => f.mesh)];
		const triCount = (ms) => ms.reduce((a, m) => a + geoTris(m.geometry).length / 9, 0);
		const sig = (ms) => ms.map((m) => Array.from(geoTris(m.geometry)).map((v) => Math.round(v * 2)).join(',')).join('|');

		// per-shape signatures, to mark what varies per hull
		const COMPONENTS = [
			{ g: ['struts'], name: 'Frame struts', note: 'A-pillars, header, top rails; 2.4 cm', dir: [-1, 0.7, -1.3] },
			{ g: ['glass'], name: 'Canopy glass', note: 'windscreen, sides, top; tint 4.5% + fresnel', dir: [-1, 0.7, -1.3] },
			{ g: ['sills'], name: 'Sills and coaming', note: 'canopy sill rails, coaming rim, trim stripe', dir: [-1, 1.2, -0.8] },
			{ g: ['tub'], name: 'Tub', note: 'walls floor-to-hull-deck, floor', dir: [-1, 1.2, -0.8] },
			{ g: ['seat'], name: 'Seat', note: 'cushion, reclined back', dir: [-1, 0.6, -1] },
			{ g: ['bulkhead'], name: 'Rear bulkhead', note: 'at 0.40 m behind the eye, trim cap', dir: [-0.8, 0.5, -1.2] },
			{ g: ['headrest'], name: 'Headrest', note: 'tower to +0.10 m, pad', dir: [-0.8, 0.4, -1.2] },
			{ g: ['hoop'], name: 'Roll hoop', note: 'posts to the strut tips, crossbar; 3 cm', dir: [-1, 0.5, 1] },
			{ g: ['dash'], name: 'Dash shell', note: 'across the cabin opening, above the knees', dir: [-0.6, 0.9, 1.4] },
			{ g: ['podL'], name: 'Left pod', note: 'bezel, lip with trim edge, face', dir: [0.35, 0.35, 1.2] },
			{ g: ['podR'], name: 'Right pod', note: 'bezel, lip with trim edge, face', dir: [-0.35, 0.35, 1.2] },
			{ g: ['weaponPod'], name: 'Weapon display', note: 'keystone on the dash centre, trim surround', dir: [-0.3, 0.3, 1.2] },
		];
		const sigs = {};
		for (const sh of SHAPES) {
			S.ship = sh.ship; S.variation = 'H'; build(); drawInstruments();
			for (const c of COMPONENTS) (sigs[c.name] = sigs[c.name] || []).push(sig(groupMeshes(c.g)));
		}
		S.ship = 0; build(); drawInstruments();
		const varies = (c) => new Set(sigs[c.name]).size > 1;

		let y = 36;
		text('WipEout VR, cockpit H (slim frame, split dash): asset sheet for approval', 20, y, 26, '#f0c840', 'bold'); y += 26;
		text(`Built at world_scale ${S.ws}: the cockpit is sized in metres for a real pilot, eye fitted at (0, ${(-EYE_Y).toFixed(1)}, 0). Ship space, metres (game units in brackets). Reference hull: Feisar. Colours are material slots; trim is sampled per ship at runtime. Mockup only, nothing is built.`, 20, y, 15, '#9aa3ad'); y += 30;
		y = heading('Components', y);
		const cols = 6, gap = 10;
		COMPONENTS.forEach((c, i) => {
			const x = 20 + (i % cols) * (TW + gap), ty = y + Math.floor(i / cols) * (TH + 92);
			const ms = groupMeshes(c.g);
			const bb = renderTile(ms, new THREE.Vector3(...c.dir), x, ty);
			text(c.name, x, ty + TH + 20, 16, '#f0c840', 'bold');
			text(dims(bb), x, ty + TH + 40, 12.5);
			const slots = [...new Set(ms.map((m) => m.userData.slot || 'face'))].join(', ');
			text(`${triCount(ms)} tris; slots: ${slots}`, x, ty + TH + 57, 12.5, '#9aa3ad');
			text(`${c.note}${varies(c) ? '; varies per hull' : '; same on all hulls'}`, x, ty + TH + 74, 12.5, varies(c) ? '#ffb070' : '#9aa3ad');
		});
		y += 2 * (TH + 92) + 20;

		// per-hull variants
		y = heading('Per-hull variants (the parts that follow the deck)', y);
		const VAR = COMPONENTS.filter(varies);
		const sw = 280, shh = 200;
		for (const sh of SHAPES) { S.ship = sh.ship; build(); drawInstruments(); sh.meshes = {}; for (const c of VAR) sh.meshes[c.name] = groupMeshes(c.g); }
		VAR.forEach((c, r) => {
			const ty = y + r * (shh + 46);
			text(c.name, 20, ty + 20, 16, '#f0c840', 'bold');
			SHAPES.forEach((sh, k) => {
				const x = 230 + k * (sw + 140);
				const bb = renderTile(sh.meshes[c.name], new THREE.Vector3(...c.dir), x, ty, sw, shh);
				text(sh.name, x, ty + shh + 18, 13.5, '#cfd6df', 'bold');
				text(dims(bb).split('  ')[0], x, ty + shh + 34, 12);
			});
		});
		y += VAR.length * (shh + 46) + 24;
		S.ship = 0; build(); drawInstruments();

		// trim
		y = heading('Trim, sampled from each ship\'s own textures (primary, secondary)', y);
		text('Rule: area-weighted texel x vertex colour x 2 samples (6 per triangle); keep saturation >= 0.35, value >= 0.20; 24 hue bins, smoothed;', 20, y + 8, 13.5, '#9aa3ad');
		text('primary = mean colour around the peak bin; secondary = strongest bin >= 45° away with >= 15% weight; both to value 0.70-0.95 (no secondary: primary at 55%; under 0.5% saturated area: neutral silver).', 20, y + 26, 13.5, '#9aa3ad');
		y += 44;
		const hullMeshFor = (i) => { S.ship = i; build(); return [built.hull]; };
		for (let i = 0; i < M.ships.length; i++) {
			const x = 20 + (i % 4) * 590, ty = y + Math.floor(i / 4) * 250;
			renderTile(hullMeshFor(i), new THREE.Vector3(-1, 0.8, -1.2), x, ty, 300, 200);
			const t = TRIM[i], si = M.ships[i];
			const hx = (c) => '#' + c.map((v) => v.toString(16).padStart(2, '0')).join('');
			text(`${TEAM_NAME[PILOT_TEAM[si.pilot]]}: ${PILOT_NAME[si.pilot]} (${si.name})`, x + 310, ty + 22, 14, '#f0c840', 'bold');
			ctx.fillStyle = hx(t.primary); ctx.fillRect(x + 310, ty + 36, 110, 60);
			ctx.fillStyle = hx(t.secondary); ctx.fillRect(x + 430, ty + 36, 110, 60);
			text(`primary ${hx(t.primary)}`, x + 310, ty + 116, 12.5); text(`hue ${t.primaryHue.toFixed(0)}°`, x + 310, ty + 132, 12.5, '#9aa3ad');
			text(`secondary ${hx(t.secondary)}`, x + 430, ty + 116, 12.5); text(t.neutral ? 'neutral' : t.secondaryHue === null ? 'fallback: primary at 55%' : `hue ${t.secondaryHue.toFixed(0)}°`, x + 430, ty + 132, 12.5, '#9aa3ad');
			text(`saturated area ${(t.saturatedShare * 100).toFixed(0)}%`, x + 310, ty + 152, 12.5, '#9aa3ad');
		}
		y += 2 * 250 + 20;
		S.ship = 0; build(); drawInstruments();

		// faces at planned resolution
		y = heading('Instrument faces at planned texture resolution (1 texel = 0.547 mm; about 20 texels per degree at the pods, the Frame shows about 16 px per degree)', y);
		y += 10;
		let x = 20;
		const faceInfo = {
			left: ['LAP n OF 3, POSITION n: UI_SIZE_8 accent + UI_SIZE_16', 'lives: UI_ICON_STAR (drfonts.cmp #9) per life', 'previous laps: UI_SIZE_8 accent, up to 2 lines', 'lap time UI_SIZE_16; LAP RECORD UI_SIZE_8', 'all at ui_scale 2'],
			right: ['speed + thrust: hud.c speedo bars', 'and the speedo.tim facia, ui_scale 3', 'status slot (generic): now the shield timer,', '"SHIELD" UI_SIZE_8 + 8 segments, ui_scale 3'],
			weapon: ['current weapon: wicons.cmp icon (32 px),', 'framed, ui_scale 3; empty frame when none'],
		};
		for (const f of built.faces) {
			const [tw, th] = f.pnl.tex;
			const c = document.createElement('canvas'); c.width = tw; c.height = th;
			composeFace(c.getContext('2d'), f.pnl, built.inst, 1, () => true);
			ctx.imageSmoothingEnabled = false;
			ctx.drawImage(c, x, y);
			ctx.strokeStyle = '#3a404a'; ctx.strokeRect(x - 0.5, y - 0.5, tw + 1, th + 1);
			text(`${f.pnl.id} face: ${tw} x ${th} texels, ${(f.pnl.w).toFixed(2)} x ${(f.pnl.h).toFixed(2)} m`, x, y + th + 22, 14, '#f0c840', 'bold');
			faceInfo[f.pnl.id].forEach((l, k) => text(l, x, y + th + 42 + k * 17, 12.5));
			// 2x inset
			x += tw + 40;
		}
		y += 292 + 150;
		text('Lives show in championship races only (3 at the start); previous laps appear from lap 2 (up to 2 lines). Both as hud.c draws them.', 20, y, 12.5, '#9aa3ad');
		y += 22;
		text('Element rectangles (texels): ' + built.inst.filter((i) => i.tx !== undefined).map((i) => `${i.id} ${i.panel} @${i.tx},${i.ty} x${i.scale}`).join('; '), 20, y, 12.5, '#9aa3ad');
		y += 30;
		const final = document.createElement('canvas'); final.width = W; final.height = y;
		final.getContext('2d').drawImage(sheet, 0, 0);
		sheet.remove();
		const img = document.createElement('img');
		img.src = final.toDataURL('image/png');
		img.style.cssText = 'position:absolute;left:0;top:0;z-index:5';
		document.body.appendChild(img);
		await new Promise((r) => { img.onload = r; });
		document.getElementById('checks').dataset.sheetHeight = y;
		document.title = 'READY';
	}

	if (Q.get('export') === 'geometry') { exportGeometry(); return; }
	if (Q.get('sheet') === 'assets') { buildUI(); await assetSheet(); return; }
	window.addEventListener('resize', resize);
	buildUI();
	build();
	resize();
	pose();
	drawInstruments();
	if (SHOT) {
		render();
		// headless Chrome does not composite the WebGL canvas into --screenshot:
		// show a copy of it as an image instead
		const img = document.createElement('img');
		img.src = canvas.toDataURL('image/png');
		img.style.cssText = 'position:absolute;left:0;top:0;width:100%;height:100%';
		canvas.parentNode.insertBefore(img, canvas.nextSibling);
		await new Promise((r) => { img.onload = r; });
		document.title = 'READY';
		const el = document.getElementById('checks');
		el.dataset.bank = bankDeg.toFixed(2);
	} else requestAnimationFrame(frame);
	window.MOCK = { S, build, runChecks, render, lapLen, latS, NS };
}

main().catch((e) => { document.title = 'ERROR'; document.body.insertAdjacentHTML('beforeend', `<pre style="color:#f66">${e.stack}</pre>`); });
