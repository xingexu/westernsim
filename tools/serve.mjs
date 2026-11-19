import { createReadStream } from "node:fs";
import { createServer } from "node:http";
import { attachRelay } from "../server/relay.js";

const files = new Map([
  ["/index.html", "text/html"], ["/play.css", "text/css"], ["/play.js", "text/javascript"],
  ["/multiplayer.js", "text/javascript"], ["/westernsim.js", "text/javascript"],
  ["/westernsim.wasm", "application/wasm"], ["/silkscreen.woff2", "font/woff2"], ["/campus.webp", "image/webp"],
]);
const host = process.env.HOST ?? "127.0.0.1";
const port = Number(process.env.PORT ?? 8080);
const server = createServer((request, response) => {
  const pathname = new URL(request.url, "http://localhost").pathname;
  const path = pathname === "/" ? "/index.html" : pathname;
  const type = files.get(path);
  if (!type || (request.method !== "GET" && request.method !== "HEAD")) {
    response.writeHead(404).end();
    return;
  }
  const stream = createReadStream(new URL(`../dist${path}`, import.meta.url));
  stream.once("error", () => response.writeHead(404).end());
  stream.once("open", () => {
    response.writeHead(200, { "Content-Type": type });
    if (request.method === "HEAD") {
      stream.destroy();
      response.end();
    } else stream.pipe(response);
  });
});
attachRelay(server);
server.listen(port, host, () => console.log(`Western Sim on http://${host}:${server.address().port}`));
