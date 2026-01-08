"""
Pseudocode Language - Standard Library
Built-in functions and utilities
"""

from typing import Any, List, Callable, Dict
import math
import random as rand_module

# Math functions
def abs_fn(x):
    return abs(x)

def min_fn(*args):
    if len(args) == 1 and isinstance(args[0], list):
        return min(args[0])
    return min(args)

def max_fn(*args):
    if len(args) == 1 and isinstance(args[0], list):
        return max(args[0])
    return max(args)

def floor(x):
    return math.floor(x)

def ceil(x):
    return math.ceil(x)

def round_fn(x, digits=0):
    return round(x, digits)

def sqrt(x):
    return math.sqrt(x)

def pow_fn(base, exp):
    return base ** exp

def log(x, base=math.e):
    return math.log(x, base)

def log10(x):
    return math.log10(x)

def sin(x):
    return math.sin(x)

def cos(x):
    return math.cos(x)

def tan(x):
    return math.tan(x)

# String functions
def str_fn(x) -> str:
    return str(x)

def int_fn(x) -> int:
    return int(x)

def float_fn(x) -> float:
    return float(x)

def split(s: str, sep: str = None) -> List[str]:
    return s.split(sep)

def join(arr: List, sep: str = "") -> str:
    return sep.join(str(x) for x in arr)

def upper(s: str) -> str:
    return s.upper()

def lower(s: str) -> str:
    return s.lower()

def strip(s: str) -> str:
    return s.strip()

def replace(s: str, old: str, new: str) -> str:
    return s.replace(old, new)

def contains(s: str, sub: str) -> bool:
    return sub in s

def starts_with(s: str, prefix: str) -> bool:
    return s.startswith(prefix)

def ends_with(s: str, suffix: str) -> bool:
    return s.endswith(suffix)

def char_at(s: str, i: int) -> str:
    return s[i]

def char_code(c: str) -> int:
    return ord(c[0])

def from_char_code(code: int) -> str:
    return chr(code)

# Array functions
def range_fn(start, end=None, step=1):
    if end is None:
        return list(range(start))
    return list(range(start, end, step))

def reverse(arr: List) -> List:
    return arr[::-1]

def sort(arr: List) -> List:
    return sorted(arr)

def sort_by(arr: List, key: Callable) -> List:
    return sorted(arr, key=key)

def filter_fn(arr: List, pred: Callable) -> List:
    return [x for x in arr if pred(x)]

def map_fn(arr: List, fn: Callable) -> List:
    return [fn(x) for x in arr]

def reduce(arr: List, fn: Callable, initial=None):
    if initial is None:
        result = arr[0]
        start = 1
    else:
        result = initial
        start = 0
    
    for i in range(start, len(arr)):
        result = fn(result, arr[i])
    return result

def sum_fn(arr: List):
    return sum(arr)

def find(arr: List, pred: Callable):
    for x in arr:
        if pred(x):
            return x
    return None

def index_of(arr: List, value) -> int:
    try:
        return arr.index(value)
    except ValueError:
        return -1

def slice_fn(arr: List, start: int, end: int = None) -> List:
    return arr[start:end]

def concat(*arrays) -> List:
    result = []
    for arr in arrays:
        result.extend(arr)
    return result

def flatten(arr: List) -> List:
    result = []
    for item in arr:
        if isinstance(item, list):
            result.extend(flatten(item))
        else:
            result.append(item)
    return result

def unique(arr: List) -> List:
    seen = set()
    result = []
    for x in arr:
        if x not in seen:
            seen.add(x)
            result.append(x)
    return result

def zip_fn(*arrays) -> List:
    return list(zip(*arrays))

# Random functions
def random() -> float:
    return rand_module.random()

def random_int(min_val: int, max_val: int) -> int:
    return rand_module.randint(min_val, max_val)

def random_choice(arr: List):
    return rand_module.choice(arr)

def shuffle(arr: List) -> List:
    result = arr.copy()
    rand_module.shuffle(result)
    return result

# Type checking
def type_of(x) -> str:
    if isinstance(x, bool):
        return "bool"
    elif isinstance(x, int):
        return "int"
    elif isinstance(x, float):
        return "float"
    elif isinstance(x, str):
        return "string"
    elif isinstance(x, list):
        return "array"
    elif callable(x):
        return "fn"
    return "unknown"

def is_int(x) -> bool:
    return isinstance(x, int) and not isinstance(x, bool)

def is_float(x) -> bool:
    return isinstance(x, float)

def is_string(x) -> bool:
    return isinstance(x, str)

def is_array(x) -> bool:
    return isinstance(x, list)

def is_bool(x) -> bool:
    return isinstance(x, bool)

# I/O
def read_file(path: str) -> str:
    with open(path, 'r') as f:
        return f.read()

def write_file(path: str, content: str):
    with open(path, 'w') as f:
        f.write(content)

def read_lines(path: str) -> List[str]:
    with open(path, 'r') as f:
        return f.read().splitlines()

# Standard library registry
STDLIB: Dict[str, Callable] = {
    # Math
    "abs": abs_fn,
    "min": min_fn,
    "max": max_fn,
    "floor": floor,
    "ceil": ceil,
    "round": round_fn,
    "sqrt": sqrt,
    "pow": pow_fn,
    "log": log,
    "log10": log10,
    "sin": sin,
    "cos": cos,
    "tan": tan,
    "PI": math.pi,
    "E": math.e,
    
    # Type conversion
    "str": str_fn,
    "int": int_fn,
    "float": float_fn,
    
    # String
    "split": split,
    "join": join,
    "upper": upper,
    "lower": lower,
    "strip": strip,
    "replace": replace,
    "contains": contains,
    "starts_with": starts_with,
    "ends_with": ends_with,
    "char_at": char_at,
    "char_code": char_code,
    "from_char_code": from_char_code,
    
    # Array
    "range": range_fn,
    "reverse": reverse,
    "sort": sort,
    "filter": filter_fn,
    "map": map_fn,
    "reduce": reduce,
    "sum": sum_fn,
    "find": find,
    "index_of": index_of,
    "slice": slice_fn,
    "concat": concat,
    "flatten": flatten,
    "unique": unique,
    "zip": zip_fn,
    
    # Random
    "random": random,
    "random_int": random_int,
    "random_choice": random_choice,
    "shuffle": shuffle,
    
    # Type checking
    "type_of": type_of,
    "is_int": is_int,
    "is_float": is_float,
    "is_string": is_string,
    "is_array": is_array,
    "is_bool": is_bool,
    
    # I/O
    "read_file": read_file,
    "write_file": write_file,
    "read_lines": read_lines,
}

def get_stdlib() -> Dict[str, Any]:
    """Get copy of standard library"""
    return STDLIB.copy()
