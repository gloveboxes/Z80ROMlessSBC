---
name: banked-memory-apps
description: 'Use for DCC/dccmake banked-memory CP/M applications: bank-0 hosts, bank-1 .LIB services, BANKSDK, generated typed APIs, copied buffers/records, opaque handles, lifecycle, ABI and heap/stack budgets, and LIBDEMO validation.'
---

# Banked-Memory Applications

## Workflow

1. Read the [design and ABI gates](./references/workflow.md#1-partition-the-application)
   and its linked specification before partitioning the application.
2. Keep the host/console in bank 0 and service-owned objects in bank 1.
   Use synchronous, non-reentrant calls with counted copies or opaque handles;
   never pass bank-0 pointers as bank-1 addresses.
3. Follow the [contract workflow](./references/workflow.md#2-implement-the-service-and-contract).
   Reuse BANKSDK and generated adapters; do not hand-edit contracts or invent
   another bank-switch/marshalling mechanism. Define ownership and failure cleanup.
4. [Generate and build both sides](./references/workflow.md#3-generate-and-build)
   with the actual DCC checkout. Check linked image, BSS, heap, stack and frame
   budgets rather than assuming bank capacity is free heap.
5. Run the [application checks](./references/workflow.md#4-validate-the-application):
   boundary/error cases, output preservation, stale handles, repeated lifecycle
   and file-state interactions. LIBDEMO requires all eight client checks to pass.
6. Report artifacts, measured budgets and observed results. Contract generation
   alone is not an execution test; emulator success is not hardware qualification.

Use [firmware engineering](../z80-romless-sbc/SKILL.md) for BIOS, loader, memory
layout or ABI changes, and [disk flashing](../flash-cpm-disks/SKILL.md) only for
authorized deployment. Application-only changes do not require firmware flashing.
