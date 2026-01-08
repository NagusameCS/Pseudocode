"""
Pseudocode Language - Bytecode Compiler
Compiles AST to stack-based bytecode for fast execution
"""

from enum import IntEnum, auto
from dataclasses import dataclass, field
from typing import List, Dict, Any, Optional
from ast_nodes import *

class OpCode(IntEnum):
    """Stack-based VM opcodes"""
    # Constants
    CONST = auto()       # Push constant
    TRUE = auto()        # Push true
    FALSE = auto()       # Push false
    
    # Variables
    LOAD = auto()        # Load variable
    STORE = auto()       # Store variable
    LOAD_GLOBAL = auto() # Load global
    STORE_GLOBAL = auto()# Store global
    
    # Arithmetic
    ADD = auto()
    SUB = auto()
    MUL = auto()
    DIV = auto()
    MOD = auto()
    NEG = auto()
    
    # Comparison
    EQ = auto()
    NEQ = auto()
    LT = auto()
    GT = auto()
    LTE = auto()
    GTE = auto()
    
    # Logical
    NOT = auto()
    
    # Bitwise
    BAND = auto()
    BOR = auto()
    BXOR = auto()
    SHL = auto()
    SHR = auto()
    
    # Control flow
    JMP = auto()         # Unconditional jump
    JMP_IF_FALSE = auto()# Jump if top is false
    JMP_IF_TRUE = auto() # Jump if top is true
    
    # Functions
    CALL = auto()        # Call function
    RET = auto()         # Return from function
    
    # Arrays
    ARRAY = auto()       # Create array
    INDEX = auto()       # Array indexing
    INDEX_SET = auto()   # Array index assignment
    
    # Stack
    POP = auto()         # Pop top of stack
    DUP = auto()         # Duplicate top
    
    # Built-ins
    PRINT = auto()
    LEN = auto()
    PUSH = auto()
    POP_ARRAY = auto()
    TIME = auto()
    INPUT = auto()
    
    # Iteration
    ITER = auto()        # Create iterator
    ITER_NEXT = auto()   # Get next or jump
    
    HALT = auto()

@dataclass
class Chunk:
    """Compiled bytecode chunk"""
    code: List[int] = field(default_factory=list)
    constants: List[Any] = field(default_factory=list)
    lines: List[int] = field(default_factory=list)  # Line info for errors
    
    def emit(self, op: OpCode, line: int):
        self.code.append(op)
        self.lines.append(line)
    
    def emit_arg(self, op: OpCode, arg: int, line: int):
        self.code.append(op)
        self.code.append(arg)
        self.lines.append(line)
        self.lines.append(line)
    
    def add_constant(self, value: Any) -> int:
        # Reuse existing constants
        for i, c in enumerate(self.constants):
            if c == value and type(c) == type(value):
                return i
        self.constants.append(value)
        return len(self.constants) - 1
    
    def current_offset(self) -> int:
        return len(self.code)
    
    def patch_jump(self, offset: int):
        """Patch jump target at offset to current position"""
        self.code[offset] = self.current_offset()

@dataclass
class CompiledFunction:
    """Compiled function object"""
    name: str
    arity: int
    chunk: Chunk
    locals_count: int

