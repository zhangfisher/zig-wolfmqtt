# Build System Quick Reference

## Directory Structure

```
build.zig              # Main coordinator (83 lines)
build/
├── utils/             # Shared utilities
│   ├── options.zig    # Build options parsing
│   ├── modules.zig    # Module handling
│   ├── readme.zig     # README generation
│   └── common_steps.zig # Common steps
├── entries/           # Build entry points
│   ├── wolfmqtt.zig   # Library build
│   └── broker.zig     # Broker build
├── cli.zig            # CLI parser (reserved)
└── steps/             # Custom steps (reserved)
```

## Quick Commands

### Build Components
```bash
zig build                    # Build all (default)
zig build wolfmqtt          # Build library only
zig build broker            # Build broker only
```

### Using -Dcommand Parameter
```bash
zig build -Dcommand=all
zig build -Dcommand=wolfmqtt
zig build -Dcommand=broker
```

### Common Options
```bash
# Feature flags
-Dtls=true                  # Enable TLS
-Dv5=true                   # Enable MQTT v5
-Dmt=true                   # Enable multithreading
-Dmqtt-sn=true              # Enable MQTT-SN
-Dbroker=false              # Disable broker

# Build configuration
-Dstatic-link=true          # Static linking (Linux)
-Dtarget=aarch64-linux-gnu  # Cross-compile
-Doptimize=ReleaseFast      # Optimization level
```

### Utility Steps
```bash
zig build fmt               # Format code
zig build fmt-check         # Check formatting
```

## Adding New Components

1. Create `build/entries/mycomponent.zig`:
```zig
const std = @import("std");
const options_mod = @import("../utils/options.zig");

pub fn build(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    opts: options_mod.BuildOptions,
) void {
    // Your build logic
}
```

2. Import in `build.zig`:
```zig
const mycomponent_entry = @import("build/entries/mycomponent.zig");
```

3. Add to switch statement:
```zig
switch (command) {
    .mycomponent => {
        mycomponent_entry.build(b, target, optimize, opts);
    },
    // ...
}
```

4. Create step:
```zig
const mycomponent_step = b.step("mycomponent", "Build my component");
mycomponent_step.dependOn(&install_step.step);
```

## Output Structure

```
zig-out/
├── wolfmqtt/linux-arm/
│   ├── libwolfmqtt.a
│   └── wolfmqtt-linux-arm.md
└── broker/linux-arm/
    ├── mqtt_broker-gnueabihf
    ├── mqtt_broker-musleabihf
    └── mqtt_broker-*.md
```

## Key Benefits

✅ **Modular**: Each component in separate file  
✅ **Maintainable**: Easy to understand and modify  
✅ **Extensible**: Simple to add new components  
✅ **Documented**: Auto-generated README files  
✅ **Compatible**: All original options preserved  
