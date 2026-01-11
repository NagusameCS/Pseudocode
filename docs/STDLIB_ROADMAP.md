# Standard Library Roadmap

**Version:** 1.0  
**Date:** January 2026  
**Status:** Active Development

---

## Current State

###  Built-in Functions (80+)
Already part of the VM - no import needed:

| Category | Functions |
|----------|-----------|
| **I/O** | `print`, `input` |
| **Math** | `abs`, `sqrt`, `pow`, `min`, `max`, `floor`, `ceil`, `round`, `sin`, `cos`, `tan`, `log`, `exp`, `random` |
| **Strings** | `len`, `upper`, `lower`, `trim`, `split`, `join`, `substr`, `replace`, `starts_with`, `ends_with`, `contains`, `index_of` |
| **Arrays** | `len`, `push`, `pop`, `slice`, `reverse`, `sort`, `map`, `filter`, `reduce`, `find` |
| **Dicts** | `keys`, `values`, `dict_has`, `dict_get`, `dict_set`, `dict` |
| **Files** | `read_file`, `write_file`, `append_file`, `file_exists` |
| **HTTP** | `http_get`, `http_post`, `http_put`, `http_delete` |
| **JSON** | `json_parse`, `json_stringify` |
| **Crypto** | `sha256`, `md5`, `encode_base64`, `decode_base64` |
| **Types** | `int`, `float`, `str`, `type` |
| **Time** | `time`, `sleep` |

### Implemented Standard Libraries (v1.2.0)

**Core Libraries** (in `lib/`):

| Library | Import | Status | Functions |
|---------|--------|--------|-----------|
| **io** | `import "io"` | [DONE] | path_join, path_dirname, path_basename, path_extension, temp_file, temp_dir, path_exists, is_file, is_dir |
| **csv** | `import "csv"` | [DONE] | csv_parse, csv_parse_file, csv_stringify, csv_write_file, csv_get_column, csv_filter_rows |
| **http_ext** | `import "http_ext"` | [DONE] | http_request, http_get_json, http_post_json, build_query_string, parse_url |
| **regex** | `import "regex"` | [DONE] | regex_match, regex_find_all, regex_replace, regex_split, is_email, is_url, extract_numbers |
| **websocket** | `import "websocket"` | [DONE] | ws_connect, ws_send, ws_receive, ws_close, ws_on_message, ws_on_error, ws_on_close |
| **database** | `import "database"` | [DONE] | db_open, db_create_table, db_insert, db_select_all, db_select_where, db_update_by_id, db_delete_by_id, kv_set, kv_get, kv_has, kv_delete, kv_keys |

**Utility Libraries** (in `examples/lib/`):

| Library | Import | Functions |
|---------|--------|-----------|
| **math** | `import "./lib/math.pseudo"` | PI, E, TAU, factorial, gcd, lcm, fib, is_prime, power, sum, average, clamp, lerp, deg_to_rad, rad_to_deg |
| **strings** | `import "./lib/strings.pseudo"` | repeat_string, pad_left, pad_right, center, reverse_string, is_palindrome, count_substring, word_count, title_case, truncate, word_wrap |
| **collections** | `import "./lib/collections.pseudo"` | range_array, range_step, index_of, array_contains, unique, flatten, zip, reverse_array, take, drop, chunk, frequencies, group_by, Stack, Queue |
| **functional** | `import "./lib/functional.pseudo"` | compose, pipe, curry, partial, memoize |
| **datetime** | `import "./lib/datetime.pseudo"` | now, format_date, parse_date, add_days, diff_days |
| **testing** | `import "./lib/testing.pseudo"` | assert, assert_equal, describe, it, run_tests |

---

## Phase 1: Core Libraries (Priority: HIGH) - MOSTLY COMPLETE

### 1.1 Testing Library - [DONE]
**Path:** `examples/lib/testing.pseudo`  
**Purpose:** Unit testing and assertions

```pseudocode
// Assertions
fn assert(condition, message)
fn assert_equal(actual, expected, message)
fn assert_not_equal(a, b, message)
fn assert_true(value, message)
fn assert_false(value, message)
fn assert_nil(value, message)
fn assert_not_nil(value, message)
fn assert_contains(haystack, needle, message)
fn assert_throws(fn_to_call, expected_error)

// Approximate equality for floats
fn assert_approx(actual, expected, tolerance, message)

// Test runner
fn describe(name, test_fn)
fn it(description, test_fn)
fn run_tests()

// Output
fn test_summary()
```

