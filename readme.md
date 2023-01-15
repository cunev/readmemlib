# readmemlib Library - N-API Binding for reading process memory on linux.

This library allows you to read and write integers to a specific memory address of a running process.

## Installation

1. Clone this repository
2. Run `npm install` in the project directory
3. Build the project using `npm run build`

## Usage

```ts
const memoryAccess = require("./build/Release/addon");

// Read integer from memory address
let value = memoryAccess.read_integer(pid, address);
console.log(value);

// Write integer to memory address
memoryAccess.write_integer(pid, address, newValue);
```

### Parameters

- `pid`: The process ID of the running process
- `address`: The memory address where you want to read/write the integer
- `newValue`: The integer value you want to write to the memory address (Only required for `write_integer`)

### Note

- For read_integer and write_integer, you need to have the permissions to access the pid process
- The library uses ptrace for process tracing and reading memory. Be aware that it may cause process to stop/crash, so use it with caution
