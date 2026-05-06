# Build System Refactoring

## Overview

The build system has been refactored to follow a modular architecture where `build.zig` serves as the coordination entry point, and the actual build logic is organized in the `build/` directory.

## Directory Structure

```
PROJECT
 | --- build.zig          # Main entry point (coordinator)
 |--- build/
         |--- utils/      # Common utility functions (one file per function)
         |     |--- options.zig      # Build options definition and parsing
         |     |--- modules.zig      # Module handling utilities
         |     |--- readme.zig       # README generation utilities
         |     |--- common_steps.zig # Common build steps (fmt, fmt-check)
         |
         |--- cli.zig     # CLI argument parsing (deprecated - logic moved to build.zig)
         |
         |--- entries/    # Build entry points for each component
              |--- broker.zig    # MQTT broker build logic
              |--- wolfmqtt.zig  # wolfMQTT library build logic
              |--- ......        # Additional entries can be added here
              
     └── steps/               # Optional: Complex custom steps (for future use)
          ├── run.zig
          ├── deploy.zig
           └── analyze.zig
```

## Usage

### Build All Components (Default)
```bash
zig build
```

### Build Specific Components
```bash
# Build only wolfMQTT library
zig build wolfmqtt

# Build only broker executable
zig build broker
```

### Build with Custom Options
```bash
# Enable TLS support
zig build -Dtls=true

# Enable MQTT-SN support
zig build -Dmqtt-sn=true

# Disable broker
zig build -Dbroker=false

# Static linking for Linux
zig build -Dstatic-link=true

# Specify target platform
zig build -Dtarget=aarch64-linux-gnu

# Combine multiple options
zig build -Dtls=true -Dv5=true -Dmt=true -Dtarget=arm-linux-gnueabihf
```

### Build Commands via `-Dcommand` Parameter
```bash
# Build all (default)
zig build -Dcommand=all

# Build only wolfMQTT
zig build -Dcommand=wolfmqtt

# Build only broker
zig build -Dcommand=broker
```

## Architecture Benefits

1. **Separation of Concerns**: Build coordination is separated from implementation details
2. **Modularity**: Each component has its own build entry point
3. **Reusability**: Common utilities are centralized in the `utils/` directory
4. **Maintainability**: Easier to add new components or modify existing ones
5. **Extensibility**: The `steps/` directory is reserved for complex custom build steps

## Adding New Components

To add a new build component:

1. Create a new file in `build/entries/` (e.g., `mycomponent.zig`)
2. Implement the build logic with a public `build()` function
3. Import and call it from `build.zig` in the appropriate switch case
4. Add a new step in `build.zig` if needed

Example:
```zig
// build/entries/mycomponent.zig
const std = @import("std");
const options_mod = @import("../utils/options.zig");

pub fn build(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    opts: options_mod.BuildOptions,
) void {
    // Your build logic here
}
```

## Migration Notes

- The old monolithic `build.zig` has been split into modular components
- All functionality remains the same
- Command-line interface is backward compatible
- The `cli.zig` file is currently not used but kept for potential future use