**Example usage:**
```pseudocode
import "testing"

describe("Math functions", fn()
    it("should calculate factorial correctly", fn()
        assert_equal(factorial(5), 120, "5! should be 120")
        assert_equal(factorial(0), 1, "0! should be 1")
    end)
    
    it("should detect primes", fn()
        assert_true(is_prime(17), "17 is prime")
        assert_false(is_prime(4), "4 is not prime")
    end)
end)

run_tests()
```

**Effort:** ~200 lines

---

### 1.2 DateTime Library 
**Path:** `lib/datetime.pseudo`  
**Purpose:** Date and time manipulation

```pseudocode
// Current time
fn now()           // Returns timestamp (ms since epoch)
fn today()         // Returns date dict {year, month, day}

// Parsing/Formatting
fn parse_date(str, format)      // "2026-01-10" → date dict
fn format_date(date, format)    // date dict → "January 10, 2026"
fn parse_time(str, format)      // "14:30:00" → time dict
fn format_time(time, format)    // time dict → "2:30 PM"
fn parse_datetime(str, format)  // ISO 8601 support
fn format_datetime(dt, format)

// Components
fn year(timestamp)
fn month(timestamp)
fn day(timestamp)
fn hour(timestamp)
fn minute(timestamp)
fn second(timestamp)
fn day_of_week(timestamp)       // 0=Sunday, 6=Saturday
fn day_of_year(timestamp)

// Arithmetic
fn add_days(date, n)
fn add_months(date, n)
fn add_years(date, n)
fn diff_days(date1, date2)
fn diff_seconds(dt1, dt2)

// Utilities
fn is_leap_year(year)
fn days_in_month(year, month)
fn start_of_day(timestamp)
fn end_of_day(timestamp)
fn start_of_month(timestamp)
fn start_of_year(timestamp)
```

**Effort:** ~400 lines

---

### 1.3 IO Library (Extended)
**Path:** `lib/io.pseudo`  
**Purpose:** Enhanced file and path operations

```pseudocode
// Path operations
fn path_join(parts)           // ["a", "b", "c"] → "a/b/c"
fn path_dirname(path)         // "/a/b/c.txt" → "/a/b"
fn path_basename(path)        // "/a/b/c.txt" → "c.txt"
fn path_extension(path)       // "/a/b/c.txt" → "txt"
fn path_without_extension(path)
fn path_normalize(path)       // "a/../b/./c" → "b/c"
fn path_is_absolute(path)

// File operations
fn read_lines(path)           // Returns array of lines
fn write_lines(path, lines)
fn copy_file(src, dest)
fn move_file(src, dest)
fn delete_file(path)
fn file_size(path)
fn file_modified_time(path)

// Directory operations  
fn list_dir(path)             // Returns array of names
fn make_dir(path)
fn make_dirs(path)            // mkdir -p
fn remove_dir(path)
fn is_file(path)
fn is_dir(path)

// Temp files
fn temp_file()                // Returns path to temp file
fn temp_dir()                 // Returns path to temp dir
```

**Effort:** ~300 lines (may need VM builtins)

---

## Phase 2: Data Processing (Priority: MEDIUM)

### 2.1 Regex Library
**Path:** `lib/regex.pseudo`  
**Purpose:** Pattern matching (wrapper around VM builtin if added)

```pseudocode
fn regex_match(pattern, text)        // Returns match or nil
fn regex_match_all(pattern, text)    // Returns array of matches
fn regex_replace(pattern, text, replacement)
fn regex_split(pattern, text)
fn regex_test(pattern, text)         // Returns boolean

// Match object
// {"matched": "abc", "groups": ["a", "b"], "index": 5}
```

**Note:** May require VM-level regex engine for performance

---

### 2.2 CSV Library
**Path:** `lib/csv.pseudo`  
**Purpose:** CSV parsing and generation

```pseudocode
fn csv_parse(text)                   // Returns array of arrays
fn csv_parse_with_header(text)       // Returns array of dicts
fn csv_stringify(data)               // Array of arrays → CSV string
fn csv_stringify_with_header(data, headers)

fn csv_read_file(path)
fn csv_write_file(path, data)

// Options
fn csv_parse_options(text, options)
// options: {delimiter: ",", quote: '"', escape: "\\", header: true}
```

**Effort:** ~200 lines

---

### 2.3 Validation Library
**Path:** `lib/validate.pseudo`  
**Purpose:** Data validation utilities

