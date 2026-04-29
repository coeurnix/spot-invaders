import { DurableObject } from "cloudflare:workers";

type Session = LatestPlayerState & {
	ws: WebSocket;
};

type LatestPlayerState = {
	playerId: string;
	color: string;
	x: number;
	y: number;
	vx: number;
	vy: number;
	lastSeq: number;
	lastMediaMs: number;
	started: boolean;
};

const ROOM_CONFIG = {
	renderDelayMs: 300,
	inputLeadMs: 300,
	maxExtrapolateMs: 250,
	sendHz: 10,
	minSendHz: 1,
	maxSendHz: 10,
	fullRatePlayerCount: 10,
	minRatePlayerCount: 100,
	renderHz: 30,
};

const clamp01 = (value: number) => Math.min(1, Math.max(0, value));

const isFiniteNumber = (value: unknown): value is number => typeof value === "number" && Number.isFinite(value);

const playerSnapshot = (session: Session): LatestPlayerState => ({
	playerId: session.playerId,
	color: session.color,
	x: session.x,
	y: session.y,
	vx: session.vx,
	vy: session.vy,
	lastSeq: session.lastSeq,
	lastMediaMs: session.lastMediaMs,
	started: session.started,
});

export class MyDurableObject extends DurableObject {
	private sessions = new Map<WebSocket, Session>();
	private roomStartServerMs: number | null = null;
	private epoch = 0;
	private latestByPlayer = new Map<string, LatestPlayerState>();

	async fetch(request: Request): Promise<Response> {
		if (request.headers.get("Upgrade")?.toLowerCase() !== "websocket") {
			return new Response("Expected WebSocket upgrade", { status: 426 });
		}

		const pair = new WebSocketPair();
		const [client, server] = Object.values(pair);

		this.acceptSession(server);

		return new Response(null, {
			status: 101,
			webSocket: client,
		});
	}

	private acceptSession(server: WebSocket): void {
		const isFirstSession = this.sessions.size === 0;
		if (isFirstSession) {
			this.roomStartServerMs = null;
			this.epoch += 1;
		}

		const session: Session = {
			ws: server,
			playerId: crypto.randomUUID(),
			color: `hsl(${Math.floor(Math.random() * 360)}, 80%, 58%)`,
			x: 0.5,
			y: 0.5,
			vx: 0,
			vy: 0,
			lastSeq: 0,
			lastMediaMs: 0,
			started: false,
		};

		this.sessions.set(server, session);
		this.latestByPlayer.set(session.playerId, playerSnapshot(session));

		server.accept();
		server.addEventListener("message", (event) => this.handleMessage(server, event));
		server.addEventListener("close", (event) => {
			event.preventDefault();
			this.removeSession(server);
		});
		server.addEventListener("error", (event) => {
			event.preventDefault();
			this.removeSession(server);
		});

		this.safeSend(server, {
			type: "welcome",
			playerId: session.playerId,
			color: session.color,
			roomStartServerMs: this.roomStartServerMs,
			serverNowMs: Date.now(),
			epoch: this.epoch,
			players: [...this.latestByPlayer.values()],
			config: ROOM_CONFIG,
		});

		this.broadcast(
			{
				type: "player_joined",
				player: playerSnapshot(session),
			},
			server,
		);
	}

	private handleMessage(ws: WebSocket, event: MessageEvent): void {
		const session = this.sessions.get(ws);
		if (!session || typeof event.data !== "string") {
			return;
		}

		let message: unknown;
		try {
			message = JSON.parse(event.data);
		} catch {
			this.safeSend(ws, { type: "error", message: "Invalid JSON" });
			return;
		}

		if (!message || typeof message !== "object") {
			return;
		}

		const data = message as Record<string, unknown>;
		if (data.type === "ping") {
			this.handlePing(ws, data);
			return;
		}

		if (data.type === "move") {
			this.handleMove(ws, session, data);
		}
	}

