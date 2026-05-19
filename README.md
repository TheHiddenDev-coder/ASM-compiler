# asmcc

A compiler written in C that translates a simple assembly-like language into C code.

## Building

```bash
gcc -std=c99 -o asmcc compiler.c
```

## Usage

```bash
./asmcc <input> <output.c>
gcc -std=c99 -o program output.c
./program
```

The input file can have any extension — `.asm`, `.s`, `.txt`, whatever you like.

---

## Language Reference

### Comments

```asm
; This is a comment. Everything after ; is ignored.
```

### Variables

Variables must be declared before use. Declaration and assignment are separate.

```asm
uint32 x        ; declare x
mov x, 10       ; assign 10 to x
```

### Data Types

| Type | Description | C equivalent |
|------|-------------|--------------|
| `bool` | Boolean | `int` (prints as `true`/`false`) |
| `str` | String (max 255 chars) | `char[256]` |
| `float32` | 32-bit float | `float` |
| `float64` | 64-bit float | `double` |
| `uint4` – `uint128` | Unsigned integers | `uint8_t` … `unsigned __int128` |
| `int4` – `int128` | Signed integers | `int8_t` … `__int128` |

> **Note:** `uint4`/`int4` map to 8-bit C types since C has no 4-bit integers.
> `uint128`/`int128` use GCC's `__int128` extension and include custom print/read logic.

### Assignment

```asm
mov x, 42
mov flag, true
mov name, "Alice"
```

### Arithmetic

```asm
add x, 5    ; x += 5
sub x, 3    ; x -= 3
mul x, 2    ; x *= 2
div x, 4    ; x /= 4
mod x, 7    ; x %= 7  (integers only)
```

The right-hand operand can be a literal or another variable name.

### Output

`log` prints without a newline. `logln` prints with a newline.

```asm
log "Hello, "
logln "world!"      ; prints: Hello, world!\n

logln x             ; prints the value of x followed by a newline
```

Booleans print as `true` or `false`.

### Input

```asm
read x      ; reads a value from stdin into x
```

---

## Example Program

```asm
int16 x
log "Enter a number: "
read x
add x, 10
log "That plus 10 is: "
logln x
```

Compile and run:

```
$ ./asmcc example.asm example.c
$ gcc -std=c99 -o example example.c
$ ./example
Enter a number: 5
That plus 10 is: 15
```

---

## Limitations & Roadmap

- **If-statements** — work in progress
- Up to **256 variables** per program
- `str` variables are capped at **255 characters**
- No functions, loops, or jump instructions yet
- `float32`/`float64` do not support `mod`