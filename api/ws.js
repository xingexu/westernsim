import { createServer } from "node:http";
import { attachRelay } from "../server/relay.js";

const server = createServer((_request, response) => {
  response.writeHead(426, { "Content-Type": "text/plain" });
  response.end("Connect with WebSocket to join a Western Sim room.");
});
attachRelay(server);

export default server;
