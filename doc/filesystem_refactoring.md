# Filesystem Refactoring Plan

## Overview

This document describes the refactoring of the filesystem implementation in FMRuby Core to eliminate dependencies on FatFS/VFS and use a HAL-based abstraction layer.

## Background

### Current Issues

1. **VM Isolation Problem**: Each mruby VM has its own `VFS::VOLUMES` array, preventing filesystem state sharing across VMs
2. **Unnecessary Complexity**: FatFS device prefixes (e.g., `"0:"`) are not needed on Linux
3. **Fork-Specific Implementation**: Current FatFS implementation is inherited from upstream picoruby-esp32
4. **Future Requirements**: Need to support LittleFS on ESP32, SDCard, and original RAMFS

### Goals

- Enable `require "/lib/my_lib"` functionality (dynamic file loading)
- Provide unified File API across Linux and ESP32 platforms
- Use HAL layer for platform abstraction
- Support future filesystem additions (SDCard, RAMFS)

## Architecture

### New Design

```
┌─────────────────────────────────────────┐
│  Application Layer (Ruby)               │
│  - require "/lib/my_lib"                │
│  - File.file?, File.exist?, etc.        │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│  picoruby-fmrb-filesystem (New mrbgem)  │
│  ├─ mrblib/file.rb                      │
│  │  └─ Ruby implementations             │
│  │     (expand_path, basename, etc.)    │
│  └─ src/fmrb_filesystem.c               │
│     └─ C extensions calling HAL         │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│  fmrb_hal_file (HAL Layer)              │
│  ├─ fmrb_hal_file.h (API definition)    │
│  ├─ fmrb_hal_file_posix.c (Linux)       │
│  │  └─ POSIX stat/open/read/write       │
│  └─ fmrb_hal_file_esp32.c (Future)      │
│     └─ LittleFS API                     │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│  OS/Hardware Layer                      │
│  ├─ Linux: POSIX filesystem             │
│  └─ ESP32: LittleFS on Flash/SDCard    │
└─────────────────────────────────────────┘
```

### Component Responsibilities

#### picoruby-fmrb-filesystem (New)

**Purpose**: Provide File class API for FMRuby applications

**Ruby Layer** (`mrblib/file.rb`):
- Pure Ruby path manipulation methods
  - `File.expand_path(path, base)` - Path normalization
  - `File.basename(path, suffix)` - Extract filename
  - `File.dirname(path, level)` - Extract directory
  - `File.join(*parts)` - Path concatenation

**C Extension** (`src/fmrb_filesystem.c`):
- Thin wrapper calling HAL functions
  - `File.file?(path)` → `fmrb_hal_file_stat()`
  - `File.exist?(path)` → `fmrb_hal_file_stat()`
  - `File.directory?(path)` → `fmrb_hal_file_stat()`
  - `File.open(path, mode)` → `fmrb_hal_file_open()`

#### fmrb_hal_file (HAL Layer)

**Purpose**: Abstract filesystem operations across platforms

**API** (`fmrb_hal_file.h`):
```c
typedef struct {
  uint32_t mode;   // File mode (POSIX-like)
  uint64_t size;   // File size in bytes
  time_t mtime;    // Last modification time
} fmrb_hal_stat_t;

// File status
int fmrb_hal_file_stat(const char *path, fmrb_hal_stat_t *st);

// File operations
int fmrb_hal_file_open(const char *path, int flags, int mode);
ssize_t fmrb_hal_file_read(int fd, void *buf, size_t count);
ssize_t fmrb_hal_file_write(int fd, const void *buf, size_t count);
int fmrb_hal_file_close(int fd);

// Directory operations
int fmrb_hal_file_mkdir(const char *path, int mode);
int fmrb_hal_file_rmdir(const char *path);
int fmrb_hal_file_unlink(const char *path);
```

**Linux Implementation** (`fmrb_hal_file_posix.c`):
- Use POSIX `stat()`, `open()`, `read()`, `write()`, `close()`
- Path prefix handling: prepend `"flash/"` to all paths

**ESP32 Implementation** (Future: `fmrb_hal_file_esp32.c`):
- Use LittleFS API
- Support multiple mount points

## Implementation Plan

### Phase 1: Documentation ✓

- [x] Create `doc/filesystem_refactoring.md` (this document)
- [ ] Create `doc/filesystem_api.md` (API specification)

### Phase 2: HAL Layer Extension

