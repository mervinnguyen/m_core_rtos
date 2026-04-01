<div align="center">

# M-Core RTOS

**A Real-Time Operating System built from scratch on ARM Cortex-M**

[![Platform](https://img.shields.io/badge/platform-ARM%20Cortex--M-blue?style=flat-square)](.)
[![Language](https://img.shields.io/badge/language-C%20%7C%20ASM-00d4aa?style=flat-square)](.)
[![Target](https://img.shields.io/badge/target-STM32F4-orange?style=flat-square)](.)
[![Toolchain](https://img.shields.io/badge/toolchain-arm--none--eabi--gcc-green?style=flat-square)](.)
[![License](https://img.shields.io/badge/license-MIT-purple?style=flat-square)](LICENSE)

</div>

---

A ground-up implementation of a preemptive real-time operating system targeting ARM Cortex-M microcontrollers. Implements a round-robin scheduler with SysTick-driven context switching, a thread control block subsystem, and binary semaphore synchronization, built directly on top of SysTick and NVIC hardware primitives.

Periodic task execution and cooperative yielding are both supported, not as separate scheduler modes, but as deliberate extensions layered on top of the single round-robin core. Periodic callbacks are driven by a tick counter inside the scheduler itself; cooperative behavior is opt-in via `osThreadYield()`, which any thread can call to surrender its quanta early.

---

## Table of Contents

- [System Architecture](#system-architecture)
- [Context Switching](#context-switching)
- [Thread Control Block](#thread-control-block)
- [Scheduler](#scheduler)
- [Semaphore Synchronization](#semaphore-synchronization)
- [Kernel API](#kernel-api)
- [Boot Sequence](#boot-sequence)
- [Build & Flash](#build--flash)

---

## System Architecture

The system is structured as four strict layers. Each layer has a single responsibility and communicates only with the layer directly below it.

```
┌─────────────────────────────────────────────┐
│              L4 — Application               │
│        User Tasks / Threads / App Logic     │
├─────────────────────────────────────────────┤
│              L3 — RTOS Kernel               │
│  ┌─────────────┐  ┌──────────────────────┐  │
│  │  Scheduler  │  │   Context Switcher   │  │
│  │  Round-Robin│  │   SysTick Handler    │  │
│  │             │  │   R4–R11 save/restore│  │
│  └─────────────┘  └──────────────────────┘  │
│  ┌─────────────┐  ┌──────────────────────┐  │
│  │ TCB Manager │  │  Semaphore / IPC     │  │
│  │ Circular LL │  │  Binary / counting   │  │
│  └─────────────┘  └──────────────────────┘  │
├─────────────────────────────────────────────┤
│              L2 — Driver Layer              │
│   LED (debug)  │  UART (log)  │  Timebase   │
│                               │  SysTick    │
├─────────────────────────────────────────────┤
│          L1 — Cortex-M Hardware             │
│    SysTick    │    TIM2      │    NVIC      │
│  (sched tick) │  (1 Hz ISR)  │ (priorities) │
└─────────────────────────────────────────────┘
```

> **Design note:** SysTick drives both scheduling and context switching. TIM2 provides an independent 1 Hz periodic interrupt for application-level timing. NVIC manages interrupt priorities so the SysTick handler always runs at the lowest kernel priority (15), preventing it from preempting device driver ISRs.

---

## Scheduler Execution Flow

SysTick fires every 10 ms (configurable via `QUANTA`). Scheduling logic and the context switch both execute inside the SysTick ISR. The switch is triggered manually by writing to `INTCTRL` (the `ICSR` register) to set the `PENDSTSET` bit, which re-pends SysTick when a thread calls `osThreadYield()`.

```
SysTick IRQ (every QUANTA ms)
      │
      ▼
Disable global interrupts (CPSID I)
      │
      ▼
Push R4–R11 of current task onto MSP
      │
      ▼
Save MSP → currentPt->stackPt
      │
      ▼
osSchedulerRoundRobin()
  └── currentPt = currentPt->next
      │
      ▼
Load MSP ← currentPt->stackPt
      │
      ▼
Pop R4–R11 of next task
      │
      ▼
Enable global interrupts (CPSIE I)
      │
      ▼
BX LR → exception return → next task resumes
```

---

## Context Switching

The Cortex-M hardware exception mechanism automatically saves a partial register frame on interrupt entry (R0–R3, R12, LR, PC, xPSR). The SysTick handler manually saves and restores the remaining callee-saved registers (R4–R11), completing the full architectural context before swapping the stack pointer.

```
Current Task Stack (MSP)         Next Task Stack (MSP)
────────────────────             ─────────────────────
 xPSR  ◄─┐                      ┌─►  xPSR
 PC    ◄──┤  hardware auto-save  ├──►  PC
 LR    ◄──┤  (exception entry)   ├──►  LR
 R12   ◄──┤                      ├──►  R12
 R3–R0 ◄──┘                      └──►  R3–R0

 R11–R4 ◄──── manual PUSH/POP ──────► R11–R4
 SP ◄──── saved/loaded via TCB.stackPt ──► SP
```

The handler is declared `__attribute__((naked))` so the compiler emits no prologue or epilogue — register state is managed entirely by the inline assembly:

```c
__attribute__((naked)) void SysTick_Handler(void) {
    __asm("CPSID  I");               /* disable interrupts               */
    __asm("PUSH   {R4-R11}");        /* save callee-saved registers      */
    __asm("LDR    R0, =currentPt");  /* R0 = &currentPt                  */
    __asm("LDR    R1, [R0]");        /* R1 = currentPt                   */
    __asm("STR    SP, [R1]");        /* currentPt->stackPt = SP          */
    __asm("PUSH   {R0, LR}");
    __asm("BL     osSchedulerRoundRobin");  /* select next TCB           */
    __asm("POP    {R0, LR}");
    __asm("LDR    R1, [R0]");        /* R1 = new currentPt               */
    __asm("LDR    SP, [R1]");        /* SP = next task's stackPt         */
    __asm("POP    {R4-R11}");        /* restore callee-saved registers   */
    __asm("CPSIE  I");               /* enable interrupts                */
    __asm("BX     LR");              /* exception return                 */
}
```

> **Note:** The MSP (Main Stack Pointer) is used for all thread stacks in this implementation. A production kernel would move threads to the PSP (Process Stack Pointer) to fully separate kernel and thread stack spaces.

---

## Thread Control Block

Each thread is described by a TCB containing its saved stack pointer and a pointer to the next TCB in the circular linked list. The stack pointer is always the **first field**, this ensures zero-offset access from assembly without struct member arithmetic.

```c
typedef struct tcb {
    int32_t    *stackPt;   /* MUST be first — accessed directly from asm */
    struct tcb *next;      /* circular linked list pointer               */
} tcbType;
```

Each thread is allocated a fixed stack of `STACKSIZE` (400) words = 1600 bytes. Before launch, each stack is pre-initialized with a fake exception frame so the first context switch returns cleanly into the thread function:

```c
void osKernelStackInit(int i) {
    tcbs[i].stackPt = &TCB_STACK[i][STACKSIZE - 16];

    TCB_STACK[i][STACKSIZE - 1] = (1u << 24);    /* xPSR: Thumb bit set */
    TCB_STACK[i][STACKSIZE - 2] = (int32_t)taskFn; /* PC: thread entry  */
    /* R14–R4 pre-filled with 0xAAAAAAAA for debug visibility            */
}
```

> **Critical:** xPSR bit 24 (Thumb mode) must be set. Without it, the first exception return causes an immediate hard fault on Cortex-M.

---

## Scheduler

The kernel is built on a single **preemptive round-robin** core. Periodic task execution and cooperative yielding are both supported, not as separate scheduler implementations, but as deliberate features layered on top of that one core. There is no mode switch; all three behaviors flow through the same SysTick interrupt and the same context switch path.

```
currentPt
    │
    ▼
 ┌──────┐    ┌──────┐    ┌──────┐
 │ tcb0 │───►│ tcb1 │───►│ tcb2 │─┐
 └──────┘    └──────┘    └──────┘ │
    ▲                             │
    └─────────────────────────────┘
```

### Round-robin (base)

On every SysTick the scheduler advances `currentPt` one step around the circular list. Every thread gets equal CPU time in fixed 10 ms slices.

### Periodic execution (layered feature)

A periodic callback is built directly into `osSchedulerRoundRobin()` using an internal tick counter. Every `PERIOD` scheduler ticks, a designated function pointer (`task3`) is invoked before advancing `currentPt`. This gives you a hard periodic execution cadence without a second scheduler or a second timer:

```c
void osSchedulerRoundRobin(void) {
    if ((++period_tick) == PERIOD) {
        (*task3)();        /* periodic callback fires every PERIOD ticks */
        period_tick = 0;
    }
    currentPt = currentPt->next;  /* round-robin advance always happens  */
}
```

> The periodic callback executes in the SysTick ISR context, so it must be kept short and non-blocking.

### Cooperative yield (layered feature)

Any thread can voluntarily surrender the remainder of its quanta by calling `osThreadYield()`. This is purely opt-in, the scheduler itself remains preemptive. The yield works by resetting the SysTick countdown and immediately re-pending the interrupt, routing straight back through the normal context switch:

```c
void osThreadYield(void) {
    SysTick->VAL = 0;       /* reset countdown so next quanta is full   */
    INTCTRL = PENDSTSET;    /* pend SysTick immediately                 */
}
```

No separate code path, no mode flag, cooperative behavior is just an early trigger of the same mechanism that preemption uses.

| Property | Value |
|---|---|
| Scheduling algorithm | Round-robin |
| Time quanta | 10 ms (configurable via `QUANTA`) |
| Preemption | Yes, SysTick fires every quanta |
| Periodic callbacks | Yes, tick counter inside `osSchedulerRoundRobin()` |
| Voluntary yield | Yes, `osThreadYield()`, opt-in per thread |
| Max threads | 3 (fixed, set by `NUM_OF_THREADS`) |

---

## Semaphore Synchronization

The kernel provides counting semaphores for inter-task synchronization. All semaphore operations disable global interrupts to ensure atomicity.

```c
void osSemaphoreInit(int32_t *semaphore, int32_t value);
void osSemaphoreWait(int32_t *semaphore);   /* blocks (spin) if count == 0 */
void osSemaphoreSet (int32_t *semaphore);   /* increments count            */
```

`osSemaphoreWait` uses a spin-wait loop that briefly re-enables interrupts on each iteration to allow SysTick to fire and context-switch to a thread that may signal the semaphore:

```c
void osSemaphoreWait(int32_t *semaphore) {
    __disable_irq();
    while (*semaphore <= 0) {
        __enable_irq();   /* allow context switch to unblocking thread */
        __disable_irq();
    }
    *semaphore -= 1;
    __enable_irq();
}
```

Example usage in `main.c` — task2 gates on `semaphore2`, then signals `semaphore1` to unblock task1:

```
semaphore2 (init=0)          semaphore1 (init=1)
      │                             │
      ▼                             ▼
  [task2 waits]               [task1 waits]
      │                             │
  [sem2 signalled]             [task2 signals sem1]
      │                             │
  valve_open() ────────────────► motor_run()
```

---

## Kernel API

```c
/* ── Kernel Lifecycle ─────────────────────────────────────────────── */
void    osKernelInit    (void);                    /* init MILLIS_PRESCALER           */
void    osKernelLaunch  (uint32_t quanta);         /* configure SysTick + start       */

/* ── Thread Management ────────────────────────────────────────────── */
uint8_t osKernelAddThreads (void(*task0)(void),
                            void(*task1)(void),
                            void(*task2)(void));   /* register 3 threads              */

/* ── Scheduling Primitives ────────────────────────────────────────── */
void    osThreadYield   (void);                    /* yield remainder of quanta       */
void    osSchedulerLaunch (void);                  /* drop into first thread (asm)    */

/* ── Synchronization ──────────────────────────────────────────────── */
void    osSemaphoreInit (int32_t *s, int32_t val);
void    osSemaphoreWait (int32_t *s);              /* acquire — spins if count == 0   */
void    osSemaphoreSet  (int32_t *s);              /* release — increments count      */
```

---

## Boot Sequence

```
Reset → main()
           │
      uart_tx_init()        — USART2 PA2, 115200 baud
           │
      tim2_1hz_interrupt_init() — 1 Hz periodic ISR
           │
      osSemaphoreInit() × 2 — semaphore1=1, semaphore2=0
           │
      osKernelInit()        — compute MILLIS_PRESCALER
           │
      osKernelAddThreads()  — init TCBs + stacks, link circular list
           │
      osKernelLaunch(QUANTA=10) — configure SysTick, set priority 15
           │
      osSchedulerLaunch()   — load first TCB stack, BX LR into task0
           │
      [thread pool runs indefinitely]
```

`osSchedulerLaunch()` manually unwinds the fake exception frame from the first TCB's stack and jumps into `task0`. From this point on, SysTick drives all further scheduling.

---

## Build & Flash

The recommended workflow is STM32CubeIDE, which handles the ARM toolchain, build system, and flashing through the STM32F411 Nucleo's built-in ST-Link, no external debugger or OpenOCD setup required.

### 1. Clone the repository

```bash
git clone https://github.com/mervinnguyen/m_core_rtos.git
```

### 2. Import into STM32CubeIDE

1. Open STM32CubeIDE
2. Go to **File → Import**
3. Select **General → Existing Projects into Workspace** and click **Next**
4. Under **Select root directory**, click **Browse** and navigate to the cloned `m-core-rtos` folder
5. The project should appear in the **Projects** list with its checkbox ticked
6. Click **Finish**

> If the project does not appear, ensure the folder contains a `.project` and `.cproject` file. These are the Eclipse project descriptors STM32CubeIDE requires to recognise the project.

### 3. Build

1. In the **Project Explorer**, right-click the project and select **Build Project**, or press **Ctrl+B** (Windows/Linux) / **Cmd+B** (macOS)
2. The **Console** panel at the bottom will show the build output — confirm it ends with `Build Finished` and **0 errors**
3. The compiled `.elf` binary is written to the `Debug/` folder inside the project directory

### 4. Flash and run

1. Connect the STM32F411 Nucleo board to your computer via USB — the Nucleo's built-in ST-Link appears as a COM port and debug probe simultaneously
2. In STM32CubeIDE, go to **Run → Debug Configurations**
3. Expand **STM32 Cortex-M C/C++ Application** and select the existing launch configuration for this project (or create one by double-clicking the entry)
4. Under the **Debugger** tab, confirm **ST-LINK (OpenOCD)** is selected and the interface is set to **SWD**
5. Click **Debug** — CubeIDE will flash the binary, reset the board, and halt at `main()`
6. Press **F8** (or the **Resume** button) to start full execution

To flash and run without the debugger attached, use **Run → Run** instead of **Run → Debug**.

### Requirements

- [STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html) (includes arm-none-eabi-gcc and OpenOCD)
- STM32F411 Nucleo board (built-in ST-Link — no external programmer needed)
- USB Type-A to Mini-B cable

---

<div align="center">

Built on ARM Cortex-M4 · STM32F411 · GNU arm-none-eabi-gcc · OpenOCD · STM32CubeIDE

</div>
