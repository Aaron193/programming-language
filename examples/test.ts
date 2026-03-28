const WINDOW_WIDTH = 800;
const WINDOW_HEIGHT = 600;

const BIRD_X = 160;
const BIRD_WIDTH = 34;
const BIRD_HEIGHT = 24;

const PIPE_WIDTH = 80;
const PIPE_GAP = 180;
const GROUND_HEIGHT = 100;

const FIXED_DT = 1 / 60;
const GRAVITY = 1400.0;
const FLAP_IMPULSE = -350.0;
const PIPE_SPEED = 180.0;
const SPAWN_INTERVAL = 1.4;

export interface Pipe {
  x: number;
  gapY: number;
  passed: boolean;
}

export interface GameState {
  birdY: number;
  birdVelocity: number;
  pipes: Pipe[];
  spawnTimer: number;
  spawnIndex: number;
  score: number;
  running: boolean;
  dead: boolean;
}

class Player {
  name: string;
  
  constructor(name: string) {
    this.name = name;
  }
}

export function makePipe(x: number, gapY: number): Pipe {
  return {
    x,
    gapY,
    passed: false,
  };
}

export function nextGapY(spawnIndex: number): number {
  const cycle = spawnIndex % 8;

  if (cycle === 0) return 180.0;
  if (cycle === 1) return 260.0;
  if (cycle === 2) return 320.0;
  if (cycle === 3) return 230.0;
  if (cycle === 4) return 150.0;
  if (cycle === 5) return 300.0;
  if (cycle === 6) return 210.0;

  return 280.0;
}

export function spawnPipe(state: GameState, x: number): void {
  const pipe = makePipe(x, nextGapY(state.spawnIndex));
  state.spawnIndex += 1;
  state.pipes.push(pipe);
}

export function seedInitialPipes(state: GameState): void {
  spawnPipe(state, 500.0);
  spawnPipe(state, 500.0 + PIPE_SPEED * SPAWN_INTERVAL);
}

export function resetGame(): GameState {
  const state: GameState = {
    birdY: 220.0,
    birdVelocity: 0.0,
    pipes: [],
    spawnTimer: 0.0,
    spawnIndex: 0,
    score: 0,
    running: true,
    dead: false,
  };

  seedInitialPipes(state);
  return state;
}

export function updateBird(state: GameState, flapPressed: boolean, dt: number): void {
  if (flapPressed) {
    state.birdVelocity = FLAP_IMPULSE;
  }

  state.birdVelocity += GRAVITY * dt;
  state.birdY += state.birdVelocity * dt;
}

export function updatePipes(state: GameState, dt: number): void {
  const activePipes: Pipe[] = [];

  for (const pipe of state.pipes) {
    const updatedPipe: Pipe = {
      ...pipe,
      x: pipe.x - PIPE_SPEED * dt,
    };

    if (updatedPipe.x + PIPE_WIDTH > 0.0) {
      activePipes.push(updatedPipe);
    }
  }

  state.pipes = activePipes;
  state.spawnTimer += dt;

  while (state.spawnTimer >= SPAWN_INTERVAL) {
    state.spawnTimer -= SPAWN_INTERVAL;
    spawnPipe(state, WINDOW_WIDTH + PIPE_WIDTH);
  }
}

export function updateScore(state: GameState): void {
  for (const pipe of state.pipes) {
    if (!pipe.passed && pipe.x + PIPE_WIDTH < BIRD_X) {
      pipe.passed = true;
      state.score += 1;
    }
  }
}

export function checkCollisions(state: GameState): boolean {
  const birdLeft = BIRD_X;
  const birdRight = birdLeft + BIRD_WIDTH;
  const birdTop = state.birdY;
  const birdBottom = state.birdY + BIRD_HEIGHT;

  const groundTop = WINDOW_HEIGHT - GROUND_HEIGHT;
  const halfGap = PIPE_GAP / 2.0;

  if (birdTop < 0.0) {
    return true;
  }

  if (birdBottom >= groundTop) {
    return true;
  }

  for (const pipe of state.pipes) {
    const pipeLeft = pipe.x;
    const pipeRight = pipe.x + PIPE_WIDTH;

    if (birdRight > pipeLeft && birdLeft < pipeRight) {
      const gapTop = pipe.gapY - halfGap;
      const gapBottom = pipe.gapY + halfGap;

      if (birdTop < gapTop || birdBottom > gapBottom) {
        return true;
      }
    }
  }

  return false;
}

export function stepGame(state: GameState, flapPressed: boolean, dt: number): void {
  if (!state.running) {
    return;
  }

  updateBird(state, flapPressed, dt);
  updatePipes(state, dt);
  updateScore(state);

  if (checkCollisions(state)) {
    state.birdVelocity = 0.0;
    state.running = false;
    state.dead = true;
  }
}