// Loads gameplan.wasm (WASI reactor) in a browser or Node with a minimal WASI shim and exposes a
// small API. The simulation is the same C++ core as the desktop/headless builds.
(function (root) {
  function makeWasi(getMemory, log) {
    const enc = new TextDecoder();
    let errBuf = "";
    const view = () => new DataView(getMemory().buffer);
    return {
      fd_write(fd, iovs, iovsLen, nwritten) {
        const v = view(); let total = 0; let s = "";
        for (let i = 0; i < iovsLen; i++) {
          const p = v.getUint32(iovs + i * 8, true), l = v.getUint32(iovs + i * 8 + 4, true);
          s += enc.decode(new Uint8Array(getMemory().buffer, p, l)); total += l;
        }
        errBuf += s;
        let k; while ((k = errBuf.indexOf("\n")) >= 0) { log(errBuf.slice(0, k)); errBuf = errBuf.slice(k + 1); }
        v.setUint32(nwritten, total, true); return 0;
      },
      fd_close() { return 0; }, fd_seek() { return 70; }, fd_read() { return 8; },
      fd_fdstat_get() { return 8; }, fd_fdstat_set_flags() { return 0; }, fd_prestat_get() { return 8; }, fd_prestat_dir_name() { return 8; },
      path_open() { return 44; }, fd_filestat_get() { return 8; },
      environ_get() { return 0; }, environ_sizes_get(c, s) { const v = view(); v.setUint32(c, 0, true); v.setUint32(s, 0, true); return 0; },
      args_get() { return 0; }, args_sizes_get(c, s) { const v = view(); v.setUint32(c, 0, true); v.setUint32(s, 0, true); return 0; },
      clock_time_get(id, prec, out) { view().setBigUint64(out, 0n, true); return 0; },   // simulation never reads wall time
      random_get(buf, len) { new Uint8Array(getMemory().buffer, buf, len).fill(7); return 0; }, // deterministic; core uses its own RNG
      proc_exit(code) { throw new Error("gameplan exited " + code); },
      sched_yield() { return 0; }, poll_oneoff() { return 52; },
    };
  }
  async function create(wasmBytes, log = console.log) {
    let memory = null;
    const wasi = makeWasi(() => memory, log);
    const imports = { wasi_snapshot_preview1: new Proxy(wasi, { get: (t, k) => t[k] || (() => 52) }) };
    const { instance } = await WebAssembly.instantiate(wasmBytes, imports);
    const x = instance.exports;
    memory = x.memory;
    if (x._initialize) x._initialize();
    const enc = new TextEncoder(), dec = new TextDecoder();
    const str = (s) => { const b = enc.encode(s); const p = x.gp_alloc(b.length + 1); new Uint8Array(memory.buffer, p, b.length + 1).set([...b, 0]); return p; };
    const cstr = (p) => { const m = new Uint8Array(memory.buffer); let e = p; while (m[e]) e++; return dec.decode(m.subarray(p, e)); };
    return {
      init(season, comps, userRobot, aiRobot, settings) {
        const a = [season, comps, userRobot, aiRobot, JSON.stringify(settings)].map(str);
        const n = x.gp_init(...a); a.forEach((p) => x.gp_free(p)); return n;
      },
      setPad(p) { x.gp_set_pad(p.lx, p.ly, p.rx, p.ry, p.lt, p.rt, p.buttons); },
      step(dt) { return x.gp_step(dt) === 1; },
      frame() { const p = x.gp_frame(); return new Float32Array(memory.buffer, p, x.gp_frame_len()).slice(); },
      elec() { const p = x.gp_elec_sample(); return new Float32Array(memory.buffer, p, x.gp_frame_len()).slice(); },
      hud() { return JSON.parse(cstr(x.gp_hud())); },
      robotInfo(i) { return JSON.parse(cstr(x.gp_robot_info(i))); },
      result() { return JSON.parse(cstr(x.gp_result())); },
      parseInstruction(t) { const p = str(t); const r = JSON.parse(cstr(x.gp_parse_instruction(p))); x.gp_free(p); return r; },
    };
  }
  root.GamePlanWasm = { create };
  if (typeof module !== "undefined") module.exports = { create };
})(typeof window !== "undefined" ? window : globalThis);
