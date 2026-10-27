// 2D collision shapes on the ground plane: circles and rotated boxes.
export function resolve(x, z, r, colliders) {
  for (let pass = 0; pass < 2; pass++) for (const c of colliders) {
    if (c.off) continue;
    if (c.type === 'circle') {
      const dx = x - c.x, dz = z - c.z, d = Math.hypot(dx, dz), m = c.r + r;
      if (d < m && d > 1e-6) { x = c.x + dx / d * m; z = c.z + dz / d * m; }
    } else {
      const cs = Math.cos(c.rot || 0), sn = Math.sin(c.rot || 0);
      // into box space
      let lx = (x - c.x) * cs - (z - c.z) * sn, lz = (x - c.x) * sn + (z - c.z) * cs;
      const ex = c.hw + r, ez = c.hd + r;
      if (Math.abs(lx) < ex && Math.abs(lz) < ez) {
        const px = ex - Math.abs(lx), pz = ez - Math.abs(lz);
        if (px < pz) lx = Math.sign(lx || 1) * ex; else lz = Math.sign(lz || 1) * ez;
        x = c.x + lx * cs + lz * sn; z = c.z - lx * sn + lz * cs;
      }
    }
  }
  return [x, z];
}
// does the segment a->b (2D) pass through a collider? returns fraction 0..1 of the first hit, or 1
export function segmentHit(ax, az, bx, bz, colliders, pad = 0.2) {
  let best = 1;
  const steps = 16;
  for (const c of colliders) {
    if (c.off || c.noCam) continue;
    for (let i = 1; i <= steps; i++) {
      const t = i / steps, x = ax + (bx - ax) * t, z = az + (bz - az) * t;
      let inside;
      if (c.type === 'circle') inside = Math.hypot(x - c.x, z - c.z) < c.r + pad;
      else {
        const cs = Math.cos(c.rot || 0), sn = Math.sin(c.rot || 0);
        const lx = (x - c.x) * cs - (z - c.z) * sn, lz = (x - c.x) * sn + (z - c.z) * cs;
        inside = Math.abs(lx) < c.hw + pad && Math.abs(lz) < c.hd + pad;
      }
      if (inside) { best = Math.min(best, (i - 1) / steps); break; }
    }
  }
  return best;
}