```pseudocode
// Type checks
fn is_number(v)
fn is_string(v)
fn is_array(v)
fn is_dict(v)
fn is_function(v)
fn is_boolean(v)
fn is_nil(v)

// String validation
fn is_email(s)
fn is_url(s)
fn is_uuid(s)
fn is_ipv4(s)
fn is_ipv6(s)
fn is_alpha(s)
fn is_alphanumeric(s)
fn is_numeric(s)

// Number validation
fn is_integer(n)
fn is_positive(n)
fn is_negative(n)
fn is_between(n, min, max)

// Collection validation
fn is_empty(v)
fn has_length(v, n)
fn min_length(v, n)
fn max_length(v, n)
```

**Effort:** ~250 lines

---

## Phase 3: Network & Web (Priority: MEDIUM)

### 3.1 HTTP Client Library
**Path:** `lib/http.pseudo`  
**Purpose:** Enhanced HTTP client

```pseudocode
// Full request control
fn http_request(options)
// options: {method, url, headers, body, timeout}

// Convenience with headers
fn http_get_json(url, headers)
fn http_post_json(url, data, headers)

// Response handling
fn is_success(response)      // 2xx
fn is_redirect(response)     // 3xx
fn is_client_error(response) // 4xx
fn is_server_error(response) // 5xx

// URL utilities
fn url_encode(str)
fn url_decode(str)
fn url_parse(url)            // Returns {scheme, host, port, path, query}
fn url_build(parts)
fn query_string_parse(qs)
fn query_string_build(params)
```

**Effort:** ~200 lines

---

### 3.2 WebSocket Library (Requires VM support)
**Path:** `lib/websocket.pseudo`  
**Purpose:** WebSocket client

```pseudocode
fn ws_connect(url)
fn ws_send(ws, message)
fn ws_receive(ws)
fn ws_close(ws)
fn ws_on_message(ws, callback)
fn ws_on_close(ws, callback)
fn ws_on_error(ws, callback)
```

**Note:** Requires VM-level WebSocket implementation

---

## Phase 4: Algorithms & Data Structures (Priority: LOW)

### 4.1 Sorting Library
**Path:** `lib/sorting.pseudo`  
**Purpose:** Various sorting algorithms

```pseudocode
fn quicksort(arr)
fn mergesort(arr)
fn heapsort(arr)
fn insertion_sort(arr)
fn selection_sort(arr)
fn bubble_sort(arr)
fn radix_sort(arr)           // For integers

fn sort_by(arr, key_fn)      // Sort by key function
fn sort_desc(arr)            // Descending order
fn is_sorted(arr)
fn is_sorted_by(arr, key_fn)
```

**Effort:** ~300 lines

---

### 4.2 Searching Library
**Path:** `lib/searching.pseudo`  
**Purpose:** Search algorithms

```pseudocode
fn binary_search(arr, target)
fn linear_search(arr, target)
fn find_first(arr, predicate)
fn find_last(arr, predicate)
fn find_all(arr, predicate)
fn count_if(arr, predicate)
fn any(arr, predicate)
fn all(arr, predicate)
fn none(arr, predicate)
```

**Effort:** ~150 lines

---

### 4.3 Graph Library
**Path:** `lib/graph.pseudo`  
**Purpose:** Graph data structure and algorithms

```pseudocode
// Graph construction
fn graph_new()
fn graph_add_node(g, id, data)
fn graph_add_edge(g, from, to, weight)
fn graph_remove_node(g, id)
fn graph_remove_edge(g, from, to)

// Queries
fn graph_neighbors(g, id)
fn graph_has_node(g, id)
fn graph_has_edge(g, from, to)
fn graph_nodes(g)
fn graph_edges(g)

// Algorithms
fn bfs(g, start)
fn dfs(g, start)
fn dijkstra(g, start, end)
fn bellman_ford(g, start)
fn topological_sort(g)
fn is_cyclic(g)
fn connected_components(g)
fn shortest_path(g, start, end)
```

**Effort:** ~500 lines

---

## Phase 5: Advanced Libraries (Priority: FUTURE)

### 5.1 Tensor/Matrix Library
Already partially implemented in `cvm/tensor.c`

