# Pico Tool (focused Apple-only tooling)

This repository is intentionally slimmed down to only the tools I authored:

- `reboot_tool`
- `load_tool`
- `load_tool_iokit`

Everything else from the original fork has been removed so these tools can stand alone, stay easy to reason about, and be used without pulling in a larger cross-platform stack.

## Why these tools exist

The goal of this project is to provide efficient, small, **Apple-official API** ways to perform these features on macOS. Instead of mixing platform-specific work into a broad, cross-platform toolchain, these utilities focus on the native Apple surfaces (like IOKit where appropriate) and keep the codebase focused and lightweight.

That narrow scope makes the tools:

- **Faster to build and inspect** (fewer dependencies and less code).
- **Easier to audit** (no unrelated platform code paths).
- **More reliable on macOS** (built on official Apple APIs rather than compatibility layers).

## Tool overview

### `reboot_tool`
A focused utility for reboot-related workflows in this ecosystem. It is designed to do one job well and relies on macOS-native APIs where possible.

### `load_tool`
A lightweight loader tool intended to perform its specific load workflow without any cross-platform scaffolding. It exists to keep the workflow direct and macOS-native.

### `load_tool_iokit`
A variant of the loader that uses IOKit for device interaction, emphasizing the “small, official Apple API” goal even further.

## Why keep them standalone?

These tools are meant to stay **small, inspectable, and macOS-specific**. Pulling in a broader, cross-platform project makes it harder to understand the codepaths that matter, and it adds extra layers that aren’t necessary when the primary platform is macOS. Keeping them standalone makes the codebase more focused and more useful for Apple-first workflows.

## Built with Codex

This work was done using **Codex**, and the result is both impressive and concerning. It’s impressive because it enables rapid, high-quality changes across a codebase with minimal friction. It’s concerning because it demonstrates how quickly complex software can be reshaped, which raises questions about velocity, review rigor, and the long-term impacts on the software industry.

We should be very careful and deliberate about how tools like this are used. The ability to do complex work quickly is powerful, but it also demands stronger discipline around review, intent, and accountability.
