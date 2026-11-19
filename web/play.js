(() => {
  const canvas = document.querySelector("#canvas");
  const launcher = document.querySelector("#launcher");
  const play = document.querySelector("#play");
  const status = document.querySelector("#status");
  let ready = false;
  let entered = false;
  let broken = false;
  let storage = false;
  let saving = false;
  let changed = false;

  function fail(message) {
    if (broken) return;
    broken = true;
    launcher.hidden = false;
    if (document.pointerLockElement === canvas) document.exitPointerLock();
    status.textContent = message;
    play.textContent = "Reload";
    play.disabled = false;
  }

  function flushSave() {
    if (!storage) {
      Module._westernsim_storage_result(0);
      return;
    }
    changed = true;
    if (saving) return;
    saving = true;
    changed = false;
    Module.FS.syncfs(false, (error) => {
      saving = false;
      if (error) storage = false;
      if (changed && storage) flushSave();
      else Module._westernsim_storage_result(Number(storage));
    });
  }

  function prepareStorage() {
    const { FS, addRunDependency, removeRunDependency } = Module;
    FS.mkdir("/saves");
    addRunDependency("campus-saves");
    try {
      FS.mount(FS.filesystems.IDBFS, {}, "/saves");
      FS.syncfs(true, (error) => {
        storage = !error;
        removeRunDependency("campus-saves");
      });
    } catch {
      removeRunDependency("campus-saves");
    }
  }

  window.Module = {
    canvas,
    preRun: [prepareStorage],
    westernSimFlush: flushSave,
    westernSimFailed: fail,
    westernSimReady() {
      if (broken) return;
      ready = true;
      launcher.classList.add("ready");
      status.textContent = storage ? "Explore campus with a keyboard and mouse." :
        "Explore with a keyboard and mouse. Browser storage is unavailable.";
      play.disabled = false;
    },
    onAbort() { fail("The game could not start. Reload to try again."); },
  };

  play.addEventListener("click", () => {
    if (broken) return window.location.reload();
    if (!ready) return;
    canvas.focus();
    Module._westernsim_capture();
  });
  document.addEventListener("pointerlockchange", () => {
    if (broken || !ready) return;
    const locked = document.pointerLockElement === canvas;
    launcher.hidden = locked;
    if (locked) entered = true;
    else if (entered) {
      play.textContent = "Resume";
      status.textContent = "Click Resume to capture the mouse again.";
    }
  });
  document.addEventListener("pointerlockerror", () => {
    if (ready && !broken) status.textContent = "Mouse capture was blocked. Try Play again in a desktop browser tab.";
  });
  // Use each Pointer Lock delta. The GLFW web port's position callback drops repeated deltas.
  document.addEventListener("mousemove", (event) => {
    if (ready && !broken && document.pointerLockElement === canvas) {
      Module._westernsim_look(event.movementX, event.movementY);
    }
  }, true);
  canvas.addEventListener("contextmenu", (event) => event.preventDefault());
  canvas.addEventListener("webglcontextlost", () => fail("The browser lost its graphics context. Reload to resume."));
  window.addEventListener("error", (event) => {
    if (event.filename?.endsWith("/westernsim.js")) fail("The game stopped unexpectedly. Reload to try again.");
  });

  if (!window.WebGL2RenderingContext || !canvas.requestPointerLock) {
    status.textContent = "Open Western Sim in a desktop browser with WebGL 2 and mouse capture.";
    return;
  }
  const runtime = document.createElement("script");
  runtime.src = "./westernsim.js";
  runtime.onerror = () => fail("The game files could not load. Reload to try again.");
  document.body.append(runtime);
})();
