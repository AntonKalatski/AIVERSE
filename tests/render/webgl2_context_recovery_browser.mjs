#!/usr/bin/env node
// Real browser WebGL2 context-loss acceptance for an AIVERSE Emscripten build.
// Requires: npm install --no-save playwright, Playwright Chromium installation.
// Usage: node tests/render/webgl2_context_recovery_browser.mjs http://localhost:8000/index.html
import { chromium } from "playwright";

const url = process.argv[2];
if (!url || !/^https?:\/\//.test(url)) {
  console.error("Usage: node tests/render/webgl2_context_recovery_browser.mjs http://host/AIVERSE.html");
  process.exit(2);
}
const verifyVT = process.argv.includes("--vt");
// --vt requires the standalone AIVERSE production VirtualTextureStreamer app.
const assert = (condition, message) => {
  if (!condition) throw new Error("FAIL: " + message);
};
const browser = await chromium.launch({ headless: true, args: ["--enable-webgl", "--use-gl=angle", "--use-angle=swiftshader"] });
try {
  const page = await browser.newPage();
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.goto(url, { waitUntil: "domcontentloaded" });
  const metric = key => page.evaluate(name => {
    const m = globalThis.Module;
    const f = m && m["_aiverse_web_test_" + name];
    return typeof f === "function" ? f() : null;
  }, key);
  await page.waitForFunction(() => {
    const m = globalThis.Module;
    return m && typeof m._aiverse_web_test_generation === "function"
      && m._aiverse_web_test_generation() === 1
      && m._aiverse_web_test_frames() > 2;
  }, null, { timeout: 30000 });
  const vt = key => page.evaluate(name => {
    const f = globalThis.Module?.["_aiverse_web_vt_" + name];
    return typeof f === "function" ? f() : null;
  }, key);
  if (verifyVT) {
    await page.waitForFunction(() => {
      const m = globalThis.Module;
      return m && typeof m._aiverse_web_vt_completed === "function"
        && m._aiverse_web_vt_completed() === 1
        && m._aiverse_web_vt_failures() === 0;
    }, null, { timeout: 30000 });
    assert((await vt("stage")) === 6 && (await vt("gpu_reads")) === 3 &&
      (await vt("shader_verified")) === 1 &&
      (await vt("seam_verified")) === 1,
      "production VT completes physical atlas and table GPU readback");
  }
  const initialFrames = await metric("frames");
  assert(initialFrames > 2, "AIVERSE rendering started");

  // Browser-only low-level pixel oracle. The lifecycle counters above belong to
  // the real AIVERSE platform. This separate oracle checks restored WebGL2
  // array/mip upload and framebuffer readback; it does NOT claim VT integration.
  const checkTexture = () => page.evaluate(() => {
    const canvas = globalThis.Module?.canvas || document.querySelector("canvas");
    const gl = canvas?.getContext("webgl2");
    if (!gl || gl.isContextLost()) return false;
    const oldActive = gl.getParameter(gl.ACTIVE_TEXTURE);
    const oldFramebuffer = gl.getParameter(gl.FRAMEBUFFER_BINDING);
    const oldUnpack = gl.getParameter(gl.UNPACK_ALIGNMENT);
    const oldPack = gl.getParameter(gl.PACK_ALIGNMENT);
    gl.activeTexture(gl.TEXTURE0);
    const oldTexture = gl.getParameter(gl.TEXTURE_BINDING_2D_ARRAY);
    const texture = gl.createTexture();
    const framebuffer = gl.createFramebuffer();
    if (!texture || !framebuffer) return false;
    let ok = true;
    try {
      gl.bindTexture(gl.TEXTURE_2D_ARRAY, texture);
      gl.texStorage3D(gl.TEXTURE_2D_ARRAY, 2, gl.RGBA8, 4, 4, 2);
      gl.texParameteri(gl.TEXTURE_2D_ARRAY, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
      gl.pixelStorei(gl.PACK_ALIGNMENT, 1);
      for (let mip = 0; mip < 2; ++mip) for (let layer = 0; layer < 2; ++layer) {
        const side = 4 >> mip;
        const color = [41 + mip * 70 + layer * 25, 65 + layer * 50, 110 + mip * 60, 255];
        const data = new Uint8Array(side * side * 4);
        for (let i = 0; i < data.length; i += 4) data.set(color, i);
        gl.texSubImage3D(gl.TEXTURE_2D_ARRAY, mip, 0, 0, layer,
                         side, side, 1, gl.RGBA, gl.UNSIGNED_BYTE, data);
        gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
        gl.framebufferTextureLayer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0,
                                   texture, mip, layer);
        if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE)
          ok = false;
        const pixels = new Uint8Array(data.length);
        gl.readPixels(0, 0, side, side, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
        if (!pixels.every((value, index) => value === color[index % 4])) ok = false;
      }
      if (gl.getError() !== gl.NO_ERROR) ok = false;
    } finally {
      gl.bindFramebuffer(gl.FRAMEBUFFER, oldFramebuffer);
      gl.bindTexture(gl.TEXTURE_2D_ARRAY, oldTexture);
      gl.activeTexture(oldActive);
      gl.pixelStorei(gl.UNPACK_ALIGNMENT, oldUnpack);
      gl.pixelStorei(gl.PACK_ALIGNMENT, oldPack);
      gl.deleteFramebuffer(framebuffer);
      gl.deleteTexture(texture);
    }
    return ok;
  });
  assert(await checkTexture(), "initial WebGL2 array mip/layer GPU pixels");

  for (let cycle = 1; cycle <= 2; ++cycle) {
    await page.evaluate(() => {
      const canvas = globalThis.Module?.canvas || document.querySelector("canvas");
      const gl = canvas.getContext("webgl2");
      const ext = gl.getExtension("WEBGL_lose_context");
      if (!ext) throw new Error("FAIL: WEBGL_lose_context extension unavailable");
      globalThis.__aiverseLoseContextExt = ext;
      ext.loseContext();
    });
    await page.waitForFunction(expected => {
      const m = globalThis.Module;
      return m && m._aiverse_web_test_losses() === expected &&
             m._aiverse_web_test_suspended() === 1;
    }, cycle, { timeout: 10000 });
    // One rAF interval may have been queued before the loss notification.
    await page.waitForTimeout(100);
    const stopped = await metric("frames");
    await page.waitForTimeout(250);
    assert((await metric("frames")) === stopped, "frame count frozen while context lost");
    await page.evaluate(() => globalThis.__aiverseLoseContextExt.restoreContext());
    await page.waitForFunction(expected => {
      const m = globalThis.Module;
      return m && m._aiverse_web_test_generation() === expected + 1 &&
             m._aiverse_web_test_suspended() === 0;
    }, cycle, { timeout: 15000 });
    const before = await metric("frames");
    await page.waitForFunction(previous => globalThis.Module._aiverse_web_test_frames() > previous + 2,
                               before, { timeout: 10000 });
    if (verifyVT) {
      await page.waitForFunction(expected => {
        const m = globalThis.Module;
        return m && m._aiverse_web_vt_generation() === expected + 1
          && m._aiverse_web_vt_completed() === expected + 1
          && m._aiverse_web_vt_shutdowns() >= expected
          && m._aiverse_web_vt_stage() === 6 && m._aiverse_web_vt_gpu_reads() === 3 &&
          m._aiverse_web_vt_shader_verified() === 1 &&
          m._aiverse_web_vt_seam_verified() === 1
          && m._aiverse_web_vt_failures() === 0;
      }, cycle, { timeout: 30000 });
      assert((await vt("failures")) === 0,
        "production VT no streaming failures after restore cycle " + cycle);
    }
    assert(await checkTexture(), "WebGL2 mip/layer readback after restore cycle " + cycle);
    console.log("PASS: WebGL2 restore cycle " + cycle + ", generation=" + await metric("generation"));
  }
  assert(errors.length === 0, "no page errors: " + errors.join("; "));
  console.log("PASS: AIVERSE WebGL2 context-loss suspension, device regeneration, browser texture-array readback");
  if (verifyVT) console.log("PASS: production VT atlas/table, trilinear and Repeat/MirrorRepeat nonuniform seam GPU parity after two context restores");
} finally {
  await browser.close();
}
