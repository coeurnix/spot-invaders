(() => {
	const params = new URLSearchParams(location.search);
	const roomId = params.get("room") || "default";
	const video = document.getElementById("media");
	const hitOverlayEl = document.getElementById("hit-overlay");
	const canvas = document.getElementById("stage");
	const ctx = canvas.getContext("2d", { alpha: true });
	const counterEl = document.getElementById("counter");
	const scoreEl = document.getElementById("score");
	const playerCountEl = document.getElementById("player-count");
	const titleScreenEl = document.getElementById("title-screen");
	const gameOverLabelEl = document.getElementById("game-over-label");
	const portalLinkEl = document.getElementById("portal-link");
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
		renderHz: 30,
		energyChargeMs: 3000,
		energyRechargeMs: 5000,
		energyBlastImmunityMs: 5000,
		energyBlastRadiusPlayerScale: 10,
	};

	const videoAspect = 1280 / 720;
	const portalRef = params.get("ref") || "";
	const fromPortal = params.get("portal") === "true";
	const portalSpawn = { x: 0.12, y: 0.5 };

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
	let isPlaying = false;
	let isGameOver = false;
	let scoreStartedAt = 0;
	let lastScoreText = "";
	let pendingStartPoint = null;
	let activePointerId = null;
	let pointerUpdatedDuringActive = false;
	let lastPointerSample = null;
	let energyPointerId = null;
	let mustReleaseBeforeCharge = false;
	let localEnergyRechargeUntilServerMs = 0;
	let localBlastImmuneUntilServerMs = 0;
	let hitEffectStartedAt = 0;
	let hitEffectUntil = 0;
	let hitEffectSeed = 0;
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
	const activeBlasts = [];
	const seenBlastIds = new Set();
	const localPreview = {
		x: 0.5,
		y: 0.5,
		vx: 0,
		vy: 0,
	};

	debugRoomEl.textContent = roomId;
	gameOverLabelEl.hidden = true;

	function clamp01(value) {
		return Math.min(1, Math.max(0, value));
	}

	function energyChargeMs() {
		return Math.max(500, config.energyChargeMs || defaultConfig.energyChargeMs);
	}

	function energyRechargeMs() {
		return Math.max(0, config.energyRechargeMs || defaultConfig.energyRechargeMs);
	}

	function energyBlastImmunityMs() {
		return Math.max(0, config.energyBlastImmunityMs || defaultConfig.energyBlastImmunityMs);
	}

	function energyBlastRadiusScale() {
		return Math.max(1, config.energyBlastRadiusPlayerScale || defaultConfig.energyBlastRadiusPlayerScale);
	}

	function videoUnitPx() {
		return Math.max(1, Math.min(videoRect.width, videoRect.height));
	}

	function playerRadiusPx() {
		return Math.max(5, videoUnitPx() * 0.0195);
	}

	function blastRadiusPx() {
		return playerRadiusPx() * energyBlastRadiusScale();
	}

	function syncVideoCssVars() {
		const root = document.documentElement;
		root.style.setProperty("--video-left", `${videoRect.x}px`);
		root.style.setProperty("--video-top", `${videoRect.y}px`);
		root.style.setProperty("--video-width", `${videoRect.width}px`);
		root.style.setProperty("--video-height", `${videoRect.height}px`);
		root.style.setProperty("--video-unit", `${videoUnitPx()}px`);
	}

	function cssColorToPortalColor(color) {
		const hslMatch = /^hsl\(\s*(\d+(?:\.\d+)?)/i.exec(color);
		if (hslMatch) {
			return `hsl-${Math.round(Number(hslMatch[1]))}`;
		}
		return color || "cyan";
	}

	function portalParams() {
		const outbound = new URLSearchParams(location.search);
		outbound.delete("portal");
		outbound.set("username", playerId ? `spot-${playerId.slice(0, 6)}` : "spot-player");
		outbound.set("color", cssColorToPortalColor(localColor));
		outbound.set("speed", Math.hypot(localPreview.vx, localPreview.vy).toFixed(2));
		outbound.set("ref", "spot-invaders.coeurnix.party");
		return outbound;
	}

	function updatePortalLink() {
		if (fromPortal && portalRef) {
			const url = new URL(/^https?:\/\//i.test(portalRef) ? portalRef : `https://${portalRef}`);
			const existing = new URLSearchParams(location.search);
			for (const [key, value] of existing) {
				if (key !== "portal" && key !== "ref") {
					url.searchParams.set(key, value);
				}
			}
			url.searchParams.set("portal", "true");
			url.searchParams.set("ref", "spot-invaders.coeurnix.party");
			portalLinkEl.href = url.toString();
			portalLinkEl.textContent = "Return Portal";
			return;
		}

		portalLinkEl.href = `https://vibejam.cc/portal/2026?${portalParams().toString()}`;
		portalLinkEl.textContent = "Vibe Jam Portal";
	}

	function setOverlayVisible(visible) {
		titleScreenEl.classList.toggle("has-return-portal", Boolean(fromPortal && portalRef));
		titleScreenEl.classList.toggle("is-hidden", !visible);
		gameOverLabelEl.hidden = !isGameOver;
	}

	function beginPlaying() {
		if (!isPlaying) {
			scoreStartedAt = performance.now();
			lastScoreText = "";
		}
		isPlaying = true;
		isGameOver = false;
		setOverlayVisible(false);
		if (roomStartServerMs !== null) {
			video.play().catch(() => {});
		}
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

	function mediaClockSeconds(mediaMs) {
		if (!video.duration || !Number.isFinite(video.duration)) {
			return 0;
		}

		const duration = video.duration;
		return ((mediaMs / 1000) % duration + duration) % duration;
	}

	function videoDriftSeconds(targetSeconds) {
		const duration = video.duration;
		let drift = targetSeconds - video.currentTime;
		if (duration && Number.isFinite(duration)) {
			if (drift > duration / 2) {
				drift -= duration;
			} else if (drift < -duration / 2) {
				drift += duration;
			}
		}
		return drift;
	}

	function syncVideo(mediaMs, force = false) {
		if (roomStartServerMs === null) {
			video.playbackRate = 1;
			if (!video.paused) {
				video.pause();
			}
			if (video.duration && Number.isFinite(video.duration) && !video.seeking && video.currentTime !== 0) {
				video.currentTime = 0;
			}
			return;
		}

		const now = performance.now();
		if (!video.duration || !Number.isFinite(video.duration)) {
			return;
		}
		if (!force && now - lastVideoSyncAt < 100) {
			return;
		}
		lastVideoSyncAt = now;

		const targetSeconds = mediaClockSeconds(mediaMs);
		const drift = videoDriftSeconds(targetSeconds);
		if (!video.seeking && (force || Math.abs(drift) > 0.08)) {
			video.currentTime = targetSeconds;
		}
		video.playbackRate = Math.max(0.9, Math.min(1.1, 1 + drift * 0.35));
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
		} else if (message.type === "room_started") {
			handleRoomStarted(message);
		} else if (message.type === "move") {
			handleMove(message);
		} else if (message.type === "energy_start") {
			handleEnergyStart(message);
		} else if (message.type === "energy_cancel") {
			handleEnergyCancel(message);
		} else if (message.type === "energy_blast") {
			handleEnergyBlast(message);
		} else if (message.type === "energy_state") {
			handleEnergyState(message);
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
		activeBlasts.length = 0;
		seenBlastIds.clear();
		localEnergyRechargeUntilServerMs = 0;
		localBlastImmuneUntilServerMs = 0;
		mustReleaseBeforeCharge = false;
		energyPointerId = null;

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
			localEnergyRechargeUntilServerMs = local.energyRechargeUntilServerMs || 0;
		}

		updatePortalLink();
		setStatus("synced");
		markCanvasDirty();
		startPing();
		if (pendingStartPoint) {
			startAtNormalizedPoint(pendingStartPoint, true);
			pendingStartPoint = null;
			beginPlaying();
			maybeSendMove(true);
			return;
		}
		if (fromPortal && !localStarted) {
			startAtNormalizedPoint(portalSpawn, true);
			beginPlaying();
			maybeSendMove(true);
		}
	}

	function handleRoomStarted(message) {
		if (!Number.isFinite(message.roomStartServerMs)) {
			return;
		}

		roomStartServerMs = message.roomStartServerMs;
		if (Number.isFinite(message.epoch)) {
			epoch = message.epoch;
		}
		if (Number.isFinite(message.serverNowMs)) {
			const sampleOffset = message.serverNowMs - performance.now();
			clockOffsetMs = clockOffsetMs * 0.8 + sampleOffset * 0.2;
		}
		syncVideo(displayedMediaMs(), true);
		markCanvasDirty(displayedMediaMs() + currentMaxExtrapolateMs());
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

	function startAtNormalizedPoint(point, resetVelocity) {
		const now = performance.now();
		const x = clamp01(point.x);
		const y = clamp01(point.y);

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
			energyChargeStartedServerMs: Number.isFinite(player.energyChargeStartedServerMs)
				? player.energyChargeStartedServerMs
				: player.energyChargeStartedServerMs === null
					? null
					: existing?.energyChargeStartedServerMs ?? null,
			energyRechargeUntilServerMs: Number.isFinite(player.energyRechargeUntilServerMs)
				? player.energyRechargeUntilServerMs
				: existing?.energyRechargeUntilServerMs || 0,
			energyBlastSeq: Number.isFinite(player.energyBlastSeq) ? player.energyBlastSeq : existing?.energyBlastSeq || 0,
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
			energyChargeStartedServerMs: null,
			energyRechargeUntilServerMs: 0,
			energyBlastSeq: 0,
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

	function playerForEnergyMessage(message) {
		if (!message.playerId) {
			return null;
		}

		const player = players.get(message.playerId) || {
			playerId: message.playerId,
			color: message.color || "#ffffff",
			x: Number.isFinite(message.x) ? message.x : 0.5,
			y: Number.isFinite(message.y) ? message.y : 0.5,
			vx: 0,
			vy: 0,
			lastSeq: 0,
			lastMediaMs: 0,
			started: true,
			energyChargeStartedServerMs: null,
			energyRechargeUntilServerMs: 0,
			energyBlastSeq: 0,
		};
		players.set(player.playerId, player);
		return player;
	}

	function handleEnergyStart(message) {
		const player = playerForEnergyMessage(message);
		if (!player || !Number.isFinite(message.chargeStartedServerMs)) {
			return;
		}

		player.energyChargeStartedServerMs = message.chargeStartedServerMs;
		player.energyRechargeUntilServerMs = Number.isFinite(message.rechargeUntilServerMs)
			? message.rechargeUntilServerMs
			: player.energyRechargeUntilServerMs || 0;
		if (Number.isFinite(message.serverNowMs)) {
			const sampleOffset = message.serverNowMs - performance.now();
			clockOffsetMs = clockOffsetMs * 0.9 + sampleOffset * 0.1;
		}
		if (player.playerId === playerId) {
			localEnergyRechargeUntilServerMs = player.energyRechargeUntilServerMs;
		}
		markCanvasDirty(displayedMediaMs() + energyChargeMs() + 500);
	}

	function handleEnergyCancel(message) {
		const player = playerForEnergyMessage(message);
		if (!player) {
			return;
		}

		player.energyChargeStartedServerMs = null;
		if (player.playerId === playerId) {
			energyPointerId = null;
		}
		markCanvasDirty();
	}

	function handleEnergyState(message) {
		const player = playerForEnergyMessage(message);
		if (!player) {
			return;
		}

		player.energyChargeStartedServerMs = Number.isFinite(message.energyChargeStartedServerMs)
			? message.energyChargeStartedServerMs
			: null;
		player.energyRechargeUntilServerMs = Number.isFinite(message.energyRechargeUntilServerMs)
			? message.energyRechargeUntilServerMs
			: player.energyRechargeUntilServerMs || 0;
		if (player.playerId === playerId) {
			localEnergyRechargeUntilServerMs = player.energyRechargeUntilServerMs;
		}
		markCanvasDirty();
	}

	function handleEnergyBlast(message) {
		const player = playerForEnergyMessage(message);
		if (
			!player ||
			!message.blastId ||
			!Number.isFinite(message.blastServerMs) ||
			!Number.isFinite(message.x) ||
			!Number.isFinite(message.y)
		) {
			return;
		}
		if (seenBlastIds.has(message.blastId)) {
			return;
		}
		seenBlastIds.add(message.blastId);
		if (seenBlastIds.size > 100) {
			seenBlastIds.delete(seenBlastIds.values().next().value);
		}

		player.energyChargeStartedServerMs = null;
		player.energyRechargeUntilServerMs = Number.isFinite(message.rechargeUntilServerMs)
			? message.rechargeUntilServerMs
			: message.blastServerMs + energyRechargeMs();
		if (player.playerId === playerId) {
			localEnergyRechargeUntilServerMs = player.energyRechargeUntilServerMs;
			mustReleaseBeforeCharge = energyPointerId !== null;
		}

		activeBlasts.push({
			id: message.blastId,
			playerId: player.playerId,
			x: clamp01(message.x),
			y: clamp01(message.y),
			color: message.color || player.color,
			blastServerMs: message.blastServerMs,
		});
		if (activeBlasts.length > 32) {
			activeBlasts.splice(0, activeBlasts.length - 32);
		}

		if (player.playerId !== playerId) {
			maybeApplyBlastHit(message);
		}
		markCanvasDirty(displayedMediaMs() + 1500);
	}

	function maybeApplyBlastHit(blast) {
		if (!isPlaying || !localStarted || estimatedServerNowMs() < localBlastImmuneUntilServerMs) {
			return;
		}

		const dx = (localPreview.x - clamp01(blast.x)) * videoRect.width;
		const dy = (localPreview.y - clamp01(blast.y)) * videoRect.height;
		if (Math.hypot(dx, dy) > blastRadiusPx()) {
			return;
		}

		localBlastImmuneUntilServerMs = blast.blastServerMs + energyBlastImmunityMs();
		hitEffectStartedAt = performance.now();
		hitEffectUntil = hitEffectStartedAt + 3000;
		hitEffectSeed = Math.random() * 1000;
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
		const point = pointToVideoSpace(event.clientX, event.clientY);
		if (!playerId) {
			pendingStartPoint = point;
			return;
		}
		startAtNormalizedPoint(point, resetVelocity);
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
		updatePortalLink();
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

	function canStartEnergyCharge() {
		return (
			isPlaying &&
			localStarted &&
			playerId &&
			ws?.readyState === WebSocket.OPEN &&
			!mustReleaseBeforeCharge &&
			estimatedServerNowMs() >= localEnergyRechargeUntilServerMs
		);
	}

	function beginEnergyCharge(event) {
		if (event.pointerType !== "mouse" || event.button !== 0 || !canStartEnergyCharge()) {
			return;
		}

		const local = players.get(playerId);
		if (local?.energyChargeStartedServerMs !== null && local?.energyChargeStartedServerMs !== undefined) {
			return;
		}

		energyPointerId = event.pointerId;
		const chargeStartedServerMs = estimatedServerNowMs();
		if (local) {
			local.energyChargeStartedServerMs = chargeStartedServerMs;
			local.energyRechargeUntilServerMs = localEnergyRechargeUntilServerMs;
		}
		ws.send(JSON.stringify({ type: "energy_start" }));
		markCanvasDirty(displayedMediaMs() + energyChargeMs() + 500);
	}

	function endEnergyCharge(event) {
		if (event.pointerId !== energyPointerId) {
			return;
		}

		const local = players.get(playerId);
		const chargeStartedServerMs = local?.energyChargeStartedServerMs;
		if (Number.isFinite(chargeStartedServerMs)) {
			const elapsedMs = estimatedServerNowMs() - chargeStartedServerMs;
			if (elapsedMs < energyChargeMs() - 80 && ws?.readyState === WebSocket.OPEN) {
				ws.send(JSON.stringify({ type: "energy_cancel" }));
				local.energyChargeStartedServerMs = null;
				markCanvasDirty();
			}
		}

		energyPointerId = null;
		mustReleaseBeforeCharge = false;
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
		syncVideoCssVars();

		ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
		canvasNeedsResize = false;
		markCanvasDirty();
	}

	function drawChargeOutline(position, player, serverNowMs) {
		if (!Number.isFinite(player.energyChargeStartedServerMs)) {
			return;
		}

		const elapsedMs = serverNowMs - player.energyChargeStartedServerMs;
		if (elapsedMs < 0 || elapsedMs > energyChargeMs() + 300) {
			return;
		}

		const point = normalizedToCanvas(position);
		const progress = clamp01(elapsedMs / energyChargeMs());
		const pulse = (Math.sin(performance.now() / 90) + 1) / 2;
		const radius = playerRadiusPx() * (1.55 + progress * 0.5 + pulse * 0.18);
		const hue = (elapsedMs / 8 + progress * 160) % 360;

		ctx.save();
		ctx.globalAlpha = 0.62 + pulse * 0.28;
		ctx.lineWidth = Math.max(2, playerRadiusPx() * (0.14 + progress * 0.08));
		ctx.strokeStyle = `hsl(${hue}, 100%, ${62 + pulse * 18}%)`;
		ctx.shadowColor = ctx.strokeStyle;
		ctx.shadowBlur = playerRadiusPx() * (1.2 + progress * 1.2);
		ctx.beginPath();
		ctx.arc(point.x, point.y, radius, 0, Math.PI * 2);
		ctx.stroke();
		ctx.restore();
	}

	function drawDot(position, player, isLocal, serverNowMs) {
		const point = normalizedToCanvas(position);
		const x = point.x;
		const y = point.y;
		const radius = playerRadiusPx();
		const color = player.color;

		drawChargeOutline(position, player, serverNowMs);
		ctx.globalAlpha = position.stale ? 0.72 : 1;
		ctx.lineWidth = Math.max(1.5, radius * (isLocal ? 0.14 : 0.26));
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

	function drawBlast(blast, serverNowMs) {
		const elapsedMs = serverNowMs - blast.blastServerMs;
		const durationMs = 950;
		if (elapsedMs < -100 || elapsedMs > durationMs) {
			return false;
		}

		const progress = clamp01(elapsedMs / durationMs);
		const point = normalizedToCanvas(blast);
		const radius = playerRadiusPx() + (blastRadiusPx() - playerRadiusPx()) * progress;
		const alpha = (1 - progress) ** 0.65;

		ctx.save();
		ctx.globalAlpha = alpha;
		ctx.lineWidth = Math.max(2, playerRadiusPx() * (0.4 - progress * 0.18));
		ctx.strokeStyle = blast.color;
		ctx.shadowColor = blast.color;
		ctx.shadowBlur = playerRadiusPx() * (3.2 - progress * 1.5);
		ctx.beginPath();
		ctx.arc(point.x, point.y, radius, 0, Math.PI * 2);
		ctx.stroke();

		ctx.globalAlpha = alpha * 0.18;
		ctx.fillStyle = blast.color;
		ctx.beginPath();
		ctx.arc(point.x, point.y, radius, 0, Math.PI * 2);
		ctx.fill();
		ctx.restore();
		return true;
	}

	function hasActiveEnergyEffects(serverNowMs) {
		for (const player of players.values()) {
			if (
				Number.isFinite(player.energyChargeStartedServerMs) &&
				serverNowMs - player.energyChargeStartedServerMs <= energyChargeMs() + 300
			) {
				return true;
			}
		}

		return activeBlasts.some((blast) => serverNowMs - blast.blastServerMs <= 950);
	}

	function updateHitEffect(now) {
		if (now >= hitEffectUntil) {
			video.style.setProperty("--shake-x", "0px");
			video.style.setProperty("--shake-y", "0px");
			video.style.filter = "";
			hitOverlayEl.style.opacity = "0";
			return;
		}

		const durationMs = Math.max(1, hitEffectUntil - hitEffectStartedAt);
		const progress = clamp01((now - hitEffectStartedAt) / durationMs);
		const intensity = (1 - progress) ** 1.35;
		const amplitude = videoUnitPx() * 0.024 * intensity;
		const phase = (now + hitEffectSeed) / 22;
		const shakeX = Math.sin(phase * 1.7) * amplitude + Math.sin(phase * 0.63) * amplitude * 0.45;
		const shakeY = Math.cos(phase * 1.3) * amplitude + Math.sin(phase * 0.91) * amplitude * 0.4;

		video.style.setProperty("--shake-x", `${shakeX.toFixed(2)}px`);
		video.style.setProperty("--shake-y", `${shakeY.toFixed(2)}px`);
		video.style.filter = `brightness(${Math.max(0.48, 1 - intensity * 0.42).toFixed(3)})`;
		hitOverlayEl.style.opacity = (intensity * 0.48).toFixed(3);
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

	function updateHud(now) {
		playerCountEl.textContent = String(players.size);
		const survivalScore = isPlaying ? Math.floor((now - scoreStartedAt) / 200) * 10 : 0;
		const lapBonus =
			isPlaying && video.duration && Number.isFinite(video.duration)
				? Math.floor(displayedMediaMs() / (video.duration * 1000)) * 1500
				: 0;
		const score = survivalScore + lapBonus;
		const scoreText = String(Math.max(0, score));
		if (scoreText !== lastScoreText) {
			lastScoreText = scoreText;
			scoreEl.textContent = scoreText;
		}
	}

	function render() {
		const now = performance.now();
		if (canvasNeedsResize) {
			resizeCanvas();
		}

		const mediaMs = displayedMediaMs();
		syncVideo(mediaMs);
		maybeSendMove(false);
		updateHud(now);
		updateHitEffect(now);
		const serverNowMs = estimatedServerNowMs();

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
		const shouldDrawCanvas = canvasDirty || mediaMs <= canvasDirtyUntilMediaMs || hasActiveEnergyEffects(serverNowMs);
		if (!shouldDrawCanvas || now < nextCanvasDrawAt) {
			requestAnimationFrame(render);
			return;
		}
		nextCanvasDrawAt = now + 1000 / targetRenderHz;

		pruneSamples(mediaMs);
		for (let index = activeBlasts.length - 1; index >= 0; index -= 1) {
			if (serverNowMs - activeBlasts[index].blastServerMs > 1000) {
				activeBlasts.splice(index, 1);
			}
		}
		ctx.clearRect(0, 0, viewWidth, viewHeight);

		for (const blast of activeBlasts) {
			drawBlast(blast, serverNowMs);
		}

		for (const [id, player] of players) {
			if (id !== playerId && player.started) {
				drawDot(reconstruct(id, mediaMs), player, false, serverNowMs);
			}
		}

		if (playerId && localStarted) {
			const local = players.get(playerId) || {
				playerId,
				color: localColor,
				energyChargeStartedServerMs: null,
				energyRechargeUntilServerMs: localEnergyRechargeUntilServerMs,
			};
			drawDot({ ...localPreview, stale: false }, local, true, serverNowMs);
		}
		canvasDirty = false;
		requestAnimationFrame(render);
	}

	canvas.addEventListener("pointerdown", (event) => {
		event.preventDefault();
		const wasStarted = localStarted;
		activePointerId = event.pointerId;
		pointerUpdatedDuringActive = false;
		canvas.setPointerCapture?.(event.pointerId);
		if (!wasStarted || event.pointerType !== "mouse") {
			updatePointer(event, true);
			pointerUpdatedDuringActive = true;
			beginPlaying();
			maybeSendMove(true);
		} else {
			beginEnergyCharge(event);
		}
	});

	titleScreenEl.addEventListener("pointerdown", (event) => {
		if (event.target.closest("a")) {
			return;
		}
		event.preventDefault();
		updatePointer(event, true);
		beginPlaying();
		maybeSendMove(true);
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
		endEnergyCharge(event);
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
		syncVideo(displayedMediaMs(), true);
		markCanvasDirty();
	});
	video.addEventListener("play", markCanvasDirty);
	video.addEventListener("seeking", markCanvasDirty);
	window.addEventListener("resize", () => {
		canvasNeedsResize = true;
		markCanvasDirty();
	});

	updatePortalLink();
	connect();
	resizeCanvas();
	setOverlayVisible(!fromPortal);
	requestAnimationFrame(render);
})();
