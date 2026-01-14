# Filesystem API Specification

## Overview

This document specifies the File class API provided by `picoruby-fmrb-filesystem` and the underlying HAL layer API.

## Ruby API (File Class)

### Class Methods Required by picoruby-require

#### `File.file?(path) → true or false`

Returns `true` if the named file exists and is a regular file.

```ruby
File.file?("/lib/my_lib.rb")  #=> true
File.file?("/lib")             #=> false (directory)
File.file?("/nonexistent")     #=> false
```

**Implementation**: C extension calling `fmrb_hal_file_stat()`

#### `File.expand_path(path, dir_string = '.') → string`

Converts a pathname to an absolute pathname. Relative paths are referenced from the current working directory of the process unless `dir_string` is given, in which case it will be used as the starting point.

```ruby
File.expand_path("..") #=> "/lib"
File.expand_path("foo.rb", "/bar") #=> "/bar/foo.rb"
File.expand_path("/foo/../bar") #=> "/bar"
File.expand_path("~/config.yml") #=> "/home/user/config.yml"
```

**Implementation**: Pure Ruby (ported from picoruby-posix-io)

### Additional File Test Methods

#### `File.exist?(path) → true or false`

Returns `true` if the named file exists.

```ruby
File.exist?("/lib/my_lib.rb")  #=> true
File.exist?("/lib")             #=> true (directory also returns true)
File.exist?("/nonexistent")     #=> false
```

**Implementation**: C extension calling `fmrb_hal_file_stat()`

#### `File.directory?(path) → true or false`

Returns `true` if the named file is a directory.

```ruby
File.directory?("/lib")      #=> true
File.directory?("/lib/my_lib.rb")  #=> false
File.directory?("/nonexistent")     #=> false
```

**Implementation**: C extension calling `fmrb_hal_file_stat()`

#### `File.size(path) → integer`

Returns the size of the file in bytes.

```ruby
File.size("/lib/my_lib.rb")  #=> 1024
```

**Implementation**: C extension calling `fmrb_hal_file_stat()`

### Path Manipulation Methods

#### `File.basename(path, suffix = '') → string`

Returns the last component of the filename given in `path`. If `suffix` is given and present at the end of `path`, it is removed.

```ruby
File.basename("/lib/my_lib.rb")        #=> "my_lib.rb"
File.basename("/lib/my_lib.rb", ".rb") #=> "my_lib"
File.basename("/lib/")                 #=> "lib"
```

**Implementation**: Pure Ruby (ported from picoruby-posix-io)

#### `File.dirname(path, level = 1) → string`

Returns all components of the filename given in `path` except the last one.

```ruby
File.dirname("/lib/my_lib.rb")     #=> "/lib"
File.dirname("/lib/foo/bar.rb", 2) #=> "/lib"
File.dirname("my_lib.rb")          #=> "."
```

**Implementation**: Pure Ruby (ported from picoruby-posix-io)

#### `File.join(string, ...) → string`

Returns a new string formed by joining the strings using `File::SEPARATOR` (typically `/`).

```ruby
File.join("lib", "my_lib.rb")      #=> "lib/my_lib.rb"
File.join("/lib", "foo", "bar.rb") #=> "/lib/foo/bar.rb"
```

**Implementation**: Pure Ruby (ported from picoruby-posix-io)

### File Operations (Future)

These methods are not immediately required for `require` functionality but should be implemented for completeness:

- `File.open(path, mode, perm = 0666, &block) → file or nil`
- `File.read(path, length = nil, offset = nil) → string`
- `File.write(path, data, offset = nil) → integer`
- `File.delete(path, ...) → integer` (alias: `unlink`)
- `File.rename(old_name, new_name) → 0`
- `File.chmod(mode, path, ...) → integer`

## HAL API (fmrb_hal_file)

### Data Structures

#### `fmrb_hal_stat_t`

File status structure (POSIX-like).

```c
typedef struct {
  uint32_t mode;   // File mode and type
  uint64_t size;   // File size in bytes
  time_t mtime;    // Last modification time (Unix timestamp)
} fmrb_hal_stat_t;
```

**Mode Bits** (compatible with POSIX st_mode):
```c
// File type mask
#define FMRB_HAL_S_IFMT   0170000  // File type mask

// File types
#define FMRB_HAL_S_IFREG  0100000  // Regular file
#define FMRB_HAL_S_IFDIR  0040000  // Directory
#define FMRB_HAL_S_IFLNK  0120000  // Symbolic link

// Type checking macros
#define FMRB_HAL_S_ISREG(m)  (((m) & FMRB_HAL_S_IFMT) == FMRB_HAL_S_IFREG)
#define FMRB_HAL_S_ISDIR(m)  (((m) & FMRB_HAL_S_IFMT) == FMRB_HAL_S_IFDIR)
#define FMRB_HAL_S_ISLNK(m)  (((m) & FMRB_HAL_S_IFMT) == FMRB_HAL_S_IFLNK)

// Permission bits (future use)
#define FMRB_HAL_S_IRWXU  0000700  // Owner: RWX
#define FMRB_HAL_S_IRUSR  0000400  // Owner: read
#define FMRB_HAL_S_IWUSR  0000200  // Owner: write
#define FMRB_HAL_S_IXUSR  0000100  // Owner: execute
```

