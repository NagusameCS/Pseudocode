"""
Pseudocode Language - AST (Abstract Syntax Tree)
Optimized node structures with __slots__ for memory efficiency
"""

from dataclasses import dataclass
from typing import List, Optional, Union
from enum import IntEnum, auto

class BinOp(IntEnum):
    ADD = auto()
    SUB = auto()
    MUL = auto()
    DIV = auto()
    MOD = auto()
    EQ = auto()
    NEQ = auto()
    LT = auto()
    GT = auto()
    LTE = auto()
    GTE = auto()
    AND = auto()
    OR = auto()
    BAND = auto()
    BOR = auto()
    BXOR = auto()
    SHL = auto()
    SHR = auto()

class UnaryOp(IntEnum):
    NEG = auto()
    NOT = auto()

# Base AST Node
@dataclass(slots=True)
class Node:
    line: int
    col: int

# Expressions
@dataclass(slots=True)
class IntLiteral(Node):
    value: int

@dataclass(slots=True)
class FloatLiteral(Node):
    value: float

@dataclass(slots=True)
class StringLiteral(Node):
    value: str

@dataclass(slots=True)
class BoolLiteral(Node):
    value: bool

@dataclass(slots=True)
class Identifier(Node):
    name: str

@dataclass(slots=True)
class BinaryExpr(Node):
    op: BinOp
    left: 'Expr'
    right: 'Expr'

@dataclass(slots=True)
class UnaryExpr(Node):
    op: UnaryOp
    operand: 'Expr'

@dataclass(slots=True)
class CallExpr(Node):
    callee: 'Expr'
    args: List['Expr']

@dataclass(slots=True)
class IndexExpr(Node):
    obj: 'Expr'
    index: 'Expr'

@dataclass(slots=True)
class ArrayLiteral(Node):
    elements: List['Expr']

@dataclass(slots=True)
class RangeExpr(Node):
    start: 'Expr'
    end: 'Expr'

# Statements
@dataclass(slots=True)
class VarDecl(Node):
    name: str
    type_ann: Optional[str]
    value: 'Expr'
    is_const: bool

@dataclass(slots=True)
class Assignment(Node):
    target: 'Expr'
    value: 'Expr'

@dataclass(slots=True)
class IfStmt(Node):
    condition: 'Expr'
    then_body: List['Stmt']
    elif_clauses: List[tuple['Expr', List['Stmt']]]
    else_body: Optional[List['Stmt']]

@dataclass(slots=True)
class WhileStmt(Node):
    condition: 'Expr'
    body: List['Stmt']

@dataclass(slots=True)
class ForStmt(Node):
    var_name: str
    iterable: 'Expr'
    body: List['Stmt']

@dataclass(slots=True)
class ReturnStmt(Node):
    value: Optional['Expr']

@dataclass(slots=True)
class ExprStmt(Node):
    expr: 'Expr'

# Function definition
@dataclass(slots=True)
class Param:
    name: str
    type_ann: str

@dataclass(slots=True)
class FnDecl(Node):
    name: str
    params: List[Param]
    return_type: Optional[str]
    body: List['Stmt']

# Program root
@dataclass(slots=True)
class Program:
    statements: List['Stmt']

# Type aliases
Expr = Union[
    IntLiteral, FloatLiteral, StringLiteral, BoolLiteral,
    Identifier, BinaryExpr, UnaryExpr, CallExpr, IndexExpr,
    ArrayLiteral, RangeExpr
]

Stmt = Union[
    VarDecl, Assignment, IfStmt, WhileStmt, ForStmt,
    ReturnStmt, ExprStmt, FnDecl
]
