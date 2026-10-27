// Motion-captured locomotion (idle, walk, run) retargeted onto the sculpted rigs.
// The source is a Mixamo skeleton (three.js example "Xbot"). Torso and feet copy its
// rotations relative to the rest pose; arms and legs copy the direction of each limb
// segment, which works even though our rest pose has the arms down and Mixamo's is a T-pose.
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { clone } from 'three/addons/utils/SkeletonUtils.js';

let src = null;
export const mocapReady = new GLTFLoader().loadAsync('assets/anim/locomotion.glb')
  .then(g => { src = g; return g; })
  .catch(e => { console.warn('no mocap', e.message); return null; });
export const hasMocap = () => !!src;

// metres travelled per loop of each clip, for a character with ~1 m hips
const CYCLE = { walk: 1.45, run: 2.9 };
const WALK = 1.3, RUN = 3.4;

const find = (root, n) => root.getObjectByName('mixamorig' + n) || root.getObjectByName('mixamorig:' + n);
const _q = new THREE.Quaternion(), _q2 = new THREE.Quaternion(), _v = new THREE.Vector3(), _v2 = new THREE.Vector3(), _s = new THREE.Vector3(), _p = new THREE.Vector3();
// read from matrices already updated this frame (getWorldPosition/Quaternion would recompute every ancestor)
const wpos = (o, out) => out.setFromMatrixPosition(o.matrixWorld);
const wquat = (o, out) => { o.matrixWorld.decompose(_p, out, _s); return out; };

export class Mocap {
  constructor(h) {
    this.h = h;
    const rig = this.rig = clone(src.scene);
    rig.updateMatrixWorld(true);
    const B = (n) => find(rig, n);
    this.s = {
      hips: B('Hips'), spine: B('Spine'), chest: B('Spine2'), neck: B('Neck'), head: B('Head'),
      L: { up: B('LeftArm'), fo: B('LeftForeArm'), ha: B('LeftHand'), th: B('LeftUpLeg'), sn: B('LeftLeg'), ft: B('LeftFoot'), toe: B('LeftToeBase') },
      R: { up: B('RightArm'), fo: B('RightForeArm'), ha: B('RightHand'), th: B('RightUpLeg'), sn: B('RightLeg'), ft: B('RightFoot'), toe: B('RightToeBase') },
    };
    // rest pose, in rig space
    this.rest = new Map();
    rig.traverse(o => { if (o.isBone || o.type === 'Object3D' || o.isObject3D) this.rest.set(o, o.getWorldQuaternion(new THREE.Quaternion())); });
    this.restHipY = this.s.hips.getWorldPosition(new THREE.Vector3()).y;
    this.myHipY = h.hips.position.y;
    this.mixer = new THREE.AnimationMixer(rig);
    this.act = {};
    for (const c of src.animations) {
      const a = this.mixer.clipAction(c); a.play(); a.setEffectiveWeight(0);
      this.act[c.name] = a;
    }
    this.phase = Math.random();
    this.idleT = Math.random() * 5;
  }

  update(dt, speed) {
    const h = this.h, s = this.s, act = this.act;
    // blend weights by speed, with walk and run kept in step
    const sp = Math.max(0, speed);
    const wMove = THREE.MathUtils.smoothstep(sp, 0.05, 0.6);
    const k = THREE.MathUtils.clamp((sp - WALK) / (RUN - WALK), 0, 1);
    const cyc = THREE.MathUtils.lerp(CYCLE.walk, CYCLE.run, k) * (h.legLen / 0.46);
    this.phase = (this.phase + dt * Math.max(sp, 0.3) / cyc) % 1;
    this.idleT += dt;
    act.idle.setEffectiveWeight(1 - wMove); act.idle.time = this.idleT % act.idle.getClip().duration;
    act.walk.setEffectiveWeight(wMove * (1 - k)); act.walk.time = this.phase * act.walk.getClip().duration;
    act.run.setEffectiveWeight(wMove * k); act.run.time = this.phase * act.run.getClip().duration;
    this.mixer.update(0);
    this.rig.updateMatrixWorld(true);

    // torso: copy the change from rest, in rig space, down the chain
    const parentQ = new THREE.Quaternion(); // world rotation of the bone we're about to set's parent (relative to body)
    const setDelta = (mine, theirs, pq) => {
      const d = wquat(theirs, _q).multiply(_q2.copy(this.rest.get(theirs)).invert());
      mine.quaternion.copy(pq).invert().multiply(d);
      return d.clone();
    };
    const hipsQ = setDelta(h.hips, s.hips, parentQ);
    const spineQ = setDelta(h.spine, s.spine, hipsQ);
    const chestQ = setDelta(h.chest, s.chest, spineQ);
    const neckQ = setDelta(h.neck, s.neck, chestQ);
    setDelta(h.head, s.head, neckQ);
    h.hips.position.y = this.myHipY * (wpos(s.hips, _v).y / this.restHipY);

    // limbs: point each segment the way the source's segment points
    const aim = (mine, a, b, pq) => {
      const dir = wpos(b, _v2).sub(wpos(a, _v)).normalize();
      const local = dir.applyQuaternion(_q.copy(pq).invert());
      const rest = mine.userData.restDir || (mine.userData.restDir = mine.children.find(c => c.isBone)?.position.clone().normalize() || new THREE.Vector3(0, -1, 0));
      mine.quaternion.setFromUnitVectors(rest, local);
      return pq.clone().multiply(mine.quaternion);
    };
    for (const side of ['L', 'R']) {
      const my = h[side], th = s[side];
      my.sh.quaternion.identity();
      const shQ = chestQ.clone();
      const upQ = aim(my.up, th.up, th.fo, shQ);
      aim(my.fo, th.fo, th.ha, upQ);
      my.ha.quaternion.identity();
      const thQ = aim(my.th, th.th, th.sn, hipsQ);
      const snQ = aim(my.sn, th.sn, th.ft, thQ);
      setDelta(my.ft, th.ft, snQ);
    }
  }
}