class Compiler:
    """Compile AST to bytecode"""
    
    __slots__ = ('chunk', 'locals', 'scope_depth', 'functions', 'loop_starts', 'loop_exits', 'iter_counter')
    
    def __init__(self):
        self.chunk = Chunk()
        self.locals: Dict[str, int] = {}
        self.scope_depth = 0
        self.functions: Dict[str, CompiledFunction] = {}
        self.loop_starts: List[int] = []
        self.loop_exits: List[List[int]] = []
        self.iter_counter = 0  # Unique counter for iterator names
    
    def compile(self, program: Program) -> Chunk:
        """Compile program to bytecode"""
        # First pass: collect function declarations
        for stmt in program.statements:
            if isinstance(stmt, FnDecl):
                self.compile_fn_decl(stmt)
        
        # Second pass: compile top-level code
        for stmt in program.statements:
            if not isinstance(stmt, FnDecl):
                self.compile_stmt(stmt)
        
        self.chunk.emit(OpCode.HALT, 0)
        return self.chunk
    
    def compile_stmt(self, stmt: Stmt):
        """Compile a statement"""
        if isinstance(stmt, VarDecl):
            self.compile_var_decl(stmt)
        elif isinstance(stmt, Assignment):
            self.compile_assignment(stmt)
        elif isinstance(stmt, IfStmt):
            self.compile_if(stmt)
        elif isinstance(stmt, WhileStmt):
            self.compile_while(stmt)
        elif isinstance(stmt, ForStmt):
            self.compile_for(stmt)
        elif isinstance(stmt, ReturnStmt):
            self.compile_return(stmt)
        elif isinstance(stmt, ExprStmt):
            self.compile_expr(stmt.expr)
            self.chunk.emit(OpCode.POP, stmt.line)
        elif isinstance(stmt, FnDecl):
            pass  # Already handled in first pass
    
    def compile_var_decl(self, stmt: VarDecl):
        """Compile variable declaration"""
        self.compile_expr(stmt.value)
        
        # Store as global in main scope, or local in function
        if self.scope_depth == 0:
            # Main scope - use globals
            idx = self.chunk.add_constant(stmt.name)
            self.chunk.emit_arg(OpCode.STORE_GLOBAL, idx, stmt.line)
            self.chunk.emit(OpCode.POP, stmt.line)
        else:
            # Function scope - use local slot
            slot = len(self.locals)
            self.locals[stmt.name] = slot
            # Value stays on stack as the local
    
    def compile_assignment(self, stmt: Assignment):
        """Compile assignment"""
        self.compile_expr(stmt.value)
        
        if isinstance(stmt.target, Identifier):
            if stmt.target.name in self.locals:
                slot = self.locals[stmt.target.name]
                self.chunk.emit_arg(OpCode.STORE, slot, stmt.line)
            else:
                idx = self.chunk.add_constant(stmt.target.name)
                self.chunk.emit_arg(OpCode.STORE_GLOBAL, idx, stmt.line)
        elif isinstance(stmt.target, IndexExpr):
            # array[index] = value -> value already on stack
            self.compile_expr(stmt.target.obj)
            self.compile_expr(stmt.target.index)
            self.chunk.emit(OpCode.INDEX_SET, stmt.line)
        self.chunk.emit(OpCode.POP, stmt.line)  # Clean up stack
    
    def compile_if(self, stmt: IfStmt):
        """Compile if statement"""
        self.compile_expr(stmt.condition)
        
        # Jump over then-block if false
        jump_if_false = self.chunk.current_offset()
        self.chunk.emit_arg(OpCode.JMP_IF_FALSE, 0, stmt.line)
        self.chunk.emit(OpCode.POP, stmt.line)  # Pop condition
        
        # Compile then block
        for s in stmt.then_body:
            self.compile_stmt(s)
        
        # Jump over else/elif blocks
        exit_jumps = []
        exit_jumps.append(self.chunk.current_offset())
        self.chunk.emit_arg(OpCode.JMP, 0, stmt.line)
        
        # Patch the false jump
        self.chunk.patch_jump(jump_if_false + 1)
        self.chunk.emit(OpCode.POP, stmt.line)  # Pop condition
        
        # Compile elif clauses
        for elif_cond, elif_body in stmt.elif_clauses:
            self.compile_expr(elif_cond)
            jump_if_false = self.chunk.current_offset()
            self.chunk.emit_arg(OpCode.JMP_IF_FALSE, 0, stmt.line)
            self.chunk.emit(OpCode.POP, stmt.line)
            
            for s in elif_body:
                self.compile_stmt(s)
            
            exit_jumps.append(self.chunk.current_offset())
            self.chunk.emit_arg(OpCode.JMP, 0, stmt.line)
            
            self.chunk.patch_jump(jump_if_false + 1)
            self.chunk.emit(OpCode.POP, stmt.line)
        
        # Compile else block
        if stmt.else_body:
            for s in stmt.else_body:
                self.compile_stmt(s)
        
        # Patch all exit jumps
        for offset in exit_jumps:
            self.chunk.patch_jump(offset + 1)
    
    def compile_while(self, stmt: WhileStmt):
        """Compile while loop"""
        loop_start = self.chunk.current_offset()
        
        self.compile_expr(stmt.condition)
        exit_jump = self.chunk.current_offset()
        self.chunk.emit_arg(OpCode.JMP_IF_FALSE, 0, stmt.line)
        self.chunk.emit(OpCode.POP, stmt.line)
        
        for s in stmt.body:
            self.compile_stmt(s)
        
        # Jump back to start
        self.chunk.emit_arg(OpCode.JMP, loop_start, stmt.line)
        
        # Patch exit
        self.chunk.patch_jump(exit_jump + 1)
        self.chunk.emit(OpCode.POP, stmt.line)
    
    def compile_for(self, stmt: ForStmt):
        """Compile for loop"""
        # Compile iterable and create iterator
        self.compile_expr(stmt.iterable)
        self.chunk.emit(OpCode.ITER, stmt.line)
        
        # Store iterator in a unique global
        self.iter_counter += 1
        iter_name = f"__iter_{self.iter_counter}"
        iter_idx = self.chunk.add_constant(iter_name)
        self.chunk.emit_arg(OpCode.STORE_GLOBAL, iter_idx, stmt.line)
        self.chunk.emit(OpCode.POP, stmt.line)
        
        # Loop variable
        var_idx = self.chunk.add_constant(stmt.var_name)
        
        loop_start = self.chunk.current_offset()
        
        # Get next value or jump
        self.chunk.emit_arg(OpCode.LOAD_GLOBAL, iter_idx, stmt.line)
        exit_jump = self.chunk.current_offset()
        self.chunk.emit_arg(OpCode.ITER_NEXT, 0, stmt.line)
        
        # Store in loop variable
        self.chunk.emit_arg(OpCode.STORE_GLOBAL, var_idx, stmt.line)
        self.chunk.emit(OpCode.POP, stmt.line)
        
        # Compile body
        for s in stmt.body:
            self.compile_stmt(s)
        
        # Jump back
        self.chunk.emit_arg(OpCode.JMP, loop_start, stmt.line)
        
        # Patch exit
        self.chunk.patch_jump(exit_jump + 1)
    
    def compile_return(self, stmt: ReturnStmt):
        """Compile return statement"""
        if stmt.value:
            self.compile_expr(stmt.value)
        else:
            self.chunk.emit_arg(OpCode.CONST, self.chunk.add_constant(None), stmt.line)
        self.chunk.emit(OpCode.RET, stmt.line)
    
    def compile_fn_decl(self, stmt: FnDecl):
        """Compile function declaration"""
        fn_compiler = Compiler()
        
        # Set up parameters as locals
        for i, param in enumerate(stmt.params):
            fn_compiler.locals[param.name] = i
        
        # Compile function body
        for s in stmt.body:
            fn_compiler.compile_stmt(s)
        
        # Ensure function returns
        fn_compiler.chunk.emit_arg(OpCode.CONST, fn_compiler.chunk.add_constant(None), stmt.line)
        fn_compiler.chunk.emit(OpCode.RET, stmt.line)
        
        fn = CompiledFunction(
            name=stmt.name,
            arity=len(stmt.params),
            chunk=fn_compiler.chunk,
            locals_count=len(fn_compiler.locals)
        )
        
        self.functions[stmt.name] = fn
        
        # Store function as global constant
        fn_idx = self.chunk.add_constant(fn)
        name_idx = self.chunk.add_constant(stmt.name)
        self.chunk.emit_arg(OpCode.CONST, fn_idx, stmt.line)
        self.chunk.emit_arg(OpCode.STORE_GLOBAL, name_idx, stmt.line)
    
    def compile_expr(self, expr: Expr):
        """Compile an expression"""
        if isinstance(expr, IntLiteral):
            idx = self.chunk.add_constant(expr.value)
            self.chunk.emit_arg(OpCode.CONST, idx, expr.line)
        
        elif isinstance(expr, FloatLiteral):
            idx = self.chunk.add_constant(expr.value)
            self.chunk.emit_arg(OpCode.CONST, idx, expr.line)
        
        elif isinstance(expr, StringLiteral):
            idx = self.chunk.add_constant(expr.value)
            self.chunk.emit_arg(OpCode.CONST, idx, expr.line)
        
        elif isinstance(expr, BoolLiteral):
            if expr.value:
                self.chunk.emit(OpCode.TRUE, expr.line)
            else:
                self.chunk.emit(OpCode.FALSE, expr.line)
        
        elif isinstance(expr, Identifier):
            if expr.name in self.locals:
                slot = self.locals[expr.name]
                self.chunk.emit_arg(OpCode.LOAD, slot, expr.line)
            else:
                idx = self.chunk.add_constant(expr.name)
                self.chunk.emit_arg(OpCode.LOAD_GLOBAL, idx, expr.line)
        
        elif isinstance(expr, BinaryExpr):
            self.compile_binary(expr)
        
        elif isinstance(expr, UnaryExpr):
            self.compile_expr(expr.operand)
            if expr.op == UnaryOp.NEG:
                self.chunk.emit(OpCode.NEG, expr.line)
            elif expr.op == UnaryOp.NOT:
                self.chunk.emit(OpCode.NOT, expr.line)
        
        elif isinstance(expr, CallExpr):
            self.compile_call(expr)
        
        elif isinstance(expr, IndexExpr):
            self.compile_expr(expr.obj)
            self.compile_expr(expr.index)
            self.chunk.emit(OpCode.INDEX, expr.line)
        
        elif isinstance(expr, ArrayLiteral):
            for elem in expr.elements:
                self.compile_expr(elem)
            self.chunk.emit_arg(OpCode.ARRAY, len(expr.elements), expr.line)
        
        elif isinstance(expr, RangeExpr):
            # Load range function first, then args (callee must be before args on stack)
            idx = self.chunk.add_constant("__range__")
            self.chunk.emit_arg(OpCode.LOAD_GLOBAL, idx, expr.line)
            self.compile_expr(expr.start)
            self.compile_expr(expr.end)
            self.chunk.emit_arg(OpCode.CALL, 2, expr.line)
    
    def compile_binary(self, expr: BinaryExpr):
        """Compile binary expression"""
        # Short-circuit evaluation for and/or
        if expr.op == BinOp.AND:
            self.compile_expr(expr.left)
            jump = self.chunk.current_offset()
            self.chunk.emit_arg(OpCode.JMP_IF_FALSE, 0, expr.line)
            self.chunk.emit(OpCode.POP, expr.line)
            self.compile_expr(expr.right)
            self.chunk.patch_jump(jump + 1)
            return
        
        if expr.op == BinOp.OR:
            self.compile_expr(expr.left)
            jump = self.chunk.current_offset()
            self.chunk.emit_arg(OpCode.JMP_IF_TRUE, 0, expr.line)
            self.chunk.emit(OpCode.POP, expr.line)
            self.compile_expr(expr.right)
            self.chunk.patch_jump(jump + 1)
            return
        
        # Normal binary ops
        self.compile_expr(expr.left)
        self.compile_expr(expr.right)
        
        OP_MAP = {
            BinOp.ADD: OpCode.ADD,
            BinOp.SUB: OpCode.SUB,
            BinOp.MUL: OpCode.MUL,
            BinOp.DIV: OpCode.DIV,
            BinOp.MOD: OpCode.MOD,
            BinOp.EQ: OpCode.EQ,
            BinOp.NEQ: OpCode.NEQ,
            BinOp.LT: OpCode.LT,
            BinOp.GT: OpCode.GT,
            BinOp.LTE: OpCode.LTE,
            BinOp.GTE: OpCode.GTE,
            BinOp.BAND: OpCode.BAND,
            BinOp.BOR: OpCode.BOR,
            BinOp.BXOR: OpCode.BXOR,
            BinOp.SHL: OpCode.SHL,
            BinOp.SHR: OpCode.SHR,
        }
        
        self.chunk.emit(OP_MAP[expr.op], expr.line)
    
    def compile_call(self, expr: CallExpr):
        """Compile function call"""
        # Check for built-in functions
        if isinstance(expr.callee, Identifier):
            name = expr.callee.name
            
            if name == "print":
                for arg in expr.args:
                    self.compile_expr(arg)
                    self.chunk.emit(OpCode.PRINT, expr.line)
                # Push None as return value
                idx = self.chunk.add_constant(None)
                self.chunk.emit_arg(OpCode.CONST, idx, expr.line)
                return
            
            if name == "len":
                self.compile_expr(expr.args[0])
                self.chunk.emit(OpCode.LEN, expr.line)
                return
            
            if name == "push":
                self.compile_expr(expr.args[0])  # array
                self.compile_expr(expr.args[1])  # value
                self.chunk.emit(OpCode.PUSH, expr.line)
                return
            
            if name == "pop":
                self.compile_expr(expr.args[0])
                self.chunk.emit(OpCode.POP_ARRAY, expr.line)
                return
            
            if name == "time":
                self.chunk.emit(OpCode.TIME, expr.line)
                return
            
            if name == "input":
                self.chunk.emit(OpCode.INPUT, expr.line)
                return
        
        # User-defined function
        self.compile_expr(expr.callee)
        for arg in expr.args:
            self.compile_expr(arg)
        self.chunk.emit_arg(OpCode.CALL, len(expr.args), expr.line)


def compile_program(source: str) -> Chunk:
    """Compile source code to bytecode"""
    from parser import parse
    program = parse(source)
    return Compiler().compile(program)
