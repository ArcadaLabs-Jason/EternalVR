# Pace hold: why the held game fell behind on the test PC, and the spinning wait (2026-11-13)

## Question

The benchmark routes of 2026-11-12 (`perf-baselines.md`, fix candidate 1) had "Matched to the headset"
(`ETERNALVR_PACE=headset`, the hold switch of T-194) hold the game 6 to 8 times a route and let it go each time
within about 2 s: held, 10 to 20% of the headset's frames had no new image, while let go the same scenes ran 106
to 134 game ticks a second. Every route ran well above 90 ticks a second on its own, so the game was fast enough;
something about being held made it slow. The guesses were the release timing (the game released at the headset's
frame and its tick no longer fitting before the next) and the hold switch's rules.

## Method

- Test PC: RTX 3080 Ti, i7-9700K (8 cores, no SMT, Speed Shift), Windows' Balanced power plan unless a row says
  High performance; the OpenXR simulator at 90 Hz. Build: perf-pre plus the rig switches below.
- Routes: bench-combat, bench-outdoor and bench-hub (`bench-routes.md`) at the launcher's auto size (1280 x 1400
  per eye), most also at the Quest 3 size (2048 x 2208); RT off, the launcher's defaults; 60 s of scripted input
  measured over the route's 70 s.
- Measured beside each suite: `\Processor Information(_Total)\% Processor Performance` (the CPU's clock against
  its rated 3.6 GHz, once a second), `\Energy Meter(RAPL_Package0_PKG)\Power` (package power),
  `\Process(DOOMEternalx64vk)\% Processor Time` (100% = one core), `nvidia-smi` clocks; in the log the hold
  switch's changes, the frames table (each headset frame with or without a new image), and for a spinning hold
  its spin time every 10 s. Each sample and frame is put in the held or the let-go state by the switch's log
  lines, so a route's held and let-go seconds are compared within the same run.
- Rig switches (removed again unless kept below): the hold switch's rules (`tail`: hold again only when few
  let-go images took longer than the period less a margin; `tried=1`: a hold the rate made ends after one bad
  window), the process opting out of power throttling (`PROCESS_POWER_THROTTLING_EXECUTION_SPEED`), a spinning
  wait, and a wait that sleeps and spins the last part.
- One suite per variant and round, seven rounds (the last two confirming the kept build), and an eighth after
  review (below); each row below is one run per route unless it says more.

## Results

Default size, the three routes. "Held repeats": headset frames without a new image while held. "CPU held": %
Processor Performance while held (let go it is 125 to 128 in every Balanced run).

| Variant | Runs | Held, s of 70 | Held repeats | CPU held | Package W held |
|---|---|---|---|---|---|
| Sleeping wait (as shipped), Balanced | 4 | 5 to 9 | 10.2-20.0% | 84-114 | 33-49 |
| Process opts out of power throttling | 1 | 6 to 8 | 12.9-17.6% | 91-114 | |
| Rules: `tail=0.05;margin=1` | 1 | 6 to 8 | 17.1-17.4% | 83-101 | |
| Rules: `tried=1` | 1 | 2 to 4 (holds of 1 s) | 13.2-16.3% | 94-110 | |
| High performance plan, sleeping wait | 4 | 67 to 70 | outdoor, hub 0.1-0.7%; combat 1.2-2.2% | 127 | 46-56 |
| Spin through the whole wait | 3 | 65 to 70 | outdoor, hub 0.1-0.4%; combat 1.6-2.2% | 128 | 49-57 |
| Sleep, then spin 1 or 2 ms (condition variable) | 1 each | 6 to 8 | 10.6-17.0% | 88-111 | |
| Sleep, then spin 2 ms (high-resolution timer) | 1 | 7 to 10 | 7.4-11.8% | 94-122 | 36-58 |
| ... spin 3 ms | 1 | outdoor 63, combat 14, hub 12 | 3.1-8.2% | 100-124 | |
| ... spin 5 ms | 1 | 70 | outdoor, hub 0.2%; combat 2.4% | 128 | 50-58 |
| **Spin the last 5 ms once a hold fell behind (kept)** | 4 | 67 to 70 | outdoor, hub 0.3-0.6%; combat 1.3-2.4% | 126-128 | 48-59 |
| The same on High performance | 1 | 70 | 0.1-1.2% | 127 | 46-54 |

