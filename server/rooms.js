import { createClient } from "@redis/client";

const leaseMs = 20_000;
const reserveRoom = `
local now = tonumber(ARGV[2])
redis.call('ZREMRANGEBYSCORE', KEYS[1], '-inf', now)
if redis.call('ZCARD', KEYS[1]) >= 2 then return 0 end
redis.call('ZADD', KEYS[1], now + tonumber(ARGV[3]), ARGV[1])
redis.call('PEXPIRE', KEYS[1], ARGV[3])
return 1
`;

export async function roomStore(redisUrl, deliver) {
  if (!redisUrl) {
    const members = new Map();
    return {
      reserve(room, id) {
        const roomMembers = members.get(room) ?? new Set();
        if (roomMembers.size >= 2) return false;
        roomMembers.add(id);
        members.set(room, roomMembers);
        return true;
      },
      refresh() { return true; },
      release(room, id) {
        const roomMembers = members.get(room);
        roomMembers?.delete(id);
        if (!roomMembers?.size) members.delete(room);
      },
      publish: deliver,
    };
  }

  const scope = `westernsim:v1:${process.env.VERCEL_ENV || "local"}`;
  const channel = `${scope}:events`;
  const key = (room) => `${scope}:room:${room}`;
  const client = createClient({
    url: redisUrl,
    disableOfflineQueue: true,
    socket: { connectTimeout: 3000, reconnectStrategy: (retries) => retries < 4 ? 250 * (retries + 1) : false },
  });
  const subscriber = client.duplicate();
  for (const redis of [client, subscriber]) {
    redis.on("error", () => console.error("Western Sim Redis connection failed"));
  }
  try {
    await Promise.all([client.connect(), subscriber.connect()]);
    await subscriber.subscribe(channel, (data) => {
      const [room, message] = JSON.parse(data);
      deliver(room, message);
    });
  } catch (error) {
    if (client.isOpen) client.destroy();
    if (subscriber.isOpen) subscriber.destroy();
    throw error;
  }

  return {
    async reserve(room, id) {
      return await client.eval(reserveRoom, {
        keys: [key(room)], arguments: [id, String(Date.now()), String(leaseMs)],
      }) === 1;
    },
    async refresh(room, id) {
      const script = `
      local untilAt = redis.call('ZSCORE', KEYS[1], ARGV[1])
      if not untilAt or tonumber(untilAt) <= tonumber(ARGV[2]) then return 0 end
      redis.call('ZADD', KEYS[1], tonumber(ARGV[2]) + tonumber(ARGV[3]), ARGV[1])
      redis.call('PEXPIRE', KEYS[1], ARGV[3])
      return 1
      `;
      return await client.eval(script, {
        keys: [key(room)], arguments: [id, String(Date.now()), String(leaseMs)],
      }) === 1;
    },
    release: (room, id) => client.zRem(key(room), id),
    publish: (room, message) => client.publish(channel, JSON.stringify([room, message])),
  };
}
