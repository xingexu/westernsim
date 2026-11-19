const codePattern = /^[a-f0-9]{12}$/;

export function multiplayer(canvas) {
  const panel = document.querySelector("#multiplayer");
  const form = document.querySelector("#join-room");
  const field = document.querySelector("#room-code");
  const create = document.querySelector("#create-room");
  const leave = document.querySelector("#leave-room");
  const copy = document.querySelector("#copy-invite");
  const status = document.querySelector("#room-status");
  const indicator = document.querySelector("#room-indicator");
  let room = new URL(location.href).searchParams.get("room") ?? "";
  let socket, peer, localPose, retryTimer, sendTimer;
  let ready = false, joined = false, retries = 0, peerSeenAt = 0;

  function setStatus(message) {
    status.textContent = message;
    indicator.textContent = message;
    indicator.hidden = !room || document.pointerLockElement !== canvas;
  }

  function clearPeer() {
    peer = undefined;
    peerSeenAt = 0;
    window.Module?._westernsim_remote_clear?.();
  }

  function sendPose() {
    if (peer && performance.now() - peerSeenAt > 2500) {
      clearPeer();
      setStatus("Waiting for your friend");
    }
    if (localPose && socket?.readyState === WebSocket.OPEN && socket.bufferedAmount < 4096) {
      socket.send(JSON.stringify(localPose));
    }
  }

  function connect() {
    retryTimer = undefined;
    if (!ready || !room) return;
    const url = new URL("/api/ws", location.href);
    url.protocol = location.protocol === "https:" ? "wss:" : "ws:";
    url.searchParams.set("room", room);
    const connection = new WebSocket(url);
    socket = connection;
    setStatus(retries ? "Reconnecting to room…" : "Joining room…");
    connection.addEventListener("message", ({ data }) => {
      if (socket !== connection) return;
      const message = JSON.parse(data);
      if (message.type === "joined") {
        joined = true;
        retries = 0;
        setStatus("Waiting for your friend");
        clearInterval(sendTimer);
        sendTimer = setInterval(sendPose, 50);
      } else if (message.type === "pose" && joined) {
        if (peer !== message.id) {
          clearPeer();
          peer = message.id;
          setStatus("2 players connected");
        }
        peerSeenAt = performance.now();
        window.Module._westernsim_remote(...message.pose);
      } else if (message.type === "left" && peer === message.id) {
        clearPeer();
        setStatus("Waiting for your friend");
      }
    });
    connection.addEventListener("close", ({ code }) => {
      if (socket !== connection) return;
      socket = undefined;
      joined = false;
      clearInterval(sendTimer);
      clearPeer();
      if (code === 4003) {
        setStatus("Room is full. Join again or create a room.");
        return;
      }
      setStatus("Relay disconnected. Retrying…");
      retryTimer = setTimeout(connect, Math.min(1000 * 2 ** retries++, 15000));
    });
  }

  function selectRoom(code) {
    room = code;
    clearTimeout(retryTimer);
    clearInterval(sendTimer);
    const previous = socket;
    socket = undefined;
    previous?.close();
    clearPeer();
    joined = false;
    retries = 0;
    const url = new URL(location.href);
    if (room) url.searchParams.set("room", room);
    else url.searchParams.delete("room");
    history.replaceState(null, "", url);
    field.value = room;
    copy.hidden = leave.hidden = !room;
    if (room) connect();
    else setStatus("");
  }

  form.addEventListener("submit", (event) => {
    event.preventDefault();
    const code = field.value.trim().toLowerCase();
    if (codePattern.test(code)) selectRoom(code);
    else status.textContent = "Enter a 12-character room code.";
  });
  create.addEventListener("click", () => {
    const bytes = crypto.getRandomValues(new Uint8Array(6));
    selectRoom(Array.from(bytes, byte => byte.toString(16).padStart(2, "0")).join(""));
  });
  copy.addEventListener("click", async () => {
    try {
      await navigator.clipboard.writeText(location.href);
      copy.textContent = "Copied";
      setTimeout(() => { copy.textContent = "Copy invite"; }, 1800);
    } catch {
      field.select();
      status.textContent = "Copy the room code to invite a friend.";
    }
  });
  leave.addEventListener("click", () => selectRoom(""));
  document.addEventListener("pointerlockchange", () => {
    indicator.hidden = !room || document.pointerLockElement !== canvas;
  });
  window.addEventListener("pagehide", () => {
    clearTimeout(retryTimer);
    clearInterval(sendTimer);
    const previous = socket;
    socket = undefined;
    previous?.close();
    joined = false;
    clearPeer();
  });
  window.addEventListener("pageshow", ({ persisted }) => {
    if (persisted && ready && room && !socket) connect();
  });

  return {
    ready() {
      ready = true;
      panel.hidden = false;
      if (codePattern.test(room)) {
        panel.open = true;
        selectRoom(room);
      } else {
        room = "";
      }
    },
    send(...pose) { localPose = pose; },
  };
}