	private handlePing(ws: WebSocket, data: Record<string, unknown>): void {
		if (!isFiniteNumber(data.clientNowMs)) {
			return;
		}

		this.safeSend(ws, {
			type: "pong",
			clientNowMs: data.clientNowMs,
			serverNowMs: Date.now(),
		});
	}

	private handleMove(ws: WebSocket, session: Session, data: Record<string, unknown>): void {
		if (
			!isFiniteNumber(data.seq) ||
			!Number.isInteger(data.seq) ||
			!isFiniteNumber(data.mediaMs) ||
			!isFiniteNumber(data.x) ||
			!isFiniteNumber(data.y)
		) {
			return;
		}

		const seq = data.seq;
		const mediaMs = data.mediaMs;
		if (seq <= session.lastSeq || seq < 0) {
			return;
		}

		let roomStarted = false;
		if (this.roomStartServerMs === null) {
			if (mediaMs > ROOM_CONFIG.renderDelayMs + 10_000 || mediaMs < -30_000) {
				return;
			}
			this.roomStartServerMs = Date.now() - ROOM_CONFIG.renderDelayMs;
			roomStarted = true;
		} else {
			const expectedMediaMs = Date.now() - this.roomStartServerMs;
			if (mediaMs > expectedMediaMs + 10_000 || mediaMs < expectedMediaMs - 30_000) {
				return;
			}
		}

		const x = clamp01(data.x);
		const y = clamp01(data.y);
		const vx = isFiniteNumber(data.vx) ? data.vx : 0;
		const vy = isFiniteNumber(data.vy) ? data.vy : 0;

		session.x = x;
		session.y = y;
		session.vx = vx;
		session.vy = vy;
		session.lastSeq = seq;
		session.lastMediaMs = mediaMs;
		session.started = true;
		this.latestByPlayer.set(session.playerId, playerSnapshot(session));

		if (roomStarted) {
			this.broadcast({
				type: "room_started",
				roomStartServerMs: this.roomStartServerMs,
				serverNowMs: Date.now(),
				epoch: this.epoch,
			});
		}

		this.broadcast({
			type: "move",
			playerId: session.playerId,
			seq,
			mediaMs,
			x,
			y,
			vx,
			vy,
			serverNowMs: Date.now(),
			started: true,
		});
	}

	private safeSend(ws: WebSocket, data: unknown): boolean {
		const encoded = typeof data === "string" ? data : JSON.stringify(data);
		try {
			ws.send(encoded);
			return true;
		} catch {
			try {
				ws.close(1011, "Send failed");
			} catch {
				// The socket may already be gone.
			}
			this.removeSession(ws);
			return false;
		}
	}

	private broadcast(data: unknown, exceptWs?: WebSocket): void {
		const encoded = JSON.stringify(data);
		for (const ws of this.sessions.keys()) {
			if (ws !== exceptWs) {
				this.safeSend(ws, encoded);
			}
		}
	}

	private removeSession(ws: WebSocket): void {
		const session = this.sessions.get(ws);
		if (!session) {
			return;
		}

		this.sessions.delete(ws);
		this.latestByPlayer.delete(session.playerId);

		this.broadcast({
			type: "player_left",
			playerId: session.playerId,
		});

		if (this.sessions.size === 0) {
			this.roomStartServerMs = null;
			this.latestByPlayer.clear();
		}
	}
}

export default {
	async fetch(request, env): Promise<Response> {
		const url = new URL(request.url);
		const match = url.pathname.match(/^\/api\/room\/([^/]+)\/ws\/?$/);

		if (!match) {
			return new Response("Not found", { status: 404 });
		}

		if (request.headers.get("Upgrade")?.toLowerCase() !== "websocket") {
			return new Response("Expected WebSocket upgrade", { status: 426 });
		}

		const roomId = decodeURIComponent(match[1]);
		const id = env.MY_DURABLE_OBJECT.idFromName(roomId);
		const stub = env.MY_DURABLE_OBJECT.get(id);
		return stub.fetch(request);
	},
} satisfies ExportedHandler<Env>;