- **The CPU's clock falls while the game is held.** On Balanced, held seconds ran the CPU at 84 to 114% of its
  rated clock against 125 to 127% let go in the same run; the held tick then took longer than the period and
  the switch let the game go. On High performance (minimum processor state 100% against Balanced's 5%) the clock
  stayed at 127% held, and the same build held every route for the whole minute. The hold switch's rules and
  the release timing were not the cause: neither rule changed the held repeats, and the held runs on High
  performance repeat as little as the let-go runs do.
- **The render thread's sleep is what lowers it.** Held, the render thread sleeps 2 to 4 ms of each period
  waiting for the headset's frame. Spinning through that wait kept the clock at 128% on Balanced, as High
  performance does. Sleeping and spinning only the last 2 ms did not (94 to 122%), 3 ms held one route of three
  and 5 ms held all three: on this PC most waits are under 5 ms, so a 5 ms spin is nearly the whole wait.
  [inferred] The CPU's own clock control (Speed Shift under Balanced) reads the core's idle time, and the idle
  time early in the wait is enough to lower it.
- **A condition variable's timed wait wakes late.** Sleeping until 2 ms before the frame on the condition
  variable woke 1.6 ms late on average, so the spin hardly ran (0.08 to 0.4 ms a frame). A high-resolution
  waitable timer woke 0.31 to 0.37 ms late.
- **Opting the process out of power throttling did nothing** on this CPU (no hybrid cores): 91 to 114% held.
- **Held and spinning, against let go:** the game runs 90 ticks a second instead of 106 to 134, so it draws less:
  package power 48 to 59 W held and spinning against 61 to 64 W let go with the sleeping wait (High performance
  held, no spin: 46 to 56 W). The spin itself is 18 to 37% of one core (the log's 10 s lines; hub, the lightest
  route, waits the longest). The game process used 350 to 416% of a core held and spinning against 450 to 468%
  let go.
- **What holding buys** (kept, four runs, against the sleeping wait's four): missed display periods 0.33 to 0.63%
  on outdoor and hub against 1.6 to 2.2%; combat 1.2 to 2.4% against 1.1 to 2.1%. The shown image's p99 11.2 to
  11.4 ms on outdoor and hub (one period) against 14 to 22 ms; pose age p99 25 to 32 ms against 36 to 55 ms
  (let go, the game runs ahead of the headset and some poses wait). No images drawn and never shown.
- **Quest 3 size** (2048 x 2208, one run each; kept / sleeping / High performance): missed periods outdoor 4.3 /
  5.0 / 6.2%, hub 0.9 / 3.6 / 0.5%, combat 9.0 / 4.6 / 4.0%. Outdoor and combat are GPU-bound there (82 to 87
  ticks a second), so the switch lets them go part of the time whatever the wait; combat's 9.0% run is its heavy
  encounter (82.8 ticks a second; the same route measured 2.9 to 10.4% on 2026-11-12). Hub holds 68 of 70 s.
- Two runs of 90 failed their QA check on a handled access violation in `body_follow`'s guarded copy at the
  map's load, before the route; unrelated to pacing, recorded separately.

## Decision (T-232)

Held, the render thread sleeps until 5 ms before the headset's frame is due (a high-resolution waitable timer)
and spins the rest, once a hold has fallen behind in play twice within 2 minutes; until then it only sleeps. (First
measured and merged with the latch at the first fall; see "Latch at the second fall" below for the second-fall latch's own runs.) With the first-fall latch (the numbers
of the tables above) a PC like this one lost at most its
first hold (the spin turned on 17 to 44 s into each run, before the measured route), then held. On High
performance 4 of 6 route runs never spun, and in combat one hitch turned it on 101 s in (below). Once on, it stays on for the session: a spin that
decays lets the clock fall again and brings the flapping back. Only in play (menus and loading screens sleep),
never in the low hold while nobody looks (T-228). `ETERNALVR_TEST_HOLD_SPIN=0` only sleeps, `=1` spins through
the whole wait in play from the start, `ETERNALVR_TEST_HOLD_SPIN_MS` sets the spin (rig tests).

## Review fixes (2026-11-13)

After review the held wait (`features/pacing/held_spin.*` plans it, `vkcore/held_wait.*` carries it out):
spins only in play; gives the core up at each turn of the spin (`SwitchToThread`), for CPUs with few cores
where the spin would compete with the game's workers and the XR worker that ends it; spins at most the spin
plus 1 ms a frame (a late or stalled headset frame used to spin the whole timeout, up to two periods), then
sleeps out the rest; falls back to a plain timer, then to the condition variable, never to a full spin.
Re-measured with no switches, one suite each (r8), against the kept build's four Balanced runs and its one High
performance run:

| | Balanced, kept (4 runs) | Balanced, fixed | High performance, kept | High performance, fixed |
|---|---|---|---|---|
| Held, s of 70 (combat / outdoor / hub) | 67-70 / 70 / 70 | 69 / 70 / 70 | 70 / 70 / 70 | 69 / 70 / 70 |
| Held repeats (combat / outdoor / hub) | 1.3-2.4 / 0.3-0.6 / 0.3-0.6% | 2.0 / 0.3 / 0.5% | 1.2 / 0.3 / 0.1% | 2.0 / 0.5 / 0.2% |
| Missed periods (combat / outdoor / hub) | 1.2-2.4 / 0.33-0.63 / 0.37-0.54% | 2.13 / 0.35 / 0.48% | 1.37 / 0.35 / 0.07% | 2.19 / 0.46 / 0.19% |
| CPU held, % performance | 126-128 | 126-128 | 127 | 127 |
| Spin, % of one core (combat / outdoor / hub) | 18-22 / 21-25 / 35-37 | 19 / 22 / 36 | none | 20 in combat after 101 s; none |
| Package power held, W (combat / outdoor / hub) | 53-58 / 56-59 / 48-51 | 57.2 / 57.8 / 49.4 | 51.2 / 53.9 / 46.2 | 55.3 / 55.8 / 47.3 |

- **The yield does not cost the hold**: the clock stays at 126 to 128% held and every number is within the kept
  build's spread, so the spin yields at every turn.
- On this CPU-bound PC the waits are mostly under 5 ms: the timer sleeps rarely ran (the 10 s lines count 0 to
  5 sleeps), and the spin is the wait, as before.
- **High performance**: the fixed run's combat hold fell behind once at 101 s (one hitch), so the spin ran for
  the rest of that run (20% of one core, package 55.3 W against 51.2 W in the kept run, which never fell
  behind); outdoor turned it on after its route. The held repeats did not change.
- The game presents from several threads, so each has its own timer; the timer's kind is logged once.

## Limits

- One Intel desktop CPU on Balanced, the OpenXR simulator, rig only. An AMD CPU (CPPC under Windows' own
  control) or a hybrid-core Intel CPU may lower its clock differently while the thread sleeps, more or less than
  here; a laptop pays the spin in battery and heat. Not measured in a headset.
- The spin's cost is the wait, at most 5 ms a period: 18 to 37% of one core here, where the tick nearly fills the
  period. A PC that falls behind once and then runs far below the period would spin up to 5 ms of every period
  (45% of a core at 90 Hz). A PC whose clock does not fall would not spin at all.
- `present-latency.md` (2026-10-30) put the old hold's repeats on this PC down to the game's priority; its runs
  were on Balanced too, and the clock falling while held likely explains part of them [inferred].

## Latch at the second fall

The first version latched the spin at the first fall in play. On High performance the combat route latched at
101 s from a single hitch: 20% of a core, about 4 W more, no frame gain. Now the spin latches at the second
fall within 120 s (`HoldSwitch::kLatchWindowSeconds`). Why 120 s, from the timings: a fall takes two bad
windows (2 s or more), a let-go lasts at least the back-off (1 s, doubling to 32 s while holds do not last 10 s),
so a game that cannot keep up falls again within about 10 s and at the latest within a minute; a single hitch
falls once. A menu, a loading screen or the unseen hold (T-228) forgets the first fall: the hitch around a load
is not evidence. Unit tests: `hold_switch_tests.cpp` (one fall latches nothing, repeated falls latch and stay,
two falls further apart than the window do not, a menu or the unseen hold between falls forgets the first).
Rig, 2026-11-14 (my test PC, one suite per power plan, the three routes at the auto size, no switches; the
numbers of r8 above for comparison; analysis as for the other rows) [rig]:

| | Balanced, second-fall latch | Balanced, r8 (first-fall latch) | High performance, second-fall latch | High performance, r8 |
|---|---|---|---|---|
| Held, s of 70 (combat / outdoor / hub) | 67 / 70 / 70 | 69 / 70 / 70 | 69 / 70 / 70 | 69 / 70 / 70 |
| Missed periods (combat / outdoor / hub) | 2.30 / 0.81 / 0.24% | 2.13 / 0.35 / 0.48% | 1.33 / 0.41 / 0.07% | 2.19 / 0.46 / 0.19% |
| Spin, % of a core (combat / outdoor / hub) | 18 / 23 / 37 | 19 / 22 / 36 | none in any route | |
| Latch in the log (s into the run) | 24.6 / 22.2 / 46.5 | | none of the three | |
| CPU held, % performance | 128 / 127 / 127 | 126-128 | 127 / 127 / 127 | 127 |

Balanced: the PC that needs the spin latched in all three routes before the measured route (the second fall
came within about 25 s of the first in each), held 67 to 70 s, and the spin cost is the same as before; combat
held 67 s against 69 (the kept build's four runs spanned 67 to 70), outdoor's missed periods 0.81% against
0.35% (earlier runs 0.33 to 0.63%): one run each, so within what I cannot tell from run-to-run spread.
High performance: no latch in any of the three runs, so no spin; held 69 to 70 s, missed periods no higher than r8's.
