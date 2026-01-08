"""
Pseudocode Language - Virtual Machine
High-performance stack-based bytecode interpreter
"""

from typing import List, Dict, Any, Optional
from dataclasses import dataclass, field
import time
from compiler import Chunk, OpCode, CompiledFunction

class RuntimeError(Exception):
    def __init__(self, message: str, line: int):
        super().__init__(f"Runtime error at line {line}: {message}")
        self.line = line

@dataclass
class CallFrame:
    """Stack frame for function calls"""
    __slots__ = ('fn', 'ip', 'stack_base', 'chunk')
    fn: CompiledFunction
    ip: int
    stack_base: int
    chunk: Chunk  # Caller's chunk for restoration

class RangeIterator:
    """Efficient range iterator"""
    __slots__ = ('current', 'end')
    
    def __init__(self, start: int, end: int):
        self.current = start
        self.end = end
    
    def next(self) -> tuple[bool, int]:
        if self.current >= self.end:
            return False, 0
        val = self.current
        self.current += 1
        return True, val

class ArrayIterator:
    """Array iterator"""
    __slots__ = ('array', 'index')
    
    def __init__(self, array: list):
        self.array = array
        self.index = 0
    
    def next(self) -> tuple[bool, Any]:
        if self.index >= len(self.array):
            return False, None
        val = self.array[self.index]
        self.index += 1
        return True, val