Files to modify:
- `components/fmrb_hal/include/fmrb_hal_file.h`
- `components/fmrb_hal/platform/posix/fmrb_hal_file_posix.c`

Tasks:
1. Add `fmrb_hal_stat_t` struct definition
2. Add `fmrb_hal_file_stat()` function declaration
3. Implement `fmrb_hal_file_stat()` in POSIX version
4. Remove device prefix handling (no longer needed)
5. Remove debug logging

### Phase 3: Create picoruby-fmrb-filesystem

Directory structure:
```
lib/mrbgem/picoruby-fmrb-filesystem/
├── mrbgem.rake
├── mrblib/
│   └── file.rb
└── src/
    ├── fmrb_filesystem.c
    └── fmrb_filesystem.h
```

Files to create:
1. `lib/mrbgem/picoruby-fmrb-filesystem/mrbgem.rake`
   - Gem specification
   - Conflict declarations (fat, vfs, posix-io)

2. `lib/mrbgem/picoruby-fmrb-filesystem/mrblib/file.rb`
   - Port Ruby implementations from `picoruby-posix-io`
   - `File.expand_path`, `File.basename`, `File.dirname`, `File.join`

3. `lib/mrbgem/picoruby-fmrb-filesystem/src/fmrb_filesystem.c`
   - C extensions calling HAL
   - `File.file?`, `File.exist?`, `File.directory?`, `File.size`

### Phase 4: Build Configuration Changes

Files to modify:
- `lib/add/family_mruby_linux.rb`
- `components/picoruby-esp32/picoruby/build_config/family_mruby_linux.rb`
- `Rakefile`

Tasks:
1. Remove `picoruby-filesystem-fat` from build config
2. Remove `picoruby-vfs` from build config
3. Add `picoruby-fmrb-filesystem` to build config
4. Remove VFS patch from `lib/patch/picoruby-vfs/`
5. Remove VFS patch copy logic from `Rakefile`

### Phase 5: Build and Test

Commands:
```bash
rake clean
rake setup
rake build:linux
```

Verification:
- Test `require "/lib/my_lib"` functionality
- Verify `File.file?()` works correctly
- Check `File.expand_path()` behavior

## Files to Remove

- `lib/patch/picoruby-vfs/vfs.rb` (debug patch)
- VFS patch copy logic in `Rakefile`

## Files to Modify

- `lib/add/family_mruby_linux.rb`
- `components/picoruby-esp32/picoruby/build_config/family_mruby_linux.rb`
- `components/fmrb_hal/include/fmrb_hal_file.h`
- `components/fmrb_hal/platform/posix/fmrb_hal_file_posix.c`
- `Rakefile`

## Files to Create

- `doc/filesystem_api.md`
- `lib/mrbgem/picoruby-fmrb-filesystem/mrbgem.rake`
- `lib/mrbgem/picoruby-fmrb-filesystem/mrblib/file.rb`
- `lib/mrbgem/picoruby-fmrb-filesystem/src/fmrb_filesystem.c`
- `lib/mrbgem/picoruby-fmrb-filesystem/src/fmrb_filesystem.h`

## Benefits

1. **Unified Platform Abstraction**: HAL layer handles Linux/ESP32 differences
2. **VM Independence**: No global state in VFS, works across multiple VMs
3. **Future Extensibility**: Easy to add SDCard, RAMFS support
4. **Clean Dependencies**: No picoruby-posix-io wrapper, direct HAL usage
5. **Maintainability**: FMRuby-specific implementation, not dependent on upstream

## Migration Notes

### For ESP32 Target

Currently, ESP32 still uses FatFS. Future migration steps:
1. Implement `fmrb_hal_file_esp32.c` using LittleFS API
2. Update `family_mruby_esp32.rb` to use `picoruby-fmrb-filesystem`
3. Remove FatFS/VFS dependencies from ESP32 build

### For Application Code

No changes required. File API remains compatible:
```ruby
# All these continue to work
File.file?("/lib/my_lib.rb")
File.exist?("/etc/config.yml")
File.expand_path("../foo.rb", __FILE__)
require "/lib/my_lib"
```

## Timeline

- Phase 1 (Documentation): Complete
- Phase 2 (HAL Extension): In progress
- Phase 3 (New mrbgem): Pending
- Phase 4 (Build Config): Pending
- Phase 5 (Test): Pending

## References

- Original issue: `require "/lib/my_lib"` failing with LoadError
- Root cause: VFS not mounted, device prefix handling issues
- Solution: HAL-based filesystem abstraction
