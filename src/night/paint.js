// Unified photorealistic PBR lighting & atmospheric helpers for the Snowbound alpine prologue.
// Smooth inverse-square lantern/cabin warmth (zero banded cel steps), linear-HDR snow & basalt,
// ethereal aurora sky bounce, and starry polar night sky.
export const NIGHT = /* glsl */ `
uniform vec3 uGlow; // x: aurora intensity (about 0.6..1.3)
uniform vec4 uWarm[3]; // cabin lit openings: xyz position, w strength (+ south, - north)
uniform vec4 uRoom; // cabin floor center (x, z) and half width/depth

bool inRoom(vec2 xz) { return abs(xz.x - uRoom.x) < uRoom.z && abs(xz.y - uRoom.y) < uRoom.w; }

float warmAt(vec3 P, vec3 Nn) {
  float sum = 0.0;
  for (int i = 0; i < 3; i++) {
    vec3 to = uWarm[i].xyz - P;
    float front = sign(uWarm[i].w) * (P.z - uWarm[i].z);
    float d2 = dot(to, to);
    float wedge = smoothstep(0.0, 0.9, front) * smoothstep(0.32, 1.35, front / (abs(to.x) + 0.45));
    sum += abs(uWarm[i].w) * wedge * clamp(dot(Nn, to) * inversesqrt(d2 + 1e-4) * 0.7 + 0.35, 0.0, 1.0) / (0.45 + d2 * 0.22);
  }
  return sum;
}

float moonOn(vec3 P, vec3 Nn) {
  return woodSunAt(P + Nn * 0.5 + vec3(0.0, 0.25, 0.0)) * clamp(dot(Nn, uSunDir) * 1.15 + 0.08, 0.0, 1.0);
}

float auroraOn(vec2 xz, vec3 Nn) {
  float patch_ = texture(tNoise, xz / 900.0 + vec2(uTime * 0.009, -uTime * 0.0055)).r;
  return clamp(uGlow.x * smoothstep(0.18, 0.76, patch_) * max(Nn.y, 0.0), 0.0, 0.9);
}

// Smooth linear-HDR snow shading (moonlight + aurora mint bounce + icy blue shadow bounce).
vec3 snowPaint(float x, float r, float a, float kind) {
  vec3 nightL = mix(vec3(0.18, 0.28, 0.46), vec3(0.12, 0.38, 0.36), a * 0.65);
  vec3 nightS = mix(vec3(0.015, 0.032, 0.075), vec3(0.012, 0.058, 0.072), a * 0.65);
  vec3 nightCol = mix(nightS, nightL, smoothstep(0.0, 1.0, x));
  vec3 dayCol = mix(vec3(0.28, 0.40, 0.62), vec3(0.92, 0.95, 1.0), smoothstep(0.0, 1.0, x));
  return mix(dayCol, nightCol, uNight);
}

// Smooth linear-HDR alpine basalt / dark slate rock shading.
vec3 rockPaint(float x, float r, float kind) {
  vec3 nightCol = mix(vec3(0.008, 0.015, 0.034), vec3(0.075, 0.11, 0.17), smoothstep(0.0, 1.0, x));
  vec3 dayCol = mix(vec3(0.05, 0.065, 0.095), vec3(0.28, 0.30, 0.34), smoothstep(0.0, 1.0, x));
  return mix(dayCol, nightCol, uNight);
}

// Smooth physically-based warm firelight / lantern falloff (no cartoon bands!).
vec3 lampOn(vec3 c, vec3 own, vec3 P, vec3 Nn) {
  float k = (lampAt(P, Nn) + warmAt(P, Nn)) * 1.45;
  vec3 warm = toLinear(own) * vec3(2.85, 1.48, 0.55);
  return c + warm * k;
}

vec3 nightSky(vec3 v) {
  if (v.y < 0.0) return airColour(v);
  float up = pow(max(clamp(v.y, 0.0, 1.0), 1e-4), 0.52);
  vec3 c = mix(vec3(0.012, 0.028, 0.056), vec3(0.003, 0.007, 0.020), up);
  float north = max(-v.z, 0.0) / max(length(v.xz), 1e-4);
  float ng = (v.y - 0.24) / 0.32;
  c += vec3(0.008, 0.068, 0.052) * north * north * exp(-ng * ng) * uGlow.x;
  float moon = max(dot(v, uSunDir), 1e-4);
  c += vec3(0.08, 0.12, 0.18) * pow(moon, 24.0) + vec3(0.32, 0.42, 0.55) * pow(moon, 220.0);
  vec3 nightCol = mix(airColour(v), c, smoothstep(0.0, 0.06, v.y));
  vec3 dayCol = mix(vec3(0.56, 0.72, 0.90), vec3(0.12, 0.34, 0.76), up);
  dayCol += vec3(1.1, 0.95, 0.74) * pow(moon, 16.0) * 0.28 + vec3(4.5, 4.2, 3.6) * smoothstep(0.9982, 0.9993, moon);
  dayCol += vec3(1.1, 0.48, 0.20) * uTwilight * pow(moon, 3.0) * (1.0 - smoothstep(0.0, 0.45, v.y)) * 0.65;
  dayCol = mix(airColour(v), dayCol, smoothstep(0.0, 0.06, v.y));
  return mix(dayCol, nightCol, uNight);
}

vec3 auroraSoft(vec3 v) {
  float az = atan(v.x, -v.z);
  float bt = (v.y - 0.30 - 0.05 * sin(az * 3.0 + uTime * 0.04)) / 0.14;
  float lt = (v.y - 0.17) / 0.06;
  float band = exp(-bt * bt);
  float low = 0.65 * exp(-lt * lt);
  vec3 emerald = vec3(0.04, 0.68, 0.44);
  vec3 violet = vec3(0.42, 0.14, 0.78);
  vec3 col = mix(emerald, violet, smoothstep(0.26, 0.52, v.y));
  return col * (band + low) * smoothstep(1.75, 0.35, abs(az)) * uGlow.x * smoothstep(0.08, 0.52, uNight);
}
`;