```pseudocode
fn tensor_new(shape)
fn tensor_zeros(shape)
fn tensor_ones(shape)
fn tensor_random(shape)
fn tensor_from_array(arr, shape)

fn tensor_add(a, b)
fn tensor_sub(a, b)
fn tensor_mul(a, b)      // Element-wise
fn tensor_matmul(a, b)   // Matrix multiply
fn tensor_transpose(t)
fn tensor_reshape(t, shape)
fn tensor_sum(t, axis)
fn tensor_mean(t, axis)
fn tensor_max(t, axis)
fn tensor_min(t, axis)
```

---

### 5.2 Crypto Library (Extended)
**Path:** `lib/crypto.pseudo`  
**Purpose:** Extended cryptography

```pseudocode
// Hashing (already in VM)
fn sha256(data)
fn md5(data)
fn sha512(data)
fn sha1(data)
fn hmac(key, data, algorithm)

// Encoding (already in VM)
fn encode_base64(data)
fn decode_base64(str)
fn encode_hex(data)
fn decode_hex(str)

// Random
fn random_bytes(n)
fn random_uuid()
fn random_token(length)

// Utilities
fn constant_time_compare(a, b)
```

---

### 5.3 Concurrency Library (Requires VM support)
**Path:** `lib/async.pseudo`  
**Purpose:** Async/await and coroutines

```pseudocode
// Future proposal - requires VM changes
fn async(fn_ref)
fn await(promise)
fn all(promises)
fn race(promises)
fn timeout(promise, ms)

// Channels
fn channel_new()
fn channel_send(ch, value)
fn channel_receive(ch)
fn channel_close(ch)
```

**Note:** Major VM change required

---

## Implementation Priority

| Library | Priority | Effort | Dependencies |
|---------|----------|--------|--------------|
| **testing** |  HIGH | 200 lines | None |
| **datetime** |  HIGH | 400 lines | VM time builtins |
| **io** |  HIGH | 300 lines | VM file builtins |
| csv | MEDIUM | 200 lines | None |
| validate | MEDIUM | 250 lines | None |
| http | MEDIUM | 200 lines | None |
| sorting | LOW | 300 lines | None |
| searching | LOW | 150 lines | None |
| graph | LOW | 500 lines | collections |
| regex | FUTURE | - | VM regex engine |
| websocket | FUTURE | - | VM websocket |
| async | FUTURE | - | Major VM changes |

---

## Package Ecosystem (Future)

### Package Manager
```bash
pseudo install <package>       # Install from registry
pseudo install ./local-pkg     # Install local package
pseudo uninstall <package>     # Remove package
pseudo update <package>        # Update package
pseudo list                    # List installed packages
pseudo search <query>          # Search registry
pseudo publish                 # Publish to registry
```

### Package Structure
```
my-package/
├── package.json           # Package manifest
├── lib/
│   └── main.pseudo        # Entry point
├── src/                   # Source files
├── tests/                 # Test files
└── README.md
```

### package.json
```json
{
    "name": "my-package",
    "version": "1.0.0",
    "description": "My awesome package",
    "main": "lib/main.pseudo",
    "author": "Your Name",
    "license": "MIT",
    "dependencies": {
        "other-package": "^1.2.0"
    },
    "devDependencies": {
        "testing": "^1.0.0"
    }
}
```

### Standard Library Path
```bash
~/.pseudo/lib/                 # User packages
/usr/local/lib/pseudo/         # System packages
./lib/                         # Project local
PSEUDO_PATH=/custom/path       # Custom paths
```

### Package Registry
- URL: `https://packages.pseudo.dev` (future)
- API: REST + JSON
- Features: Versioning, downloads, stats, verified publishers

---

## Next Steps

1.  Import system implemented (v1.2.0)
2. ⏳ Create `testing` library (estimated: 1-2 days)
3. ⏳ Create `datetime` library (estimated: 2-3 days)
4. ⏳ Enhance `io` library (may need VM changes)
5. ⏳ Add `from X import Y` selective imports
6. ⏳ Package manager design document
7. ⏳ Package registry infrastructure

---

## Files to Create

```
examples/lib/
├── math.pseudo        Done
├── strings.pseudo     Done
├── collections.pseudo  Done
├── testing.pseudo    ⏳ Next
├── datetime.pseudo   ⏳ Next
├── io.pseudo         ⏳ Planned
├── csv.pseudo        ⏳ Planned
├── validate.pseudo   ⏳ Planned
├── http.pseudo       ⏳ Planned
├── sorting.pseudo    ⏳ Planned
├── searching.pseudo  ⏳ Planned
└── graph.pseudo      ⏳ Planned
```
