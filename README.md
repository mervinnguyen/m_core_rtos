<div align="center">

# M-Core RTOS

**A Real-Time Operating System built from scratch on ARM Cortex-M**

[![Platform](https://img.shields.io/badge/platform-ARM%20Cortex--M-blue?style=flat-square)](.)
[![Language](https://img.shields.io/badge/language-C%20%7C%20ASM-00d4aa?style=flat-square)](.)
[![Target](https://img.shields.io/badge/target-STM32F4-orange?style=flat-square)](.)
[![Toolchain](https://img.shields.io/badge/toolchain-arm--none--eabi--gcc-green?style=flat-square)](.)
[![License](https://img.shields.io/badge/license-MIT-purple?style=flat-square)](LICENSE)

[**📄 Full Documentation & Architecture Diagrams →**](https://your-handle.github.io/m-core-rtos)

</div>

---

A ground-up implementation of a preemptive real-time operating system targeting ARM Cortex-M microcontrollers. Implements round-robin, cooperative, and periodic schedulers with PendSV-based context switching, a thread control block subsystem, and binary semaphore synchronization — built directly on top of SysTick and NVIC hardware primitives.

---

## Table of Contents

- [System Architecture](#system-architecture)
- [Context Switching](#context-switching)
- [Thread Control Block](#thread-control-block)
- [Scheduler Implementations](#scheduler-implementations)
- [Kernel API](#kernel-api)
- [Boot Sequence](#boot-sequence)
- [Repository Structure](#repository-structure)
- [Build & Flash](#build--flash)

---

## System Architecture

The system is structured as four strict layers. Each layer has a single responsibility and communicates only with the layer directly below it — a dependency hierarchy that mirrors production-grade embedded OS design.

```
┌─────────────────────────────────────────────┐
│              L4 — Application               │
│        User Tasks / Threads / App Logic     │
├─────────────────────────────────────────────┤
│              L3 — RTOS Kernel               │
│  ┌─────────────┐  ┌──────────────────────┐  │
│  │  Scheduler  │  │   Context Switcher   │  │
│  │  RR / Coop  │  │   PendSV Handler     │  │
│  │  Periodic   │  │   R4–R11 save/restore│  │
│  └─────────────┘  └──────────────────────┘  │
│  ┌─────────────┐  ┌──────────────────────┐  │
│  │ TCB Manager │  │  Semaphore / IPC     │  │
│  │ Circular LL │  │  Binary counting     │  │
│  └─────────────┘  └──────────────────────┘  │
├─────────────────────────────────────────────┤
│              L2 — Driver Layer              │
│   LED (debug)  │  UART (log)  │  Timebase  │
│                               │  SysTick   │
├─────────────────────────────────────────────┤
│          L1 — Cortex-M Hardware             │
│    SysTick    │    PendSV    │    NVIC      │
│  (sched tick) │ (ctx switch) │ (priorities) │
└─────────────────────────────────────────────┘
```

> **Design Principle:** SysTick, PendSV, and NVIC are the three hardware primitives the entire kernel is built on. Every scheduling decision ultimately flows through these three peripheral interfaces.

---

## Scheduler Execution Flow

SysTick fires at a fixed 1 ms interval. Scheduling logic runs in the SysTick ISR; the actual register swap is deferred to PendSV — the lowest-priority interrupt on the system — ensuring context switches never preempt device driver ISRs.

```
SysTick IRQ (1ms)
      │
      ▼
increment sys_tick
      │
      ▼
scheduler evaluate → select next TCB
      │
      ▼
set PendSV pending (SCB→ICSR)
      │
      ▼  [lowest priority — fires after all higher ISRs complete]
PendSV Handler
      │
      ├── save R4–R11 of current task
      ├── store PSP → current TCB
      ├── load PSP ← next TCB
      └── restore R4–R11 of next task
            │
            ▼
        exception return → next task resumes
```

---

## Context Switching

The Cortex-M hardware exception mechanism saves a partial register frame automatically on interrupt entry. The PendSV handler manually saves the remaining callee-saved registers, completing the full architectural context before switching the process stack pointer.

```
Running Task                     Next Task
──────────────                   ──────────────
 xPSR  ◄─┐                      ┌─►  xPSR
 PC    ◄──┤  hardware auto-save  ├──►  PC
 LR    ◄──┤  (exception entry)   ├──►  LR
 R12   ◄──┤                      ├──►  R12
 R3–R0 ◄──┘                      └──►  R3–R0
                  PendSV
 R11–R4 ◄─────── manual ────────────► R11–R4
 PSP    ◄── saved to TCB / loaded from TCB ──► PSP
```

```c
__attribute__((naked)) void PendSV_Handler(void) {
    __asm volatile (
        "MRS     R0, PSP            \n"  /* load PSP of running task  */
        "STMDB   R0!, {R4-R11}      \n"  /* push callee-saved regs    */
        "LDR     R1, =currentTCB    \n"
        "LDR     R2, [R1]           \n"
        "STR     R0, [R2]           \n"  /* save new SP into TCB      */
        "BL      os_scheduler       \n"  /* select next task          */
        "LDR     R2, [R1]           \n"
        "LDR     R0, [R2]           \n"  /* restore SP from next TCB  */
        "LDMIA   R0!, {R4-R11}      \n"  /* pop callee-saved regs     */
        "MSR     PSP, R0            \n"
        "BX      LR                 \n"  /* exception return          */
    );
}
```

---

## Thread Control Block

Each thread is fully described by its TCB. The stack pointer is always the **first field** — this ensures zero-offset access from assembly without struct member arithmetic, a hard requirement for correct context switching from the PendSV handler.

```c
typedef enum {
    THREAD_READY   = 0x00,
    THREAD_BLOCKED = 0x01,
    THREAD_SLEEP   = 0x02,
} thread_state_t;

typedef struct tcb {
    uint32_t       *stack_ptr;    /* MUST be first — accessed from asm  */
    uint32_t        priority;     /* scheduler priority (0 = highest)   */
    thread_state_t  state;        /* READY | BLOCKED | SLEEP            */
    uint32_t        delay_ticks;  /* tick count until wake              */
    struct tcb     *next;         /* circular linked list pointer       */
    const char     *name;         /* debug identifier                   */
} tcb_t;
```

> **Critical:** Each thread stack must be pre-initialized with a fake exception frame before `os_launch()`. The PC field must point to the thread function; xPSR bit 24 (Thumb) must be set. Without this, the first exception return faults immediately.

---

## Scheduler Implementations

| Scheduler | Preemption | Trigger | Use Case | Tradeoff |
|---|---|---|---|---|
| **Round Robin** | ✅ Yes | SysTick (1 ms) | Equal-priority task fairness | Jitter; no priority differentiation |
| **Cooperative** | ❌ No | Explicit `os_yield()` | Deterministic, low-overhead | Starvation if tasks don't yield |
| **Periodic** | ✅ Yes | Hardware timer (configurable) | Hard real-time, control loops | Strict period budgeting required |

---

## Kernel API

```c
/* ── Kernel Lifecycle ─────────────────────────────────────────────── */
void    os_init    (void);                      /* init kernel data structures     */
void    os_launch  (void);                      /* start scheduler — never returns */

/* ── Thread Management ────────────────────────────────────────────── */
void    os_add_thread (void (*fn)(void),
                       uint32_t *stack,
                       uint32_t  stack_size,
                       uint32_t  priority);

/* ── Scheduling Primitives ────────────────────────────────────────── */
void    os_yield   (void);                      /* cooperative yield               */
void    os_sleep   (uint32_t ticks);            /* block for N ticks               */

/* ── Synchronization ──────────────────────────────────────────────── */
void    os_sem_init   (semaphore_t *s, int val);
void    os_sem_wait   (semaphore_t *s);         /* acquire — blocks if count == 0  */
void    os_sem_signal (semaphore_t *s);         /* release — unblocks waiting thread */
```

---

## Boot Sequence

```
Reset Handler → Stack Init → Vector Table → main()
                                               │
                                          os_init()
                                               │
                                      os_add_thread() × N
                                               │
                                          os_launch()  ← never returns
                                               │
                                      [first task runs]
```

`os_launch()` sets the PSP to the first TCB's stack, drops to unprivileged thread mode, and triggers the initial context switch. Execution continues inside the thread pool indefinitely.

---

## Repository Structure

```
m-core-rtos/
├── kernel/
│   ├── os_core.c              # os_init, os_launch, thread management
│   ├── os_core.h
│   ├── scheduler_rr.c         # round-robin scheduler
│   ├── scheduler_coop.c       # cooperative scheduler
│   ├── scheduler_periodic.c   # periodic scheduler
│   ├── pendsv_handler.s       # assembly context switch
│   ├── tcb.h                  # thread control block definition
│   └── semaphore.c/h          # synchronization primitives
│
├── drivers/
│   ├── led.c/h                # GPIO LED — task debug output
│   ├── uart.c/h               # UART logging layer
│   └── timebase.c/h           # SysTick config + sys_tick counter
│
├── app/
│   └── main.c                 # thread definitions + os_launch entry
│
├── startup/
│   ├── startup_stm32.s        # vector table + reset handler
│   └── linker.ld              # memory layout — flash + SRAM regions
│
└── README.md
```

---

## Build & Flash

```bash
# Clone
git clone https://github.com/your-handle/m-core-rtos.git
cd m-core-rtos

# Build with ARM toolchain
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -O2 \
    -T startup/linker.ld \
    -o m-core-rtos.elf \
    kernel/*.c drivers/*.c app/main.c startup/startup_stm32.s

# Flash via OpenOCD (ST-Link)
openocd -f interface/stlink.cfg \
        -f target/stm32f4x.cfg \
        -c "program m-core-rtos.elf verify reset exit"
```

**Requirements:** `arm-none-eabi-gcc`, `openocd`, STM32F4 board, ST-Link debugger.

---

<div align="center">

Built on ARM Cortex-M4 · STM32F4 · GNU arm-none-eabi-gcc · OpenOCD · STM32CubeIDE

</div>
