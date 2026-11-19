import assert from "node:assert/strict";
import { once } from "node:events";
import { createServer } from "node:http";
import { test } from "node:test";
import WebSocket from "ws";
import { attachRelay } from "../server/relay.js";

const code = "c53f4d5a6b7c";
const otherCode = "000000000001";
const waitForMessage = async (socket) => {
  const [raw] = await once(socket, "message", { signal: AbortSignal.timeout(3000) });
  return JSON.parse(raw.toString());
};

test("relay pairs exactly two peers, isolates rooms, and releases disconnects", { timeout: 8000 }, async (t) => {
  const server = createServer();
  const relay = attachRelay(server, "", { maxConnections: 4 });
  await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
  t.after(() => {
    for (const peer of relay.clients) peer.terminate();
    relay.close();
    server.close();
  });
  const host = `127.0.0.1:${server.address().port}`;
  const open = (room, origin = `http://${host}`) =>
    new WebSocket(`ws://${host}/api/ws?room=${room}`, { origin });

  const badOrigin = open(code,"https://elsewhere.example");
  const [error] = await once(badOrigin,"error");
  assert.match(error.message,/403/);
  const one = open(code);
  assert.deepEqual(await waitForMessage(one),{ type: "joined" });
  const two = open(code);
  assert.deepEqual(await waitForMessage(two),{ type: "joined" });
  const isolated = open(otherCode);
  assert.deepEqual(await waitForMessage(isolated),{ type: "joined" });
  let isolatedPackets = 0;
  isolated.on("message", () => isolatedPackets++);
  const extra = open(code);
  assert.equal((await once(extra,"close"))[0],4003);
  const fourth = open("000000000002");
  assert.deepEqual(await waitForMessage(fourth),{ type: "joined" });
  const busy = open("000000000003");
  assert.match((await once(busy,"error"))[0].message,/503/);

  const fromOne = waitForMessage(two);
  one.send(JSON.stringify([480,12,320,1.5,4,.5]));
  const pose = await fromOne;
  assert.equal(pose.type,"pose");
  assert.deepEqual(pose.pose,[480,12,320,1.5,4,.5]);
  const fromTwo = waitForMessage(one);
  two.send(JSON.stringify([482,12,321,3,10,1]));
  assert.deepEqual((await fromTwo).pose,[482,12,321,3,10,1]);
  assert.equal(isolatedPackets,0);

  const disconnect = waitForMessage(two);
  one.close();
  assert.deepEqual(await disconnect,{ type: "left", id: pose.id });
  const replacement = open(code);
  assert.deepEqual(await waitForMessage(replacement),{ type: "joined" });
});
