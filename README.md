# BasicOS

A hobby 32-bit operating system for x86, written in C and assembly.
It boots with GRUB (Multiboot) and runs under QEMU. No standard library:
even `memcpy` and `strlen` are implemented in the kernel.

Current version: **0.4**

## Quick start

Prerequisites: a 32-bit-capable GCC toolchain (the build uses `-m32`),
GNU `ld`, `grub-mkrescue`, and QEMU (`qemu-system-i386`).

```sh
make run          # boot the kernel directly in QEMU
make run-serial   # also mirror screen output to the terminal via COM1
make              # build basicos.iso
make run-iso      # boot the ISO in QEMU
make clean
```

At the `basicos>` prompt, type `help` for the full command list.

## What it has

- **Boot:** Multiboot header, 16 KiB stack, flat GDT, IDT with 32
  exception handlers plus timer and keyboard interrupts.
- **Paging:** the first 16 MiB are identity-mapped with 4 KiB pages.
  Page 0 is deliberately unmapped, so a NULL dereference faults, and
  kernel code is mapped read-only (with CR0.WP, so that applies to
  the kernel itself).
- **Multitasking:** preemptive, round-robin scheduling at 100 Hz,
  up to 8 tasks, with `sleep` and tick-based wake-up.
- **Input:** PS/2 keyboard with a scancode table, Shift support, and
  command history on the up/down arrow keys.
- **Memory:** a 256 KiB first-fit heap with splitting and coalescing.
- **Filesystem:** an in-RAM filesystem with 16 files. It does not
  survive a reboot.
- **Clock and display:** RTC time from CMOS, a status bar with the
  time and task count, VGA text output, and COM1 serial output that
  mirrors the screen.
- **Shell:** `help`, `ver`, `clear`, `uptime`, `date`, `mem`,
  `history`, `reboot`, `halt`, `echo`, `color`, `sleep`,
  `spawn` / `ps` / `kill`, file commands (`ls`, `cat`, `write`,
  `append`, `cp`, `mv`, `rm`), and `crash null|code|div|ud`,
  which deliberately triggers a CPU exception so the handler can
  be observed.

## Layout

| File | Contents |
| --- | --- |
| `boot.s` | Multiboot header, entry point, IRQ and exception stubs |
| `kernel.c` | VGA/serial output, GDT/IDT, PIC/PIT, paging, keyboard, scheduler, heap, RAM filesystem, RTC, shell |
| `linker.ld` | Kernel layout, loaded at 1 MiB |
| `Makefile` | Build, ISO, and QEMU targets |

## Not yet

- Everything runs in ring 0: there is no user mode, syscall
  interface, or memory isolation between tasks yet.
- There is no disk driver; storage is RAM-only.

## License

MIT — see `LICENSE`.