class VM:
    """High-performance stack-based virtual machine"""
    
    # Pre-allocated stack for speed
    STACK_SIZE = 65536
    MAX_FRAMES = 1024
    
    __slots__ = ('chunk', 'main_chunk', 'ip', 'stack', 'sp', 'globals', 'frames', 'frame_count')
    
    def __init__(self, chunk: Chunk):
        self.chunk = chunk
        self.main_chunk = chunk  # Store main chunk for return
        self.ip = 0
        self.stack: List[Any] = [None] * self.STACK_SIZE
        self.sp = 0  # Stack pointer
        self.globals: Dict[str, Any] = {}
        self.frames: List[CallFrame] = []
        self.frame_count = 0
        
        # Register built-in range function
        self.globals["__range__"] = self._builtin_range
    
    def _builtin_range(self, start: int, end: int) -> RangeIterator:
        return RangeIterator(start, end)
    
    def push(self, value: Any):
        """Push value onto stack"""
        self.stack[self.sp] = value
        self.sp += 1
    
    def pop(self) -> Any:
        """Pop value from stack"""
        self.sp -= 1
        return self.stack[self.sp]
    
    def peek(self, offset: int = 0) -> Any:
        """Peek at stack value"""
        return self.stack[self.sp - 1 - offset]
    
    def read_byte(self) -> int:
        """Read next byte from bytecode"""
        byte = self.chunk.code[self.ip]
        self.ip += 1
        return byte
    
    def get_line(self) -> int:
        """Get current line number for errors"""
        if self.ip < len(self.chunk.lines):
            return self.chunk.lines[self.ip]
        return 0
    
    def run(self) -> Any:
        """Execute bytecode - main interpreter loop"""
        code = self.chunk.code
        constants = self.chunk.constants
        
        while True:
            op = code[self.ip]
            self.ip += 1
            
            if op == OpCode.CONST:
                idx = code[self.ip]
                self.ip += 1
                self.push(constants[idx])
            
            elif op == OpCode.TRUE:
                self.push(True)
            
            elif op == OpCode.FALSE:
                self.push(False)
            
            elif op == OpCode.LOAD:
                slot = code[self.ip]
                self.ip += 1
                if self.frames:
                    frame = self.frames[-1]
                    self.push(self.stack[frame.stack_base + slot])
                else:
                    # In main, locals are at fixed stack positions
                    self.push(self.stack[slot])
            
            elif op == OpCode.STORE:
                slot = code[self.ip]
                self.ip += 1
                value = self.peek()
                if self.frames:
                    frame = self.frames[-1]
                    self.stack[frame.stack_base + slot] = value
                else:
                    # In main, store to fixed stack position
                    self.stack[slot] = value
            
            elif op == OpCode.LOAD_GLOBAL:
                idx = code[self.ip]
                self.ip += 1
                name = constants[idx]
                if name not in self.globals:
                    raise RuntimeError(f"Undefined variable: {name}", self.get_line())
                self.push(self.globals[name])
            
            elif op == OpCode.STORE_GLOBAL:
                idx = code[self.ip]
                self.ip += 1
                name = constants[idx]
                self.globals[name] = self.peek()
            
            elif op == OpCode.ADD:
                b = self.pop()
                a = self.pop()
                self.push(a + b)
            
            elif op == OpCode.SUB:
                b = self.pop()
                a = self.pop()
                self.push(a - b)
            
            elif op == OpCode.MUL:
                b = self.pop()
                a = self.pop()
                self.push(a * b)
            
            elif op == OpCode.DIV:
                b = self.pop()
                a = self.pop()
                if isinstance(a, int) and isinstance(b, int):
                    self.push(a // b)
                else:
                    self.push(a / b)
            
            elif op == OpCode.MOD:
                b = self.pop()
                a = self.pop()
                self.push(a % b)
            
            elif op == OpCode.NEG:
                self.push(-self.pop())
            
            elif op == OpCode.EQ:
                b = self.pop()
                a = self.pop()
                self.push(a == b)
            
            elif op == OpCode.NEQ:
                b = self.pop()
                a = self.pop()
                self.push(a != b)
            
            elif op == OpCode.LT:
                b = self.pop()
                a = self.pop()
                self.push(a < b)
            
            elif op == OpCode.GT:
                b = self.pop()
                a = self.pop()
                self.push(a > b)
            
            elif op == OpCode.LTE:
                b = self.pop()
                a = self.pop()
                self.push(a <= b)
            
            elif op == OpCode.GTE:
                b = self.pop()
                a = self.pop()
                self.push(a >= b)
            
            elif op == OpCode.NOT:
                self.push(not self.pop())
            
            elif op == OpCode.BAND:
                b = self.pop()
                a = self.pop()
                self.push(a & b)
            
            elif op == OpCode.BOR:
                b = self.pop()
                a = self.pop()
                self.push(a | b)
            
            elif op == OpCode.BXOR:
                b = self.pop()
                a = self.pop()
                self.push(a ^ b)
            
            elif op == OpCode.SHL:
                b = self.pop()
                a = self.pop()
                self.push(a << b)
            
            elif op == OpCode.SHR:
                b = self.pop()
                a = self.pop()
                self.push(a >> b)
            
            elif op == OpCode.JMP:
                target = code[self.ip]
                self.ip = target
            
            elif op == OpCode.JMP_IF_FALSE:
                target = code[self.ip]
                self.ip += 1
                if not self.peek():
                    self.ip = target
            
            elif op == OpCode.JMP_IF_TRUE:
                target = code[self.ip]
                self.ip += 1
                if self.peek():
                    self.ip = target
            
            elif op == OpCode.CALL:
                arg_count = code[self.ip]
                self.ip += 1
                
                callee = self.stack[self.sp - arg_count - 1]
                
                if callable(callee):
                    # Built-in function
                    args = [self.pop() for _ in range(arg_count)][::-1]
                    self.pop()  # Pop callee
                    result = callee(*args)
                    self.push(result)
                elif isinstance(callee, CompiledFunction):
                    if arg_count != callee.arity:
                        raise RuntimeError(
                            f"Expected {callee.arity} arguments but got {arg_count}",
                            self.get_line()
                        )
                    
                    # Save current state (including current chunk)
                    # stack_base points to callee slot, args are at stack_base+1, +2, etc.
                    frame = CallFrame(
                        fn=callee,
                        ip=self.ip,
                        stack_base=self.sp - arg_count,  # Point to first arg, not callee
                        chunk=self.chunk
                    )
                    self.frames.append(frame)
                    
                    # Switch to function's chunk
                    self.chunk = callee.chunk
                    self.ip = 0
                    code = self.chunk.code
                    constants = self.chunk.constants
                else:
                    raise RuntimeError(f"Cannot call {type(callee)}", self.get_line())
            
            elif op == OpCode.RET:
                result = self.pop()
                
                if not self.frames:
                    return result
                
                frame = self.frames.pop()
                # Restore stack to just before the callee (stack_base - 1 points to callee)
                self.sp = frame.stack_base - 1
                self.ip = frame.ip
                
                # Restore caller's chunk from the frame
                self.chunk = frame.chunk
                
                code = self.chunk.code
                constants = self.chunk.constants
                self.push(result)
            
            elif op == OpCode.ARRAY:
                count = code[self.ip]
                self.ip += 1
                elements = [self.pop() for _ in range(count)][::-1]
                self.push(elements)
            
            elif op == OpCode.INDEX:
                index = self.pop()
                obj = self.pop()
                self.push(obj[index])
            
            elif op == OpCode.INDEX_SET:
                index = self.pop()
                obj = self.pop()
                value = self.pop()
                obj[index] = value
                self.push(value)
            
            elif op == OpCode.POP:
                self.pop()
            
            elif op == OpCode.DUP:
                self.push(self.peek())
            
            elif op == OpCode.PRINT:
                print(self.pop())
            
            elif op == OpCode.LEN:
                self.push(len(self.pop()))
            
            elif op == OpCode.PUSH:
                value = self.pop()
                array = self.pop()
                array.append(value)
                self.push(array)
            
            elif op == OpCode.POP_ARRAY:
                array = self.pop()
                self.push(array.pop())
            
            elif op == OpCode.TIME:
                self.push(int(time.time_ns()))
            
            elif op == OpCode.INPUT:
                self.push(input())
            
            elif op == OpCode.ITER:
                obj = self.pop()
                if isinstance(obj, RangeIterator):
                    self.push(obj)
                elif isinstance(obj, ArrayIterator):
                    self.push(obj)  # Already an iterator
                elif isinstance(obj, list):
                    self.push(ArrayIterator(obj))
                else:
                    raise RuntimeError(f"Cannot iterate over {type(obj)}", self.get_line())
            
            elif op == OpCode.ITER_NEXT:
                target = code[self.ip]
                self.ip += 1
                iterator = self.peek()
                has_next, value = iterator.next()
                if has_next:
                    self.push(value)
                else:
                    self.pop()  # Remove iterator
                    self.ip = target
            
            elif op == OpCode.HALT:
                return self.peek() if self.sp > 0 else None


def execute(source: str) -> Any:
    """Compile and execute source code"""
    from compiler import compile_program
    chunk = compile_program(source)
    return VM(chunk).run()
