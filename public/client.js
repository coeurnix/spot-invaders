(() => {
	const params = new URLSearchParams(location.search);
	const roomId = params.get("room") || "default";
	const video = document.getElementById("media");
	const canvas = document.getElementById("stage");
	const ctx = canvas.getContext("2d", { alpha: true });
	const counterEl = document.getElementById("counter");
	const debugRoomEl = document.getElementById("debug-room");
	const debugPlayersEl = document.getElementById("debug-players");
	const debugPlayerEl = document.getElementById("debug-player");
	const debugStatusEl = document.getElementById("debug-status");
	const debugOffsetEl = document.getElementById("debug-offset");
	const debugRttEl = document.getElementById("debug-rtt");
	const debugSendHzEl = document.getElementById("debug-sendhz");
	const debugMediaEl = document.getElementById("debug-media");

	const defaultConfig = {
		renderDelayMs: 300,
		inputLeadMs: 300,
		maxExtrapolateMs: 250,
		sendHz: 10,
		minSendHz: 1,
		maxSendHz: 10,
		fullRatePlayerCount: 10,
		minRatePlayerCount: 100,
		renderHz: 24,
	};

	const videoAspect = 1280 / 720;

	let ws = null;
	let socketStatus = "idle";
	let reconnectDelayMs = 250;
	let reconnectTimer = 0;
	let pingTimer = 0;
	let playerId = "";
	let localColor = "#ffffff";
	let roomStartServerMs = null;
	let epoch = 0;
	let clockOffsetMs = 0;
	let rttMs = null;
	let bestRttMs = Infinity;
	let config = { ...defaultConfig };
	let seq = 0;
	let lastSentAt = 0;
	let localDirty = false;
	let localStarted = false;
	let activePointerId = null;
	let pointerUpdatedDuringActive = false;
	let lastPointerSample = null;
	let lastCounterText = "";
	let lastDebugUpdateAt = 0;
	let lastVideoSyncAt = 0;
	let nextCanvasDrawAt = 0;
	let canvasNeedsResize = true;
	let canvasDirty = true;
	let canvasDirtyUntilMediaMs = 0;
	let dpr = 1;
	let viewWidth = 0;
	let viewHeight = 0;
	let videoRect = {
		x: 0,
		y: 0,
		width: 1,
		height: 1,
	};

	const players = new Map();
	const sampleBuffers = new Map();
	const localPreview = {
		x: 0.5,
		y: 0.5,
		vx: 0,
		vy: 0,
	};

	debugRoomEl.textContent = roomId;

	function clamp01(value) {
		return Math.min(1, Math.max(0, value));
	}

	function pointToVideoSpace(clientX, clientY) {
		return {
			x: clamp01((clientX - videoRect.x) / videoRect.width),
			y: clamp01((clientY - videoRect.y) / videoRect.height),
		};
	}

	function normalizedToCanvas(position) {
		return {
			x: videoRect.x + position.x * videoRect.width,
			y: videoRect.y + position.y * videoRect.height,
		};
	}

	function wsUrl() {
		const protocol = location.protocol === "https:" ? "wss:" : "ws:";
		return `${protocol}//${location.host}/api/room/${encodeURIComponent(roomId)}/ws`;
	}

	function setStatus(nextStatus) {
		socketStatus = nextStatus;
		debugStatusEl.textContent = nextStatus;
	}

	function markCanvasDirty(untilMediaMs = displayedMediaMs()) {
		canvasDirty = true;
		canvasDirtyUntilMediaMs = Math.max(canvasDirtyUntilMediaMs, untilMediaMs);
	}

	function currentSendHz() {
		const playerCount = Math.max(1, players.size);
		const minHz = Math.max(0.25, config.minSendHz || 1);
		const maxHz = Math.max(minHz, config.maxSendHz || config.sendHz || 10);
		const fullRateCount = Math.max(1, config.fullRatePlayerCount || 10);
		const minRateCount = Math.max(fullRateCount + 1, config.minRatePlayerCount || 100);
		if (playerCount <= fullRateCount) {
			return maxHz;
		}
		if (playerCount >= minRateCount) {
			return minHz;
		}

		const t = (playerCount - fullRateCount) / (minRateCount - fullRateCount);
		return maxHz + (minHz - maxHz) * t;
	}

	function currentMaxExtrapolateMs() {
		return Math.max(config.maxExtrapolateMs || 250, 1000 / currentSendHz() + 120);
	}

	function syncVideo(mediaMs) {
		const now = performance.now();
		if (now - lastVideoSyncAt < 250 || !video.duration || !Number.isFinite(video.duration)) {
			return;
		}
		lastVideoSyncAt = now;

		const targetSeconds = (mediaMs / 1000) % video.duration;
		if (!video.seeking && Math.abs(video.currentTime - targetSeconds) > 0.35) {
			video.currentTime = targetSeconds;
		}
		if (video.paused && video.readyState >= HTMLMediaElement.HAVE_CURRENT_DATA) {
			video.play().catch(() => {
				// Muted autoplay is normally allowed; if blocked, the next user gesture retries.
			});
		}
	}

	function connect() {
		clearTimeout(reconnectTimer);
		setStatus("connecting");

		const socket = new WebSocket(wsUrl());
		ws = socket;

		socket.addEventListener("open", () => {
			if (ws === socket) {
				setStatus("open");
			}
		});

		socket.addEventListener("message", (event) => {
			if (ws !== socket) {
				return;
			}
			handleServerMessage(event.data);
		});

		socket.addEventListener("close", () => {
			if (ws !== socket) {
				return;
			}
			clearInterval(pingTimer);
			setStatus("disconnected");
			scheduleReconnect();
		});

		socket.addEventListener("error", () => {
			if (ws === socket) {
				setStatus("error");
			}
		});
	}

	function scheduleReconnect() {
		clearTimeout(reconnectTimer);
		reconnectTimer = setTimeout(() => {
			reconnectDelayMs = Math.min(5000, reconnectDelayMs * 1.8);
			connect();
		}, reconnectDelayMs);
	}

	function startPing() {
		clearInterval(pingTimer);
		sendPing();
		pingTimer = setInterval(sendPing, 2000);
	}

	function sendPing() {
		if (ws?.readyState === WebSocket.OPEN) {
			ws.send(JSON.stringify({ type: "ping", clientNowMs: performance.now() }));
		}
	}

	function handleServerMessage(raw) {
		let message;
		try {
			message = JSON.parse(raw);
		} catch {
			return;
		}

		if (message.type === "welcome") {
			handleWelcome(message);
		} else if (message.type === "player_joined") {
			upsertPlayer(message.player);
			if (message.player?.started) {
				markCanvasDirty();
			}
		} else if (message.type === "player_left") {
			players.delete(message.playerId);
			sampleBuffers.delete(message.playerId);
			markCanvasDirty();
		} else if (message.type === "move") {
			handleMove(message);
		} else if (message.type === "pong") {
			handlePong(message);
		}
	}

	function handleWelcome(message) {
		playerId = message.playerId;
		localColor = message.color;
		roomStartServerMs = message.roomStartServerMs;
		epoch = message.epoch;
		clockOffsetMs = message.serverNowMs - performance.now();
		rttMs = null;
		bestRttMs = Infinity;
		config = { ...defaultConfig, ...(message.config || {}) };
		seq = 0;
		lastSentAt = 0;
		localStarted = false;
		lastPointerSample = null;
		reconnectDelayMs = 250;
		players.clear();
		sampleBuffers.clear();

		for (const player of message.players || []) {
			upsertPlayer(player);
			pushSample(player.playerId, {
				mediaMs: player.lastMediaMs || 0,
				x: player.x,
				y: player.y,
				vx: player.vx || 0,
				vy: player.vy || 0,
				seq: player.lastSeq || 0,
			});
		}

		const local = players.get(playerId);
		if (local) {
			localStarted = Boolean(local.started);
			localPreview.x = local.x;
			localPreview.y = local.y;
			localPreview.vx = local.vx || 0;
			localPreview.vy = local.vy || 0;
		}

		setStatus("synced");
		markCanvasDirty();
		startPing();
	}

	function handlePong(message) {
		if (typeof message.clientNowMs !== "number" || typeof message.serverNowMs !== "number") {
			return;
		}

		const now = performance.now();
		const rtt = Math.max(0, now - message.clientNowMs);
		const sampleOffset = message.serverNowMs + rtt / 2 - now;
		const weight = rtt <= bestRttMs ? 0.25 : 0.1;
		bestRttMs = Math.min(bestRttMs, rtt);
		clockOffsetMs = clockOffsetMs * (1 - weight) + sampleOffset * weight;
		rttMs = rtt;
	}

	function upsertPlayer(player) {
		if (!player?.playerId) {
			return;
		}

		const existing = players.get(player.playerId);
		players.set(player.playerId, {
			playerId: player.playerId,
			color: player.color || existing?.color || "#ffffff",
			x: Number.isFinite(player.x) ? player.x : existing?.x || 0.5,
			y: Number.isFinite(player.y) ? player.y : existing?.y || 0.5,
			vx: Number.isFinite(player.vx) ? player.vx : existing?.vx || 0,
			vy: Number.isFinite(player.vy) ? player.vy : existing?.vy || 0,
			lastSeq: Number.isFinite(player.lastSeq) ? player.lastSeq : existing?.lastSeq || 0,
			lastMediaMs: Number.isFinite(player.lastMediaMs) ? player.lastMediaMs : existing?.lastMediaMs || 0,
			started: Boolean(player.started ?? existing?.started),
		});
	}

	function handleMove(message) {
		if (!message.playerId || !Number.isFinite(message.seq)) {
			return;
		}

		const player = players.get(message.playerId) || {
			playerId: message.playerId,
			color: "#ffffff",
			x: 0.5,
			y: 0.5,
			vx: 0,
			vy: 0,
			lastSeq: 0,
			lastMediaMs: 0,
			started: false,
		};

		if (message.seq <= player.lastSeq) {
			return;
		}

		player.x = clamp01(message.x);
		player.y = clamp01(message.y);
		player.vx = Number.isFinite(message.vx) ? message.vx : 0;
		player.vy = Number.isFinite(message.vy) ? message.vy : 0;
		player.lastSeq = message.seq;
		player.lastMediaMs = Number.isFinite(message.mediaMs) ? message.mediaMs : player.lastMediaMs;
		player.started = Boolean(message.started ?? true);
		players.set(player.playerId, player);
		if (player.playerId === playerId) {
			localStarted = true;
		}

		pushSample(player.playerId, {
			mediaMs: player.lastMediaMs,
			x: player.x,
			y: player.y,
			vx: player.vx,
			vy: player.vy,
			seq: player.lastSeq,
		});
		markCanvasDirty(player.lastMediaMs + currentMaxExtrapolateMs());
	}

	function pushSample(id, sample) {
		if (!Number.isFinite(sample.mediaMs)) {
			return;
		}

		const buffer = sampleBuffers.get(id) || [];
		const staleIndex = buffer.findIndex((existing) => existing.seq >= sample.seq);
		if (staleIndex !== -1 && buffer[staleIndex].seq === sample.seq) {
			return;
		}

		if (buffer.length === 0 || buffer[buffer.length - 1].mediaMs <= sample.mediaMs) {
			buffer.push(sample);
		} else {
			const index = buffer.findIndex((existing) => existing.mediaMs > sample.mediaMs);
			buffer.splice(index === -1 ? buffer.length : index, 0, sample);
		}

		sampleBuffers.set(id, buffer);
	}

	function pruneSamples(displayedMediaMs) {
		const cutoff = displayedMediaMs - 3000;
		for (const [id, buffer] of sampleBuffers) {
			while (buffer.length > 2 && buffer[1].mediaMs < cutoff) {
				buffer.shift();
			}
			if (!players.has(id)) {
				sampleBuffers.delete(id);
			}
		}
	}

	function estimatedServerNowMs() {
		return performance.now() + clockOffsetMs;
	}

	function displayedMediaMs() {
		if (roomStartServerMs === null) {
			return 0;
		}
		const canonical = estimatedServerNowMs() - roomStartServerMs;
		return Math.max(0, canonical - config.renderDelayMs);
	}

	function reconstruct(id, mediaMs) {
		const player = players.get(id);
		const buffer = sampleBuffers.get(id) || [];
		if (!player || buffer.length === 0) {
			return {
				x: player?.x || 0.5,
				y: player?.y || 0.5,
				stale: true,
			};
		}

		let a = null;
		let b = null;
		for (const sample of buffer) {
			if (sample.mediaMs <= mediaMs) {
				a = sample;
			}
			if (sample.mediaMs >= mediaMs) {
				b = sample;
				break;
			}
		}

		if (a && b && a !== b) {
			const span = Math.max(1, b.mediaMs - a.mediaMs);
			const t = clamp01((mediaMs - a.mediaMs) / span);
			return {
				x: clamp01(a.x + (b.x - a.x) * t),
				y: clamp01(a.y + (b.y - a.y) * t),
				stale: false,
			};
		}

		if (a) {
			const dtMs = mediaMs - a.mediaMs;
			if (dtMs <= currentMaxExtrapolateMs()) {
				return {
					x: clamp01(a.x + a.vx * (dtMs / 1000)),
					y: clamp01(a.y + a.vy * (dtMs / 1000)),
					stale: false,
				};
			}
			return { x: a.x, y: a.y, stale: true };
		}

		if (b) {
			return { x: b.x, y: b.y, stale: false };
		}

		return { x: player.x, y: player.y, stale: true };
	}

	function updatePointer(event, resetVelocity) {
		const now = performance.now();
		const point = pointToVideoSpace(event.clientX, event.clientY);
		const x = point.x;
		const y = point.y;

		let vx = 0;
		let vy = 0;
		if (!resetVelocity && lastPointerSample) {
			const dt = Math.max(1, now - lastPointerSample.t) / 1000;
			vx = (x - lastPointerSample.x) / dt;
			vy = (y - lastPointerSample.y) / dt;
		}

		localPreview.x = x;
		localPreview.y = y;
		localPreview.vx = vx;
		localPreview.vy = vy;
		lastPointerSample = { x, y, t: now };
		localDirty = true;
		localStarted = true;
		markCanvasDirty();

		const local = players.get(playerId);
		if (local) {
			local.x = x;
			local.y = y;
			local.vx = vx;
			local.vy = vy;
			local.started = true;
		}
	}

	function maybeSendMove(force = false) {
		if (!localStarted || !playerId || ws?.readyState !== WebSocket.OPEN || (!localDirty && !force)) {
			return;
		}

		const now = performance.now();
		const minInterval = 1000 / currentSendHz();
		const canBypassRateLimit = force && seq === 0;
		if (!canBypassRateLimit && now - lastSentAt < minInterval) {
			return;
		}

		seq += 1;
		lastSentAt = now;
		localDirty = false;
		ws.send(
			JSON.stringify({
				type: "move",
				seq,
				mediaMs: displayedMediaMs() + config.inputLeadMs,
				x: localPreview.x,
				y: localPreview.y,
				vx: localPreview.vx,
				vy: localPreview.vy,
				clientSentMs: now,
			}),
		);
	}

	function resizeCanvas() {
		const rect = canvas.getBoundingClientRect();
		const nextDpr = 1;
		const nextWidth = Math.max(1, rect.width);
		const nextHeight = Math.max(1, rect.height);
		const viewAspect = nextWidth / nextHeight;
		let videoWidth = nextWidth;
		let videoHeight = nextHeight;
		if (viewAspect > videoAspect) {
			videoHeight = nextHeight;
			videoWidth = videoHeight * videoAspect;
		} else {
			videoWidth = nextWidth;
			videoHeight = videoWidth / videoAspect;
		}

		if (canvas.width !== Math.round(nextWidth * nextDpr) || canvas.height !== Math.round(nextHeight * nextDpr)) {
			dpr = nextDpr;
			viewWidth = nextWidth;
			viewHeight = nextHeight;
			canvas.width = Math.round(viewWidth * dpr);
			canvas.height = Math.round(viewHeight * dpr);
		}
		videoRect = {
			x: (nextWidth - videoWidth) / 2,
			y: (nextHeight - videoHeight) / 2,
			width: videoWidth,
			height: videoHeight,
		};

		ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
		canvasNeedsResize = false;
		markCanvasDirty();
	}

	function drawDot(position, color, isLocal) {
		const point = normalizedToCanvas(position);
		const x = point.x;
		const y = point.y;
		const radius = 14;

		ctx.globalAlpha = position.stale ? 0.72 : 1;
		ctx.lineWidth = isLocal ? 2 : 4;
		ctx.strokeStyle = isLocal ? "rgba(255, 255, 255, 0.9)" : color;
		ctx.fillStyle = isLocal ? color : "rgba(255, 255, 255, 0.02)";
		ctx.beginPath();
		ctx.arc(x, y, radius, 0, Math.PI * 2);
		if (isLocal) {
			ctx.fill();
		}
		ctx.stroke();
		ctx.globalAlpha = 1;
	}

	function updateDebug(mediaMs) {
		debugPlayersEl.textContent = String(players.size);
		debugPlayerEl.textContent = playerId ? playerId.slice(0, 8) : "-";
		debugStatusEl.textContent = socketStatus;
		debugOffsetEl.textContent = `${Math.round(clockOffsetMs)} ms`;
		debugRttEl.textContent = rttMs === null ? "-" : `${Math.round(rttMs)} ms`;
		debugSendHzEl.textContent = currentSendHz().toFixed(1);
		debugMediaEl.textContent = `${(mediaMs / 1000).toFixed(2)} s`;
	}

	function render() {
		const now = performance.now();
		if (canvasNeedsResize) {
			resizeCanvas();
		}

		const mediaMs = displayedMediaMs();
		syncVideo(mediaMs);
		maybeSendMove(false);

		const counterText = String(Math.floor(mediaMs / 1000));
		if (counterText !== lastCounterText) {
			lastCounterText = counterText;
			counterEl.textContent = counterText;
		}

		if (now - lastDebugUpdateAt >= 100) {
			lastDebugUpdateAt = now;
			updateDebug(mediaMs);
		}

		const targetRenderHz = Math.max(15, Math.min(60, config.renderHz || 30));
		const shouldDrawCanvas = canvasDirty || mediaMs <= canvasDirtyUntilMediaMs;
		if (!shouldDrawCanvas || now < nextCanvasDrawAt) {
			requestAnimationFrame(render);
			return;
		}
		nextCanvasDrawAt = now + 1000 / targetRenderHz;

		pruneSamples(mediaMs);
		ctx.clearRect(0, 0, viewWidth, viewHeight);

		for (const [id, player] of players) {
			if (id !== playerId && player.started) {
				drawDot(reconstruct(id, mediaMs), player.color, false);
			}
		}

		if (playerId && localStarted) {
			drawDot({ ...localPreview, stale: false }, localColor, true);
		}
		canvasDirty = false;
		requestAnimationFrame(render);
	}

	canvas.addEventListener("pointerdown", (event) => {
		event.preventDefault();
		video.play().catch(() => {});
		const wasStarted = localStarted;
		activePointerId = event.pointerId;
		pointerUpdatedDuringActive = false;
		canvas.setPointerCapture?.(event.pointerId);
		if (!wasStarted || event.pointerType !== "mouse") {
			updatePointer(event, true);
			pointerUpdatedDuringActive = true;
			maybeSendMove(true);
		}
	});

	canvas.addEventListener("pointermove", (event) => {
		if (!localStarted || (activePointerId !== null && event.pointerId !== activePointerId)) {
			return;
		}
		event.preventDefault();
		const events = typeof event.getCoalescedEvents === "function" ? event.getCoalescedEvents() : [event];
		updatePointer(events[events.length - 1] || event, false);
		pointerUpdatedDuringActive = true;
		maybeSendMove(false);
	});

	function endPointer(event) {
		if (event.pointerId !== activePointerId) {
			return;
		}
		event.preventDefault();
		if (pointerUpdatedDuringActive || event.pointerType !== "mouse") {
			updatePointer(event, false);
			maybeSendMove(true);
		}
		activePointerId = null;
		pointerUpdatedDuringActive = false;
		canvas.releasePointerCapture?.(event.pointerId);
	}

	canvas.addEventListener("pointerup", endPointer);
	canvas.addEventListener("pointercancel", endPointer);
	video.addEventListener("loadedmetadata", () => {
		syncVideo(displayedMediaMs());
		markCanvasDirty();
	});
	video.addEventListener("play", markCanvasDirty);
	window.addEventListener("resize", () => {
		canvasNeedsResize = true;
		markCanvasDirty();
	});

	connect();
	resizeCanvas();
	requestAnimationFrame(render);
})();