### Functions

#### `fmrb_hal_file_stat`

Get file status.

```c
int fmrb_hal_file_stat(const char *path, fmrb_hal_stat_t *st);
```

**Parameters**:
- `path`: File path (absolute or relative)
- `st`: Pointer to stat structure to fill

**Returns**:
- `0` on success
- `-1` on error (file not found, permission denied, etc.)

**Platform Notes**:
- **Linux**: Internally calls POSIX `stat()` with `"flash/"` prefix
- **ESP32**: Will use LittleFS stat equivalent

**Example**:
```c
fmrb_hal_stat_t st;
if (fmrb_hal_file_stat("/lib/my_lib.rb", &st) == 0) {
  if (FMRB_HAL_S_ISREG(st.mode)) {
    printf("Regular file, size: %llu bytes\n", st.size);
  }
}
```

#### `fmrb_hal_file_open` (Future)

Open a file for reading/writing.

```c
int fmrb_hal_file_open(const char *path, int flags, int mode);
```

**Parameters**:
- `path`: File path
- `flags`: Open flags (O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_APPEND)
- `mode`: Permission mode (for O_CREAT)

**Returns**:
- File descriptor (>= 0) on success
- `-1` on error

#### `fmrb_hal_file_read` (Future)

Read from a file descriptor.

```c
ssize_t fmrb_hal_file_read(int fd, void *buf, size_t count);
```

**Parameters**:
- `fd`: File descriptor from `fmrb_hal_file_open()`
- `buf`: Buffer to read into
- `count`: Number of bytes to read

**Returns**:
- Number of bytes read on success
- `-1` on error

#### `fmrb_hal_file_write` (Future)

Write to a file descriptor.

```c
ssize_t fmrb_hal_file_write(int fd, const void *buf, size_t count);
```

**Parameters**:
- `fd`: File descriptor from `fmrb_hal_file_open()`
- `buf`: Buffer to write from
- `count`: Number of bytes to write

**Returns**:
- Number of bytes written on success
- `-1` on error

#### `fmrb_hal_file_close` (Future)

Close a file descriptor.

```c
int fmrb_hal_file_close(int fd);
```

**Parameters**:
- `fd`: File descriptor to close

**Returns**:
- `0` on success
- `-1` on error

## Path Handling

### Linux Platform

**Path Prefix**: All paths are prefixed with `"flash/"` in the HAL layer.

```
Ruby path:        "/lib/my_lib.rb"
                       ↓
HAL receives:     "/lib/my_lib.rb"
                       ↓
POSIX API sees:   "flash/lib/my_lib.rb"
```

The `flash/` directory is created by the build system and populated with application files.

### ESP32 Platform (Future)

**Mount Points**: Multiple filesystems with mount points.

```
/              → LittleFS on internal flash
/sdcard        → FAT32 on SD card
/ram           → RAMFS in memory
```

The HAL layer will manage mount point resolution.

## Error Handling

### C Layer (HAL)

HAL functions return:
- `0` or positive value on success
- `-1` on error

Error information can be retrieved via `errno` or platform-specific error codes.

### Ruby Layer

Ruby methods raise exceptions on critical errors:

```ruby
begin
  File.open("/nonexistent", "r")
rescue => e
  puts "Error: #{e.message}"
end
```

File test methods (`file?`, `exist?`, `directory?`) return `false` on error instead of raising exceptions.

## Performance Considerations

### Stat Caching

The HAL layer does NOT cache stat results. Each call to `fmrb_hal_file_stat()` performs a fresh filesystem query.

Rationale:
- Simple implementation
- Avoids cache invalidation complexity
- File stat is relatively fast on modern systems

If performance becomes an issue, caching can be added later.

### Path String Allocation

Path strings are passed as `const char *` to avoid unnecessary copying. The HAL layer may allocate temporary buffers for path prefix handling.

## Testing Requirements

### Unit Tests

Test each File method with:
- Existing files
- Non-existent files
- Directories
- Edge cases (empty string, `"/"`, `".."`, `"~"`)

### Integration Tests

Test `require` functionality:
```ruby
# flash/lib/test_lib.rb
module TestLib
  VERSION = "1.0.0"
end

# flash/app/main.rb
require "/lib/test_lib"
puts TestLib::VERSION  #=> "1.0.0"
```

### Platform-Specific Tests

**Linux**:
- Verify `flash/` prefix is applied correctly
- Test with symlinks (should follow)

**ESP32** (Future):
- Test multiple mount points
- Test SDCard hot-plug
- Test RAMFS operations

## Migration Path

### Phase 1: Minimal Implementation (Current)

Implement only methods required for `require`:
- `File.file?(path)`
- `File.expand_path(path, base)`
- `fmrb_hal_file_stat()`

### Phase 2: Complete File API

Add remaining File methods:
- `File.open()`, `File.read()`, `File.write()`
- `File.delete()`, `File.rename()`
- Directory operations

### Phase 3: Advanced Features

- Multiple filesystem support
- SDCard mounting
- RAMFS implementation
- File watching/notification

## References

- POSIX stat(2): https://man7.org/linux/man-pages/man2/stat.2.html
- Ruby File class: https://ruby-doc.org/core/File.html
- picoruby-posix-io implementation (reference)
